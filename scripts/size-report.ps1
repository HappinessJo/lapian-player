[CmdletBinding()]
param(
  [string]$DistDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

if (-not $DistDir) { $DistDir = Join-Path (Get-RepoRoot) 'dist' }
if (-not (Test-Path $DistDir)) { throw "目录不存在：$DistDir" }

$files = Get-ChildItem -LiteralPath $DistDir -Recurse -File
$total = ($files | Measure-Object -Property Length -Sum).Sum

$groups = [ordered]@{
  '主程序'    = @($files | Where-Object { $_.Extension -eq '.exe' })
  'Qt 运行库'  = @($files | Where-Object { $_.Name -like 'Qt6*.dll' })
  'FFmpeg'    = @($files | Where-Object { $_.Name -match '^(av|swscale|swresample)' })
  'VC 运行库'  = @($files | Where-Object { $_.Name -match '^(vcruntime|msvcp|concrt|vccorlib)' })
  'Qt 插件'   = @($files | Where-Object { $_.DirectoryName -ne (Resolve-Path $DistDir).Path })
  '其他'      = @()
}

$known = @()
foreach ($key in @('主程序', 'Qt 运行库', 'FFmpeg', 'VC 运行库', 'Qt 插件')) {
  foreach ($f in $groups[$key]) { $known += $f.FullName }
}
$groups['其他'] = @($files | Where-Object { $known -notcontains $_.FullName })

$rows = @()
foreach ($key in $groups.Keys) {
  $set = $groups[$key]
  $bytes = ($set | Measure-Object -Property Length -Sum).Sum
  if (-not $bytes) { $bytes = 0 }
  $rows += [pscustomobject]@{
    分类   = $key
    文件数 = $set.Count
    大小MB = [math]::Round($bytes / 1MB, 2)
  }
}

Write-Host ''
Write-Host '=== 分发体积（P12 上限 100MB） ==='
$rows | Format-Table -AutoSize | Out-String -Width 120 | Write-Host

$totalMB = [math]::Round($total / 1MB, 2)
Write-Host ("合计：{0} 个文件，{1} MB" -f $files.Count, $totalMB)

if ($totalMB -gt 100) {
  Write-Host ("[WARN] 超过 P12 上限 100MB，超出 {0} MB" -f [math]::Round($totalMB - 100, 2)) -ForegroundColor Yellow
} else {
  Write-Host ("[OK] 在 P12 上限内，余量 {0} MB" -f [math]::Round(100 - $totalMB, 2)) -ForegroundColor Green
}
