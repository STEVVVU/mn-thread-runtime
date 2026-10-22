# Feature map

| Feature | Real consumer path | Verification |
| --- | --- | --- |
| Waitable M:N fibers and nested task waits | `mn_thread_runtime_tests` holds more callbacks at `Event::wait()` than the worker count, resumes them, then runs nested `WaitGroup` tasks with 1, 2, and 4 workers. | `powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1` |
| Same-thread and owning-thread contracts | The consumer records the scheduling thread for `SameThread` tasks and the OS thread before and after a suspended fiber resumes. | Same command. |
| Affinity mailbox and work stealing | The consumer holds worker 0 in a bounded CPU loop while worker 1 drains duplicated affinity entries, then checks one callback and one stale-reference skip per task. It also checks that task captures are released after both references drain. | Same command. |
| Iterative stencil and irregular control workload | `mn_thread_runtime_tests --benchmark` compares normal scheduling with preferred-worker scheduling at 1, 2, and 4 workers and checks each checksum against a serial implementation. | `build/Release/mn_thread_runtime_tests.exe --benchmark` |
| Shutdown drain with stolen affinity work | The consumer blocks the worker that claimed an affinity blocker, waits for peers to execute 128 tasks preferred to it, verifies exact-once callbacks and retained captures while mailbox references remain, then tears down the scheduler and checks capture reclamation. | Same command. |
| Longer granularity, working-set and overhead matrix | The same consumer accepts `--benchmark-metrics <workers> <cells> <steps> <tasks-per-worker> <rounds>`. The maintained `scripts/metrics.py` drives 8192/131072 cells, 256 steps, 1/4/16 tasks per worker, 1/8 recurrence scaling, plus 4096-cell/256-tasks-per-worker/zero-round scheduler control. Five alternating pairs follow a warmup for each mode; both modes validate against the timed serial oracle. Available CPUs cap 1/2/4 workers. | Add `-Metrics` to the Windows verifier, or use the native workflow's `metrics=true` input. |

The metrics helper records exact commands, source SHA, host, raw CSV, CPU/RSS
on Linux and five-repeat medians/ranges under `.agents/runs/`. Timers exclude
scheduler creation, serial oracle and mailbox drain but include scheduling and
step waits. The overhead control still performs stencil memory work. Placement
and reuse counters describe observed workers, not cache/NUMA performance.
Samples shorter than 10ms are flagged; they cannot establish a kernel speedup.

The workflow's `profile_only=true` input builds/tests the real consumer and
profiles the frozen two-worker4096-cell/256-step/256-tasks-per-worker/zero-round
control with Callgrind under a300-second deadline. Raw instruction counts,
annotation, source/binary identity, consumer correctness output and explicit
exit receipt survive failure. Instrumented timing columns are diagnostic only.
This separates allocation/queue/synchronization instruction cost from the prior
unreported strace run; no product change follows until its limiter is inspected.

For calibrated scheduler overhead, use the existing pinned Google Benchmark
target with the workflow input `framework=true`. `Schedule/HintedForkJoin`
compares 512 completed callbacks per fork/join with ordinary or preferred
placement at 1/2/4 available logical workers. The existing real consumer and
upstream suite run first. Google Benchmark controls duration, warmup, five
randomly interleaved repetitions, process CPU/wall time, throughput and native
JSON; no custom timing/report engine is added. Scheduler creation and final
losing-reference drain are outside the calibrated loop. Full task/fiber and
reclamation correctness remains the maintained consumer's oracle.

The pinned implementation already supports these flags and APIs. See its
[command flags](https://github.com/google/benchmark/blob/2dd015dfef425c866d9a43f2c67d8b52d709acb6/src/benchmark.cc)
and [timing APIs](https://github.com/google/benchmark/blob/2dd015dfef425c866d9a43f2c67d8b52d709acb6/include/benchmark/benchmark.h).
