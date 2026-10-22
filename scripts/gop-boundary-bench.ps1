# T7.11 P7 跨 GOP 边界后退专项驱动（开发/验收用；不入分发）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\gop-boundary-bench.ps1 `
#     -File build\test-media\real-gop2s-full.mp4 -Samples 20 -Tag gop2s-hard
#
# 说明：
# - 产品 --gop-boundary-test 把当前帧精确 seek 到 GOP 第一个关键帧，再按一次
#   PgUp 到前一 GOP 末帧；目标本身是关键帧，seek 不会保留“目标前一帧”，
#   因此 PgUp 必须从上一个关键帧解完整个 GOP —— 这是跨 GOP 边界的真实成本，
#   不是 T7.9 普通 seek 后单步的 0.06ms。
# - 2s GOP 1080p 按 80ms（4K 按 150ms）；长 GOP 只作可用档证据，不参与判定。
# - 脚本不判断素材规格，只驱动和汇总；素材必须自己确认是 2s GOP。
# - PowerShell 5.1 Start-Process 传中文参数会按 ANSI 转码；路径含非 ASCII 时
#   脚本自动在 build\gop-media 下建 ASCII 硬链接。日志是 UTF-8。

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$File,
  [int]$Samples = 20,
  [string]$Tag = 'gop-boundary',
  [string]$Exe = '',
  [string]$LogDir = '',
  [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
  $Exe = Join-Path $root 'dist\lapian.exe'
}
if (-not (Test-Path -LiteralPath $Exe)) {
  throw "找不到 $Exe；先跑 scripts\build-release.ps1"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
  $LogDir = Join-Path $root 'build\bench'
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$source = (Resolve-Path -LiteralPath $File).Path
$asciiPath = $source
$nonAscii = $source.ToCharArray() | Where-Object { [int]$_ -gt 127 } | Select-Object -First 1
if ($null -ne $nonAscii -or $source -match '\s') {
  $linkDir = Join-Path $root 'build\gop-media'
  New-Item -ItemType Directory -Force -Path $linkDir | Out-Null
  $safeTag = $Tag -replace '[^A-Za-z0-9_.-]', '_'
  $link = Join-Path $linkDir ("gop-" + $safeTag + ".mp4")
  if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link -Force }
  New-Item -ItemType HardLink -Path $link -Target $source | Out-Null
  $asciiPath = $link
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$safeTag = $Tag -replace '[^A-Za-z0-9_.-]', '_'
$log = Join-Path $LogDir ("gop-boundary-$safeTag-$stamp.log")
$stdout = Join-Path $LogDir ("gop-boundary-$safeTag-$stamp.stdout.log")

$arguments = @(
  '--file', $asciiPath,
  '--gop-boundary-test',
  '--gop-boundary-samples', "$Samples",
  '--gop-boundary-tag', $Tag
)
Write-Host ("启动跨 GOP 边界专项：file={0} samples={1} tag={2}" -f $asciiPath, $Samples, $Tag)
Write-Host ("日志：{0}" -f $log)

$process = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru `
  -RedirectStandardError $log -RedirectStandardOutput $stdout
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$timedOut = $false
while (-not $process.HasExited) {
  Start-Sleep -Seconds 2
  $process.Refresh()
  if ((Get-Date) -gt $deadline) {
    $timedOut = $true
    try { $process.Kill() } catch { }
    break
  }
}
$process.WaitForExit()
# PowerShell 5.1 在重定向输出时可能拿不到 ExitCode（返回空）；有 summary 时以
# summary 为准，没有 summary 才按失败处理，避免把成功测量误报成 infra。
$rawExitCode = $process.ExitCode
$exitCodeText = if ($null -eq $rawExitCode) { 'n/a' } else { "$rawExitCode" }

$text = ''
if (Test-Path -LiteralPath $log) {
  $bytes = [IO.File]::ReadAllBytes($log)
  $text = [Text.Encoding]::UTF8.GetString($bytes)
}
$lines = $text -split "`r?`n"
$begin = ($lines | Where-Object { $_ -match '^T7\.11 gop_boundary_begin ' } | Select-Object -Last 1)
$plan = ($lines | Where-Object { $_ -match '^T7\.11 gop_boundary_plan ' } | Select-Object -Last 1)
$groups = @($lines | Where-Object { $_ -match '^T7\.11 gop_boundary_group ' })
$summary = ($lines | Where-Object { $_ -match '^T7\.11 gop_boundary_summary ' } | Select-Object -Last 1)
$failLine = ($lines | Where-Object { $_ -match '^T7\.11 gop_boundary_fail ' } | Select-Object -Last 1)

if ($begin) { Write-Host $begin }
if ($plan) { Write-Host $plan }
foreach ($group in $groups) { Write-Host $group }

if ($timedOut) {
  Write-Host "[FAIL] 外部超时（$TimeoutSeconds s），已终止进程"
  exit 5
}
if (-not $summary) {
  if ($failLine) { Write-Host $failLine }
  Write-Host "[FAIL] 没有找到 T7.11 gop_boundary_summary（exit=$exitCodeText）"
  exit 2
}
if ($null -ne $rawExitCode -and [int]$rawExitCode -ne 0) {
  Write-Host "[WARN] runner exit=$exitCodeText（summary 已产出，按 summary 判定）"
}
Write-Host $summary
if ($summary -match 'verdict=no_2s_samples') {
  Write-Host '[N/A] 素材没有 2s GOP 边界，只能作为可用档证据；P7 80ms 专项不成立'
  exit 4
}
if ($summary -match 'verdict=4k_diagnostic') {
  Write-Host '[N/A] 4K 跨 GOP 边界只作诊断（P7 4K 档 150ms），不判 1080p 80ms 专项'
  exit 4
}
if ($summary -match 'pass=1') {
  Write-Host '[OK] 跨 GOP 边界后退专项通过（2s GOP）'
  exit 0
}
Write-Host '[FAIL] 跨 GOP 边界后退专项未通过'
exit 3
