# Feature map

| Feature | Real consumer path | Verification |
| --- | --- | --- |
| Waitable M:N fibers and nested task waits | `mn_thread_runtime_tests` holds more callbacks at `Event::wait()` than the worker count, resumes them, then runs nested `WaitGroup` tasks with 1, 2, and 4 workers. | `powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1` |
| Same-thread and owning-thread contracts | The consumer records the scheduling thread for `SameThread` tasks and the OS thread before and after a suspended fiber resumes. | Same command. |
| Affinity mailbox and work stealing | The consumer holds worker 0 in a bounded CPU loop while worker 1 drains duplicated affinity entries, then checks one callback and one stale-reference skip per task. It also checks that task captures are released after both references drain. | Same command. |
| Iterative stencil and irregular control workload | `mn_thread_runtime_tests --benchmark` compares normal scheduling with preferred-worker scheduling at 1, 2, and 4 workers and checks each checksum against a serial implementation. | `build/Release/mn_thread_runtime_tests.exe --benchmark` |
| Shutdown drain with stolen affinity work | The consumer blocks the worker that claimed an affinity blocker, waits for peers to execute 128 tasks preferred to it, verifies exact-once callbacks and retained captures while mailbox references remain, then tears down the scheduler and checks capture reclamation. | Same command. |
