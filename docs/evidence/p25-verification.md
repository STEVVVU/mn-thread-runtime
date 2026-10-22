# P25 verification record

## Native behavior

The maintained driver ran the real `mn_thread_runtime_tests.exe` consumer on Windows x64 with MSVC 19.44.35228 and CMake 3.31.6. The test passed for 1, 2, and 4 workers. It held 64 callbacks at an event wait, exercised nested task waits, checked wake-before-wait and wait-before-wake, verified that fibers resumed on their owner threads, checked `SameThread`, held the preferred worker busy while a peer stole 128 tasks, and confirmed that every callback ran once and every losing queue reference was reclaimed. It also checked queued work during shutdown.

The consumer passed 20 consecutive native runs after the affinity shutdown test was added. Marl's pinned GoogleTest submodule was initialized at `fb49e6c164490a227bbb7cf5223b846c836a0305`; all 322 upstream unit tests passed. CTest then passed both `marl-unittests` and the maintained consumer. The existing `fractal`, `hello_task`, `primes`, and `tasks_in_tasks` examples also built after the scheduler change. The baseline checkout built `hello_task` and `tasks_in_tasks` before the change.

The affinity shutdown case holds the worker that claimed an affinity blocker while peers steal 128 hinted tasks. Before releasing the blocker, it checks once-only callbacks, peer execution, and that task captures remain retained by queued mailbox entries. After releasing, unbinding, and destroying the scheduler, it confirms the captures are reclaimed. Latest verifier run `run-20261022-030904-596` passed; its transcript remains at `.agents/runs/run-20261022-030904-596/evidence/transcript.txt`.

The read-only Windows/Linux workflow is `.github/workflows/native-checks.yml`. It uses the existing pinned GoogleTest gitlink, builds the consumer plus upstream unit tests, and runs CTest. It is commit-pinned and only reads repository contents. Hosted execution is pending publication.

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1 -Doctor
powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1
ctest --test-dir build -C Release --output-on-failure
build/Release/marl-unittests.exe
build/Release/mn_thread_runtime_tests.exe --benchmark
```

The driver records its launch and CTest output in `.agents/runs/` and removes only its own temporary directory. Run `run-20261022-030125-851` passed with `MN_THREAD_RUNTIME_OK`; both tests passed and the transcript remains at `.agents/runs/run-20261022-030125-851/evidence/transcript.txt`. The driver removed its temporary directory, and the transcript remains available after cleanup.

## Stencil workload

The benchmark uses the same initialized 8,192-cell array for ordinary Marl scheduling and preferred-worker scheduling. Each mode runs 32 stencil iterations and a deterministic 1-to-7 arithmetic loop per cell. The serial implementation checks the resulting checksum. Each worker count has one warmup per mode and five measured pairs with the first mode alternating between pairs. The timed region includes task creation, scheduling, and 32 barriers. It excludes worker startup, input creation, and the serial oracle.

The run used 24 logical processors and observed 9% total CPU use before measurement. The five-sample medians and full ranges were:

| Workers | Ordinary scheduling, ms | Preferred worker, ms |
| ---: | ---: | ---: |
| 1 | 0.7803 (0.7680–1.3655) | 0.7663 (0.7593–1.1922) |
| 2 | 0.5289 (0.5187–0.5556) | 0.4994 (0.4871–0.9009) |
| 4 | 0.5704 (0.4804–0.8456) | 0.4496 (0.4134–0.4740) |

All 30 checksums were `93.7686` at the printed precision, and the consumer checked each result against the serial oracle with a `1e-8` tolerance. The output records per-partition worker reuse, tasks that ran on their preferred worker, steals, and duplicate skips in [p25-stencil.csv](p25-stencil.csv).

These sub-millisecond synthetic timings do not show whether locality scheduling improves an application. The runs show that the hint increases same-worker reuse and that stealing still occurs. I did not profile the runtime to identify the timing limit, and no cache-miss, NUMA, or speedup claim follows from this run.
