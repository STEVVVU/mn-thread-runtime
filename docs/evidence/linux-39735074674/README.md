# Separate fixed-count Google Benchmark profiles

[Run 39735074674](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39735074674)
passed at source `db6ecf6d62de6a20533077ce5c5c352d8b2fd42b`, with the existing
Google Benchmark pin `2dd015dfef425c866d9a43f2c67d8b52d709acb6` (v1.8.0).
Both normal native CTest jobs and the profiling job passed. Each profile's
dedicated receipt is true with exit code zero, and each native JSON records
exactly 256 completed batches of 512 joined callbacks: 131072 callbacks/mode.

Ubuntu 24.04, GCC 13.3.0 Release, Valgrind 3.22.0, AMD EPYC 7763 VM reporting
four logical CPUs on two cores. Binary SHA-256:
`70a1fceecedc4c405073eefc4578f13a54c6dab7af43f6fa68952316ed0b962a`.

After the documented native build, invoke each mode separately:

```sh
for mode in 0 1; do
  timeout 300 valgrind --tool=callgrind --fair-sched=yes --dump-every-bb=1000000000 \
    --callgrind-out-file=callgrind-$mode.out \
    build/marl-benchmarks \
    --benchmark_filter="^Schedule/HintedForkJoin/tasks:512/threads:1/hinted:$mode/" \
    --benchmark_min_time=256x --benchmark_min_warmup_time=0 \
    --benchmark_repetitions=1 --benchmark_out=profile-$mode.json --benchmark_out_format=json
done
```

The hosted `framework_profile=true` path also retains each exact command, raw
profile, native JSON, stdout/stderr, annotation, environment and exit receipt.
`raw-sha256.txt` identifies all 17 files. `summary.json` gives exact source,
framework/binary identity and full instruction totals. `exclusive-summary.csv`
contains the union of the top 25 exclusive symbols in each mode, affinity
control-block symbols and one aggregate row for all other symbols. Its columns
sum exactly to both raw headers; call counts are recorded where symbols exist.
No profiler timing is promoted to a native benchmark result.

| Counter, full diagnostic process | Ordinary | Hinted |
| --- | ---: | ---: |
| Exclusive instructions | 111151455 | 175420970 |
| Operator-new calls | 132289 | 263361 |
| AffinityTask final disposal calls | 0 | 131072 |
| AffinityTask control-block destruction calls | 0 | 131072 |
| spinForWorkAndLock exclusive instructions | 5047946 | 5262016 |

The hinted process executes 64,269,515 more instructions (1.578 times the
ordinary total). The exact additional 131072 operator-new calls match one
shared AffinityTask allocation per callback in Scheduler.enqueue. Exclusive
malloc/free plus their internal allocation/free routines increase by
28,813,703 instructions. Duplicate queue drain, mutex and execute/claim costs
also grow. Startup and final reference drain are included; profiler scheduling
changes execution. These counts establish structural extra work, not its
native wall-time percentage or the complete cause of the native twofold loss.

Source inspection found one bounded avoidable reference copy: enqueue passes
its local shared pointer by lvalue into a by-value enqueueAffinity argument,
temporarily retaining a third reference beyond the two required queue refs.
Moving that local argument could eliminate its increment/release without
changing the queue protocol. Main source review approved that one-line move;
native validation and a same-runner comparison against db6ecf6 remain pending.
The allocation, duplicate queues and exact-once claim cannot simply be removed
without a separately reviewed ownership redesign. Existing Pool reuse was
examined: pool.h depends on conditionvariable.h and scheduler.h, so direct
embedding creates a header cycle and would broaden this change. No pool or
ownership rewrite is accepted from this diagnostic.
