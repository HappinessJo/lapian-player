# 生成 V1/V3/V4 的代理素材（仅验证 T0.3 探针与工具链，不用于正式 Go/No-Go 结论）
#
# 代理刻意用长 GOP，覆盖“关键帧回退 + 解码到目标帧”的最坏路径：
#   proxy-V1: 1080p H.264 24fps 30s，GOP 250（≈10.4s）
#   proxy-V3: 1080p HEVC 60fps 20s，GOP 120（2s）
#   proxy-V4: 4K H.264 30fps 15s，GOP 60（2s）

[CmdletBinding()]
param(
  [string]$Ffmpeg = '',
  [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\common.ps1')

$repo = Get-RepoRoot
if (-not $OutDir) { $OutDir = Join-Path $repo 'media\proxy' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not $Ffmpeg) {
  $candidate = Get-ChildItem (Join-Path $repo 'third_party\tools\ffmpeg-gpl') -Recurse -Filter 'ffmpeg.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $candidate) { throw '找不到带编码器的 FFmpeg：先运行 scripts\asset\fetch-encoder-ffmpeg.ps1' }
  $Ffmpeg = $candidate.FullName
}

function New-Proxy {
  param([string]$Name, [string[]]$Arguments, [string]$OutFile)
  Write-Host "生成 $Name -> $OutFile"
  $args = @('-y', '-hide_banner', '-loglevel', 'error') + $Arguments + @($OutFile)
  & $Ffmpeg @args
  if ($LASTEXITCODE -ne 0) { throw "$Name 生成失败（退出码 $LASTEXITCODE）" }
  $size = (Get-Item $OutFile).Length / 1MB
  Write-Host ("  {0}（{1:N1} MB）" -f $OutFile, $size)
}

New-Proxy -Name 'proxy-V1（1080p24 H.264，GOP 250）' -OutFile (Join-Path $OutDir 'proxy-V1.mp4') -Arguments @(
  '-f', 'lavfi', '-i', 'testsrc2=size=1920x1080:rate=24:duration=30',
  '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=30',
  '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '20',
  '-g', '250', '-keyint_min', '250', '-sc_threshold', '0', '-pix_fmt', 'yuv420p',
  '-c:a', 'aac', '-b:a', '96k', '-shortest'
)

New-Proxy -Name 'proxy-V3（1080p60 HEVC，GOP 120）' -OutFile (Join-Path $OutDir 'proxy-V3.mp4') -Arguments @(
  '-f', 'lavfi', '-i', 'testsrc2=size=1920x1080:rate=60:duration=20',
  '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=20',
  '-c:v', 'libx265', '-preset', 'ultrafast', '-crf', '24',
  '-x265-params', 'keyint=120:min-keyint=120:scenecut=0',
  '-pix_fmt', 'yuv420p', '-tag:v', 'hvc1',
  '-c:a', 'aac', '-b:a', '96k', '-shortest'
)

New-Proxy -Name 'proxy-V4（4K30 H.264，GOP 60）' -OutFile (Join-Path $OutDir 'proxy-V4.mp4') -Arguments @(
  '-f', 'lavfi', '-i', 'testsrc2=size=3840x2160:rate=30:duration=15',
  '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=15',
  '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '23',
  '-g', '60', '-keyint_min', '60', '-sc_threshold', '0', '-pix_fmt', 'yuv420p',
  '-c:a', 'aac', '-b:a', '96k', '-shortest'
)

$ffprobe = Join-Path (Split-Path $Ffmpeg -Parent) 'ffprobe.exe'
foreach ($file in Get-ChildItem $OutDir -Filter 'proxy-*.mp4') {
  $info = & $ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames -of default=noprint_wrappers=1 $file.FullName
  Write-Host "--- $($file.Name) ---"
  $info
}
Write-Host '[OK] 代理素材就位（仅用于工具验证）'
