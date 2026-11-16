[CmdletBinding()]
param([string]$Item='',[switch]$SpecialOnly,[string]$ReportPointer='')
$ErrorActionPreference='Stop'
if(-not [Environment]::Is64BitProcess -or $PSVersionTable.PSVersion.Major -lt 7){throw 'Use x64 PowerShell 7.'}
if($Item -and $Item -notin @('F-152','F-153','F-200b','F-107')){throw 'Unknown component specialization.'}
. (Join-Path $PSScriptRoot 'process.ps1')
$simulationRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=Join-Path $simulationRoot 'build'
$candidateBuild=Join-Path $buildRoot 'candidate'
$componentHost=Join-Path $buildRoot 'tools\dx11_proxy_component_host.exe'
$nativeCompiler=Join-Path ([Environment]::SystemDirectory) 'd3dcompiler_47.dll'
$nativeDxgi=Join-Path ([Environment]::SystemDirectory) 'dxgi.dll'
$providerHash=(Get-FileHash -LiteralPath $nativeCompiler).Hash
$runRoot=Join-Path $buildRoot ('c\'+[guid]::NewGuid().ToString('N').Substring(0,12))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$results=[Collections.Generic.List[object]]::new()
$nativePassed=@{}
foreach($kind in @('compiler','dxgi')) {
    $caseRoot=Join-Path $runRoot "native-$kind"
    New-Item -ItemType Directory -Path $caseRoot | Out-Null
    $caseExe=Join-Path $caseRoot 'component-host.exe'
    Copy-Item -LiteralPath $componentHost -Destination $caseExe
    $library=if($kind -eq 'compiler'){$nativeCompiler}else{$nativeDxgi}
    $result=Invoke-IsolatedSimulationProcess -Executable $caseExe -Arguments @($library,$kind) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host')
    $nativePassed[$kind]=$result.status -eq 'PASS'
    $results.Add([pscustomobject]@{id="native-$kind";status=$result.status;input=$library;sha256=(Get-FileHash -LiteralPath $library).Hash;detail=$result})
    Write-Host "[$($result.status)] native-$kind"
}
foreach($kind in @('compiler','dxgi')) {
    $inputDll=if($kind -eq 'compiler'){Join-Path $candidateBuild 'src\D3DCompiler_46\d3dcompiler_47.dll'}else{Join-Path $candidateBuild 'src\DirectXGI\dxgi.dll'}
    $inputHash=(Get-FileHash -LiteralPath $inputDll).Hash
    foreach($mode in @('normal','disabled','blocked')) {
        $id="$kind-$mode"
        if(-not $nativePassed[$kind]){$results.Add([pscustomobject]@{id=$id;status='NOT_RUN';detail='Native prerequisite failed.'});continue}
        $caseRoot=Join-Path $runRoot $id
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $caseExe=Join-Path $caseRoot 'component-host.exe'
        Copy-Item -LiteralPath $componentHost -Destination $caseExe
        $copiedDll=Join-Path $caseRoot ([IO.Path]::GetFileName($inputDll))
        Copy-Item -LiteralPath $inputDll -Destination $copiedDll
        if((Get-FileHash -LiteralPath $copiedDll).Hash -ne $inputHash){throw 'Component DLL changed during copying.'}
        if($kind -eq 'compiler') {
            $provider=Join-Path $caseRoot 'D3DCompiler_46_org.dll'
            Copy-Item -LiteralPath $nativeCompiler -Destination $provider
            if((Get-FileHash -LiteralPath $provider).Hash -ne $providerHash){throw 'Compatible native provider differs from system source.'}
        }
        $calls=if($mode -eq 'disabled'){0}else{1}
        "[Logging]`ncalls=$calls`nunbuffered=1`ndebug=0`nforce_cpu_affinity=0`n" |
            Set-Content -LiteralPath (Join-Path $caseRoot 'd3dx.ini') -Encoding utf8
        $logName=if($kind -eq 'compiler'){'D3DCompiler_46_log.txt'}else{'dxgi_log.txt'}
        $logPath=Join-Path $caseRoot $logName
        if($mode -eq 'blocked'){New-Item -ItemType Directory -Path $logPath | Out-Null}
        $result=Invoke-IsolatedSimulationProcess -Executable $caseExe -Arguments @($copiedDll,$kind) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host')
        if($result.status -eq 'PASS' -and $mode -eq 'blocked' -and -not (Test-Path -LiteralPath $logPath -PathType Container)) {
            $result.status='FAIL';$result.message='Log-open failure fixture was modified.'
        }
        if((Get-FileHash -LiteralPath $copiedDll).Hash -ne $inputHash){$result.status='HARNESS_ERROR';$result.message='DLL changed during testing.'}
        $results.Add([pscustomobject]@{id=$id;status=$result.status;input=$copiedDll;sha256=$inputHash;loggingCalls=$calls;unbuffered=1;detail=$result})
        Write-Host "[$($result.status)] $id"
    }
}
 $asanRoot=Join-Path $runRoot 'compiler-ownership'
New-Item -ItemType Directory -Path $asanRoot | Out-Null
$asanHost=Join-Path $buildRoot 'tools\dx11_proxy_component_asan_host.exe'
$asanSource=Join-Path $candidateBuild 'audit\compiler_audit_asan.dll'
$asanExe=Join-Path $asanRoot 'component-host.exe'; $asanDll=Join-Path $asanRoot 'compiler_audit_asan.dll'
Copy-Item -LiteralPath $asanHost -Destination $asanExe
Copy-Item -LiteralPath $asanSource -Destination $asanDll
Copy-Item -LiteralPath $nativeCompiler -Destination (Join-Path $asanRoot 'D3DCompiler_46_org.dll')
Copy-Item -LiteralPath (Join-Path $candidateBuild 'src\DirectX11\clang_rt.asan_dynamic-x86_64.dll') -Destination $asanRoot
$asanEnvironment=@{ASAN_OPTIONS='halt_on_error=1:alloc_dealloc_mismatch=1:new_delete_type_mismatch=1'}
"[Logging]`ncalls=0`nunbuffered=0`n[Rendering]`nstorage_directory=shaders`nexport_shaders=1`n" | Set-Content -LiteralPath (Join-Path $asanRoot 'd3dx.ini') -Encoding utf8
$export=Invoke-IsolatedSimulationProcess -Executable $asanExe -Arguments @($asanDll,'compiler-export') -WorkingDirectory $asanRoot -LogPrefix (Join-Path $asanRoot 'export') -EnvironmentOverrides $asanEnvironment
$exported=@(Get-ChildItem -LiteralPath (Join-Path $asanRoot 'shaders') -Filter '*.txt' -ErrorAction SilentlyContinue)
if($export.status -eq 'PASS' -and $exported.Count -ne 1){$export.status='FAIL';$export.message='Compiler did not export the unique real original source fixture.'}
$results.Add([pscustomobject]@{id='compiler-asan-export';status=$export.status;input=$asanDll;sha256=(Get-FileHash -LiteralPath $asanDll).Hash;detail=$export})
Write-Host "[$($export.status)] compiler-asan-export"
if($export.status -eq 'PASS') {
    $replace=Join-Path $exported[0].DirectoryName ($exported[0].BaseName+'_replace.txt')
    [IO.File]::WriteAllText($replace,'float4 main():SV_Target{return float4(4,3,2,1);}',[Text.UTF8Encoding]::new($false))
    "[Logging]`ncalls=0`nunbuffered=0`n[Rendering]`nstorage_directory=shaders`nexport_shaders=0`n" | Set-Content -LiteralPath (Join-Path $asanRoot 'd3dx.ini') -Encoding utf8
    $replacement=Invoke-IsolatedSimulationProcess -Executable $asanExe -Arguments @($asanDll,'compiler-replacement') -WorkingDirectory $asanRoot -LogPrefix (Join-Path $asanRoot 'replacement') -EnvironmentOverrides $asanEnvironment
}else{$replacement=[pscustomobject]@{status='NOT_RUN';exitCode=$null;message='Export prerequisite failed.'}}
$results.Add([pscustomobject]@{id='compiler-asan-replacement';status=$replacement.status;input=$asanDll;sha256=(Get-FileHash -LiteralPath $asanDll).Hash;detail=$replacement})
Write-Host "[$($replacement.status)] compiler-asan-replacement"
if($Item -eq 'F-107') {
    $wrapper=Join-Path $candidateBuild 'src\D3DCompiler_46\d3dcompiler_47.dll'
    $partialProvider=Join-Path $buildRoot 'tools\dx11_compiler_partial_provider.dll'
    if(-not (Test-Path -LiteralPath $wrapper)){throw "F-107 wrapper was not built: $wrapper"}
    if(-not (Test-Path -LiteralPath $partialProvider)){throw "F-107 partial provider was not built: $partialProvider"}
    foreach($mode in @('compiler-exports','compiler-no-provider','compiler-partial-provider','compiler-late-provider')) {
        $id="F-107-$mode"
        $caseRoot=Join-Path $runRoot $id
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        Copy-Item -LiteralPath $componentHost -Destination (Join-Path $caseRoot 'component-host.exe')
        $copiedDll=Join-Path $caseRoot 'd3dcompiler_47.dll'
        Copy-Item -LiteralPath $wrapper -Destination $copiedDll
        if($mode -eq 'compiler-partial-provider') {
            Copy-Item -LiteralPath $partialProvider -Destination (Join-Path $caseRoot 'D3DCompiler_46_org.dll')
        }
        if($mode -eq 'compiler-late-provider') {
            Copy-Item -LiteralPath $partialProvider -Destination (Join-Path $caseRoot 'late-provider.dll')
        }
        "[Logging]`ncalls=1`nunbuffered=1`ndebug=0`nforce_cpu_affinity=0`n" |
            Set-Content -LiteralPath (Join-Path $caseRoot 'd3dx.ini') -Encoding utf8
        $result=Invoke-IsolatedSimulationProcess -Executable (Join-Path $caseRoot 'component-host.exe') -Arguments @($copiedDll,$mode) -WorkingDirectory $caseRoot -LogPrefix (Join-Path $caseRoot 'host')
        if($mode -eq 'compiler-late-provider' -and $result.status -eq 'PASS') {
            $logPath=Join-Path $caseRoot 'D3DCompiler_46_log.txt'
            $latched=@(Select-String -LiteralPath $logPath -Pattern 'initialization is latched for this process' -SimpleMatch -ErrorAction SilentlyContinue).Count
            $moduleMissing=@(Select-String -LiteralPath $logPath -Pattern 'GetLastError=126' -SimpleMatch -ErrorAction SilentlyContinue).Count
            $buildMarker=@(Select-String -LiteralPath $logPath -Pattern 'f107-latch-v2' -SimpleMatch -ErrorAction SilentlyContinue).Count
            $pathChars=@(Select-String -LiteralPath $logPath -Pattern 'path_chars=' -SimpleMatch -ErrorAction SilentlyContinue).Count
            if($latched -ne 1){$result.status='FAIL';$result.message="Expected exactly one latched diagnostic, found $latched."}
            elseif($moduleMissing -lt 1){$result.status='FAIL';$result.message='Latched diagnostic does not contain GetLastError=126.'}
            $result | Add-Member -NotePropertyName latchedMarker -NotePropertyValue $buildMarker -Force
            $result | Add-Member -NotePropertyName pathChars -NotePropertyValue $pathChars -Force
        }
        $results.Add([pscustomobject]@{id=$id;status=$result.status;input=$copiedDll;sha256=(Get-FileHash -LiteralPath $copiedDll).Hash;detail=$result})
        Write-Host "[$($result.status)] $id"
    }

    # Independent export-table record: dumpbin prints ordinal, hint, RVA and
    # name. Cross-check the DebugSetMute RVA against the address the loader
    # actually resolves for ordinal 2 so a wrong @2 target cannot pass.
    $dumpbin=Get-Command dumpbin.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
    if(-not $dumpbin) {
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if(Test-Path -LiteralPath $vswhere) {
            $vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if($LASTEXITCODE -eq 0 -and $vsPath) {
                $dumpbin=Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match '\\Hostx64\\x64\\dumpbin\.exe$' } |
                    Select-Object -First 1 -ExpandProperty FullName
            }
        }
    }
    $exportCase=Join-Path $runRoot 'F-107-dumpbin-exports'
    New-Item -ItemType Directory -Path $exportCase | Out-Null
    $dumpbinLog=Join-Path $exportCase 'dumpbin-exports.txt'
    $dumpbinStatus='NOT_RUN'; $dumpbinMessage='dumpbin.exe was not found'
    $exports=$null
    if($dumpbin) {
        & $dumpbin /nologo /exports $wrapper 2>&1 | Set-Content -LiteralPath $dumpbinLog -Encoding utf8
        if($LASTEXITCODE -eq 0) {
            $exports=@()
            foreach($line in (Get-Content -LiteralPath $dumpbinLog)) {
                if($line -match '^\s+(\d+)\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]{8,16})\s+(\S+)(?:\s+=.*)?\s*$') {
                    $exports += [pscustomobject]@{ordinal=[int]$Matches[1];rva=[Convert]::ToInt64($Matches[2],16);name=$Matches[3]}
                }
            }
            $expected=@('D3DAssemble','DebugSetMute','D3DCompile2','D3DCompile','D3DCompileFromFile','D3DCompressShaders','D3DCreateBlob','D3DDecompressShaders','D3DDisassemble10Effect','D3DDisassemble11Trace','D3DDisassemble','D3DDisassembleRegion','D3DGetBlobPart','D3DGetDebugInfo','D3DGetInputAndOutputSignatureBlob','D3DGetInputSignatureBlob','D3DGetOutputSignatureBlob','D3DGetTraceInstructionOffsets','D3DPreprocess','D3DReadFileToBlob','D3DReflect','D3DReturnFailure1','D3DSetBlobPart','D3DStripShader','D3DWriteBlobToFile')
            $dumpbinStatus='PASS'; $dumpbinMessage=''
            if($exports.Count -ne 25){$dumpbinStatus='FAIL';$dumpbinMessage="Expected 25 exports, found $($exports.Count)."}
            elseif(@($exports | Group-Object ordinal | Where-Object Count -ne 1).Count){$dumpbinStatus='FAIL';$dumpbinMessage='Duplicate export ordinal.'}
            elseif((@($exports | ForEach-Object ordinal | Sort-Object) -join ',') -ne ((1..25) -join ',')){$dumpbinStatus='FAIL';$dumpbinMessage='Export ordinals are not exactly 1..25.'}
            else {
                foreach($ordinal in 1..25) {
                    $entry=$exports | Where-Object ordinal -eq $ordinal
                    if($entry.name -ne $expected[$ordinal-1]){$dumpbinStatus='FAIL';$dumpbinMessage="Ordinal $ordinal is $($entry.name), expected $($expected[$ordinal-1]).";break}
                }
            }
            if($dumpbinStatus -eq 'PASS') {
                $debugEntry=$exports | Where-Object ordinal -eq 2
                $module=[IntPtr]::Zero
                try {
                    $module=[System.Runtime.InteropServices.NativeLibrary]::Load($wrapper)
                    $address=[System.Runtime.InteropServices.NativeLibrary]::GetExport($module,'DebugSetMute')
                    $loadedRva=$address.ToInt64()-$module.ToInt64()
                    if($loadedRva -ne $debugEntry.rva){$dumpbinStatus='FAIL';$dumpbinMessage="DebugSetMute RVA mismatch: dumpbin=$($debugEntry.rva), loaded=$loadedRva"}
                } catch {
                    $dumpbinStatus='FAIL'; $dumpbinMessage="DebugSetMute RVA cross-check failed: $($_.Exception.Message)"
                } finally {
                    if($module -ne [IntPtr]::Zero){[System.Runtime.InteropServices.NativeLibrary]::Free($module)}
                }
            }
        } else { $dumpbinStatus='FAIL'; $dumpbinMessage="dumpbin failed with exit code $LASTEXITCODE" }
    }
    $scriptPath=if($PSCommandPath){$PSCommandPath}else{$MyInvocation.MyCommand.Path}
    $scriptHash=if($scriptPath -and (Test-Path -LiteralPath $scriptPath)){(Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash}else{''}
    $results.Add([pscustomobject]@{id='F-107-dumpbin-exports';status=$dumpbinStatus;input=$wrapper;sha256=(Get-FileHash -LiteralPath $wrapper).Hash;detail=[pscustomobject]@{message=$dumpbinMessage;log=$dumpbinLog;count=@($exports).Count;parser='suffix-aware';script=$scriptPath;scriptSha256=$scriptHash}})
    Write-Host "[$($dumpbinStatus)] F-107-dumpbin-exports"
}
if($Item -eq 'F-200b') {
    $pathRoot=Join-Path $asanRoot 'path-reject'
    New-Item -ItemType Directory -Path $pathRoot -Force | Out-Null
    $pathExe=Join-Path $pathRoot 'component-host.exe'; $pathDll=Join-Path $pathRoot 'compiler_audit_asan.dll'
    Copy-Item -LiteralPath $asanHost -Destination $pathExe
    Copy-Item -LiteralPath $asanSource -Destination $pathDll
    Copy-Item -LiteralPath $nativeCompiler -Destination (Join-Path $pathRoot 'D3DCompiler_46_org.dll')
    Copy-Item -LiteralPath (Join-Path $asanRoot 'clang_rt.asan_dynamic-x86_64.dll') -Destination $pathRoot
    # Keep EXE directory + storage_directory below MAX_PATH so the #10 prefix
    # helper still succeeds; 240 leaves the #12 filename without room.
    $directoryLength=$pathRoot.Length+1
    if($directoryLength -ge 240){throw "F-200 fixture directory is too long: $pathRoot"}
    $storageLength=240-$directoryLength
    $longStorage='a'*$storageLength
    "[Logging]`ncalls=1`nunbuffered=1`n[Rendering]`nstorage_directory=$longStorage`nexport_shaders=1`n" |
        Set-Content -LiteralPath (Join-Path $pathRoot 'd3dx.ini') -Encoding utf8
    $pathRun=Invoke-IsolatedSimulationProcess -Executable $pathExe -Arguments @($pathDll,'compiler-export') -WorkingDirectory $pathRoot -LogPrefix (Join-Path $pathRoot 'reject') -EnvironmentOverrides $asanEnvironment
    $longDir=Join-Path $pathRoot $longStorage
    $pathExports=@(Get-ChildItem -LiteralPath $longDir -Filter '*.txt' -ErrorAction SilentlyContinue)
    $pathLog=Join-Path $pathRoot 'D3DCompiler_46_log.txt'
    $pathLogText=if(Test-Path -LiteralPath $pathLog){Get-Content -LiteralPath $pathLog -Raw}else{''}
    if($pathRun.status -eq 'PASS') {
        if($pathExports.Count -ne 0){$pathRun.status='FAIL';$pathRun.message='Long storage_directory still exported a file.'}
        elseif($pathLogText -notmatch '路径过长被拒'){$pathRun.status='FAIL';$pathRun.message='Long storage_directory rejection was not logged.'}
    }
    $results.Add([pscustomobject]@{id='compiler-asan-path-reject';status=$pathRun.status;input=$pathDll;sha256=(Get-FileHash -LiteralPath $pathDll).Hash;detail=$pathRun;storageLength=$storageLength})
    Write-Host "[$($pathRun.status)] compiler-asan-path-reject"
}
$status=if(@($results | Where-Object status -ne 'PASS').Count){'FAIL'}else{'PASS'}
$report=[ordered]@{item=$Item;status=$status;cases=$results.ToArray();nativeCompilerProvider=$nativeCompiler;providerSha256=$providerHash;providerMode='System D3DCompiler_47 copied under the declared legacy _46_org dependency name; public API compatibility coverage, not original 46/private export certification';scope='Actual DXGI/Compiler DLL public normal/failure business with real native providers and controlled logging modes';bridgeProtocolTested=$false}
$reportPath=Join-Path $runRoot 'results.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
if($ReportPointer){[IO.File]::WriteAllText($ReportPointer,$reportPath,[Text.UTF8Encoding]::new($false))}
Write-Host "Component report: $reportPath"
if($status -ne 'PASS'){exit 1}
if($SpecialOnly){Write-Host "PASS audit item: $Item"}
