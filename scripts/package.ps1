[CmdletBinding()]
param(
  [switch]$SkipBuild,
  [switch]$SkipInstaller,
  [switch]$SkipZip,
  [string]$Version = '',
  [string]$DistDir = '',
  [string]$OutDir = '',
  [string]$IsccPath = ''
)

# T7.6：把 dist（程序负载）组装成两种分发形态：
#   - Lapian-<ver>-portable.zip  解压即用，不写注册表；
#   - Lapian-<ver>-setup.exe     Inno Setup 6 每用户安装，注册“打开方式”候选。
# 输出统一写到 out\package（.gitignore 已忽略），不污染 dist 与源码树。
# 许可文本随两种形态一起分发：licenses\ 全文 + SOURCE-OFFER.md + README.txt。

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo = Get-RepoRoot
Initialize-BuildTemp | Out-Null
if (-not $DistDir) { $DistDir = Join-Path $repo 'dist' }
if (-not $OutDir) { $OutDir = Join-Path $repo 'out\package' }

function Get-ProjectVersion {
  $cmakeLists = Join-Path $repo 'CMakeLists.txt'
  $text = Get-Content -LiteralPath $cmakeLists -Raw -Encoding UTF8
  if ($text -match 'project\s*\(\s*lapian\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    return $Matches[1]
  }
  throw "无法从 CMakeLists.txt 读取 project VERSION：$cmakeLists"
}

function Get-VersionQuad([string]$value) {
  $parts = @($value.Split('.'))
  while ($parts.Count -lt 4) { $parts += '0' }
  $parts = @($parts | Select-Object -First 4 | ForEach-Object {
      if ($_ -match '^\d+$') { [string][int]$_ } else { '0' }
    })
  return ($parts -join '.')
}

function Get-FileSizeMB([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return 0 }
  return [math]::Round((Get-Item -LiteralPath $path).Length / 1MB, 2)
}

function Get-TreeStats([string]$path) {
  $files = @(Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue)
  $bytes = ($files | Measure-Object -Property Length -Sum).Sum
  if (-not $bytes) { $bytes = 0 }
  return [pscustomobject]@{
    Files = $files.Count
    Bytes = $bytes
    MB    = [math]::Round($bytes / 1MB, 2)
  }
}

function Resolve-Iscc {
  if ($IsccPath) {
    if (Test-Path -LiteralPath $IsccPath) { return (Resolve-Path -LiteralPath $IsccPath).Path }
    throw "指定的 ISCC.exe 不存在：$IsccPath"
  }
  $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $candidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe')
  )
  foreach ($candidate in $candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
  }

  throw @'
未找到 ISCC.exe（Inno Setup 6.3+ 编译器）。
请任选一种方式后重跑：
  1) 当前用户安装（不需要管理员）：winget install JRSoftware.InnoSetup --scope user
  2) 已有离线安装包：安装到默认目录，或用 -IsccPath 指向 ISCC.exe
  3) 只做便携 zip：scripts\package.ps1 -SkipInstaller
'@
}

if (-not $SkipBuild) {
  Write-Host '=== 1/5 构建 Release（build-release.ps1） ==='
  & (Join-Path $PSScriptRoot 'build-release.ps1')
} else {
  Write-Host '=== 1/5 构建（已跳过，使用现有 dist） ==='
}

if (-not (Test-Path -LiteralPath (Join-Path $DistDir 'lapian.exe'))) {
  throw "dist 不存在或不完整：$DistDir（先跑 scripts\build-release.ps1）"
}

if (-not $Version) { $Version = Get-ProjectVersion }
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+') {
  throw "版本号格式应为 x.y.z：$Version"
}
$versionQuad = Get-VersionQuad $Version

Write-Host '=== 2/5 校验 dist 与许可文本 ==='
$requiredFiles = @(
  'lapian.exe', 'lapian_env_check.exe',
  'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
  'platforms\qwindows.dll',
  'avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll',
  'swscale-8.dll', 'swresample-5.dll'
)
foreach ($rel in $requiredFiles) {
  if (-not (Test-Path -LiteralPath (Join-Path $DistDir $rel))) {
    throw "dist 缺少必需文件：$rel"
  }
}
foreach ($name in @('Qt6Network.dll')) {
  if (Test-Path -LiteralPath (Join-Path $DistDir $name)) {
    throw "dist 不应包含网络模块：$name"
  }
}
foreach ($dir in @('tls', 'networkinformation')) {
  if (Test-Path -LiteralPath (Join-Path $DistDir $dir)) {
    throw "dist 不应包含网络插件：$dir\"
  }
}

$licenseNames = @(
  'LGPL-2.1.txt', 'LGPL-3.0.txt', 'GPL-3.0.txt',
  'THIRD-PARTY-NOTICES.md', 'SOURCE-OFFER.md'
)
foreach ($name in $licenseNames) {
  if (-not (Test-Path -LiteralPath (Join-Path $repo "licenses\$name"))) {
    throw "缺少许可文本：licenses\$name"
  }
}
$readme = Join-Path $repo 'packaging\README.txt'
if (-not (Test-Path -LiteralPath $readme)) { throw "缺少打包说明：$readme" }

Write-Host '=== 3/5 组装 stage（程序 + licenses + README） ==='
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir 'stage'
$portableRoot = Join-Path $OutDir 'portable'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $portableRoot) { Remove-Item -LiteralPath $portableRoot -Recurse -Force }
Get-ChildItem -LiteralPath $OutDir -File | Where-Object {
  $_.Name -like 'Lapian-*-setup.exe' -or
  $_.Name -like 'Lapian-*-portable.zip' -or
  $_.Name -like 'package-report.*'
} | Remove-Item -Force
New-Item -ItemType Directory -Path $stage | Out-Null

