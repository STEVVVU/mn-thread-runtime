"""Drive the real release consumer's frozen scheduler matrix."""
import argparse
import csv
import hashlib
import io
import json
import os
import pathlib
import platform
import shutil
import statistics
import subprocess
import time


def checked_rows(output):
    rows = list(csv.DictReader(io.StringIO(output)))
    assert len(rows) == 10, 'five paired samples required'
    assert {r['sample'] for r in rows} == set(map(str, range(5)))
    for row in rows:
        assert int(row['errors']) == 0
        assert float(row['ms']) > 0 and float(row['serial_ms']) > 0
        if row['mode'] == 'locality':
            assert int(row['preferred']) + int(row['stolen']) == int(row['tasks'])
            assert int(row['duplicate_skips']) == int(row['tasks'])
    checksums = [float(r['checksum']) for r in rows]
    assert max(checksums) - min(checksums) < 1e-8
    for mode in ('baseline', 'locality'):
        selected = [r for r in rows if r['mode'] == mode]
        assert len(selected) == 5
        yield dict(mode=mode, repeats=5, **{
            field: dict(median=statistics.median(values), min=min(values), max=max(values))
            for field in ('ms', 'serial_ms', 'worker_reuses', 'preferred_matches', 'stolen')
            for values in [[float(r[field]) for r in selected]]})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[4]
    output = args.output.resolve()
    output.relative_to(root / '.agents' / 'runs')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(passed=False, platform=platform.platform(), python=platform.python_version(),
                  source_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                  warmup='one run per mode before five alternating pairs', commands=[], summaries=[])
    result['executable_sha256'] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    result['working_tree_dirty'] = subprocess.run(['git', 'diff', '--quiet'], cwd=root).returncode != 0
    try:
        assert shutil.disk_usage(root).free >= 10 * 1024 ** 3, '10 GiB free disk required'
        cpus = len(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else os.cpu_count()
        result['available_cpus'] = cpus
        if platform.system() == 'Linux':
            for command in (['lscpu'], ['free', '-b'], ['uptime'], ['c++', '--version']):
                captured = subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
                (output / (command[0] + '.txt')).write_text(captured.stdout)
        for workers in (1, 2, 4):
            if workers > cpus:
                continue
            configs = [(cells, 256, partitions, rounds)
                       for cells in (8192, 131072) for partitions in (1, 4, 16)
                       for rounds in (1, 8)] + [(4096, 256, 256, 0)]
            for cells, steps, partitions, rounds in configs:
                label = f'{workers}-{cells}-{steps}-{partitions}-{rounds}'
                command = [str(args.exe.resolve()), '--benchmark-metrics', str(workers),
                           str(cells), str(steps), str(partitions), str(rounds)]
                if platform.system() == 'Linux':
                    command = ['/usr/bin/time', '-v', *command]
                result['commands'].append(command)
                started = time.monotonic()
                captured = subprocess.run(command, capture_output=True, text=True, timeout=240)
                (output / f'{label}.csv').write_text(captured.stdout)
                (output / f'{label}.stderr').write_text(captured.stderr)
                assert captured.returncode == 0, (label, captured.returncode)
                for summary in checked_rows(captured.stdout):
                    result['summaries'].append(dict(summary, workers=workers, cells=cells,
                        steps=steps, partitions=partitions * workers, rounds=rounds,
                        process_seconds=time.monotonic() - started,
                        short_sample=summary['ms']['min'] < 10))
        result['passed'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
