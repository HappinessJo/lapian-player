# 合成 V9 测试片：1080p24、第 100 帧整帧白闪、同一时刻的 click 声
#
# 只在构建期使用外部带编码器的 FFmpeg（third_party\tools\ffmpeg-gpl，不随产品分发）；
# 产品自身仍是 LGPL 最小构建，运行期不联网。

[CmdletBinding()]
param(
  [string]$Ffmpeg = '',
  [string]$OutFile = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')

$repo = Get-RepoRoot
if (-not $OutFile) { $OutFile = Join-Path $repo 'media\V9.mp4' }
New-Item -ItemType Directory -Force -Path (Split-Path $OutFile -Parent) | Out-Null

if (-not $Ffmpeg) {
  $candidate = Get-ChildItem (Join-Path $repo 'third_party\tools\ffmpeg-gpl') -Recurse -Filter 'ffmpeg.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $candidate) {
    throw '找不到带编码器的 FFmpeg：先运行 scripts\asset\fetch-encoder-ffmpeg.ps1'
  }
  $Ffmpeg = $candidate.FullName
}
$ffprobe = Join-Path (Split-Path $Ffmpeg -Parent) 'ffprobe.exe'

$videoFilter = "[0:v]drawbox=x=0:y=0:w=1920:h=1080:color=white:t=fill:enable='eq(n,100)',format=yuv420p[v]"
$audioFilter = '[1:a]adelay=4167:all=1,apad=whole_dur=5[a]'

$args = @(
  '-y', '-hide_banner', '-loglevel', 'error',
  '-f', 'lavfi', '-i', 'color=c=black:s=1920x1080:r=24:d=5',
  '-f', 'lavfi', '-i', 'sine=frequency=1080:duration=0.01:sample_rate=48000',
  '-filter_complex', "$videoFilter;$audioFilter",
  '-map', '[v]', '-map', '[a]',
  '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '16', '-g', '24', '-pix_fmt', 'yuv420p',
  '-c:a', 'aac', '-b:a', '128k', '-ar', '48000', '-ac', '1',
  '-movflags', '+faststart',
  $OutFile
)

Write-Host "合成 V9：$OutFile"
& $Ffmpeg @args
if ($LASTEXITCODE -ne 0) { throw "FFmpeg 合成失败（退出码 $LASTEXITCODE）" }

# ---- 校验 1：总帧数与帧率 ----
$probe = & $ffprobe -v error -select_streams v:0 -count_frames -show_entries stream=nb_read_frames,r_frame_rate -of default=noprint_wrappers=1 $OutFile
$probeText = ($probe -join "`n")
$probeText
if ($probeText -notmatch 'nb_read_frames=120') { throw "V9 帧数不是 120：`n$probeText" }
if ($probeText -notmatch 'r_frame_rate=24/1') { throw "V9 帧率不是 24fps：`n$probeText" }

# ---- 校验 2：第 99/100/101 帧中心像素 黑/白/黑 ----
$frameDir = Join-Path (Split-Path $OutFile -Parent) '_v9_frames'
if (Test-Path $frameDir) { Remove-Item $frameDir -Recurse -Force }
New-Item -ItemType Directory -Path $frameDir | Out-Null

& $Ffmpeg -y -hide_banner -loglevel error -i $OutFile -vf "select='between(n\,99\,101)'" -vsync 0 (Join-Path $frameDir 'f%02d.png')
if ($LASTEXITCODE -ne 0) { throw 'V9 抽帧失败' }

Add-Type -AssemblyName System.Drawing
$expected = @{ 'f01.png' = 'black'; 'f02.png' = 'white'; 'f03.png' = 'black' }
foreach ($name in $expected.Keys) {
  $path = Join-Path $frameDir $name
  if (-not (Test-Path $path)) { throw "缺少抽帧：$name" }
  $bmp = [System.Drawing.Bitmap]::FromFile($path)
  $c = $bmp.GetPixel(960, 540)
  $brightness = ($c.R + $c.G + $c.B) / 3
  $bmp.Dispose()
  $kind = if ($brightness -gt 200) { 'white' } elseif ($brightness -lt 30) { 'black' } else { 'gray' }
  Write-Host ("  {0}: 亮度 {1:N0} -> {2}" -f $name, $brightness, $kind)
  if ($kind -ne $expected[$name]) { throw "V9 第 $name 帧应为 $($expected[$name])，实际 $kind" }
}
Remove-Item $frameDir -Recurse -Force

# ---- 校验 3：音频 click 起点 ≈ 第 100 帧时间（4.1667s） ----
$previous = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$detect = (& $Ffmpeg -hide_banner -i $OutFile -af 'silencedetect=noise=-40dB:d=0.005' -f null - 2>&1 | Out-String)
$ErrorActionPreference = $previous
if ($detect -match 'silence_end:\s*([0-9.]+)') {
  $clickAt = [double]$matches[1]
  $target = 100.0 / 24.0
  $deltaMs = [math]::Abs($clickAt - $target) * 1000
  Write-Host ("  click 起点 {0:N4}s，目标 {1:N4}s，偏差 {2:N2}ms" -f $clickAt, $target, $deltaMs)
  if ($deltaMs -gt 1.0) { throw "V9 click 与第 100 帧偏差 ${deltaMs}ms，超过 1ms" }
} else {
  throw "V9 未检测到 click（silencedetect 无 silence_end）"
}

Write-Host ('[OK] V9 就位：{0}（{1:N1} MB）' -f $OutFile, ((Get-Item $OutFile).Length / 1MB))
