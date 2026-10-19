# P1 冷启动到窗口可交互（外部测量；P2 需要文件打开能力，T1.x 后启用）
#
# 口径说明：这里用“进程创建 → 主窗口句柄可用且响应”近似 P1；
# 不清理 OS 文件缓存、不重启系统，因此是“温启动”基线，正式 P1 在 T7.1 按 spec 口径复测。

[CmdletBinding()]
param(
  [string]$Exe = '',
  [int]$Runs = 5,
  [string]$OutDir = '',
  [string]$MediaPath = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\measure.ps1')

$repo = Get-RepoRoot
if (-not $Exe) { $Exe = Join-Path $repo 'dist\lapian.exe' }
if (-not (Test-Path $Exe)) { throw "找不到可执行文件：$Exe（先运行 scripts\build-release.ps1）" }
if (-not $OutDir) {
  $OutDir = Join-Path $repo ('docs\perf\runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-startup')
}
$procName = [System.IO.Path]::GetFileNameWithoutExtension($Exe)

$run = New-MeasurementRun -OutDir $OutDir -Label 'startup'
$values = @()

for ($i = 1; $i -le $Runs; $i++) {
  Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 800

  $result = Start-AppAndWaitWindow -Exe $Exe -TimeoutMs 10000
  if ($result.Exited -or $result.Handle -eq [IntPtr]::Zero) {
    Add-Metric -Id 'P1' -Name '冷启动到窗口可交互' -Value $null -Status 'error' -Note "run $i：窗口未出现或进程退出"
    continue
  }

  Add-Metric -Id 'P1' -Name '冷启动到窗口可交互' -Value $result.ElapsedMs -Unit 'ms' -Note "run $i（温启动，未清 OS 缓存）"
  Add-Sample -Kind 'startup_window_ms' -Value $result.ElapsedMs -Unit 'ms'
  $values += [double]$result.ElapsedMs

  $result.Process.Refresh()
  Add-Sample -Kind 'working_set_mb_at_window' -Value ([math]::Round($result.Process.WorkingSet64 / 1MB, 1)) -Unit 'MB'

  if ($MediaPath) {
    Add-Metric -Id 'P2' -Name '双击已关联文件到首帧' -Value $null -Status 'skipped' `
      -Note '需要 T1.4 命令行/关联打开 + T1.2 首帧解码；T0.2 只预留入口'
  } else {
    Add-Metric -Id 'P2' -Name '双击已关联文件到首帧' -Value $null -Status 'skipped' -Note '未提供 -MediaPath'
  }

  Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
}

if ($values.Count -gt 0) {
  Add-Metric -Id 'P1' -Name '冷启动到窗口可交互（p50）' -Value (Get-Percentile $values 50) -Unit 'ms' -Note "$($values.Count) 次"
  Add-Metric -Id 'P1' -Name '冷启动到窗口可交互（p95）' -Value (Get-Percentile $values 95) -Unit 'ms' -Note "$($values.Count) 次"
}

$saved = Save-MeasurementRun
Write-Host "[OK] P1 测量完成：$($saved.Dir)"
Write-Host ("     runs=$($values.Count)  p50={0}ms  p95={1}ms" -f (Get-Percentile $values 50), (Get-Percentile $values 95))
