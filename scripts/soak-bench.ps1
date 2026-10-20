# T7.3 长时稳定性浸泡驱动（开发/验收用；不入分发）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\soak-bench.ps1 `
#     -File build\t7.2-media\test1080.mp4 -Seconds 7200 -Interval 30 -Tag soak-1080
#
# 说明：
# - 默认 7200s（2h）。没有 V10 2h 素材时用现有素材跑短时探针，报告里必须写
#   “不是 V10/P14”；脚本不判断素材规格，只负责驱动和汇总。
# - 产品 --memory-mode soak 会整片 A/B 循环播放，每 30s 采 WS/private/handles/
#   线程/帧号/缓存/音频 underrun，并在 UI 线程 30s 无心跳时直接 hard_stall 退出。
# - 本脚本额外做外部超时（Seconds+600s），防止进程在启动/打开阶段卡死。
# - PowerShell 5.1 Start-Process 传中文参数会按 ANSI 转码；路径含非 ASCII 时
#   脚本自动在 build\soak-media 下建 ASCII 硬链接。日志是 UTF-8。

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$File,
  [int]$Seconds = 7200,
  [int]$Interval = 30,
  [string]$Tag = 'soak',
  [string]$Exe = '',
  [string]$SettingsPath = '',
  [string]$LogDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
  $Exe = Join-Path $root 'dist\lapian.exe'
}
if (-not (Test-Path -LiteralPath $Exe)) {
  throw "找不到 $Exe；先跑 scripts\build-release.ps1"
}
if ([string]::IsNullOrWhiteSpace($SettingsPath)) {
  $SettingsPath = Join-Path $root 'build\t7.3-soak-settings'
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
  $LogDir = Join-Path $root 'build\soak'
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $SettingsPath | Out-Null

# ASCII 硬链接：避免 PowerShell 5.1 把中文路径按 ANSI 塞进 argv。
$source = (Resolve-Path -LiteralPath $File).Path
$asciiPath = $source
$nonAscii = $source.ToCharArray() | Where-Object { [int]$_ -gt 127 } | Select-Object -First 1
if ($null -ne $nonAscii -or $source -match '\s') {
  $linkDir = Join-Path $root 'build\soak-media'
  New-Item -ItemType Directory -Force -Path $linkDir | Out-Null
  $link = Join-Path $linkDir ('soak-' + $Tag + '.mp4')
  if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link -Force }
  New-Item -ItemType HardLink -Path $link -Target $source | Out-Null
  $asciiPath = $link
  Write-Host "路径含非 ASCII/空白，使用硬链接：$asciiPath"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$safeTag = $Tag -replace '[^A-Za-z0-9_.-]', '_'
$log = Join-Path $LogDir ("$safeTag-$stamp.log")
$stdout = Join-Path $LogDir ("$safeTag-$stamp.stdout.log")

$arguments = @(
  '--file', $asciiPath,
  '--memory-test', '--memory-mode', 'soak',
  '--memory-seconds', "$Seconds",
  '--memory-interval', "$Interval",
  '--memory-tag', $Tag,
  '--settings-path', $SettingsPath
)
Write-Host ("启动 soak：file={0} seconds={1} interval={2} tag={3}" -f $asciiPath, $Seconds, $Interval, $Tag)
Write-Host ("日志：{0}" -f $log)

$process = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru `
  -RedirectStandardError $log -RedirectStandardOutput $stdout
$deadline = (Get-Date).AddSeconds($Seconds + 600)
$timedOut = $false
while (-not $process.HasExited) {
  Start-Sleep -Seconds 5
  $process.Refresh()
  if ((Get-Date) -gt $deadline) {
    $timedOut = $true
    try { $process.Kill() } catch { }
    break
  }
}
$process.WaitForExit()
$exitCode = $process.ExitCode

$text = ''
if (Test-Path -LiteralPath $log) {
  $bytes = [IO.File]::ReadAllBytes($log)
  $text = [Text.Encoding]::UTF8.GetString($bytes)
}
$summary = ($text -split "`r?`n" | Where-Object { $_ -match '^T7\.3 soak_summary ' } | Select-Object -Last 1)
if ($summary) {
  Write-Host $summary
} else {
  Write-Host "没有找到 T7.3 soak_summary；进程 exit=$exitCode timedOut=$timedOut"
}
$hardStall = ($text -split "`r?`n" | Where-Object { $_ -match 'soak_watchdog hard_stall' } | Select-Object -Last 1)
if ($hardStall) { Write-Host $hardStall }
if ($timedOut) {
  Write-Host "[FAIL] 外部超时（Seconds+600s），已终止进程"
  exit 5
}
if ($exitCode -ne 0) {
  Write-Host "[FAIL] soak exit=$exitCode"
  exit $exitCode
}
if (-not $summary -or ($summary -notmatch 'pass=1')) {
  Write-Host "[FAIL] soak 未通过"
  exit 3
}
Write-Host "[OK] soak 通过（短时探针不等于 P14/V10 正式验收）"
exit 0
