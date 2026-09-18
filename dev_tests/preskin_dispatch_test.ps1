# Standalone production-body tests. No main build or deployment.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\preskin_dispatch_test'
New-Item -ItemType Directory -Force $out | Out-Null
function Source([string]$path) { [IO.File]::ReadAllText((Join-Path $root $path)) }
function Body([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing function: $signature" }
    $open = $text.IndexOf('{', $start); $depth = 1; $end = $open + 1
    while ($depth -gt 0 -and $end -lt $text.Length) {
        if ($text[$end] -eq '{') { ++$depth }
        if ($text[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth) { throw "Unbalanced function: $signature" }
    $text.Substring($start, $end - $start)
}
function Section([string]$text, [string]$begin, [string]$end) {
    $i = $text.IndexOf($begin, [StringComparison]::Ordinal)
    if ($i -lt 0) { throw "Missing section: $begin" }
    $j = $text.IndexOf($end, $i, [StringComparison]::Ordinal)
    if ($j -lt 0) { throw "Missing section end: $end" }
    $text.Substring($i, $j - $i)
}
function Header([string]$name, [string]$text) {
    [IO.File]::WriteAllText((Join-Path $out $name), $text, [Text.UTF8Encoding]::new($false))
}
$pre = Source 'src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp'
$parts = @()
foreach ($fn in @('bool FindComputeSrvByRegister(', 'bool MatchComputeInputSrvsTextureOverrideLocked(', 'bool BuildPreSkinSrvReplacementsFromConfigLocked(', 'uint64_t HashPreSkinMatchSrvKeysForProbe(', 'bool FindMatchCsTextureOverrideLocked(')) {
    $parts += Body $pre $fn
}
Header 'preskin_match_under_test.h' ($parts -join "`n`n")
$hook = Source 'src/DirectX12/Hooks/DX12DispatchHookFlow.h'
Header 'preskin_probe_type_under_test.h' (Section $hook 'class DX12DispatchPreSkinProbe' 'class DX12PreSkinStableFingerprintCacheUtils')
Header 'preskin_verdict_enum_under_test.h' (Section $hook 'enum class DX12ExecVerdictBlock' 'class DX12ExecVerdictDiag')
$probeMethods = @('static DX12DispatchPreSkinProbe &ScratchProbe()', 'static bool ProbePreSkinBindingIdentity(', 'static DX12ExecVerdictBlock EvaluatePreSkinExecVerdict(') | ForEach-Object { Body $hook $_ }
Header 'preskin_hook_under_test.h' ("class HookUnderTest { public:`ninline static bool sIdentityProbeFailLogged = false;`n" + ($probeMethods -join "`n") + "`n};")
$tracker = Source 'src/DirectX12/Tracking/DX12BindingTracker.cpp'
$models = Source 'src/DirectX12/Core/Models/DX12BindingTrackerModels.h'
$declaration = Section $models 'static bool CollectPreSkinBindings(' 'static bool GetSimulateContextPreSkinBindings('
if (!$declaration.Contains('bool tryResidentBindings = true')) { throw 'Existing collector callers must retain resident reuse by default' }
$collect = Body $tracker 'bool DX12BindingTracker::CollectPreSkinBindings('
$prefix = Section $collect 'bool DX12BindingTracker::CollectPreSkinBindings(' 'static thread_local DX12PreSkinComputeSnapshot snapshot;'
$prefix = $prefix.Replace('bool DX12BindingTracker::CollectPreSkinBindings(', 'static bool CollectPrefix(')
# Only the cold collector tail is an adapter: execute the exact production prefix
# through its resident-reuse decision, then observe entry to fresh collection.
Header 'preskin_collect_prefix_under_test.h' ($prefix + "return FreshCollect(uavs, dependencyFingerprint);`n}")
if (!$collect.Contains('if (!snapshotReady) {') -or !$collect.Contains('BuildRuntimeComputeBindingSnapshot(') -or !$collect.Contains('FillPreSkinComputeSnapshotFromFull(') -or !$collect.Contains('DX12PreSkinBindingCacheUtils::BuildKey<DX12BindingTrackerQueryCacheUtils>')) { throw 'Collector snapshot/fallback/content-key contract changed' }
$prepare = Body $hook 'static void PreparePreSkinProbe('
if ($prepare -notmatch '&probe->rootConstants,\s*&probe->bindingDependencyFingerprint,\s*!probe->bindingIdentityChecked\)') { throw 'Dispatch no longer connects the per-dispatch identity memo to collector reuse policy' }
$execute = Body $hook ('static void Execute(')
if (([regex]::Matches($execute, 'EvaluatePreSkinExecVerdict\(')).Count -ne 1 -or $execute.Contains('else if (!ProbePreSkinBindingIdentity')) { throw 'Execute must evaluate verdict only once' }
$snapshot = Section $tracker 'struct DX12PreSkinComputeSnapshot' 'static bool BuildRuntimeComputeBindingSnapshot('
Header 'preskin_snapshot_type_under_test.h' $snapshot
Header 'preskin_snapshot_under_test.h' ((Body $tracker 'static bool BuildRuntimeComputeBindingSnapshot(') + "`n" + (Body $tracker 'static void FillPreSkinComputeSnapshotFromFull('))
# Enumerate snapshot fields so new fields cannot silently escape overwrite tests.
$fields = @('pipelineState','computeRootSignature','cbvSrvUavHeap','runtimeCbvSrvUavHeapResolved','runtimeCbvSrvUavHeap','computeBindingSerial','bindingQuerySerial','computeTableValidCount','computeTables','computeRootDescriptors','computeRootConstants')
$declaredFields = [regex]::Matches($snapshot, '(?m)^\s*\w+\s+\*?(\w+)(?:\[[^\]]+\])?\s*=') | ForEach-Object { $_.Groups[1].Value }
if (($declaredFields -join ',') -ne ($fields -join ',')) { throw 'Snapshot fields changed: update complete overwrite coverage' }
# Measure the real production snapshot layout, not the adapters in the logic test.
$bindingModels = Source 'src/DirectX12/Core/Models/DX12BindingModels.h'
$psoModels = Source 'src/DirectX12/Core/Models/DX12PsoModels.h'
$rootTypes = Section $bindingModels 'constexpr UINT MaxRootParameters' 'class CommandListBindingState'
$heapType = Section $psoModels 'class DX12DescriptorHeapSummary' 'class DX12PsoRootSummary'
Header 'preskin_snapshot_layout.cpp' ("#include <d3d12.h>`n#include <iostream>`n" + $heapType + $rootTypes + $snapshot + '`nint main() { std::cout << "production snapshot TLS bytes/thread=" << sizeof(DX12PreSkinComputeSnapshot) << " constants=" << sizeof(RootConstantsState) * MaxRootParameters << "\\n"; }'.Replace('`n', "`n").Replace('\\n', '\n'))
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$test = Join-Path $PSScriptRoot 'preskin_dispatch_test.cpp'
$command = "call `"$vcvars`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /I`"$out`" `"$test`" /Fo:`"$out\preskin_dispatch_test.obj`" /Fe:`"$out\preskin_dispatch_test.exe`" && `"$out\preskin_dispatch_test.exe`""
& $env:COMSPEC /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "PreSkin standalone test failed: $LASTEXITCODE" }
$command = "call `"$vcvars`" >nul && cl /nologo /std:c++20 /EHsc /O2 `"$out\preskin_snapshot_layout.cpp`" /Fo:`"$out\preskin_snapshot_layout.obj`" /Fe:`"$out\preskin_snapshot_layout.exe`" && `"$out\preskin_snapshot_layout.exe`""
& $env:COMSPEC /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "PreSkin production-layout test failed: $LASTEXITCODE" }
