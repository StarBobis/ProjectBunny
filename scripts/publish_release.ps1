# Build and publish DirectX 11 + DirectX 12 Release x64 to StarBobis/ProjectBunny.
# Usage: ./scripts/publish_release.ps1 [-Version X.Y.Z] [-PackageOnly]
# Without Version, increment the latest release patch. PackageOnly needs Version.
# Auth: -Token, GITHUB_TOKEN, GH_TOKEN, then Windows Git Credential Manager.
# Package layout: flat runtime files in ProjectBunny-PACKAGE-vX.Y.Z.zip,
# plus Manifest.json containing version, source commit and SHA256 checksums.
# Source remains in this repository; releases never change repository visibility.

param(
    [string]$Version = "",
    [string]$Token = "",
    [switch]$SkipBuild,
    [switch]$PackageOnly
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = Split-Path -Parent $PSScriptRoot

# The public repository that hosts the binary releases. Keep this in sync
# with the URL at the top of this file.
$releaseRepo = 'StarBobis/ProjectBunny'
$releaseRepoUrl = "https://github.com/$releaseRepo"

# ----------------------------------------------------------------------------
# Auth
# ----------------------------------------------------------------------------
if (-not $Token) { $Token = $env:GITHUB_TOKEN }
if (-not $Token) { $Token = $env:GH_TOKEN }
if (-not $Token -and -not $PackageOnly) {
    $credential = "protocol=https`nhost=github.com`n`n" | git credential fill
    if ($LASTEXITCODE -eq 0) {
        $password = $credential | Where-Object { $_ -like 'password=*' } | Select-Object -First 1
        if ($password) { $Token = $password.Substring(9) }
    }
}
if (-not $Token -and -not $PackageOnly) {
    Write-Host "ERROR: no GitHub token found." -ForegroundColor Red
    Write-Host "       Create a PAT with the 'repo' scope at https://github.com/settings/tokens" -ForegroundColor Yellow
    Write-Host "       and set it via `$env:GITHUB_TOKEN (or pass -Token)." -ForegroundColor Yellow
    exit 1
}

$apiHeaders = @{
    'Authorization'        = "Bearer $Token"
    'Accept'               = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent'           = 'publish_release.ps1'
}

function Invoke-GitHubApi {
    param(
        [string]$Method,
        [string]$Uri,
        [object]$Body = $null,
        [string]$ContentType = 'application/json'
    )
    try {
        $params = @{
            Method      = $Method
            Uri         = $Uri
            Headers     = $apiHeaders
            ContentType = $ContentType
        }
        if ($null -ne $Body) {
            if ($Body -is [byte[]]) {
                $params.Body = $Body
            } else {
                $params.Body = ($Body | ConvertTo-Json -Depth 10)
            }
        }
        return Invoke-RestMethod @params
    } catch {
        Write-Host "ERROR: GitHub API call failed: $Method $Uri" -ForegroundColor Red
        Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
        if ($_.ErrorDetails.Message) {
            Write-Host "       $($_.ErrorDetails.Message)" -ForegroundColor Red
        }
        exit 1
    }
}

# ----------------------------------------------------------------------------
# Working tree sanity checks (the release ships whatever is built locally,
# including uncommitted changes, so warn loudly about those)
# ----------------------------------------------------------------------------
$branch = (git -C $root rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Host "ERROR: releases must be published from main (current branch: $branch)." -ForegroundColor Red
    exit 1
}

git -C $root fetch origin main --quiet
$remote = (git -C $root rev-parse origin/main).Trim()
$base = (git -C $root merge-base HEAD origin/main).Trim()
if ($remote -ne $base) {
    Write-Host "ERROR: local main is behind origin/main. Run 'git pull' first." -ForegroundColor Red
    exit 1
}

$dirty = git -C $root status --porcelain
if ($dirty) {
    Write-Host "WARNING: uncommitted changes detected - they WILL be built into this release:" -ForegroundColor Yellow
    Write-Host $dirty -ForegroundColor Yellow
    Write-Host "Commit them first if the release should match a pushed commit." -ForegroundColor Yellow
}

# ----------------------------------------------------------------------------
# Resolve the version to publish
# ----------------------------------------------------------------------------
function Get-LatestReleaseVersion {
    # Returns the newest vX.Y.Z release tag on the release repo, or $null.
    $releases = Invoke-GitHubApi -Method Get -Uri "https://api.github.com/repos/$releaseRepo/releases?per_page=100"
    $versions = @()
    foreach ($rel in $releases) {
        if ($rel.tag_name -match '^v(\d+)\.(\d+)\.(\d+)$') {
            $versions += [version]"$($Matches[1]).$($Matches[2]).$($Matches[3])"
        }
    }
    if ($versions.Count -eq 0) {
        return $null
    }
    return ($versions | Sort-Object | Select-Object -Last 1)
}

if ($PackageOnly -and -not $Version) { throw "PackageOnly requires -Version." }
if ($Version) {
    $newVersion = $Version.Trim().TrimStart('v')
} else {
    $latest = Get-LatestReleaseVersion
    if (-not $latest) {
        Write-Host "ERROR: no v* releases found on $releaseRepo to bump from. Pass -Version explicitly for the first release (e.g. -Version 0.1.0)." -ForegroundColor Red
        exit 1
    }
    $newVersion = "{0}.{1}.{2}" -f $latest.Major, $latest.Minor, ($latest.Build + 1)
    Write-Host "Auto-bumped patch version: v$latest -> v$newVersion" -ForegroundColor Cyan
}
if ($newVersion -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "ERROR: version '$newVersion' must look like 1.2.3" -ForegroundColor Red
    exit 1
}
$tag = "v$newVersion"

# Refuse to overwrite an existing tag on the release repo.
$existingRef = $null
try {
    if (-not $PackageOnly) {
    $existingRef = Invoke-RestMethod -Method Get -Headers $apiHeaders `
        -Uri "https://api.github.com/repos/$releaseRepo/git/ref/tags/$tag"
    }
} catch {
    if ($_.Exception.Response.StatusCode.value__ -ne 404) {
        Write-Host "ERROR: could not check existing tags on $releaseRepo." -ForegroundColor Red
        Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
        if ($_.ErrorDetails.Message) {
            Write-Host "       $($_.ErrorDetails.Message)" -ForegroundColor Red
        }
        exit 1
    }
}
if ($existingRef) {
    Write-Host "ERROR: tag $tag already exists on $releaseRepo. Pick a new version." -ForegroundColor Red
    exit 1
}

# Warn (not fail) when CMakeLists.txt version drifts from the release tag:
# the DLL reports the CMake project version.
$cmakeLine = Select-String -Path "$root\CMakeLists.txt" -Pattern 'VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
if ($cmakeLine) {
    $cmakeVersion = $cmakeLine.Matches[0].Groups[1].Value
    if ($cmakeVersion -ne $newVersion) {
        Write-Host "WARNING: CMakeLists.txt project version is $cmakeVersion but the release tag is $tag. The DLL will report $cmakeVersion." -ForegroundColor Yellow
    }
}

# ----------------------------------------------------------------------------
# Build + package
# ----------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "===== Local Release x64 build =====" -ForegroundColor Cyan
    & "$root\scripts\build-cmake.ps1" -Configuration Release -Platform x64 -ReleasePackage
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: local Release build, aborting release." -ForegroundColor Red
        exit $LASTEXITCODE
    }
} else {
    Write-Host "SkipBuild: reusing existing artifacts under x64\Release." -ForegroundColor Yellow
}

$outDir = "$root\x64\Release"
$stage = "$root\build\release-stage"
$zipName = "ProjectBunny-PACKAGE-$tag.zip"
$zipPath = "$root\build\$zipName"
$dxcDir = "$root\third_party\dxc"

New-Item -ItemType Directory -Path $stage -Force | Out-Null
$stage = Join-Path $root ("build/release-stage-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null

# dxcompiler.dll + dxil.dll are listed as shipped files on purpose: the
# loader in DX12CustomShaderCompile.h probes the d3d12.dll directory first,
# so the pair must sit beside it in the unpacked release exactly as the dev
# build deploys it. Missing them is a hard error, not a degraded release.
$shipFiles = @(
    @{ Src = "$outDir\d3d11.dll"; Dst = 'd3d11.dll' },
    @{ Src = "$outDir\d3dcompiler_47.dll"; Dst = 'd3dcompiler_47.dll' },
    @{ Src = "$outDir\d3d12.dll"; Dst = 'd3d12.dll' },
    @{ Src = "$dxcDir\dxcompiler.dll"; Dst = 'dxcompiler.dll';
       Hint = "Runtime HLSL compiler for [CustomShaderX] elements. Restore the official drop: third_party\dxc\dxcompiler.dll + dxil.dll." },
    @{ Src = "$dxcDir\dxil.dll"; Dst = 'dxil.dll';
       Hint = "DXIL backend required by dxcompiler.dll (and by d3dcompiler_47's SM6 path). Restore third_party\dxc\dxil.dll." },
    @{ Src = "$root\LICENSE.txt"; Dst = 'LICENSE.txt' },
    @{ Src = "$root\config\d3dx.ini"; Dst = 'd3dx.ini' }
)
foreach ($f in $shipFiles) {
    if (-not (Test-Path $f.Src)) {
        Write-Host "ERROR: expected file not found: $($f.Src)" -ForegroundColor Red
        if ($f.Hint) {
            Write-Host "       $($f.Hint)" -ForegroundColor Yellow
        } else {
            Write-Host "       Build first (drop -SkipBuild) or restore the missing file." -ForegroundColor Yellow
        }
        exit 1
    }
    Copy-Item $f.Src (Join-Path $stage $f.Dst) -Force
}

# Upstream notice for the Microsoft DirectXShaderCompiler binaries. Shipped
# whenever the source drop carries it, so a checkout without the text can
# still publish - but the notice must travel with the binaries.
$dxcLicense = "$dxcDir\LICENSE.TXT"
if (Test-Path $dxcLicense) {
    Copy-Item $dxcLicense (Join-Path $stage 'dxc-LICENSE.TXT') -Force
    Write-Host "Packaged DXC license notice: dxc-LICENSE.TXT" -ForegroundColor Gray
} else {
    Write-Host "WARNING: $dxcLicense not found - the shipped dxcompiler/dxil pair needs its upstream" -ForegroundColor Yellow
    Write-Host "         (NCSA/LLVM) license notice. Copy LICENSE.TXT from the official DXC release into that folder." -ForegroundColor Yellow
}

$hashes = [ordered]@{}
foreach ($file in Get-ChildItem $stage -File | Sort-Object Name) {
    if ($file.Extension -in '.dll', '.exe') {
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5a4d) { throw "Invalid PE: $($file.Name)" }
        $pe = [BitConverter]::ToInt32($bytes, 60)
        if ($pe -lt 0 -or $pe + 6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) { throw "Not AMD64 PE: $($file.Name)" }
    }
    $hashes[$file.Name] = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifestPath = Join-Path $root 'build/Manifest.json'
@{ version = $newVersion; configuration = 'Release'; architecture = 'x64'; sha256 = $hashes; source_commit = (git -C $root rev-parse HEAD).Trim() } | ConvertTo-Json -Depth 5 | Set-Content $manifestPath -Encoding UTF8
Compress-Archive -Path "$stage\*" -DestinationPath $zipPath -CompressionLevel Optimal -Force
Write-Host "Packaged: $zipPath" -ForegroundColor Green

# ----------------------------------------------------------------------------
# Create the GitHub Release and upload the zip
# ----------------------------------------------------------------------------
Write-Host "===== Publishing $tag to $releaseRepoUrl =====" -ForegroundColor Cyan

# Release notes for the versions whose user-visible content needs spelling
# out. Tags not listed here fall back to the generic one-liner, so bumping
# the version never requires touching this switch.
$releaseBody = "ProjectBunny $tag - DirectX 11 + DirectX 12, Release x64."
switch ($tag) {
    'v1.3.18' {
        $releaseBody = @'
DX11 F8 帧分析修复与优化：
- 修复通用 Buffer 文本导出将问号写入文件名、导致部分 TXT 创建失败的问题；文件名与文本标签统一使用 bufN。
- Frame Analysis 日志固定使用 UTF-8，支持中文路径、名称及 emoji。
- 增大 TXT 写入缓冲，减少 VB/IB 格式化调用；DDS 紧密行布局直接同步写入，减少中间复制。
- 新增 Dump timing 分阶段汇总，可检查 Map 等待、哈希、文本/二进制/纹理保存及链接耗时。包含嵌套阶段的项目不应重复相加。
- 保留同步抓取、内容哈希、资源命名与链接流程。实际游戏中的提速幅度取决于抓取内容与环境。

验证：DX11 Release 构建及全部 4 项 CTest 通过，涵盖原生 WARP 导出、文本字节、DDS 像素/mip、UTF-8 和链接回归。发布包另执行双 DX x64 Release 构建、AMD64 PE 与 SHA256 校验。
'@
    }
    'v0.1.13' {
        $releaseBody = @'
修复：
- 修复贴图替换在切换场景后失效的问题。旧的贴图内容哈希是单发追踪：上传时的捕获一旦丢失（读回队列压力、上传形状不被识别、上传发生在追踪开启之前），该贴图整场游戏都无法再被内容哈希匹配，表现为换场景后 mod 贴图悄然失效。现在改为 wanted-texture 集合 + 有界重试：任何对顶层 mip 的写入都会把贴图标记为"需要哈希"，读回管线按指数退避自动重试（最多 6 次、约 20 秒窗口），丢失的捕获只带来一两帧延迟，不再永久失效。
- 贴图匹配时遇到未哈希贴图的按需补算也切换到同一管线：补算请求去重后进入重试循环，哈希在一两帧内发布，而不是按冷却时间零散重试。
- 通过 CopyResource 整体拷贝上传的贴图此前完全没有内容哈希覆盖（只有 CopyTextureRegion 被追踪），现已纳入，以这种方式流式加载贴图的场景也能正常匹配。

其它：
- 覆盖口径从"上传形状识别"改为"内容写入标记"，移除旧的单发上传追踪、覆盖形状判定及相关诊断代码，逻辑更直接。
'@
    }
    'v0.1.12' {
        $releaseBody = @'
修复：
- 修复卧龙启动崩溃：接管主 EXE 导入表里的 D3D12CreateDevice / D3D12GetDebugInterface / D3D12GetInterface（兼容序号导入），并修正交换链与资源包装的对象边界，不再让代理对象进入原生调用链。
- 回退到 3e94127 的 IA 内容哈希四步与全屏闪烁修复，保留 DX12StoredPso 自有存储化与崩溃报告的改进。

其它：
- Profiling HUD 显示方式优化。
'@
    }
}

if ($PackageOnly) { return }

$release = Invoke-GitHubApi -Method Post `
    -Uri "https://api.github.com/repos/$releaseRepo/releases" `
    -Body @{
        target_commitish = (git -C $root rev-parse HEAD).Trim()
        tag_name   = $tag
        name       = "ProjectBunny $tag"
        body       = $releaseBody
        draft      = $true
        prerelease = $false
    }

foreach ($assetPath in @($zipPath, $manifestPath)) {
    $assetName = Split-Path -Leaf $assetPath
    $uploadUri = ($release.upload_url -replace '\{\?name,label\}$', '') + "?name=$assetName"
    Invoke-GitHubApi -Method Post -Uri $uploadUri -Body ([IO.File]::ReadAllBytes($assetPath)) -ContentType 'application/octet-stream' | Out-Null
}

$published = Invoke-GitHubApi -Method Patch -Uri "https://api.github.com/repos/$releaseRepo/releases/$($release.id)" -Body @{ draft = $false; make_latest = 'true' }
Write-Host "Release published: $($published.html_url)" -ForegroundColor Green
