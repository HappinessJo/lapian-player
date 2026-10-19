# 下载 Qt 6.8 LTS（msvc2022_64）到 third_party\Qt
#
# 依赖 Python 3 + aqtinstall。这里把 Python 的临时目录、APPDATA 都重定向到
# 工作区内（.deps\），避免污染系统目录。
#
# 注意：aqt 用 multiprocessing 做并行下载（命名管道）。如果当前终端受沙箱
# 限制（不能建命名管道），需要在放行模式下运行本脚本，或改用 Qt 在线安装器
# 手动安装到 third_party\Qt\<版本>\msvc2022_64。

[CmdletBinding()]
param(
  [string]$Version = '6.8.3',
  [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo = Get-RepoRoot
if (-not $OutDir) { $OutDir = Join-Path $repo 'third_party\Qt' }

$tmp = Join-Path $repo '.deps\tmp'
New-Item -ItemType Directory -Force -Path $tmp, (Join-Path $repo '.deps\appdata') | Out-Null
$env:TMP = $tmp
$env:TEMP = $tmp
$env:APPDATA = Join-Path $repo '.deps\appdata'
$env:LOCALAPPDATA = Join-Path $repo '.deps\appdata\local'

$pkgs = Join-Path $repo '.deps\pyaqt'
if (-not (Test-Path (Join-Path $pkgs 'aqt'))) {
  & py -3 -m pip install --disable-pip-version-check --no-cache-dir -q --target $pkgs aqtinstall
  if ($LASTEXITCODE -ne 0) { throw 'pip 安装 aqtinstall 失败' }
}

$env:PYTHONPATH = "$(Join-Path $repo 'scripts\python');$pkgs"

& py -3 -m aqt install-qt windows desktop $Version win64_msvc2022_64 --outputdir $OutDir --archives qtbase qttools
if ($LASTEXITCODE -ne 0) { throw "Qt 下载失败（退出码 $LASTEXITCODE）" }

$qtRoot = Join-Path $OutDir "$Version\msvc2022_64"
if (-not (Test-Path (Join-Path $qtRoot 'bin\Qt6Core.dll'))) { throw "Qt 安装不完整：$qtRoot" }
Write-Host "[OK] Qt $Version 就位：$qtRoot"
