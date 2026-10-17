param([switch]$Cpu, [switch]$Gpu)
$ErrorActionPreference = 'Stop'

# Run in an x64 Visual Studio developer shell with the Vulkan SDK installed.
$timer = [Diagnostics.Stopwatch]::StartNew()
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build/windows-dev'
if ($Cpu -and $Gpu) { throw 'Choose -Cpu or -Gpu, not both.' }
$gpuTests = if ($Gpu) { 'ON' } else { 'OFF' }
if ($Gpu) {
    & python (Join-Path $PSScriptRoot 'gpu_environment.py')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

& cmake -S $repo -B $build -A x64 "-DNANOVA_GPU_TESTS=$gpuTests"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $build --config Debug --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $build --build-config Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ('Build and E2E completed in {0:N2}s.' -f $timer.Elapsed.TotalSeconds)
