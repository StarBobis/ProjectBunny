# Crash self-diagnosis test: compiles the TIMING-only report filter for real
# (same source file the DLL uses) and lets a re-executed child crash, then
# verifies the report, the symbolized stack, the breadcrumbs, the minidump and
# the chaining to the previously installed handler.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\crash-report-test'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force $out | Out-Null
New-Item -ItemType Directory -Force $obj | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$crashSource = Join-Path $root 'src\DirectX12\Core\Diag\DX12CrashReport.cpp'
# /Zi + /DEBUG: the report symbolizes our frames from the local PDB, so the test
# needs one exactly like the deployed TIMING build produces.
# The doubled backslash after the object directory survives cl's argv parsing.
$build = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 /Zi /DDX12_RELEASE_TIMING "$PSScriptRoot\crash_report_test.cpp" "$crashSource" /Fo"$obj\\" /Fe"$out\crash_report_test.exe" /link /DEBUG /INCREMENTAL:NO
exit /b %errorlevel%
"@
[System.IO.File]::WriteAllText((Join-Path $out 'build.bat'), $build)
& cmd.exe /c (Join-Path $out 'build.bat')
if ($LASTEXITCODE -ne 0) { throw 'Crash report test build failed' }

& (Join-Path $out 'crash_report_test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Crash report test failed' }
