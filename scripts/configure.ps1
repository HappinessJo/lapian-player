[CmdletBinding()]
param(
  [switch]$Clean,
  [string]$BuildDir = '',
  [string]$QtRoot = '',
  [string]$FfmpegRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo = Get-RepoRoot
Initialize-BuildTemp | Out-Null
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\release' }
if (-not $QtRoot) { $QtRoot = Find-QtRoot }
if (-not $FfmpegRoot) { $FfmpegRoot = Find-FfmpegRoot }
$cmake = Find-CMake

if ($Clean -and (Test-Path $BuildDir)) {
  Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

# 本机 CMake 3.31 在读取已有 CMakeCache.txt 重配时会崩溃（0xC0000409，
# 中文路径 + Windows 11 26200）。删缓存但保留构建产物，可继续增量编译。
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path $cache) { Remove-Item -LiteralPath $cache -Force }

$cmakeArgs = @(
  '-S', $repo,
  '-B', $BuildDir,
  '-G', 'Visual Studio 17 2022',
  '-A', 'x64',
  "-DCMAKE_PREFIX_PATH=$QtRoot",
  "-DFFMPEG_ROOT=$FfmpegRoot",
  '-DCMAKE_CONFIGURATION_TYPES=Release'
)

Write-Host "CMake:   $cmake"
Write-Host "Qt:      $QtRoot"
Write-Host "FFmpeg:  $FfmpegRoot"
Write-Host "构建目录: $BuildDir"

Invoke-Tool -Exe $cmake -Arguments $cmakeArgs -Description 'CMake 配置'

Write-Host '[OK] 配置完成'
