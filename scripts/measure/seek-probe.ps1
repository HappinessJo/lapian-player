# T0.3 精确 seek 探针调度：对一组素材跑 硬解/软解，汇总 p50/p95 与 Go/No-Go 初判
#
# 说明：探针只测“关键帧 seek → 解码到目标帧”，不含渲染/呈现/纹理拷贝；
# 因此它是 P6/P7 的**下界**：探针超标一定不达标，探针达标仍需产品级复测。

[CmdletBinding()]
param(
  [string[]]$Files = @(),
  [int]$Samples = 20,
  [string]$OutDir = '',
  [int]$Seed = 1
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')

$repo = Get-RepoRoot
$probe = Join-Path $repo 'build\release\bin\lapian_seek_probe.exe'
if (-not (Test-Path $probe)) { throw "找不到探针：$probe（先运行 scripts\build-release.ps1）" }
if (-not $OutDir) {
  $OutDir = Join-Path $repo ('docs\perf\runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-seek')
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if ($Files.Count -eq 0) {
  $Files = @(
    (Join-Path $repo 'media\V1.mp4'),
    (Join-Path $repo 'media\V3.mp4'),
    (Join-Path $repo 'media\V4.mp4')
  )
}

function Invoke-Probe {
  param([string]$File, [bool]$Hw, [string]$CsvPath)
  $hwArg = if ($Hw) { 'on' } else { 'off' }
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  $raw = (& $probe --file $File --samples $Samples --hw $hwArg --seed $Seed --csv $CsvPath 2>&1 | Out-String)
  $code = $LASTEXITCODE
  $ErrorActionPreference = $previous

  $data = @{}
  foreach ($line in ($raw -split "`r?`n")) {
    if ($line -match '^([A-Z0-9_]+)=(.*)$') {
      $data[$matches[1]] = $matches[2]
    }
  }
  $data['EXIT'] = $code
  return $data
}

$rows = @()
foreach ($file in $Files) {
  if (-not (Test-Path $file)) {
    Write-Host "[SKIP] 素材不存在：$file"
    continue
  }
  $name = [System.IO.Path]::GetFileNameWithoutExtension($file)
  foreach ($hw in @($true, $false)) {
    $label = if ($hw) { 'hw' } else { 'sw' }
    $csv = Join-Path $OutDir ("{0}-{1}.csv" -f $name, $label)
    Write-Host ("=== {0} [{1}] ===" -f $name, $label)
    $d = Invoke-Probe -File $file -Hw $hw -CsvPath $csv
    $row = [pscustomobject]@{
      file        = $name
      mode        = $label
      codec       = $d['CODEC']
      size        = $d['SIZE']
      fps         = $d['FPS']
      frames      = $d['FRAMES']
      gop_mean    = $d['GOP_MEAN_FRAMES']
      gop_max     = $d['GOP_MAX_FRAMES']
      keyframes   = $d['KEYFRAMES']
      hw_active   = $d['HW_ACTIVE']
      pix_fmt     = $d['PIX_FMT']
      samples     = $d['SAMPLES']
      ok          = $d['OK']
      exact       = $d['EXACT']
      p50_ms      = $d['P50_MS']
      p95_ms      = $d['P95_MS']
      min_ms      = $d['MIN_MS']
      max_ms      = $d['MAX_MS']
      csv         = $csv
      note        = $d['NOTE']
    }
    $rows += $row
    Write-Host ("    p50={0}ms p95={1}ms ok={2}/{3} exact={4} hw={5} pix={6}" -f $row.p50_ms, $row.p95_ms, $row.ok, $row.samples, $row.exact, $row.hw_active, $row.pix_fmt)
  }
}

$rows | Export-Csv -LiteralPath (Join-Path $OutDir 'seek-summary.csv') -NoTypeInformation -Encoding UTF8

# Go/No-Go 初判：1080p 看 P7 80ms / P6 150ms；4K 看 P7 150ms / P6 300ms
$lines = @()
$lines += '# T0.3 精确 seek 探针结果（' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '）'
$lines += ''
$lines += '> 探针只测“关键帧回退 → 解码到目标帧”，不含渲染/呈现；是 P6/P7 的下界。'
$lines += ''
$lines += '| 素材 | 模式 | 编码 | 尺寸 | GOP均值 | GOP最大 | p50(ms) | p95(ms) | 落点精确 | 硬解 |'
$lines += '|---|---|---|---|---|---|---|---|---|---|'
foreach ($r in $rows) {
  $lines += ('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8}/{9} | {10} |' -f `
      $r.file, $r.mode, $r.codec, $r.size, $r.gop_mean, $r.gop_max, $r.p50_ms, $r.p95_ms, $r.ok, $r.samples, $(if ($r.hw_active -eq '1') { '是' } else { '否' }))
}
$lines += ''
$lines += '## Go/No-Go 初判'
$lines += ''
$lines += '| 素材 | 模式 | 目标（P7 步进） | p95 | 判定 |'
$lines += '|---|---|---|---|---|'
foreach ($r in $rows) {
  if (-not $r.p95_ms) { continue }
  $is4k = ($r.size -like '3840*')
  $p7 = if ($is4k) { 150 } else { 80 }
  $verdict = if ([double]$r.p95_ms -le $p7) { '下界通过（需产品级复测）' } else { '不达标（探针已超 P7）' }
  $lines += ('| {0} | {1} | ≤{2}ms | {3}ms | {4} |' -f $r.file, $r.mode, $p7, $r.p95_ms, $verdict)
}
$lines += ''
$lines += '原始数据：同目录 `*-hw.csv` / `*-sw.csv`、`seek-summary.csv`。'

$summary = Join-Path $OutDir 'seek-summary.md'
Set-Content -LiteralPath $summary -Value ($lines -join "`r`n") -Encoding UTF8
Write-Host "[OK] 汇总：$summary"
