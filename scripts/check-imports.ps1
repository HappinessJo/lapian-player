[CmdletBinding()]
param(
  [string]$DistDir = '',
  [switch]$AllowPrebuiltFfmpeg
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

if (-not $DistDir) { $DistDir = Join-Path (Get-RepoRoot) 'dist' }
if (-not (Test-Path $DistDir)) { throw "目录不存在：$DistDir" }

Import-VsEnv | Out-Null
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (-not $dumpbin) { throw '找不到 dumpbin.exe（需要 Visual Studio C++ 工具链）' }

# 首期运行期零网络。检查分三层：
#   1. 发布包不得含 QtNetwork/TLS/网络插件（windeployqt 会多带，构建脚本已删）；
#   2. 任何文件都不得导入 winhttp/wininet/urlmon/wsock32；
#   3. ws2_32：Qt6Core 固定链接 winsock（仅初始化，不发请求），允许；
#      av*.dll 在阶段 A 用预编译包（带网络）时放行并告警，阶段 B 最小构建必须干净。
$blocked = @('WSOCK32.DLL', 'WINHTTP.DLL', 'WININET.DLL', 'URLMON.DLL')
$hits = @()
$notes = @()

$forbiddenFiles = @('Qt6Network.dll')
foreach ($name in $forbiddenFiles) {
  if (Test-Path (Join-Path $DistDir $name)) { $hits += "不应随包分发：$name" }
}
foreach ($dir in @('tls', 'networkinformation')) {
  if (Test-Path (Join-Path $DistDir $dir)) { $hits += "不应随包分发：$dir\ 插件" }
}

$files = Get-ChildItem -LiteralPath $DistDir -File | Where-Object { $_.Extension -in @('.exe', '.dll') }

foreach ($file in $files) {
  $imports = (& $dumpbin.Source /nologo /imports $file.FullName 2>$null | Out-String)
  foreach ($name in $blocked) {
    $pattern = '(?im)^\s*' + [regex]::Escape($name) + '\s*$'
    if ($imports -match $pattern) { $hits += ('{0} -> {1}' -f $file.Name, $name) }
  }

  $wsa = $imports -match '(?im)^\s*WS2_32\.DLL\s*$'
  if ($wsa) {
    if ($file.Name -eq 'Qt6Core.dll') {
      $notes += 'Qt6Core.dll -> WS2_32（Qt 运行时固定链接，仅 winsock 初始化）'
    } elseif ($AllowPrebuiltFfmpeg -and $file.Name -like 'av*.dll') {
      $notes += ('{0} -> WS2_32（阶段 A 预编译包，阶段 B 必须消除）' -f $file.Name)
    } else {
      $hits += ('{0} -> WS2_32.DLL' -f $file.Name)
    }
  }
}

if ($notes.Count -gt 0) {
  Write-Host '[NOTE] 已知并接受的网络相关导入：' -ForegroundColor Yellow
  $notes | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}

if ($hits.Count -gt 0) {
  Write-Host '[FAIL] 发布包存在不该有的网络依赖：' -ForegroundColor Red
  $hits | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
  throw '导入表检查失败'
}

Write-Host ('[OK] 网络依赖检查通过：{0} 个文件，无 QtNetwork/TLS/winhttp/wininet/urlmon' -f $files.Count) -ForegroundColor Green
