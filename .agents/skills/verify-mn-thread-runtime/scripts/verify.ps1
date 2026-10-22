param(
  [switch]$Doctor,
  [switch]$Metrics
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$runRoot = Join-Path $repo '.agents/runs'
if ($Doctor) {
  if (-not (Test-Path $cmake)) { throw "CMake not found: $cmake" }
  if (-not (Test-Path $ctest)) { throw "CTest not found: $ctest" }
  $compiler = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
  if (-not (Test-Path $compiler)) { throw "MSVC compiler not found: $compiler" }
  $doctorId = 'doctor-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
  $doctorDir = Join-Path $runRoot $doctorId
  $doctorEvidence = Join-Path $doctorDir 'evidence'
  New-Item -ItemType Directory -Force -Path $doctorEvidence | Out-Null
  $doctorLog = Join-Path $doctorEvidence 'environment.txt'
  & $cmake --version | Set-Content -LiteralPath $doctorLog -Encoding utf8
  Add-Content -LiteralPath $doctorLog -Value "CTest: $ctest" -Encoding utf8
  Add-Content -LiteralPath $doctorLog -Value "MSVC compiler: $compiler" -Encoding utf8
  Write-Output "Doctor evidence: $doctorLog"
  exit 0
}
function InvokeOwnedProcess([string]$FilePath, [string]$Arguments = '') {
  $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $FilePath
  $startInfo.Arguments = $Arguments
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $startInfo
  if (-not $process.Start()) { throw "Process did not start: $FilePath" }
  $stdoutRead = $process.StandardOutput.ReadToEndAsync()
  $stderrRead = $process.StandardError.ReadToEndAsync()
  if (-not $process.WaitForExit(90000)) {
    $process.Kill()
    $process.WaitForExit()
    throw "Process exceeded the 90 second limit: $FilePath"
  }
  $process.WaitForExit()
  return @{
    ExitCode = $process.ExitCode
    Output = "$($stdoutRead.Result)`n$($stderrRead.Result)"
  }
}
$runId = 'run-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$runDir = Join-Path $runRoot $runId
$evidenceDir = Join-Path $runDir 'evidence'
$tempDir = Join-Path $runDir 'temp'
New-Item -ItemType Directory -Force -Path $evidenceDir,$tempDir | Out-Null
$transcript = Join-Path $evidenceDir 'transcript.txt'
try {
  $build = Join-Path $repo 'build'
  $testMode = if (Test-Path (Join-Path $repo 'third_party/googletest/googletest/CMakeLists.txt')) { 'ON' } else { 'OFF' }
  & $cmake -S $repo -B $build -G 'Visual Studio 17 2022' -A x64 -DMARL_BUILD_EXAMPLES=ON "-DMARL_BUILD_TESTS=$testMode"
  if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
  $targets = if ($testMode -eq 'ON') { @('marl-unittests','mn_thread_runtime_tests') } else { @('mn_thread_runtime_tests') }
  & $cmake --build $build --config Release --target $targets
  if ($LASTEXITCODE -ne 0) { throw 'Consumer build failed' }
  $ownedTemp = Join-Path $tempDir 'cleanup-probe.txt'
  Set-Content -LiteralPath $ownedTemp -Value 'owned by this verifier' -Encoding utf8
  if (-not (Test-Path $ownedTemp)) { throw 'Drive did not create its owned probe' }
  $exe = Join-Path $build 'Release/mn_thread_runtime_tests.exe'
  $consumer = InvokeOwnedProcess $exe
  Add-Content -LiteralPath $transcript -Value "Consumer exit code: $($consumer.ExitCode)`n$($consumer.Output)" -Encoding utf8
  if ($consumer.ExitCode -ne 0) { throw "Consumer failed with exit code $($consumer.ExitCode). Evidence: $transcript" }
  if ($consumer.Output -notmatch 'MN_THREAD_RUNTIME_OK') { throw "Consumer success marker missing. Evidence: $transcript" }
  $testRun = InvokeOwnedProcess $ctest "--test-dir `"$build`" -C Release --output-on-failure"
  Add-Content -LiteralPath $transcript -Value "CTest exit code: $($testRun.ExitCode)`n$($testRun.Output)" -Encoding utf8
  if ($testRun.ExitCode -ne 0) { throw "CTest failed with exit code $($testRun.ExitCode). Evidence: $transcript" }
  if ($Metrics) {
    & python (Join-Path $PSScriptRoot 'metrics.py') --exe $exe --output (Join-Path $evidenceDir 'metrics')
    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed. Evidence: $evidenceDir" }
  }
  Write-Output "Run: $runId"
  Write-Output "Evidence: $transcript"
  Remove-Item -LiteralPath $tempDir -Force -Recurse
  if (Test-Path $tempDir) { throw 'Cleanup left the owned temporary directory behind' }
  if (-not (Test-Path $transcript)) { throw 'Evidence did not survive cleanup' }
  Write-Output 'Cleanup: owned temporary directory removed; evidence transcript retained.'
} catch {
  $_ | Out-String | Add-Content -Encoding utf8 $transcript
  Write-Output "Run: $runId"
  Write-Output "Evidence: $transcript"
  Write-Output 'Cleanup: evidence preserved for failure review.'
  throw
}
