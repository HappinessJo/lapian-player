[CmdletBinding()]
param(
  [switch]$Clean,
  [switch]$SkipTests,
  [string]$BuildDir = '',
  [string]$DistDir = '',
  [string]$QtRoot = '',
  [string]$FfmpegRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo = Get-RepoRoot
Initialize-BuildTemp | Out-Null
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\release' }
if (-not $DistDir) { $DistDir = Join-Path $repo 'dist' }
if (-not $QtRoot) { $QtRoot = Find-QtRoot }
if (-not $FfmpegRoot) { $FfmpegRoot = Find-FfmpegRoot }
$cmake = Find-CMake
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'

$timer = [System.Diagnostics.Stopwatch]::StartNew()

Write-Host '=== 1/7 配置 ==='
if ($Clean -and (Test-Path $BuildDir)) {
  Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
# 规避 CMake 3.31 在本机重配崩溃（见 scripts/configure.ps1 注释）
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
Invoke-Tool -Exe $cmake -Arguments $cmakeArgs -Description 'CMake 配置'

Write-Host '=== 2/7 构建（Release） ==='
# 本机 MSBuild 并行构建会无诊断地以退出码 1 结束（0 错误、0 警告），单线程可稳定通过；
# 构建耗时不是首期指标，这里优先保证 build-release.ps1 可重复。
Invoke-Tool -Exe $cmake -Arguments @('--build', $BuildDir, '--config', 'Release') -Description '构建'

if (-not $SkipTests) {
  Write-Host '=== 3/7 单元测试 ==='
  $env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"
  Invoke-Tool -Exe $ctest -Arguments @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure') -Description '单元测试'
} else {
  Write-Host '=== 3/7 单元测试（已跳过） ==='
}

Write-Host '=== 4/7 组装 dist ==='
if (Test-Path $DistDir) { Remove-Item -LiteralPath $DistDir -Recurse -Force }
New-Item -ItemType Directory -Path $DistDir | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir 'bin\lapian.exe') -Destination $DistDir
Copy-Item -LiteralPath (Join-Path $BuildDir 'bin\lapian_env_check.exe') -Destination $DistDir

Write-Host '=== 5/7 部署 Qt（windeployqt） ==='
$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) { throw "找不到 windeployqt.exe：$windeployqt" }
Invoke-Tool -Exe $windeployqt -Arguments @('--release', '--no-translations', '--compiler-runtime', '--dir', $DistDir, (Join-Path $DistDir 'lapian.exe')) -Description 'windeployqt'

# 首期不做网络/触摸/OpenGL：删掉 windeployqt 多带的模块，避免把网络能力带进发布包。
# check-imports.ps1 会断言这些文件不存在。
foreach ($name in @('Qt6Network.dll', 'D3dcompiler_47.dll')) {
  $path = Join-Path $DistDir $name
  if (Test-Path $path) { Remove-Item -LiteralPath $path -Force }
}
foreach ($dir in @('tls', 'networkinformation', 'generic')) {
  $path = Join-Path $DistDir $dir
  if (Test-Path $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

# MSVC 运行库：--compiler-runtime 在本机没复制出 CRT，这里从 VS 的可再发行目录
# 显式复制，保证干净机器可直接运行（VC 运行库允许随应用再分发）。
$vsInstall = Import-VsEnv
$redistRoot = Join-Path $vsInstall 'VC\Redist\MSVC'
if (Test-Path $redistRoot) {
  $crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
  if ($crtDir) {
    Copy-Item -Path (Join-Path $crtDir '*.dll') -Destination $DistDir -Force
  } else {
    Write-Host '[WARN] 未找到 Microsoft.VC143.CRT 目录，干净机器可能缺少 VC 运行库' -ForegroundColor Yellow
  }
} else {
  Write-Host '[WARN] 未找到 VC Redist 目录，干净机器可能缺少 VC 运行库' -ForegroundColor Yellow
}

Write-Host '=== 6/7 复制 FFmpeg 运行时 ==='
$ffmpegBin = Join-Path $FfmpegRoot 'bin'
foreach ($pattern in @('avformat*.dll', 'avcodec*.dll', 'avutil*.dll', 'swscale*.dll', 'swresample*.dll')) {
  $dlls = @(Get-ChildItem -LiteralPath $ffmpegBin -Filter $pattern -ErrorAction SilentlyContinue)
  if ($dlls.Count -eq 0) { throw "缺少 FFmpeg 运行时：$ffmpegBin\$pattern" }
  foreach ($dll in $dlls) {
    Copy-Item -LiteralPath $dll.FullName -Destination $DistDir -Force
  }
}

Write-Host '=== 7/7 环境自检 + 无网络导入检查 + 体积 ==='
Invoke-Tool -Exe (Join-Path $DistDir 'lapian_env_check.exe') -Arguments @() -Description '环境自检'

& (Join-Path $PSScriptRoot 'check-imports.ps1') -DistDir $DistDir -AllowPrebuiltFfmpeg:($FfmpegRoot -like '*\prebuilt\*')
& (Join-Path $PSScriptRoot 'size-report.ps1') -DistDir $DistDir

$timer.Stop()
Write-Host ("[OK] Release 构建与打包完成，用时 {0:N1}s" -f $timer.Elapsed.TotalSeconds)
Write-Host "产物目录：$DistDir"
