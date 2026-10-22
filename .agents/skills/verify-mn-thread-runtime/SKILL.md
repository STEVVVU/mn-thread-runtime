---
name: verify-mn-thread-runtime
description: Build and drive the native M:N fiber scheduler verification consumer on Windows.
---

# Verify the M:N thread runtime

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .agents/skills/verify-mn-thread-runtime/scripts/verify.ps1
```

The driver configures and builds the maintained consumer, launches it with a 90 second timeout, checks its result, saves a run transcript under `.agents/runs/`, and removes only its owned temporary files. It emits a stable run ID and evidence path. The evidence transcript survives cleanup. The consumer must run in the native product process; source inspection and compilation alone do not establish scheduler behavior.

The `-Doctor` switch checks the local CMake and MSVC prerequisites and saves its output under `.agents/runs/` without launching a build.
