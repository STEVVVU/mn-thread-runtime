# Repository guidance

This checkout preserves the upstream Google Marl library. Keep upstream history, source, license files, and notices intact. Put runtime integration beside the existing upstream code. Put the maintained consumer in `examples/` and project verification material in `.agents/`.

## Build and verification

- Configure native Windows x64 with `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMARL_BUILD_EXAMPLES=ON -DMARL_BUILD_TESTS=ON` after initializing `third_party/googletest`.
- Build with `cmake --build build --config Release --target mn_thread_runtime_tests`.
- Enable the pinned upstream suite with `-DMARL_BUILD_TESTS=ON`, then run CTest with `ctest --test-dir build -C Release --output-on-failure`.
- Run the maintained real-consumer verifier with `powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1`.
- Keep generated build and run data under ignored `build/` and `.agents/runs/`; retained compact evidence belongs under `docs/evidence/`.

Do not claim hardware locality, cache-miss, or NUMA results without corresponding measurements. Preferred-worker placement is a hint and stealing remains allowed. Preserve same-thread tasks and owner-thread fiber resumption.
