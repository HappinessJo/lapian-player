# 下载带编码器的 FFmpeg（仅用于本地合成验收素材，不随产品分发）
#
# 产品使用 third_party\ffmpeg\build-msvc 的 LGPL 最小构建；
# 这里下载的是 GPL 构建，只放在 third_party\tools\ffmpeg-gpl，构建期使用。

[CmdletBinding()]
param(
  [string]$Url = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-win64-gpl-8.1.zip'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')

$repo = Get-RepoRoot
$tools = Join-Path $repo 'third_party\tools'
New-Item -ItemType Directory -Force -Path $tools | Out-Null
$zip = Join-Path $tools 'ffmpeg-gpl.zip'

if (-not (Test-Path $zip)) {
  Write-Host "下载：$Url"
  & py -3 -c "import sys,urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])" $Url $zip
  if ($LASTEXITCODE -ne 0) { throw '下载失败' }
}

$target = Join-Path $tools 'ffmpeg-gpl'
if (-not (Test-Path $target)) {
  $tmp = Join-Path $tools 'ffmpeg-gpl-tmp'
  if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
  Expand-Archive -Path $zip -DestinationPath $tmp -Force
  $inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
  Move-Item $inner.FullName $target
  Remove-Item $tmp -Recurse -Force
}

$ffmpeg = Get-ChildItem $target -Recurse -Filter 'ffmpeg.exe' | Select-Object -First 1
if (-not $ffmpeg) { throw '解压后找不到 ffmpeg.exe' }
& $ffmpeg.FullName -version | Select-Object -First 1
Write-Host "[OK] 编码器 FFmpeg 就位：$($ffmpeg.FullName)"
