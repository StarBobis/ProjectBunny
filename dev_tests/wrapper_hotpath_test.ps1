# Standalone extracted-production-body tests. Never builds or deploys the DLL.
param([switch]$AddressSanitizer)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\wrapper_hotpath_test'
New-Item -ItemType Directory -Force $out | Out-Null
function Body([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [StringComparison]::Ordinal)
    if ($start -lt 0 -or !$signature) { throw "Missing production signature: $signature" }
    $open = $text.IndexOf('{', $start); $end = $open + 1; $depth = 1
    while ($depth -and $end -lt $text.Length) {
        if ($text[$end] -eq '{') { ++$depth }
        if ($text[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth) { throw "Unbalanced production function: $signature" }
    return $text.Substring($start, $end - $start)
}
function Header([string]$name, [string]$text) {
    [IO.File]::WriteAllText((Join-Path $out $name), $text, [Text.UTF8Encoding]::new($false))
}
$wrapper = [IO.File]::ReadAllText((Join-Path $root 'src/DirectX12/Presentation/DX12WrapperRuntimeUtils.h'))
$baseline = (& git -C $root show HEAD:src/DirectX12/Presentation/DX12WrapperRuntimeUtils.h) -join "`n"
if ($LASTEXITCODE) { throw 'Cannot read wrapper baseline' }
$barrierFunctions = @('static void UnwrapResourceBarrier(', 'static void UnwrapResourceBarrierArray(')
$current = ($barrierFunctions | ForEach-Object { Body $wrapper $_ }) -join "`n"
$old = ($barrierFunctions | ForEach-Object { Body $baseline $_ }) -join "`n"
$current += "`n" + (Body $wrapper 'static void ResourceBarrier(')
$iaSignature = [regex]::Match($wrapper, 'static void IASetVertexBuffers\(\s*ID3D12GraphicsCommandList \*commandList,\s*DX12SimulateContext \*runtimeState,').Value
$current += "`n" + (Body $wrapper $iaSignature)
# The command-list COM endpoint is a test sink; all wrapper control flow is real.
$current = $current.Replace('ID3D12GraphicsCommandList', 'TestCommandList')
Header 'wrapper_under_test.h' ("class DX12WrapperRuntimeUtils : public WrapperTestDependencies { public:`n$current`n};`nclass BaselineWrapper : public WrapperTestDependencies { public:`n$old`n};")
$runtime = [IO.File]::ReadAllText((Join-Path $root 'src/DirectX12/Core/CommandState/DX12CommandListRuntime.cpp'))
$psoSignature = [regex]::Match($runtime, 'void DX12CommandListRuntime::RememberPipelineState\(\s*DX12SimulateContext \*state,').Value
Header 'pso_under_test.h' (Body $runtime $psoSignature)
# Both IA redirect helpers MUST fully initialize every returned element; this is
# the production dependency that permits uninitialized scratch arrays.
$iaHelper = Body $wrapper 'static bool TryRedirectStaticIaVertexBuffers('
$redirect = [IO.File]::ReadAllText((Join-Path $root 'src/DirectX12/Hooks/DX12IaReplacement.h'))
$redirectHelper = Body $redirect 'static bool RedirectVertexBufferViews('
if (!$iaHelper.Contains('redirectedViews[index] = view;') -or !$redirectHelper.Contains('redirectedViews[i] = views[i];')) { throw 'Redirect helper output initialization changed' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs -or $LASTEXITCODE) { throw 'MSVC tools unavailable' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$asan = if ($AddressSanitizer) { '/fsanitize=address /Zi' } else { '' }
$cmd = "call `"$vcvars`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 $asan /I`"$out`" `"$PSScriptRoot\wrapper_hotpath_test.cpp`" /Fo:`"$out\test.obj`" /Fd:`"$out\test.pdb`" /Fe:`"$out\test.exe`" && `"$out\test.exe`""
& $env:COMSPEC /d /s /c $cmd
if ($LASTEXITCODE) { throw "Wrapper standalone test failed: $LASTEXITCODE" }
