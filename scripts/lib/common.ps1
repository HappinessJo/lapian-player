# 构建脚本共用的路径与依赖发现逻辑（PowerShell 5.1 兼容）

function Get-RepoRoot {
  return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

function Find-CMake {
  # 本机 CMake 4.2.0 在 configure 阶段崩溃（0xC0000409），锁定便携版 3.31.6
  $portableRoot = Join-Path (Get-RepoRoot) 'third_party\cmake'
  if (Test-Path $portableRoot) {
    $portable = Get-ChildItem -LiteralPath $portableRoot -Recurse -Filter 'cmake.exe' -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -like '*\data\bin\cmake.exe' } |
      Select-Object -First 1
    if ($portable) { return $portable.FullName }
  }
  $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  throw '找不到 cmake.exe：请运行 scripts\fetch-cmake.ps1，或把 CMake 3.31+ 加入 PATH。'
}

function Test-QtRoot([string]$Path) {
  if (-not $Path) { return $false }
  return (Test-Path (Join-Path $Path 'bin\Qt6Core.dll'))
}

function Find-QtRoot {
  if ($env:QT_ROOT) {
    if (Test-QtRoot $env:QT_ROOT) { return (Resolve-Path $env:QT_ROOT).Path }
    throw "QT_ROOT 无效：$env:QT_ROOT（需要 bin\Qt6Core.dll）"
  }
  $base = Join-Path (Get-RepoRoot) 'third_party\Qt'
  if (Test-Path $base) {
    $candidates = Get-ChildItem -LiteralPath $base -Directory |
      Sort-Object Name -Descending |
      ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
      Where-Object { Test-QtRoot $_ }
    if ($candidates) { return (Resolve-Path ($candidates | Select-Object -First 1)).Path }
  }
  throw '找不到 Qt 6.8：运行 scripts\fetch-qt.ps1，或设置 QT_ROOT 指向 msvc2022_64 目录。'
}

function Test-FfmpegRoot([string]$Path) {
  if (-not $Path) { return $false }
  # MSVC 版 FFmpeg 的导入库可能落在 bin/（MSYS2 install 的产物），两处都认
  $hasLib = (Test-Path (Join-Path $Path 'lib\avformat.lib')) -or (Test-Path (Join-Path $Path 'bin\avformat.lib'))
  return ((Test-Path (Join-Path $Path 'include\libavformat\avformat.h')) -and $hasLib)
}

function Find-FfmpegRoot {
  if ($env:FFMPEG_ROOT) {
    if (Test-FfmpegRoot $env:FFMPEG_ROOT) { return (Resolve-Path $env:FFMPEG_ROOT).Path }
    throw "FFMPEG_ROOT 无效：$env:FFMPEG_ROOT（需要 include\libavformat\avformat.h 与 lib\avformat.lib）"
  }
  $base = Join-Path (Get-RepoRoot) 'third_party\ffmpeg'
  foreach ($sub in @('build-msvc', 'prebuilt')) {
    $dir = Join-Path $base $sub
    if (-not (Test-Path $dir)) { continue }
    $hit = Get-ChildItem -LiteralPath $dir -Directory |
      Where-Object { Test-FfmpegRoot $_.FullName } |
      Select-Object -First 1
    if ($hit) { return $hit.FullName }
  }
  throw '找不到 FFmpeg：运行 scripts\build-ffmpeg.ps1，或设置 FFMPEG_ROOT。'
}

# 导入 vcvars64 环境（dumpbin 等工具需要）。返回 VS 安装路径。
function Import-VsEnv {
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "找不到 vswhere.exe：$vswhere" }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) { throw 'vswhere 没找到带 C++ 工具的 Visual Studio。' }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "找不到 vcvars64.bat：$vcvars" }
    $lines = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $lines) {
      $eq = $line.IndexOf('=')
      if ($eq -gt 0) {
        $name = $line.Substring(0, $eq)
        $value = $line.Substring($eq + 1)
        Set-Item -Path ('env:' + $name) -Value $value -ErrorAction SilentlyContinue
      }
    }
    return $install
  } finally {
    $ErrorActionPreference = $previous
  }
}

function Get-DirSizeMB([string]$Path) {
  if (-not (Test-Path $Path)) { return 0 }
  $sum = (Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction SilentlyContinue |
    Measure-Object -Property Length -Sum).Sum
  if (-not $sum) { return 0 }
  return [math]::Round($sum / 1MB, 1)
}

# 构建期临时目录固定到工作区内：可复现、可清理，也绕开个别机器上
# 系统 %TEMP% 对 MSBuild 子进程不可写的问题。
function Initialize-BuildTemp {
  $tmp = Join-Path (Get-RepoRoot) '.deps\tmp'
  New-Item -ItemType Directory -Force -Path $tmp | Out-Null
  $env:TMP = $tmp
  $env:TEMP = $tmp
  return $tmp
}

# 调用外部程序：临时放宽 ErrorActionPreference，避免工具写到 stderr 的警告
# 被 PowerShell 当成终止错误；退出码非 0 时抛可读错误。
function Invoke-Tool {
  param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string[]]$Arguments = @(),
    [string]$Description = ''
  )
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  & $Exe @Arguments
  $code = $LASTEXITCODE
  $ErrorActionPreference = $previous
  if ($code -ne 0) {
    if (-not $Description) { $Description = $Exe }
    throw ("{0} 失败（退出码 {1}）" -f $Description, $code)
  }
}
