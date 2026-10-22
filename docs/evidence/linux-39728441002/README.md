# Native scheduler campaign

Run [39728441002](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39728441002)
executed source `7603e0efa4ef0a7a5c1c7d5474b9b6fe866cb74a` on Ubuntu 24.04,
GCC 13.3.0, Linux 6.17 Azure. AMD EPYC 9V45 reports four logical CPUs on two cores
with SMT. CMake configured Release and built the real consumer and upstream
tests; CTest passed. The campaign receipt reports `passed=true`.

The inspected artifact is `scheduler-metrics-39728441002`. Its original
`result.json` SHA256 is
`dc0fe71e36b3f936f645a8af58d2b7fe25fe4334fb9e99dc9ce7f6c207bb53cf`.
Consumer binary SHA256 is
`b0507eaa14089f90f6637ad13688a5563afb0f1737e25f35f64e857a054183a5`.
All 39 raw CSVs were independently revalidated with the maintained driver,
including 390 measured rows, serial checksums, zero errors, five paired repeats,
and hinted winner/duplicate-reclamation counters. Recomputed median/ranges
matched the original 78 summary groups exactly. [summary.csv](summary.csv) retains
all 78 groups; [raw-sha256.txt](raw-sha256.txt) identifies every original CSV in
the full workflow artifact. Seventeen groups had a sample below 10 ms and remain
overhead controls.

The frozen matrix is 8192/131072 cells, 256 steps, 1/4/16 tasks per worker, 1/8 recurrence
scaling and 1/2/4 logical workers. Separate 4096-cell/256-tasks-per-worker/zero-round
control still performs stencil memory work. One warmup per mode precedes five
alternating pairs. Serial reference is timed independently on fresh identical
input. Kernel timers include scheduling/waits and exclude scheduler creation,
reference computation and losing-reference drain. Full process CPU/RSS includes
all of those, warmups and measured runs. Peak RSS across processes ranged
4664..8544 KiB. A representative two-worker compute process recorded 15.93 user,
0.07 system and 11.77 wall seconds for its whole twelve-run/reference workload.

For the larger 131072-cell/eight-round/four-tasks-per-worker cell, ordinary and
preferred ranges overlap at each worker count. There is no measurable hint win.
The tiny-task control is slower with hints, 29.63 versus 59.23 ms at two workers and
54.46 versus 122.65 ms at four. Shared claim allocation, duplicate queue entries and
peer wakeups are known additional work in the source, but a cause-dependent
optimization awaits a profile of this actual control.

The existing separate strace covers two-worker 131072-cell/16-tasks-per-worker/
eight-round work. It reports 11322 futex calls, 14167 yields and 0.232660 seconds total
syscall time across the complete process. Its 84.99% futex share is of syscall
time, not runtime. It does not establish the tiny-task limiter. Instrumented
CSV times are excluded from the campaign. The optional `profile_only=true`
workflow path adds a separately bounded Callgrind control diagnostic. No
cache/NUMA, fairness or general performance guarantee follows from these data.

Reproduce after the documented Release build:

```sh
python3 .agents/skills/verify-mn-thread-runtime/scripts/metrics.py \
  --exe build/mn_thread_runtime_tests --output .agents/runs/fresh-metrics
```
