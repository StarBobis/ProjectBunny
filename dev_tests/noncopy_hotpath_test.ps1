# Extract production pool/compaction logic; no game/driver is replaced by a pass claim.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\noncopy_hotpath_test'
New-Item -ItemType Directory -Force $out | Out-Null
function Body([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing production function: $signature" }
    $open = $text.IndexOf('{', $start); $end = $open + 1; $depth = 1
    while ($depth -and $end -lt $text.Length) {
        if ($text[$end] -eq '{') { ++$depth }
        if ($text[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth) { throw "Unbalanced production function: $signature" }
    return $text.Substring($start, $end - $start)
}
$executor = [IO.File]::ReadAllText((Join-Path $root 'src/DirectX12/ModRuntime/DX12CommandListExecutor.h'))
$start = $executor.IndexOf('class DX12CommandListExecutorScratch')
$end = $executor.IndexOf("`tclass CommandVmRunScope", $start)
$pool = $executor.Substring($start, $end - $start) + "`n};`n"
foreach ($invariant in @('std::unique_ptr<DX12CommandListExecutorScratch> mFallbackScratch;', 'mScratch(AcquireScratch(&mScratchPooled, &mFallbackScratch))', "mScratch->Reset();`n`t`tif (mScratchPooled)", '--ScratchDepth;')) {
    if (!$executor.Replace("`r", '').Contains($invariant)) { throw "Executor pool ownership changed: $invariant" }
}
$readback = [IO.File]::ReadAllText((Join-Path $root 'src/DirectX12/Tracking/DX12AsyncReadback.cpp'))
$compact = Body $readback 'static UINT DX12ReadbackCompactRecordedEvents('
$submit = Body $readback 'void DX12ReadbackSubmitPendingImpl('
$publication = $submit.IndexOf('gReadbackPendingBatches.push_back(batch);')
if ($publication -lt 0 -or $submit.Substring($publication).Contains('batch->')) {
    throw 'Published readback batch must never be dereferenced after ownership transfer'
}
if (!$submit.Contains('const UINT skippedRecord = DX12ReadbackCompactRecordedEvents(kept);')) { throw 'Production compaction is not wired into submit' }
[IO.File]::WriteAllText((Join-Path $out 'noncopy_under_test.h'), $pool + "`n" + $compact)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'MSVC not found' }
$test = Join-Path $PSScriptRoot 'noncopy_hotpath_test.cpp'
$command = "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /I`"$out`" `"$test`" /Fo:`"$out\test.obj`" /Fe:`"$out\test.exe`" && `"$out\test.exe`""
& $env:COMSPEC /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "Non-CopyDescriptor tests failed: $LASTEXITCODE" }
