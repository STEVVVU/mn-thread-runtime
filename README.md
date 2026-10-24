# M:N thread runtime

## Contents

- [Architecture and execution guide](#architecture-and-execution-guide)
- [Schedule a preferred worker](#schedule-a-preferred-worker)
- [How the scheduler handles a hint](#how-the-scheduler-handles-a-hint)
- [Build and run](#build-and-run)
- [Measured evidence](#measured-evidence)
- [Upstream source and maintenance](#upstream-source-and-maintenance)

## Architecture and execution guide

This repository extends the preserved Google Marl fiber scheduler with an advisory [preferred-worker](#preferred-worker) hint. Marl owns worker threads, fiber switching, task queues, and [work stealing](#work-stealing). The extension adds an [affinity mailbox](#mailbox) entry and a shared [exact-once claim](#exact-once-claim) record for hinted tasks. [Same-thread tasks](#same-thread-task) retain their existing scheduling behavior.

The hint changes task routing inside Marl rather than replacing its worker runtime.

```mermaid
flowchart LR
Caller["Consumer schedules task"] --> API["Task preferred-worker hint"]
API --> Scheduler["Marl scheduler"]
Scheduler --> Mailbox["Preferred worker mailbox"]
Scheduler --> Queue["Normal stealable queue"]
Mailbox --> Worker["Worker claims and runs task"]
Queue --> Worker
Worker --> Fiber["Marl fiber execution"]
```

A hinted task has one shared record referenced from the preferred worker's FIFO mailbox and the ordinary stealable queue. Both routes can race to claim it, but only the first successful atomic claim executes the body. A worker that loses skips the duplicate record and releases its reference. If the preferred worker is busy, a peer can steal from the normal queue. Sleeping peers receive a wake signal so they can look for work. Affinity statistics are cumulative snapshots, and duplicate-skip counts can lag until a losing queue entry drains.

```mermaid
flowchart TD
subgraph API["Task API"]
Hint["Set preferred worker"] --> Schedule["Schedule task"]
Clear["Set hint to -1"] --> Schedule
end
subgraph Scheduler["Queue and ownership"]
Schedule --> Route{"Eligible preferred-worker hint?"}
Route -->|"yes"| Record["Create shared task record with atomic claim"]
Route -->|"cleared hint or same-thread task"| Ordinary["Use existing scheduling path"]
Record --> Mailbox["Enqueue preferred mailbox reference"]
Record --> Stealable["Enqueue normal stealable reference"]
end
subgraph Workers["Worker lifecycle"]
Mailbox --> Claim["Attempt exact-once claim"]
Stealable --> Claim
Claim -->|"won"| Run["Run task body"]
Claim -->|"lost"| Skip["Skip duplicate and release reference"]
Busy["Preferred worker busy"] --> Steal["Peer steals normal queue entry"]
Steal --> Claim
Sleep["Peer waiting"] --> Wake["Signal peer to check work"]
Wake --> Steal
end
Run --> Stats["Update hinted execution counters"]
Skip --> Stats
Ordinary --> Existing["Existing Marl task execution"]
```

| Subsystem | Responsibility and owned invariant | Source |
| --- | --- | --- |
| Task API | Store or clear the preferred worker index on a task | [`task.h`](include/marl/task.h) |
| Scheduler | Route hinted records to mailbox and stealable queues while retaining same-thread semantics | [`scheduler.cpp`](src/scheduler.cpp) |
| Worker queues | Own queue references and support stealing and worker wakeups | [`scheduler.cpp`](src/scheduler.cpp) |
| Fiber execution | Run the claimed body through Marl's existing fiber scheduler | [`scheduler.h`](include/marl/scheduler.h) |
| Consumer checks | Exercise hints, stealing, duplicate reclamation, and shutdown drain | [`mn_thread_runtime_tests.cpp`](examples/mn_thread_runtime_tests.cpp) |

### Follow a hinted task

1. The consumer configures and binds a Marl scheduler, creates a task, and sets a worker index. `-1` clears the hint; a single-thread scheduler ignores it.
2. Scheduling creates the shared task record and queues one reference in the preferred mailbox and one in the normal work queue.
3. The preferred worker or a stealing peer attempts the atomic claim. The winner runs the task once.
4. The other queue entry later observes the completed claim, skips execution, and releases its reference. Its counter may update after task completion.
5. Shutdown drains remaining queue references; the consumer unbinds a bound scheduler thread before it exits.

<a id="preferred-worker"></a>
<details><summary>What is a preferred worker?</summary>

A preferred worker is the worker the scheduler tries first for a hinted task. The hint is advisory; if that worker is busy, another worker can steal the task.
</details>

<a id="mailbox"></a>
<details><summary>What is the affinity mailbox?</summary>

The affinity mailbox is a FIFO queue tied to a worker. A hinted task adds one reference there, while another reference stays in the normal queue so peers can steal it.
</details>

<a id="work-stealing"></a>
<details><summary>What is work stealing?</summary>

Work stealing lets an idle worker take runnable work from a peer's queue. Marl uses the normal queue as the fallback route when a preferred worker is occupied.
</details>

<a id="exact-once-claim"></a>
<details><summary>What is the exact-once claim?</summary>

The shared task record contains an atomic claim flag. The first worker that claims it runs the body; the second queue reference cannot run it again.
</details>

<a id="same-thread-task"></a>
<details><summary>What is a same-thread task?</summary>

A same-thread task must run on the thread that schedules it. The preferred-worker hint does not override this existing task mode.
</details>

<a id="affinity-counter"></a>
<details><summary>What is an affinity counter?</summary>

An affinity counter records hinted executions on the preferred worker, steals by another worker, or skipped duplicate references. A duplicate-skip count can lag while the losing reference remains queued.
</details>

### Design choices and limits

These rationales are inferred from implementation; the cited paper is the source of the mailbox pattern, not proof of this implementation's performance.

| Choice | Current behavior and rationale | Alternative and cost to reconsider |
| --- | --- | --- |
| Advisory hint instead of pinning | Keeps normal stealing available when the target worker is busy. | Strict pinning can preserve placement but risks idle workers and delayed tasks. |
| Duplicate queue references | Gives the preferred worker a direct route and keeps a stealable fallback. | Single-queue routing avoids duplicates but needs a separate handoff when the target is busy. |
| Shared record with atomic claim | Makes competing routes execute the body once. | Moving tasks between queues avoids shared claim state but adds transfer synchronization. |
| Mutex-protected queues | Reuses Marl's queue and ownership structure. | Lock-free queues require different reclamation and contention evidence. |
| Cumulative advisory statistics | Exposes observed placement without synchronizing task completion. | Exact live counters add synchronization and still do not prove hardware locality. |

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

The consumer and upstream CTest suites passed on both Windows and Ubuntu in [native CI run 39716823237](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39716823237). Main also reran the maintained Windows process driver at `run-20261022-032459-699`; its evidence survived scratch cleanup.

The longer [native campaign](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39728441002)
used Ubuntu 24.04, GCC 13.3 Release, and an AMD EPYC 9V45 VM reporting four
logical CPUs on two cores. All 390 measured samples matched the serial oracle
with zero reported errors. For 131072 cells, 256 steps, four tasks per worker
and eight recurrence rounds, five alternating pairs gave median (full range):

| Workers | Ordinary scheduling, ms | Preferred worker, ms |
| ---: | ---: | ---: |
| 1 | 695.8 (692.9..702.0) | 696.5 (692.0..707.5) |
| 2 | 350.6 (348.5..355.4) | 350.3 (348.3..351.7) |
| 4 | 245.7 (243.8..247.1) | 243.9 (242.6..247.8) |

The overlapping ranges show no measurable benefit from hints in this cell.
Serial control medians were 626.3..630.2 ms. Hints were slower in the tiny-task
control (4096 cells, zero recurrence rounds, 256 tasks per worker): with two
workers, ordinary scheduling took 29.63 (26.07..31.75) ms versus 59.23
(58.28..60.07) ms with hints. A [separate instruction profile](docs/evidence/linux-39729878037/)
completed but was dominated by the scheduler's time-based idle spinning under
instrumentation; it does not isolate the native cause of that loss. A narrow
comparison now reuses the pinned Google Benchmark `Schedule/HintedForkJoin`
fixture (`framework=true` in Native checks), retaining the consumer's ownership
oracle. No runtime optimization was accepted from the distorted profile.
More observed worker reuse does not establish a cache or NUMA benefit.
Process peak RSS across the matrix was
4664..8544 KiB, including serial controls and scheduler creation.

Reproduce the complete 39-configuration matrix after a Release build:

```sh
python3 .agents/skills/verify-mn-thread-runtime/scripts/metrics.py \
  --exe build/mn_thread_runtime_tests --output .agents/runs/fresh-metrics
```

The [complete compact summaries and raw hashes](docs/evidence/linux-39728441002/)
retain every measured configuration, resource scope and exact source identity.
Kernel timers exclude scheduler creation and serial controls; the matrix covers
synthetic working-set/granularity controls, not a production workload. Historical
short-run evidence remains in [the first verification notes](docs/evidence/p25-verification.md).

The [Google Benchmark run](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39732515159)
used the same Ubuntu/GCC configuration and a four-logical-CPU AMD EPYC 9V45 VM.
Five randomly interleaved repetitions, each calibrated for at least 0.5 seconds
after 0.1 seconds of warmup, measured 512 joined empty callbacks per batch:

| Workers | Ordinary, µs/batch | Preferred worker, µs/batch |
| ---: | ---: | ---: |
| 1 | 118.1 (115.4..121.9) | 231.1 (225.1..244.7) |
| 2 | 122.3 (107.4..127.1) | 229.1 (223.5..245.0) |
| 4 | 114.4 (113.4..115.5) | 239.5 (238.0..241.3) |

Values are medians (full ranges) from thousands of batches per repetition,
excluding scheduler creation and final reference drain. Hints cost about
1.87..2.09 times the ordinary path here; ordinary scheduling remains the default.
The one-worker penalty rules out peer wakeups as its sole cause. The hint path's
shared claim record, duplicate queues and exact-once claim also apply with one
worker; their individual native costs remain unresolved. Empty callbacks do
not establish locality or production-workload performance. Both CTest suites
passed before measurement. [All 30 samples, process CPU costs, exact command,
source/binary identity and hashes](docs/evidence/linux-39732515159/) are retained.

The [separate fixed-count one-worker profiles](docs/evidence/linux-39735074674/)
completed 131072 callbacks per mode. Hints added exactly one shared-record
allocation per callback and more queue/claim work: 175.42 million versus
111.15 million full-process instructions. These instrumented counts confirm
structural costs; they are not native timing percentages. The ownership
protocol remains unchanged; main review approved moving an unused local shared
pointer into its by-value argument to remove one unnecessary reference copy.
Native validation passed. The workflow's
`framework=true` input compares frozen `db6ecf6` and current source on one
runner, with identical fixtures/toolchains, CTest first and five alternating
whole passes retaining native calibration, warmup, raw JSON and resources.
The original measurements above remain preserved.

The [matched run 39739013902](https://github.com/STEVVVU/mn-thread-runtime/actions/runs/39739013902)
compared `db6ecf6` with `51fa675` on Ubuntu 24.04/GCC 13.3 Release, an AMD
EPYC 9V74 VM reporting four logical CPUs on two cores. Both sources passed
consumer/upstream CTest before measurement; Windows and Ubuntu jobs passed too.
All 300 calibrated raw iteration samples are retained. Median (full range of
25 samples per case), in µs per 512 joined callbacks:

| Workers | Scheduling | Frozen | Pointer move |
| ---: | --- | ---: | ---: |
| 1 | Ordinary | 150.9 (134.5..168.4) | 152.1 (133.1..172.7) |
| 1 | Hinted | 378.2 (342.1..405.1) | 380.2 (346.4..420.5) |
| 2 | Ordinary | 154.9 (150.2..176.0) | 153.9 (149.5..177.9) |
| 2 | Hinted | 374.6 (364.8..400.4) | 373.7 (356.1..425.8) |
| 4 | Ordinary | 166.1 (160.9..172.9) | 165.2 (158.5..171.3) |
| 4 | Hinted | 421.4 (414.4..429.2) | 426.4 (410.9..443.8) |

No native speedup is established: all six ranges overlap, ordinary controls
move too, and every five-pair control-normalized hinted range crosses no change.
The source removes one reference copy while preserving both queue references
and exact-once claims; this run cannot attribute a timing gain to it. The shared
allocation and duplicate-queue costs remain. Full-process peak RSS was
4936..5164 KiB baseline and 4932..5164 KiB candidate.
[Every sample, native pass median, control pair, CPU/resource cost, command,
receipt and raw hash](docs/evidence/linux-39739013902/) is retained. No cache,
NUMA or production-workload benefit follows from these empty callbacks.

## Upstream source and maintenance

This repository retains the complete Google Marl source tree and history at base commit `b8406ab0a82524170ead6dd6c371b579bbc28b3e`. The original project README is preserved in [README.upstream.md](README.upstream.md). Keep the Apache-2.0 license and upstream notices with the source.

Google archived Marl on April 27, 2026. This repository preserves that source and carries a local scheduling extension. See [`LICENSE`](LICENSE) and the upstream change history for the applicable notices and authorship.