Get-ChildItem -LiteralPath $DistDir | Copy-Item -Destination $stage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo 'licenses') -Destination (Join-Path $stage 'licenses') -Recurse -Force
Copy-Item -LiteralPath $readme -Destination (Join-Path $stage 'README.txt') -Force

$distStats = Get-TreeStats $DistDir
$stageStats = Get-TreeStats $stage

Write-Host '=== 4/5 运行负载自检 + 无网络导入检查 + 便携 zip ==='
Push-Location $stage
try {
  Invoke-Tool -Exe (Join-Path $stage 'lapian_env_check.exe') -Arguments @() -Description 'stage 环境自检'
} finally {
  Pop-Location
}
$ffmpegRoot = Find-FfmpegRoot
& (Join-Path $PSScriptRoot 'check-imports.ps1') -DistDir $stage `
  -AllowPrebuiltFfmpeg:($ffmpegRoot -like '*\prebuilt\*')

$zipPath = ''
if (-not $SkipZip) {
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $portableDir = Join-Path $portableRoot "Lapian-$Version"
  New-Item -ItemType Directory -Force -Path $portableDir | Out-Null
  Get-ChildItem -LiteralPath $stage | Copy-Item -Destination $portableDir -Recurse -Force
  $zipPath = Join-Path $OutDir "Lapian-$Version-portable.zip"
  [System.IO.Compression.ZipFile]::CreateFromDirectory(
    $portableDir, $zipPath,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $true)
  Remove-Item -LiteralPath $portableRoot -Recurse -Force
  Write-Host ("便携 zip：{0}（{1} MB）" -f $zipPath, (Get-FileSizeMB $zipPath))
}

Write-Host '=== 5/5 编译每用户安装包 ==='
$setupPath = ''
if ($SkipInstaller) {
  Write-Host '已跳过安装包（-SkipInstaller）。'
} else {
  $iscc = Resolve-Iscc
  $iss = Join-Path $PSScriptRoot 'installer.iss'
  $issBytes = [System.IO.File]::ReadAllBytes($iss)
  if ($issBytes.Length -lt 3 -or $issBytes[0] -ne 0xEF -or $issBytes[1] -ne 0xBB -or $issBytes[2] -ne 0xBF) {
    throw "installer.iss 必须是 UTF-8 BOM（Inno 需要 BOM 才能正确读取中文）：$iss"
  }
  Invoke-Tool -Exe $iscc -Arguments @(
    "-DAppVersion=$Version",
    "-DAppVersionQuad=$versionQuad",
    "-DSourceDir=$stage",
    "-DOutputDir=$OutDir",
    $iss
  ) -Description 'Inno Setup 编译'
  $setupPath = Join-Path $OutDir "Lapian-$Version-setup.exe"
  if (-not (Test-Path -LiteralPath $setupPath)) {
    throw "安装包未生成：$setupPath"
  }
  Write-Host ("安装包：{0}（{1} MB）" -f $setupPath, (Get-FileSizeMB $setupPath))
}

Write-Host '=== 体积与校验和（P12 ≤100MB） ==='
$limitBytes = 100MB
$artifacts = @($zipPath, $setupPath) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
foreach ($artifact in $artifacts) {
  $size = (Get-Item -LiteralPath $artifact).Length
  if ($size -gt $limitBytes) {
    throw ("P12 超限：{0} = {1:N2} MB > 100 MB" -f (Split-Path $artifact -Leaf), ($size / 1MB))
  }
}
if ($stageStats.Bytes -gt $limitBytes) {
  throw ("P12 超限：程序负载 {0:N2} MB > 100 MB" -f $stageStats.MB)
}

$gitHead = 'n/a'
try {
  $gitHead = (& git -C $repo rev-parse --short HEAD 2>$null | Out-String).Trim()
  if (-not $gitHead) { $gitHead = 'n/a' }
} catch { }

$reportLines = New-Object System.Collections.Generic.List[string]
$reportLines.Add('拉片软件 分发打包报告（T7.6）')
$reportLines.Add(('生成时间：{0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')))
$reportLines.Add(('版本：{0}（{1}）' -f $Version, $versionQuad))
$reportLines.Add(('源码 HEAD：{0}' -f $gitHead))
$reportLines.Add('')
$reportLines.Add(('dist：{0} 文件 / {1} MB' -f $distStats.Files, $distStats.MB))
$reportLines.Add(('分发负载 stage：{0} 文件 / {1} MB（含 licenses 与 README）' -f $stageStats.Files, $stageStats.MB))
foreach ($artifact in $artifacts) {
  $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash
  $reportLines.Add(('{0}：{1} MB  SHA256={2}' -f (Split-Path $artifact -Leaf), (Get-FileSizeMB $artifact), $hash))
}
$reportLines.Add('')
$reportLines.Add('P12 判定：每个分发形态均 ≤100MB；本报告不把安装包与便携 zip 相加。')
$reportLines.Add('无网络：check-imports.ps1 已断言无 QtNetwork/TLS/winhttp/wininet/urlmon 导入。')

$reportPath = Join-Path $OutDir 'package-report.txt'
$reportLines | Set-Content -LiteralPath $reportPath -Encoding UTF8
$reportLines | ForEach-Object { Write-Host $_ }
Write-Host ("报告：{0}" -f $reportPath)
Write-Host '[OK] 分发打包完成。' -ForegroundColor Green

# 外部工具（dumpbin/iscc 等）可能留下非 0 的 $LASTEXITCODE；本脚本已显式检查
# 关键步骤的退出码，收尾统一清零，避免 CI/父脚本误判失败。
$global:LASTEXITCODE = 0
