param([switch]$Baseline, [Alias('Debug')][switch]$DebugCodegen, [string]$BaselineRef = 'HEAD')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$name = if ($Baseline) { 'baseline' } else { 'current' }
$opt = if ($DebugCodegen) { '/Od' } else { '/O2' }
$out = Join-Path $root "build\descriptor-copy-$name-$($opt.Substring(1))"
New-Item -ItemType Directory -Force $out | Out-Null
function Source([string]$path) {
    if ($Baseline) {
        $text = (& git -C $root show "${BaselineRef}:$path") -join "`n"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $path" }
        return $text
    }
    return [IO.File]::ReadAllText((Join-Path $root $path))
}
function Slice([string]$text, [string]$begin, [string]$end) {
    $a = $text.IndexOf($begin)
    if ($a -lt 0) { throw "Extraction start changed: $begin" }
    $b = $text.IndexOf($end, $a)
    if ($b -le $a) { throw "Extraction end changed: $end" }
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
$threadSafeCache = $shadow.Contains('TryAcquireSRWLockShared(&set.lock)')
$cache = '#define HAS_THREAD_SAFE_HEAP_CACHE ' + [int]$threadSafeCache + "`n" + 'class CacheUnderTest { public: ' +
    (Slice $shadow 'struct ResolvedHeapSlot' '// CopyDescriptors API calls') +
    (Slice $shadow 'static constexpr UINT HeapCacheSetCount' 'static SRWLOCK *HeapLock') + '};'
[IO.File]::WriteAllText((Join-Path $out 'descriptor_cache_under_test.h'), $cache)
$commit = Slice $shadow 'struct HeapVersionCommitSet' '// Descriptor handle increments'
[IO.File]::WriteAllText((Join-Path $out 'descriptor_commit_under_test.h'), $commit)
# Keep the real ordered-map slow path, SRW locks, global cache, TLS32 and
# sequential predictor together, without modifying any resolver algorithm.
$resolver = 'namespace ResolverUnderTest {' + "`n" +
    'inline std::deque<DescriptorHeapRecord> gDescriptorHeaps;' + "`n" +
    'inline std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;' + "`n" +
    'inline SRWLOCK gDescriptorLock = SRWLOCK_INIT;' + "`n" +
    (Slice $shadow 'class DX12DescriptorHeapShadowUtils' 'static bool ResolveAndLockHeapSlot(') + '}; }'
[IO.File]::WriteAllText((Join-Path $out 'descriptor_resolver_under_test.h'), $resolver)
$hasMemo = $hooks.Contains('struct DescriptorCopyHeapMemo')
$copyStart = if ($hasMemo) { 'struct DescriptorCopyHeapMemo' } else { 'struct DescriptorCopyCallScope' }
$copy = Slice $hooks $copyStart 'void DX12ResourceTrackerHookRuntime::RecordCreatedResource('
$copy = '#define HAS_DESCRIPTOR_COPY_HEAP_MEMO ' + [int]$hasMemo + "`n" + $copy
[IO.File]::WriteAllText((Join-Path $out 'descriptor_hooks_under_test.h'), $copy)
Write-Host "Source=$name baselineRef=$BaselineRef destinationMemo=$hasMemo optimization=$opt"
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
# The executable detects cache synchronization from the extracted source; HEAD
# and current both exercise concurrency when the snapshot has the SRW fix.
$mode = if ($Baseline) { 'baseline' } else { 'current' }
& (Join-Path $out 'test.exe') $mode
if ($LASTEXITCODE -ne 0) { throw 'Descriptor test failed' }
