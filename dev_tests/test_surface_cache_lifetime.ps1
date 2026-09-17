param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$name = if ($Baseline) { 'surface-cache-baseline' } else { 'surface-cache-fixed' }
$out = Join-Path $root "build\$name"
New-Item -ItemType Directory -Force $out | Out-Null

# Compile the exact production class, without unrelated shader/runtime headers.
# This is generated on each run, not a second maintained implementation.
$relativeHeader = 'src/DirectX12/ModRuntime/DX12CustomShaderDevice.h'
if ($Baseline) {
    $source = (& git -C $root show "HEAD:$relativeHeader") -join "`n"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read HEAD baseline' }
} else {
    $source = [System.IO.File]::ReadAllText((Join-Path $root $relativeHeader))
}
$begin = $source.IndexOf('class DX12CustomShaderSurfaceCache')
$end = $source.IndexOf('// Internal direct-queue submission channel', $begin)
if ($begin -lt 0 -or $end -le $begin) { throw 'SurfaceCache class boundaries changed' }
$header = @"
#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <mutex>
#include <unordered_map>
namespace Bunny {
$($source.Substring($begin, $end - $begin))
}
"@
[System.IO.File]::WriteAllText((Join-Path $out 'surface_cache_under_test.h'), $header)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I "$out" "$PSScriptRoot\surface_cache_lifetime_test.cpp" /Fo"$out\test.obj" /Fe"$out\test.exe" /link d3d12.lib dxgi.lib user32.lib
exit /b %errorlevel%
"@
[System.IO.File]::WriteAllText((Join-Path $out 'build.bat'), $build)
& cmd.exe /c (Join-Path $out 'build.bat')
if ($LASTEXITCODE -ne 0) { throw 'SurfaceCache test build failed' }

$cases = @('invalidate', 'no-output', 'switch', 'recreate')
if (-not $Baseline) { $cases += 'resize' }
$unexpected = 0
foreach ($case in $cases) {
    & (Join-Path $out 'test.exe') $case
    $code = $LASTEXITCODE
    if ($Baseline) {
        if ($code -ne 1) {
            Write-Host "UNEXPECTED baseline result: $case exit=$code"
            ++$unexpected
        } else {
            Write-Host "EXPECTED baseline failure: $case"
        }
    } elseif ($code -ne 0) {
        Write-Host "UNEXPECTED fixed result: $case exit=$code"
        ++$unexpected
    }
}
if ($unexpected) { throw "$unexpected unexpected test results" }
Write-Host "PASS: $name ($($cases.Count) cases)"
