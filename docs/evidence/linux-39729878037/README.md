# Separate scheduler instruction profile

[Run 39729878037](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39729878037)
at source `2eb4fe690bd20130d4f4737db4e1d687ed19773f` built/tested the native
consumer on Ubuntu 24.04 before profiling the frozen tiny-task control.
Valgrind/Callgrind 3.22.0 completed with exit zero. All instrumented callback
rows retained zero errors, checksums and affinity reclamation counts. These
timing columns are excluded from the uninstrumented campaign.

The original raw files are artifact `scheduler-metrics-39729878037`.
[receipt.json](receipt.json) retains the observed profile result, and
[raw-sha256.txt](raw-sha256.txt) identifies all nine raw profile/environment
files. Exact command:

```sh
timeout 300 valgrind --tool=callgrind build/mn_thread_runtime_tests \
  --benchmark-metrics 2 4096 256 256 0
```

The raw profile totals 17,118,266,675 instructions. Exclusive instruction
counts sum exactly to that header total. `spinForWorkAndLock` accounts for
14,118,333,588 (82.475%), `Scheduler::enqueue` 0.744%, `enqueueAffinity` 0.376%,
and `pthread_cond_signal` 0.535%. Inclusive call counts overlap and must not be
summed as exclusive cost; some fiber/call-graph cycles exceed 100% inclusively.

This combined-mode profile changes thread scheduling and the scheduler's
time-bounded spinning. Instruction shares are not native wall-time shares and
cannot establish that allocation, wakeups or spinning causes the observed
native hint slowdown. No runtime code change follows from that inference.
One possible low-risk optimization is coalescing repeated peer notifications
while the existing steal signal remains set, but it requires semantic review
and matched native measurements before acceptance.

The next calibrated overhead comparison reuses the already pinned Google
Benchmark submodule/target and its `Schedule` fixture. It keeps the consumer's
fiber/claim/reclamation oracle, uses five randomly interleaved repetitions and
native framework JSON. No new custom measurement or reporting engine is added.
