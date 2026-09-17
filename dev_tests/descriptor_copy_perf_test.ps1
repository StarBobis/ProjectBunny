param([switch]$Baseline, [switch]$DebugCodegen)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$name = if ($Baseline) { 'baseline' } else { 'current' }
$opt = if ($DebugCodegen) { '/Od' } else { '/O2' }
$out = Join-Path $root "build\descriptor-copy-$name-$($opt.Substring(1))"
New-Item -ItemType Directory -Force $out | Out-Null
function Source([string]$path) {
    if ($Baseline) {
        $text = (& git -C $root show "HEAD:$path") -join "`n"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $path" }
        return $text
    }
    return [IO.File]::ReadAllText((Join-Path $root $path))
}
function Slice([string]$text, [string]$begin, [string]$end) {
    $a = $text.IndexOf($begin); $b = $text.IndexOf($end, $a)
    if ($a -lt 0 -or $b -le $a) { throw "Extraction boundaries changed: $begin" }
    return $text.Substring($a, $b - $a)
}
# Compile actual production classes/functions; no second maintained copy algorithm.
$models = Source 'src/DirectX12/Core/Models/DX12PsoModels.h'
$shard = Source 'src/DirectX12/Tracking/DX12DescriptorHeapShard.h'
$records = Source 'src/DirectX12/Tracking/DX12DescriptorRecords.h'
$slots = Source 'src/DirectX12/Tracking/DX12DescriptorSlotArray.h'
$shadow = Source 'src/DirectX12/Runtime/DX12DescriptorHeapShadowUtils.h'
$hooks = Source 'src/DirectX12/Tracking/DX12ResourceTrackerHooks.cpp'
$types = (Slice $models 'class DescriptorKind' 'class IaBufferRole') +
    (Slice $models 'class DX12DescriptorHeapSummary' 'class DX12PsoRootSummary') +
    ($shard -replace '(?m)^#include.*$', '') +
    $records.Substring($records.IndexOf('class DX12DescriptorViewPayload')) +
    ($slots -replace '(?m)^#include.*$', '')
[IO.File]::WriteAllText((Join-Path $out 'descriptor_types_under_test.h'), $types)
$cache = 'class CacheUnderTest { public: ' +
    (Slice $shadow 'struct ResolvedHeapSlot' 'struct HeapResolveCacheEntry') +
    (Slice $shadow 'static constexpr UINT HeapCacheSetCount' 'static SRWLOCK *HeapLock') + '};'
[IO.File]::WriteAllText((Join-Path $out 'descriptor_cache_under_test.h'), $cache)
$commit = Slice $shadow 'struct HeapVersionCommitSet' '// Descriptor handle increments'
[IO.File]::WriteAllText((Join-Path $out 'descriptor_commit_under_test.h'), $commit)
$copy = Slice $hooks 'struct DescriptorCopyCallScope' 'void DX12ResourceTrackerHookRuntime::RecordCreatedResource('
[IO.File]::WriteAllText((Join-Path $out 'descriptor_hooks_under_test.h'), $copy)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 $opt /I "$out" "$PSScriptRoot\descriptor_copy_perf_test.cpp" /Fo"$out\test.obj" /Fe"$out\test.exe"
exit /b %errorlevel%
"@
[IO.File]::WriteAllText((Join-Path $out 'build.bat'), $build)
& cmd.exe /c (Join-Path $out 'build.bat')
if ($LASTEXITCODE -ne 0) { throw 'Descriptor test build failed' }
# Baseline cache has known data races; do not deliberately execute undefined behavior.
$mode = if ($Baseline) { 'baseline' } else { 'current' }
& (Join-Path $out 'test.exe') $mode
if ($LASTEXITCODE -ne 0) { throw 'Descriptor test failed' }
