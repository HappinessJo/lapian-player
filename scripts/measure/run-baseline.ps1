# 跑一组基线测量并汇总成 summary.md（P1/内存/UI 心跳；P2–P9 依赖后续任务，标记 skipped）

[CmdletBinding()]
param(
  [string]$Exe = '',
  [int]$Runs = 5,
  [int]$MemorySec = 30,
  [int]$UiSec = 30,
  [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')

$repo = Get-RepoRoot
if (-not $Exe) { $Exe = Join-Path $repo 'dist\lapian.exe' }
if (-not $OutDir) {
  $OutDir = Join-Path $repo ('docs\perf\runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-baseline')
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$startupDir = Join-Path $OutDir 'startup'
$memoryDir = Join-Path $OutDir 'memory'
$uiDir = Join-Path $OutDir 'ui'

Write-Host '=== P1 启动 ==='
& (Join-Path $PSScriptRoot 'measure-startup.ps1') -Exe $Exe -Runs $Runs -OutDir $startupDir

Write-Host '=== 内存采样 ==='
& (Join-Path $PSScriptRoot 'measure-memory.ps1') -Exe $Exe -DurationSec $MemorySec -OutDir $memoryDir

Write-Host '=== UI 心跳 ==='
& (Join-Path $PSScriptRoot 'measure-ui.ps1') -Exe $Exe -DurationSec $UiSec -OutDir $uiDir

# 合并 metrics.csv
$all = @()
foreach ($dir in @($startupDir, $memoryDir, $uiDir)) {
  $csv = Join-Path $dir 'metrics.csv'
  if (Test-Path $csv) { $all += Import-Csv -LiteralPath $csv }
}
$merged = Join-Path $OutDir 'metrics.csv'
$all | Export-Csv -LiteralPath $merged -NoTypeInformation -Encoding UTF8

# summary.md
$lines = @()
$lines += '# 测量汇总（' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '）'
$lines += ''
$lines += '- 可执行文件：`' + $Exe + '`'
$lines += '- 说明：本机不是 spec §4.1 基线机；P1 为温启动近似，P2–P9 依赖后续任务'
$lines += ''
$lines += '| 指标 | 数值 | 单位 | 状态 | 备注 |'
$lines += '|---|---|---|---|---|'
foreach ($m in $all) {
  $lines += ('| {0} {1} | {2} | {3} | {4} | {5} |' -f $m.id, $m.name, $m.value, $m.unit, $m.status, $m.note)
}
$lines += ''
$lines += '数据文件：`metrics.csv`（根目录为合并结果，子目录为各场景原始数据）、`samples.csv`。'
$lines += ''
$lines += '## 未覆盖指标'
$lines += ''
$lines += '| 指标 | 阻塞任务 |'
$lines += '|---|---|'
$lines += '| P2 双击到首帧 | T1.2 首帧解码 + T1.4 文件关联 |'
$lines += '| P3/P4/P5 拖动 | T2.9 预览解码 + T2.10 拖动集成 |'
$lines += '| P6 松手精确 | T2.3 精确 seek + T2.10 |'
$lines += '| P7/P8 步进/长按 | T2.4/T2.5 |'
$lines += '| P9 输入跳转 | T2.6 |'
$lines += '| P11/P14 播放内存 | T1.x 播放 + T7.3 soak |'

$summary = Join-Path $OutDir 'summary.md'
Set-Content -LiteralPath $summary -Value ($lines -join "`r`n") -Encoding UTF8
Write-Host "[OK] 汇总：$summary"
