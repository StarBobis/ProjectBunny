# C++20 standalone production-body tests; no main build or deployment.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\executor_prepare_test'
New-Item -ItemType Directory -Force $out | Out-Null
function Source([string]$path) { [IO.File]::ReadAllText((Join-Path $root $path)).Replace("`r", '') }
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
function ExtractType([string]$text, [string]$name) { (Body $text "class $name") + ';' }
function Header([string]$name, [string]$text) {
    [IO.File]::WriteAllText((Join-Path $out $name), $text, [Text.UTF8Encoding]::new($false))
}
$cpp = Source 'src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp'
$header = Source 'src/DirectX12/ModRuntime/DX12CommandListExecutor.h'
$internal = Source 'src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h'
$oldCpp = (& git -C $root show '357ba2b:src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp') -join "`n"
if ($LASTEXITCODE) { throw 'Cannot read baseline' }
$oldInternal = (& git -C $root show '357ba2b:src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h') -join "`n"
if ($LASTEXITCODE) { throw 'Cannot read baseline types' }
# The first occurrence is a forward declaration; select the definition explicitly.
$loaded = $internal.Substring($internal.IndexOf("class DX12LoadedResource`n`n{"))
Header 'resource_types.h' ((ExtractType $loaded 'DX12LoadedResource') + "`n" + (ExtractType $internal 'DX12IaResourceViewSnapshot') + "`n" + (ExtractType $oldInternal 'DX12IaResourceViewSnapshot').Replace('DX12IaResourceViewSnapshot', 'LegacyIaResourceViewSnapshot'))
$names = @('void ReleaseIaResourceViewSnapshot(', 'void AddRefIaResourceViewSnapshot(', 'UINT IaViewByteWidthForResource(', 'static void SnapshotIaResourceViewMetadata(', 'void SnapshotIaResourceViewLocked(', 'static bool IaResourceViewSnapshotRequiresWrite(', 'static void SnapshotIaResourceViewReadOnlyLocked(', 'void SnapshotIaResourceViewForCommandList(', 'void ApplyIaResourceBarriers(', 'bool AppendResourceViewsForCommandList(')
Header 'resource_functions.h' (($names | ForEach-Object { Body $cpp $_ }) -join "`n`n")
$legacyNames = @('void ReleaseIaResourceViewSnapshot(', 'void AddRefIaResourceViewSnapshot(', 'void SnapshotIaResourceViewLocked(', 'static void SnapshotIaResourceViewReadOnlyLocked(')
$legacy = ($legacyNames | ForEach-Object { Body $oldCpp $_ }) -join "`n`n"
$legacy = $legacy.Replace('DX12IaResourceViewSnapshot', 'LegacyIaResourceViewSnapshot').Replace('ReleaseIaResourceViewSnapshot', 'LegacyRelease').Replace('AddRefIaResourceViewSnapshot', 'LegacyAddRef').Replace('SnapshotIaResourceViewReadOnlyLocked', 'LegacyReadOnly').Replace('SnapshotIaResourceViewLocked', 'LegacySnapshot')
Header 'legacy_resource_functions.h' $legacy
Header 'executor_tail.h' (Body $header 'void ApplyTextureOverrideDrawSrvResources(')
# Assert the optimized helper is invoked only after the unchanged VM/IA body.
$run = Body $header "void RunTextureOverride(`n`t`tconst DX12TextureOverrideMatch &match"
if (!$run.EndsWith("ApplyTextureOverrideDrawSrvResources(*config, compiledPlanView, match);`n`t}")) { throw 'Executor tail placement changed' }
if ($cpp.Contains('snapshot.metadata')) { throw 'Full metadata consumer remains' }
$vm = Source 'src/common/MigotoCommandVmUtils.hpp'
$oldVm = (& git -C $root show '357ba2b:src/common/MigotoCommandVmUtils.hpp') -join "`n"
if ($LASTEXITCODE) { throw 'Cannot read baseline VM' }
Header 'legacy_vm.h' $oldVm.Replace('MigotoCommandVmUtils', 'LegacyCommandVmUtils')
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$test = Join-Path $PSScriptRoot 'executor_prepare_test.cpp'
$command = "call `"$vcvars`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /I`"$out`" /I`"$root\src\common`" `"$test`" /Fo:`"$out\executor_prepare_test.obj`" /Fe:`"$out\executor_prepare_test.exe`" && `"$out\executor_prepare_test.exe`""
& $env:COMSPEC /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "Executor standalone test failed: $LASTEXITCODE" }
