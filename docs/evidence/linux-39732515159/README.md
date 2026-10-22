# Pinned Google Benchmark scheduler comparison

[Native run 39732515159](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39732515159)
passed both platform CTest jobs and the Ubuntu framework job at source
`e1d76c9c184f5b0213e6519e433794778a1911e6`. Google Benchmark is the existing
pinned submodule `2dd015dfef425c866d9a43f2c67d8b52d709acb6` (v1.8.0).

Ubuntu 24.04, GCC 13.3.0 Release, AMD EPYC 9V45 VM: four logical CPUs, two
reported cores, one NUMA node. Binary SHA-256:
`70a1fceecedc4c405073eefc4578f13a54c6dab7af43f6fa68952316ed0b962a`.

```sh
git submodule update --init third_party/googletest third_party/benchmark
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMARL_BUILD_EXAMPLES=ON -DMARL_BUILD_TESTS=ON -DMARL_BUILD_BENCHMARKS=ON
cmake --build build --parallel 2 --target mn_thread_runtime_tests marl-unittests marl-benchmarks
ctest --test-dir build --output-on-failure --timeout 120
mkdir -p .agents/runs/fresh-framework
timeout 180 build/marl-benchmarks --benchmark_filter='^Schedule/HintedForkJoin/' --benchmark_min_time=0.5s --benchmark_min_warmup_time=0.1 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=.agents/runs/fresh-framework/google-benchmark.json --benchmark_out_format=json
```

Every batch schedules and joins 512 empty callbacks. Google Benchmark calibrates
each case, warms it for 0.1 seconds and randomly interleaves five repetitions.
Actual samples contain 2912..6464 batches each. Wall values are per batch, not
process launch times; process CPU values include all scheduler threads. Creation
and final losing-reference drain occur outside the calibrated loop. These costs
are excluded, not proven absent. The maintained consumer's exact-once,
same-thread, fiber-resumption and capture-reclamation checks ran separately.

`samples.csv` preserves all 30 native iteration rows without rounding.
`summary.csv` gives six median/min/max summaries, including process CPU and
callback throughput. The raw native JSON, console log and environment file are
retained in the run's `scheduler-metrics-39732515159` artifact; `raw-sha256.txt`
identifies all three. Every row has five repetitions, positive finite wall/CPU
and throughput values, and no reported error. No samples were discarded.

Ordinary throughput medians were 4.19..4.47 million callbacks/s; hinted medians
were 2.14..2.24 million callbacks/s. One-worker wall cost rose from 118.064 to
231.122 µs/batch even with no peer worker to wake. Peer waking therefore cannot
alone explain this penalty. Allocation, two queue references and the atomic
exact-once claim are candidate costs, not isolated native attribution. The
earlier mixed-mode Callgrind run was dominated by distorted time-based spinning;
its instruction proportions are not native wall-time percentages. A separate
fixed-iteration per-mode profile is the next diagnostic. No concurrency change
is accepted from these observations. Larger stencil controls showed overlapping
hinted/ordinary ranges, and neither fixture measures cache misses or NUMA benefit.
