# Matched scheduler pointer-move comparison: no established timing gain

[Run 39739013902](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39739013902)
passed all three jobs. Baseline source:
`db6ecf6d62de6a20533077ce5c5c352d8b2fd42b`. Candidate source:
`51fa67547fd59018726a3e9398db2495623356a2`. The only runtime-source change is
passing the unused local shared pointer to enqueueAffinity with std::move.
The two queue references, claim CAS, counters, wakeups and fiber ownership are
unchanged. Main reviewed the source/lifetime argument before publishing.

Both source builds passed consumer/upstream CTest before any measurements.
The separate native Windows and Ubuntu CMake jobs also passed. All ten timed
processes exited zero, all dedicated pass receipts are true, and the atomic
terminal receipt records passed=true with owned cleanup complete. The temporary
baseline checkout was removed; raw evidence remains retained.

Environment: Ubuntu 24.04, GCC 13.3.0 Release, CMake 3.31.6, AMD EPYC 9V74 VM
with four logical CPUs on two reported cores. Every native JSON reports the
same host within this job. Both builds use Google Benchmark
`2dd015dfef425c866d9a43f2c67d8b52d709acb6` (v1.8.0) and GoogleTest
`fb49e6c164490a227bbb7cf5223b846c836a0305`. Fixture/helper SHA-256 values
are identical across both builds and match their committed Git blobs:

- src/scheduler_bench.cpp: `1288c70ed48a43ba32cc1b7fb54a9cc1f1192e99578e750b8d44f314da1c44c7`
- src/marl_bench.h: `e83ec2647a43f06d4f1f07e436e7430ccc3f84657652c0038611a5779ce74cfc`
- Baseline binary: `70a1fceecedc4c405073eefc4578f13a54c6dab7af43f6fa68952316ed0b962a`
- Candidate binary: `73535001bbdd7155f2fa608fd2f21d9b552f1e8a9224db85214fc5166131b71e`

Reproduce using framework=true in the maintained Native checks workflow. It
builds/tests both sources with the same pinned dependencies and Release flags,
verifies fixture bytes and submodule identities, then alternates five whole
passes per build. Each pass invokes the existing Google Benchmark target:

```sh
/usr/bin/time -v -o resources.txt timeout 180 build/marl-benchmarks \
  --benchmark_filter='^Schedule/HintedForkJoin/' --benchmark_min_time=0.5s \
  --benchmark_min_warmup_time=0.1 --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=true \
  --benchmark_out=google-benchmark.json --benchmark_out_format=json
```

Actual pair order is baseline/candidate, candidate/baseline,
baseline/candidate, candidate/baseline, baseline/candidate. Each native case
contains five repetitions in each of five passes, so every variant/case has
25 raw samples. All 300 rows have complete repetition indices, finite positive
real/process-CPU time and throughput, no reported error, and throughput matching
512 callbacks per batch. Reported measured callback counts total 238530560
baseline and 238904320 candidate; calibration/warmup calls are additional and
excluded from those counts. Calibrated samples span 0.609..0.809 seconds of
reported timed loops, not microsecond process launches.

`samples.csv` preserves every unrounded raw iteration field and reported batch/
callback count. `pass-medians.csv` preserves all 60 native framework median
records; native aggregate versus raw-row medians differ by at most floating
serialization rounding. `summary.csv` gives all 12 pooled 25-sample summaries,
including full ranges, process CPU and throughput. `comparison.csv` retains
every ordinary/hinted comparison:

| Workers/mode | Baseline µs/batch | Candidate µs/batch |
| --- | ---: | ---: |
| 1 ordinary | 150.883 (134.536..168.415) | 152.072 (133.105..172.739) |
| 1 hinted | 378.195 (342.062..405.061) | 380.193 (346.405..420.539) |
| 2 ordinary | 154.910 (150.192..176.029) | 153.920 (149.538..177.891) |
| 2 hinted | 374.635 (364.816..400.363) | 373.678 (356.069..425.754) |
| 4 ordinary | 166.069 (160.869..172.880) | 165.152 (158.496..171.264) |
| 4 hinted | 421.385 (414.425..429.173) | 426.377 (410.868..443.828) |

All six full ranges overlap. Ordinary median changes are about +0.8%, -0.6%
and -0.6%; hinted median changes are about +0.5%, -0.3% and +1.2%. The small
point changes do not establish a consistent hinted improvement.

`control-normalization.csv` keeps all 15 pass-pair calculations. For each
worker count and adjacent pair, it divides baseline hinted/ordinary median by
candidate hinted/ordinary median. A value above one favors the candidate
relative to its ordinary control. Median (full five-pair range):

| Workers | Control-normalized hinted ratio |
| ---: | ---: |
| 1 | 0.9855 (0.9724..1.0190) |
| 2 | 1.0049 (0.9751..1.0232) |
| 4 | 0.9898 (0.9633..1.0095) |

Every range crosses one and no consistent timing benefit is established. This
control comparison does not isolate a cause for run variation or prove zero
effect. The removed source-level reference increment/release is real, but its
native timing contribution cannot be attributed from this run. The separately
profiled shared-record allocation and duplicate queue/claim work remain; no
pool, ownership or wakeup redesign follows from this result.

`resources.csv` retains every full-process resource row. Median (range),
baseline versus candidate: CPU 69.33 (68.04..70.35) versus
68.88 (68.64..69.69) seconds; wall 23.90 (23.57..24.39) versus
23.78 (23.68..23.91) seconds; peak RSS 4996 (4936..5164) versus
5032 (4932..5164) KiB. These processes include calibration/warmup, scheduler
creation, all cases and final drain. Native loop timings exclude scheduler
creation and final reference drain; resource totals are not per-callback costs.

`raw-sha256.txt` covers every downloaded artifact file and retained primary
hosted log, including source/binary/fixture identity, actual order, exact
commands, raw JSON/logs, both CTest logs, disk checks and exit receipts. All
previous native campaigns/profiles remain intact. No local builds/timers or
new measurement/statistics engine were used for this audit. The safe one-copy
change is not reported as a measured optimization or a solution to the larger
hint penalty.
