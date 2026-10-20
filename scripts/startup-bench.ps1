[CmdletBinding()]
param(
  [string]$Exe = '',
  [string]$File = '',
  [int]$Runs = 20,
  [string]$OutDir = '',
  [string]$SettingsRoot = '',
  [int]$P1LimitMs = 1500,
  [int]$P2LimitMs = 2500
)

$ErrorActionPreference = 'Stop'

# T7.1 冷启动测量：每个样本都启动一个新进程（--startup-test）。
# P1 = 进程启动 -> 事件循环可交互（ui_ready）。
# P2 = 进程启动 -> 首帧真正 Present（first_present）；--file 必填。
# 注意：本脚本不能清 OS 文件缓存；正式 P1/P2 的“冷缓存”条件仍要按 spec §4.1 说明。

$repo = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $repo 'dist\lapian.exe' }
$Exe = (Resolve-Path $Exe).Path
if (-not $OutDir) { $OutDir = Join-Path $repo 'build\t7.1-startup-bench' }
if (-not $SettingsRoot) { $SettingsRoot = Join-Path $OutDir 'settings' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $SettingsRoot | Out-Null
if ($File) { $File = (Resolve-Path $File).Path }

function Invoke-StartupRun {
  param([string]$Kind, [int]$Index)
  $log = Join-Path $OutDir ("{0}-{1:d2}.log" -f $Kind.ToLowerInvariant(), $Index)
  $procArgs = @('--startup-test', '--settings-path', $SettingsRoot)
  if ($File -and $Kind -eq 'P2') { $procArgs += @('--file', $File) }
  $proc = Start-Process -FilePath $Exe -ArgumentList $procArgs -Wait -PassThru `
    -RedirectStandardError $log
  $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($log))
  $result = [ordered]@{
    run = $Index
    kind = $Kind
    exit = $proc.ExitCode
    app_created_ms = $null
    window_created_ms = $null
    window_shown_ms = $null
    ui_ready_ms = $null
    first_present_ms = $null
    index_ready_ms = $null
    index_wait_ms = $null
    index_ms = $null
    stage_ms = $null
  }
  foreach ($line in ($text -split "`n")) {
    if ($line -match 'T7\.1 startup stage=([a-z_]+) t_ms=([0-9.]+)') {
      $stage = $Matches[1]
      $value = [double]$Matches[2]
      switch ($stage) {
        'app_created' { $result.app_created_ms = $value }
        'window_created' { $result.window_created_ms = $value }
        'window_shown' { $result.window_shown_ms = $value }
        'ui_ready' { $result.ui_ready_ms = $value }
        'first_present' { $result.first_present_ms = $value }
      }
    } elseif ($line -match 'T7\.1 index_ready frames=(\d+) index_ms=([0-9.]+) wait_ms=(\d+)') {
      $result.index_ms = [double]$Matches[2]
      $result.index_wait_ms = [int]$Matches[3]
    }
  }
  if ($Kind -eq 'P1') { $result.stage_ms = $result.ui_ready_ms }
  else { $result.stage_ms = $result.first_present_ms }
  [pscustomobject]$result
}

function Get-Summary {
  param([object[]]$Values)
  $sorted = @($Values | Where-Object { $null -ne $_ } | Sort-Object)
  if ($sorted.Count -eq 0) {
    return [ordered]@{ n = 0; min = $null; p50 = $null; p95 = $null; max = $null }
  }
  $p50 = $sorted[[Math]::Max(0, [int][Math]::Ceiling($sorted.Count * 0.50) - 1)]
  $p95 = $sorted[[Math]::Max(0, [int][Math]::Ceiling($sorted.Count * 0.95) - 1)]
  [ordered]@{
    n = $sorted.Count
    min = [Math]::Round($sorted[0], 2)
    p50 = [Math]::Round($p50, 2)
    p95 = [Math]::Round($p95, 2)
    max = [Math]::Round($sorted[-1], 2)
  }
}

$rows = New-Object System.Collections.Generic.List[object]
Write-Host "startup-bench exe=$Exe runs=$Runs file=$File"
for ($i = 1; $i -le $Runs; ++$i) {
  $rows.Add((Invoke-StartupRun -Kind 'P1' -Index $i))
  if ($File) {
    $rows.Add((Invoke-StartupRun -Kind 'P2' -Index $i))
  }
  if ($i % 5 -eq 0) { Write-Host "  $i/$Runs" }
}

$csv = Join-Path $OutDir 'startup-samples.csv'
$rows | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8

$p1 = Get-Summary @($rows | Where-Object { $_.kind -eq 'P1' } | ForEach-Object { $_.stage_ms })
$p1Pass = @($rows | Where-Object { $_.kind -eq 'P1' -and $null -ne $_.stage_ms -and $_.stage_ms -le $P1LimitMs }).Count
Write-Host ("P1 n={0} min={1}ms p50={2}ms p95={3}ms max={4}ms pass={5}/{6} limit={7}ms" -f `
  $p1.n, $p1.min, $p1.p50, $p1.p95, $p1.max, $p1Pass, $p1.n, $P1LimitMs)

if ($File) {
  $p2 = Get-Summary @($rows | Where-Object { $_.kind -eq 'P2' } | ForEach-Object { $_.stage_ms })
  $p2Pass = @($rows | Where-Object { $_.kind -eq 'P2' -and $null -ne $_.stage_ms -and $_.stage_ms -le $P2LimitMs }).Count
  Write-Host ("P2 n={0} min={1}ms p50={2}ms p95={3}ms max={4}ms pass={5}/{6} limit={7}ms" -f `
    $p2.n, $p2.min, $p2.p50, $p2.p95, $p2.max, $p2Pass, $p2.n, $P2LimitMs)
}

$badExit = @($rows | Where-Object { $_.exit -ne 0 })
if ($badExit.Count -gt 0) {
  Write-Host ("FAIL: {0} run(s) exited non-zero: {1}" -f $badExit.Count,
    (($badExit | ForEach-Object { "$($_.kind)#$($_.run)=$($_.exit)" }) -join ', '))
  exit 1
}
if ($p1Pass -lt [Math]::Min(19, [Math]::Ceiling($Runs * 0.95))) {
  Write-Host 'FAIL: P1 pass count below 19/20 requirement'
  exit 1
}
if ($File) {
  if ($p2Pass -lt [Math]::Min(19, [Math]::Ceiling($Runs * 0.95))) {
    Write-Host 'FAIL: P2 pass count below 19/20 requirement'
    exit 1
  }
}
Write-Host "samples=$csv"
exit 0
