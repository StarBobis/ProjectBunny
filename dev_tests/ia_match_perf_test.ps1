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
# The flat view-hash memo is a translation-unit-local class the hash body calls,
# so it is extracted separately and included before the body that uses it.
Header 'ia_hash_memo_under_test.h' (Section $hash 'class IaViewHashMemo' 'uint32_t DX12HashIaBufferContentView(')
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
$parts += FunctionBody $matching 'bool IaHasAnyPrefilterCandidate('
$parts += FunctionBody $matching 'bool IaHashDefinitelyHasNoCandidate('
$parts += FunctionBody $matching 'void DX12ModBuildIaStateNegativeHashSnapshot('
Header 'ia_matching_under_test.h' ($parts -join "`n`n")
# The IA negative verdict is now keyed on the active pre-skin SECTION SET rather
# than the pre-skin runtime generation, which is only sound while every mutation
# of that set moves the set generation. These checks keep that invariant from
# rotting: each mutating function must bump it, and no mutation may appear
# anywhere else in the file.
$preSkinPath = 'src/DirectX12/ModRuntime/DX12PreSkinRuntime.cpp'
$preSkin = Source $preSkinPath
$setBump = 'BumpPreSkinSectionSetGeneration()'
$mutatingFunctions = @(
    'void ClearActivePreSkinTextureOverridesLocked(',
    'void StoreActivePreSkinTextureOverrideLocked(',
    'void ClearPreSkinRuntimeState(')
$mutatingRanges = @()
foreach ($fn in $mutatingFunctions) {
    $body = FunctionBody $preSkin $fn
    if (!$body.Contains($setBump)) { throw "Pre-skin section set mutation without a set-generation bump: $fn" }
    $start = $preSkin.IndexOf($fn, [StringComparison]::Ordinal)
    $mutatingRanges += [pscustomobject]@{ Start = $start; End = $start + $body.Length }
}
foreach ($mutation in @(
        'gActivePreSkinTextureOverrides.clear()',
        'gActivePreSkinTextureOverrides[section] =',
        'gConfirmedPreSkinSections.insert(',
        'gConfirmedPreSkinSections.clear()')) {
    $at = $preSkin.IndexOf($mutation, [StringComparison]::Ordinal)
    while ($at -ge 0) {
        $inside = $false
        foreach ($range in $mutatingRanges) { if ($at -ge $range.Start -and $at -lt $range.End) { $inside = $true } }
        if (!$inside) { throw "Pre-skin section set mutated outside the audited functions: $mutation" }
        $at = $preSkin.IndexOf($mutation, $at + 1, [StringComparison]::Ordinal)
    }
}
# The flat view-hash memo is only an accelerator, so a path that returns a hash
# without publishing it can lose speed but never correctness. Publishing on every
# returning path is what keeps the accelerator from becoming a per-path decision:
# the two cached answers, plus the one the body computes at its end.
$hashBody = FunctionBody $hash 'uint32_t DX12HashIaBufferContentView('
$hashCachedReturns = ($hashBody -split "`n" | Where-Object { $_.Trim() -eq 'return cacheValue.hash;' }).Count
$hashPublishes = ([regex]::Matches($hashBody, 'IaViewHashMemo::Publish\(')).Count
if ($hashPublishes -ne ($hashCachedReturns + 1)) {
    throw "View hash body publishes $hashPublishes of $($hashCachedReturns + 1) returning paths; review every 'return' that carries a hash"
}
# The unresolved memo relies on the production range journal. Keep the two
# lifecycle fixes source-visible: a newly published range must bump the epoch,
# and one epoch must retain both the old and new ranges of a moved resource.
$resolveCache = Source 'src/DirectX12/Tracking/DX12BufferResolveCache.h'
if (!$resolveCache.Contains('secondBegin') -or !$resolveCache.Contains('secondEnd')) {
    throw 'Range journal lost its second invalidated range; moved-resource revalidation is unsafe'
}
if ($resolveCache.Contains('if (!oldHasRange && stats.removedEntries == 0)')) {
    throw 'New resource publication still skips resolve-epoch invalidation when no old cache entry exists'
}
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
# Reset now preserves the draw template in place, rather than moving its large
# inline tables twice. Audit the real reset's complete statement list and compile
# its actual draw-cache mutation; do not replace preservation with a no-op mock.
$resetSignature = [regex]::Match($runtime, 'void DX12CommandListRuntime::Reset\(\s*DX12SimulateContext \*state').Value
if (!$resetSignature) { throw 'Missing resident Reset overload' }
$reset = FunctionBody $runtime $resetSignature
$resetCode = [regex]::Replace($reset, '(?m)//[^\r\n]*', '')
$resetCode = [regex]::Replace($resetCode.Substring($resetCode.IndexOf('{')), '\s+', '')
$expectedReset = '{if(!state)return;DX12RuntimeResetStateForRecording(state);state->barrierScratch.clear();DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan(&state->resolvedDraw);DX12CommandListRuntime::RefreshTrackingCache(state->trackingCache);DX12CommandListRuntime::RememberPipelineState(state,initialState);}'
if ($resetCode -ne $expectedReset) { throw 'Reset statements changed; review draw-cache preservation and update the production-body test' }
$resetFields = FunctionBody $runtime 'static void DX12RuntimeResetStateForRecording('
$resetFieldsCode = [regex]::Replace($resetFields, '(?m)//[^\r\n]*', '')
if ($resetFieldsCode.Contains('resolvedDraw') -or $resetFieldsCode.Contains('*state =') -or $resetFieldsCode.Contains('destroy_at') -or $resetFieldsCode.Contains('new (')) {
    throw 'Targeted reset started mutating/reconstructing draw cache; review its lifetime'
}
$invalidate = FunctionBody $runtime 'void DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan('
$invalidate = $invalidate.Replace('void DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan(', 'static void InvalidateDrawPlan(')
$drawResetCall = [regex]::Match($reset, 'DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan\(&state->resolvedDraw\);').Value.Replace('DX12ResolvedDrawRuntimeStateUtils::InvalidatePlan', 'InvalidateDrawPlan')
Header 'ia_reset_moves_under_test.h' ($invalidate + "`nclass ResetTestState { public: DX12SimulateContextDrawTemplate resolvedDraw; };`n" +
    "static void ResetDrawCacheOnly(ResetTestState *state) {`n$drawResetCall`n}")
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
