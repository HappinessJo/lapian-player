# 测量脚本共用工具：CSV、窗口就绪、UI 心跳、内存采样（PowerShell 5.1 兼容）

$script:MeasureRun = $null

if (-not ('LapianMeasure' -as [type])) {
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class LapianMeasure {
  [DllImport("user32.dll", SetLastError = true)]
  public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam,
                                                 uint flags, uint timeout, out IntPtr result);
}
"@
}

function New-MeasurementRun {
  param(
    [string]$OutDir,
    [string]$Label
  )
  if (-not $OutDir) { throw '必须提供 -OutDir' }
  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
  $script:MeasureRun = [pscustomobject]@{
    Dir     = (Resolve-Path $OutDir).Path
    Label   = $Label
    Metrics = New-Object System.Collections.ArrayList
    Samples = New-Object System.Collections.ArrayList
    Started = Get-Date
  }
  return $script:MeasureRun
}

function Add-Metric {
  param(
    [Parameter(Mandatory = $true)][string]$Id,
    [Parameter(Mandatory = $true)][string]$Name,
    $Value,
    [string]$Unit = 'ms',
    [string]$Status = 'ok',
    [string]$Note = ''
  )
  if (-not $script:MeasureRun) { throw '先调用 New-MeasurementRun' }
  [void]$script:MeasureRun.Metrics.Add([pscustomobject]@{
    id     = $Id
    name   = $Name
    value  = $Value
    unit   = $Unit
    status = $Status
    note   = $Note
    ts     = (Get-Date).ToString('o')
  })
}

function Add-Sample {
  param(
    [Parameter(Mandatory = $true)][string]$Kind,
    [Parameter(Mandatory = $true)]$Value,
    [string]$Unit = '',
    [string]$Note = ''
  )
  if (-not $script:MeasureRun) { throw '先调用 New-MeasurementRun' }
  [void]$script:MeasureRun.Samples.Add([pscustomobject]@{
    kind  = $Kind
    value = $Value
    unit  = $Unit
    note  = $Note
    ts    = (Get-Date).ToString('o')
  })
}

function Save-MeasurementRun {
  if (-not $script:MeasureRun) { throw '没有进行中的测量' }
  $run = $script:MeasureRun
  $run.Metrics | Export-Csv -LiteralPath (Join-Path $run.Dir 'metrics.csv') -NoTypeInformation -Encoding UTF8
  $run.Samples | Export-Csv -LiteralPath (Join-Path $run.Dir 'samples.csv') -NoTypeInformation -Encoding UTF8
  return $run
}

# 启动进程并等待主窗口句柄出现；返回经过的毫秒数与进程对象
# 必须显式重定向子进程 stdout/stderr：GUI 子进程继承父进程管道会让父进程无法退出。
function Start-AppAndWaitWindow {
  param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Arguments = '',
    [int]$TimeoutMs = 10000,
    [int]$PollMs = 5
  )
  $logDir = $env:TEMP
  if ($script:MeasureRun) { $logDir = $script:MeasureRun.Dir }
  $outLog = Join-Path $logDir 'app-stdout.log'
  $errLog = Join-Path $logDir 'app-stderr.log'

  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $startArgs = @{
    FilePath               = $Exe
    PassThru               = $true
    RedirectStandardOutput = $outLog
    RedirectStandardError  = $errLog
  }
  if ($Arguments) { $startArgs.ArgumentList = $Arguments }
  $proc = Start-Process @startArgs
  $deadline = $TimeoutMs
  $handle = [IntPtr]::Zero
  while ($sw.ElapsedMilliseconds -lt $deadline) {
    $proc.Refresh()
    if ($proc.HasExited) { break }
    $handle = $proc.MainWindowHandle
    if ($handle -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds $PollMs
  }
  $ms = $sw.ElapsedMilliseconds
  return [pscustomobject]@{ Process = $proc; Handle = $handle; ElapsedMs = $ms; Exited = $proc.HasExited }
}

function Test-UiHeartbeat {
  param(
    [Parameter(Mandatory = $true)]$Process,
    [int]$DurationSec = 30,
    [int]$IntervalMs = 50,
    [int]$TimeoutMs = 200
  )
  $handle = $Process.MainWindowHandle
  $deadline = (Get-Date).AddSeconds($DurationSec)
  $lastOk = Get-Date
  $maxGapMs = 0
  $stalls = 0
  $samples = 0
  while ((Get-Date) -lt $deadline) {
    $result = [IntPtr]::Zero
    $ok = [LapianMeasure]::SendMessageTimeout($handle, 0, [IntPtr]::Zero, [IntPtr]::Zero, 0x0002, [uint32]$TimeoutMs, [ref]$result)
    $now = Get-Date
    $samples++
    if ($ok -eq [IntPtr]::Zero) {
      $stalls++
    } else {
      $gap = ($now - $lastOk).TotalMilliseconds
      if ($gap -gt $maxGapMs) { $maxGapMs = $gap }
      $lastOk = $now
    }
    Start-Sleep -Milliseconds $IntervalMs
  }
  return [pscustomobject]@{
    Samples       = $samples
    Stalls        = $stalls
    MaxGapMs      = [math]::Round($maxGapMs, 1)
    DurationSec   = $DurationSec
  }
}

function Get-Percentile {
  param([double[]]$Values, [double]$Percentile)
  if (-not $Values -or $Values.Count -eq 0) { return $null }
  $sorted = $Values | Sort-Object
  $rank = [math]::Ceiling($Percentile / 100 * $sorted.Count) - 1
  if ($rank -lt 0) { $rank = 0 }
  return [math]::Round($sorted[$rank], 1)
}
