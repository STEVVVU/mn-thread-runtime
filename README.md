# M:N thread runtime

This repository extends Google Marl, a C++11 scheduler for stackful fibers and a fixed pool of worker threads. It adds a locality hint for iterative tasks while keeping tasks stealable when the preferred worker is busy.

The runtime serves two needs: ordinary work continues through Marl's scheduler, while repeated partitioned work can ask the scheduler to try the worker that handled the same partition before. The hint is advisory. A busy worker can lose the task to a peer.

## Schedule a preferred worker

Configure a Marl scheduler with dedicated workers, then set a hint on each task:

```cpp
marl::Scheduler::Config config;
config.setWorkerThreadCount(4);
marl::Scheduler scheduler(config);
scheduler.bind();

marl::Task task([partition] { process(partition); });
task.setPreferredWorker(partition % 4);
marl::schedule(std::move(task));
```

Call `marl::Scheduler::unbind()` before the bound thread exits. Use `Task::setPreferredWorker(-1)` to clear the hint. A single-threaded scheduler ignores hints. `SameThread` tasks still run on the thread that schedules them.

`Scheduler::affinityStats()` returns cumulative counts for tasks that have a hint: executions on the preferred worker, executions stolen by another worker, and duplicate references skipped after the task has already run. The counters are snapshots. Duplicate skips can lag task completion until a worker drains the losing queue entry.

## How the scheduler handles a hint

The scheduler puts one shared task record in two places: the preferred worker's FIFO mailbox and its normal stealable queue. The record contains an atomic claim flag. The first worker to claim it runs the body. The other reference is skipped and released. Sleeping peers receive a wake signal so they can steal work instead of waiting for an unrelated task.

This follows the affinity mailbox and duplicate queue entry in Acar, Blelloch, and Blumofe's [The Data Locality of Work Stealing](https://www.cs.cmu.edu/~blelloch/papers/locality2000.pdf), section 7.1. This bounded implementation does not prove the paper's theorem or reproduce its speedups. It uses mutex-protected queues and allocates a shared record for each hinted task. See [the scheduling design](docs/architecture.md) for queue and ownership details.

The coursework context is the CMU 15-418 M:N fiber assignment described in [the course project report](https://wc-0904.github.io/). This repository does not reuse the student implementation.

## Build and run

Use Visual Studio 2022 Build Tools with CMake on Windows:

```powershell
git submodule update --init third_party/googletest
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMARL_BUILD_EXAMPLES=ON -DMARL_BUILD_TESTS=ON
cmake --build build --config Release --target marl-unittests mn_thread_runtime_tests
ctest --test-dir build -C Release --output-on-failure
```

The maintained consumer checks fibers and nested waits with 1, 2, and 4 workers, same-thread execution, owner-thread fiber resumption, stealing while the preferred worker is busy, duplicate reclamation, and shutdown drain. Run the real consumer driver to include a bounded process timeout and retained evidence:

```powershell
powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1 -Doctor
powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1
```

To run the stencil workload and emit CSV measurements:

```powershell
build/Release/mn_thread_runtime_tests.exe --benchmark
```

## Measured evidence

The native stencil run used 24 logical processors. Every result matched the serial checksum. The table shows the median and full range from five alternating pairs:

| Workers | Ordinary scheduling, ms | Preferred worker, ms |
| ---: | ---: | ---: |
| 1 | 0.7803 (0.7680–1.3655) | 0.7663 (0.7593–1.1922) |
| 2 | 0.5289 (0.5187–0.5556) | 0.4994 (0.4871–0.9009) |
| 4 | 0.5704 (0.4804–0.8456) | 0.4496 (0.4134–0.4740) |

These sub-millisecond synthetic results do not establish a latency improvement. The benchmark shows more repeated worker reuse with hints, and it records steals when another worker wins. It does not profile the limiting cost or measure cache misses, NUMA traffic, or a production workload. See the [full run notes and all samples](docs/evidence/p25-verification.md).

## Upstream source and maintenance

This repository retains the complete Google Marl source tree and history at base commit `b8406ab0a82524170ead6dd6c371b579bbc28b3e`. The original project README is preserved in [README.upstream.md](README.upstream.md). Keep the Apache-2.0 license and upstream notices with the source.

Google archived Marl on April 27, 2026. This repository preserves that source and carries a local scheduling extension. See [`LICENSE`](LICENSE) and the upstream change history for the applicable notices and authorship.
