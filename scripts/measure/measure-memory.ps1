# 内存采样（P10 空态基线；P11/P14 需要打开视频后采样，T1.x 后启用）

[CmdletBinding()]
param(
  [string]$Exe = '',
  [int]$DurationSec = 30,
  [int]$IntervalMs = 1000,
  [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\measure.ps1')

$repo = Get-RepoRoot
if (-not $Exe) { $Exe = Join-Path $repo 'dist\lapian.exe' }
if (-not (Test-Path $Exe)) { throw "找不到可执行文件：$Exe" }
if (-not $OutDir) {
  $OutDir = Join-Path $repo ('docs\perf\runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-memory')
}
$procName = [System.IO.Path]::GetFileNameWithoutExtension($Exe)

$run = New-MeasurementRun -OutDir $OutDir -Label 'memory'
Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$result = Start-AppAndWaitWindow -Exe $Exe -TimeoutMs 10000
if ($result.Exited -or $result.Handle -eq [IntPtr]::Zero) { throw '窗口未出现' }
$proc = $result.Process
Start-Sleep -Milliseconds 500

$deadline = (Get-Date).AddSeconds($DurationSec)
$workingSet = @()
$private = @()
while ((Get-Date) -lt $deadline) {
  $proc.Refresh()
  if ($proc.HasExited) { break }
  $ws = [math]::Round($proc.WorkingSet64 / 1MB, 1)
  $pv = [math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
  $workingSet += $ws
  $private += $pv
  Add-Sample -Kind 'working_set_mb' -Value $ws -Unit 'MB'
  Add-Sample -Kind 'private_mb' -Value $pv -Unit 'MB'
  Add-Sample -Kind 'handles' -Value $proc.HandleCount
  Add-Sample -Kind 'threads' -Value $proc.Threads.Count
  Start-Sleep -Milliseconds $IntervalMs
}

if ($workingSet.Count -gt 0) {
  Add-Metric -Id 'P10' -Name '空态工作集（均值）' -Value ([math]::Round(($workingSet | Measure-Object -Average).Average, 1)) -Unit 'MB' -Note "$($workingSet.Count) 个采样"
  Add-Metric -Id 'P10' -Name '空态工作集（峰值）' -Value ([math]::Round(($workingSet | Measure-Object -Maximum).Maximum, 1)) -Unit 'MB'
  Add-Metric -Id 'P11' -Name '视频播放内存' -Value $null -Status 'skipped' -Note '需要打开视频（T1.x）'
  Add-Metric -Id 'P14' -Name '2 小时内存' -Value $null -Status 'skipped' -Note '需要 V10 与长时 soak（T7.3）'
}

Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
$saved = Save-MeasurementRun
Write-Host "[OK] 内存采样完成：$($saved.Dir)（$($workingSet.Count) 个采样点）"
