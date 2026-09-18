$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\ia_dictionary_hud_test'
New-Item -ItemType Directory -Force $out | Out-Null
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs -or $LASTEXITCODE) { throw 'MSVC tools unavailable' }
$cmd = "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /I`"$root\src\DirectX12`" `"$PSScriptRoot\ia_dictionary_hud_test.cpp`" /Fo:`"$out\test.obj`" /Fe:`"$out\test.exe`" && `"$out\test.exe`""
& $env:COMSPEC /d /s /c $cmd
if ($LASTEXITCODE) { throw "Dictionary HUD regression failed: $LASTEXITCODE" }
