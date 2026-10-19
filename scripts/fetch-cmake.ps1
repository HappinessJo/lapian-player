# 下载便携版 CMake 3.31.x 到 third_party\cmake
#
# 本机自带的 CMake 4.2.0 在 configure 阶段崩溃（0xC0000409），因此锁定 3.31.6。
# 用 PyPI 的 cmake 轮子（zip 结构），避免 GitHub 资产下载不稳定。

[CmdletBinding()]
param(
  [string]$Version = '3.31.6'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo = Get-RepoRoot
$env:TMP = Join-Path $repo '.deps\tmp'
$env:TEMP = $env:TMP
$env:PYTHONPATH = Join-Path $repo 'scripts\python'
New-Item -ItemType Directory -Force -Path $env:TMP | Out-Null

$wheelDir = Join-Path $repo 'third_party\cmake\wheel'
New-Item -ItemType Directory -Force -Path $wheelDir | Out-Null

& py -3 -m pip download --disable-pip-version-check --no-deps -d $wheelDir "cmake==$Version"
if ($LASTEXITCODE -ne 0) { throw "下载 cmake 轮子失败（退出码 $LASTEXITCODE）" }

$wheel = Get-ChildItem -LiteralPath $wheelDir -Filter "cmake-$Version-*.whl" | Select-Object -First 1
if (-not $wheel) { throw "找不到 cmake-$Version 轮子" }

$zip = Join-Path $wheelDir 'cmake.zip'
Copy-Item -LiteralPath $wheel.FullName -Destination $zip -Force
Expand-Archive -Path $zip -DestinationPath (Join-Path $repo "third_party\cmake\$Version") -Force

$exe = Get-ChildItem -LiteralPath (Join-Path $repo "third_party\cmake\$Version") -Recurse -Filter 'cmake.exe' |
  Where-Object { $_.FullName -like '*\data\bin\cmake.exe' } | Select-Object -First 1
if (-not $exe) { throw '解压后找不到 cmake.exe' }
Write-Host "[OK] CMake $Version 就位：$($exe.FullName)"
