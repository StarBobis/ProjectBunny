[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference='Stop'
$simulationRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=Join-Path $simulationRoot 'build'
$infoPath=Join-Path $buildRoot 'plain-performance-build-info.json'
$info=Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json
$null=New-Item -ItemType Directory -Path $Destination
Copy-Item -LiteralPath $infoPath -Destination (Join-Path $Destination 'build-info.json')
$files=@('dx11_upstream_regression.exe','d3d11.dll','wic-regression.png')
$hashes=@(foreach($name in $files){
    $source=Join-Path $buildRoot "plain-performance\src\DirectX11\$name"
    $hash=(Get-FileHash -LiteralPath $source).Hash
    $product=@($info.products | Where-Object path -eq "src\DirectX11\$name")
    if($product.Count -ne 1 -or $product[0].sha256 -ne $hash -or $info.addressSanitizer -ne $false){throw 'Snapshot does not match the completed plain build.'}
    Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $name)
    if((Get-FileHash -LiteralPath (Join-Path $Destination $name)).Hash -ne $hash){throw 'Snapshot copy differs.'}
    [pscustomobject]@{name=$name;sha256=$hash}
})
@{item='P-01';buildInfoSha256=(Get-FileHash -LiteralPath $infoPath).Hash;files=$hashes} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $Destination 'snapshot.json') -Encoding utf8
Write-Host "Frozen benchmark build: $Destination"
