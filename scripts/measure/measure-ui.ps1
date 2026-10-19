# UI 心跳采样（P5 的基础设施）
#
# P5 的完整口径是“拖动中画面无更新且 UI 无响应 >200ms 的次数 = 0”，需要拖动功能（T2.8）。
# 这里先测空窗口的 UI 响应基线：每 50ms 发一次 WM_NULL，记录超时与最大间隔。

[CmdletBinding()]
param(
  [string]$Exe = '',
  [int]$DurationSec = 30,
  [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\measure.ps1')

$repo = Get-RepoRoot
if (-not $Exe) { $Exe = Join-Path $repo 'dist\lapian.exe' }
if (-not (Test-Path $Exe)) { throw "找不到可执行文件：$Exe" }
if (-not $OutDir) {
  $OutDir = Join-Path $repo ('docs\perf\runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-ui')
}
$procName = [System.IO.Path]::GetFileNameWithoutExtension($Exe)

$run = New-MeasurementRun -OutDir $OutDir -Label 'ui'
Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$result = Start-AppAndWaitWindow -Exe $Exe -TimeoutMs 10000
if ($result.Exited -or $result.Handle -eq [IntPtr]::Zero) { throw '窗口未出现' }

$heartbeat = Test-UiHeartbeat -Process $result.Process -DurationSec $DurationSec -IntervalMs 50 -TimeoutMs 200
Add-Metric -Id 'P5' -Name 'UI 心跳超时次数（空态基线）' -Value $heartbeat.Stalls -Unit '次' -Note "$($heartbeat.Samples) 次探测 / ${DurationSec}s"
Add-Metric -Id 'P5' -Name 'UI 最大响应间隔（空态基线）' -Value $heartbeat.MaxGapMs -Unit 'ms'
Add-Metric -Id 'P5' -Name '拖动停顿 >200ms' -Value $null -Status 'skipped' -Note '需要拖动时间轴（T2.8）'

Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
$saved = Save-MeasurementRun
Write-Host "[OK] UI 心跳完成：$($saved.Dir)（stalls=$($heartbeat.Stalls)，maxGap=$($heartbeat.MaxGapMs)ms）"
