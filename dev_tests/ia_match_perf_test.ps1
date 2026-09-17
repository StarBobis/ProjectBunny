# Standalone production-body tests; no project build, artifacts stay under build.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\ia_match_perf_test'
New-Item -ItemType Directory -Force $out | Out-Null
function Source([string]$path) { [IO.File]::ReadAllText((Join-Path $root $path)) }
function FunctionBody([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing function: $signature" }
    $open = $text.IndexOf('{', $start)
    $depth = 1; $end = $open + 1
    while ($depth -gt 0 -and $end -lt $text.Length) {
        if ($text[$end] -eq '{') { ++$depth }
        if ($text[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth) { throw "Unbalanced function: $signature" }
    return $text.Substring($start, $end - $start)
}
function Section([string]$text, [string]$begin, [string]$end) {
    $i = $text.IndexOf($begin, [StringComparison]::Ordinal)
    $j = $text.IndexOf($end, $i, [StringComparison]::Ordinal)
    if ($i -lt 0 -or $j -lt 0) { throw "Missing section: $begin" }
    return $text.Substring($i, $j - $i)
}
function Header([string]$name, [string]$text) {
    [IO.File]::WriteAllText((Join-Path $out $name), $text, [Text.UTF8Encoding]::new($false))
}
$decision = Source 'src/DirectX12/Core/CommandState/DX12IaDecisionState.h'
Header 'ia_plan_under_test.h' (Section $decision 'class DX12IaHashTargetPlan' 'enum class DX12CompiledDrawDecisionKind')
$cache = Source 'src/DirectX12/Core/CommandState/DX12IaHashCaches.h'
Header 'ia_cache_under_test.h' ($cache -replace '(?m)^#(?:pragma|include).*$', '')
$simulate = Source 'src/DirectX12/Core/CommandState/DX12SimulateContext.h'
Header 'ia_draw_template_under_test.h' (Section $simulate 'class DX12SimulateContextDrawTemplate' 'class DX12ResolvedDrawRuntimeStateUtils')
$hash = Source 'src/DirectX12/Tracking/DX12ResourceContentHashing.cpp'
Header 'ia_hash_under_test.h' (FunctionBody $hash 'uint32_t DX12HashIaBufferContentView(')
$runtime = Source 'src/DirectX12/Core/CommandState/DX12CommandListRuntime.cpp'
Header 'ia_slots_under_test.h' ((FunctionBody $runtime 'uint32_t DX12CommandListRuntime::GetOrBuildCachedIndexViewHash(') + "`n" + (FunctionBody $runtime 'uint32_t DX12CommandListRuntime::GetOrBuildCachedVertexViewHash('))
$matching = Source 'src/DirectX12/ModRuntime/DX12TextureOverrideMatching.cpp'
$executor = Source 'src/DirectX12/ModRuntime/DX12CommandListExecutor.cpp'
$parts = @()
foreach ($signature in @('bool TextureOverrideHasDrawKind(', 'bool TextureOverrideHasDrawContextMatch(', 'bool TextureOverrideMatchesIaBinding(', 'UINT64 MakeIaVertexIndexKey(', 'bool NumericMatchIsExactValue(', 'void BuildDx12IaTextureOverrideIndex(', 'const std::vector<size_t> *FindIaTextureOverridePrefilterCandidatesLocked(')) {
    $parts += FunctionBody $matching $signature
}
foreach ($signature in @('bool TextureOverrideMatchCsSatisfied(', 'bool TextureOverrideDependsOnActivePreSkin(', 'const std::unordered_set<std::wstring> &EmptyActivePreSkinSections(', 'bool IaCandidateIndexesDependOnActivePreSkin(')) {
    $parts += FunctionBody $executor $signature
}
$parts += FunctionBody $matching 'bool IaHashDefinitelyHasNoCandidate('
Header 'ia_matching_under_test.h' ($parts -join "`n`n")
# Compile the real declarations, local build outputs and publication swaps.
# A missing global OR a missing swap must fail rather than be hidden by doubles.
$internal = Source 'src/DirectX12/ModRuntime/DX12ModRuntimeInternal.h'
$load = Source 'src/DirectX12/ModRuntime/DX12ModRuntimeLoad.cpp'
Header 'ia_globals_under_test.h' ((Section $internal 'class DX12IaTextureOverrideCandidate' 'class DX12TriggeredTextureOverride') + "`n" +
    ([regex]::Match($internal, '(?m)^inline Bunny::TextureOverrideMap gTextureOverrides;').Value))
$locals = Section $load 'std::vector<DX12IaTextureOverrideCandidate> iaTextureOverrides;' 'std::vector<DX12PreSkinTextureOverrideCandidate> preSkinTextureOverrides;'
$buildCall = Section $load "`tBuildDx12IaTextureOverrideIndex(" "`tBuildDx12CompiledCommandListPlans("
$countFlag = Section $load 'const bool iaPrefilterCountSensitive =' 'bool hasIaResidentStateRequirements ='
$publish = Section $load 'gIaTextureOverrides.swap(iaTextureOverrides);' 'DX12ModClearIaReplacementRuntimeCaches();'
$owningSwap = [regex]::Match($load, 'gTextureOverrides\.swap\(textureOverrides\);').Value
$lockStart = $load.IndexOf('DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock', $load.IndexOf($buildCall))
$lockEnd = $load.IndexOf('ReleaseSRWLockExclusive(&gModLock)', $lockStart)
$locked = $load.Substring($lockStart, $lockEnd - $lockStart)
foreach ($name in @('gIaTextureOverrides', 'gIaIndexTextureOverrideIndex', 'gIaAnyVertexTextureOverrideIndex', 'gIaMergedVertexTextureOverrideIndex')) {
    if (!$locked.Contains("$name.swap(")) { throw "IA publication missing from gModLock: $name" }
}
if (!$locked.Contains('++gReloadGeneration;')) { throw 'IA publication lacks reload generation bump' }
$publication = @"
static void Build(const Bunny::TextureOverrideMap &configs) {
    Bunny::TextureOverrideMap textureOverrides = configs;
    std::unordered_map<std::wstring, uint32_t> textureOverrideSectionIds;
    std::unordered_map<std::wstring, bool> textureOverridePreRuntimeEffect, textureOverridePostRuntimeEffect;
    $locals
    $buildCall
    $countFlag
    $owningSwap
    $publish
}
"@
Header 'ia_publication_under_test.h' $publication
$moveOut = [regex]::Match($runtime, 'DX12SimulateContextDrawTemplate persistentResolvedDraw\s*=\s*std::move\(state->resolvedDraw\);').Value
$moveIn = [regex]::Match($runtime, 'state->resolvedDraw = std::move\(persistentResolvedDraw\);').Value
if (!$moveOut -or !$moveIn) { throw 'Reset draw-template preservation changed; review cache lifetime' }
Header 'ia_reset_moves_under_test.h' ("class ResetTestState { public: DX12SimulateContextDrawTemplate resolvedDraw; };`n" +
    "static void ResetDrawCacheOnly(ResetTestState *state) {`n$moveOut`n$moveIn`n}")
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) { throw 'Visual Studio Installer vswhere not found' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path $vcvars)) { throw "Missing MSVC environment: $vcvars" }
$test = Join-Path $PSScriptRoot 'ia_match_perf_test.cpp'
$command = "call `"$vcvars`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /I`"$out`" `"$test`" /Fo:`"$out\ia_match_perf_test.obj`" /Fe:`"$out\ia_match_perf_test.exe`" && `"$out\ia_match_perf_test.exe`""
& $env:COMSPEC /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "IA standalone test failed: $LASTEXITCODE" }
