# 构建 FFmpeg 7.1 最小版（LGPL、动态链接、D3D11VA）
#
# 本机踩过的坑（脚本已处理）：
#   1. 用 MSYS2 + MSVC 工具链（--toolchain=msvc），不引入 MinGW 运行时
#   2. MSYS2 自带的 /usr/bin/link.exe 会遮蔽 MSVC 的 link.exe，必须改名
#   3. 必须用 `bash -c`（非登录 shell），否则 /etc/profile 重置 PATH，cl 找不到
#   4. `--disable-autodetect` 会连 D3D11VA 一起关掉，必须显式 --enable-d3d11va
#   5. 中文 VS 的 CC_IDENT 含中文，会让 RC 编译器报 RC2001；configure 后要改写
#      config.h 的 CC_IDENT（VSLANG=1033 对 cl 横幅无效）
#   6. MSYS2 的 make install 把 .lib/.def 放在 bin/，这里归位到 lib/
#
# 产物：third_party\ffmpeg\build-msvc\ffmpeg-<版本>-minimal\{include,lib,bin}

[CmdletBinding()]
param(
  [string]$FfmpegVersion = '7.1.1',
  [string]$BuildRoot = 'E:\ffmpeg-build',
  [string]$Msys2Root = 'E:\msys64',
  [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

function Download-File([string]$Url, [string]$OutFile) {
  if (Test-Path $OutFile) { return }
  & py -3 -c "import sys,urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])" $Url $OutFile
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $OutFile)) { throw "下载失败：$Url" }
}

function ConvertTo-MsysPath([string]$Path) {
  $p = $Path -replace '\\', '/'
  if ($p -match '^([A-Za-z]):(.*)$') { return '/' + $matches[1].ToLower() + $matches[2] }
  return $p
}

function Invoke-MsysBuildStep([string]$Command, [string]$LogPath, [string]$What) {
  $bat = @"
@echo off
set VSLANG=1033
call "E:\VS\C++\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=$Msys2Root\usr\bin;%PATH%
$Msys2Root\usr\bin\bash.exe -c "$Command"
echo STEP_EXIT=%errorlevel%
"@
  $batPath = Join-Path $BuildRoot ("step-" + ($What -replace '[^A-Za-z0-9]', '-') + ".cmd")
  Set-Content -LiteralPath $batPath -Value $bat -Encoding ASCII
  & cmd.exe /c $batPath *> $LogPath
  if ($LASTEXITCODE -ne 0) {
    $tail = Get-Content -LiteralPath $LogPath -Tail 25 | Out-String
    throw "$What 失败（退出码 $LASTEXITCODE）`n$tail"
  }
}

function Ensure-Msys2([string]$Root) {
  $bash = Join-Path $Root 'usr\bin\bash.exe'
  if (Test-Path $bash) { return $bash }
  $tarball = Join-Path $BuildRoot 'msys2-base.tar.xz'
  Write-Host '[1/6] 下载 MSYS2 基础包（约 41MB）...'
  Download-File 'https://mirrors.tuna.tsinghua.edu.cn/msys2/distrib/msys2-x86_64-latest.tar.xz' $tarball
  & tar.exe -xf $tarball -C (Split-Path $Root -Parent)
  if (-not (Test-Path $bash)) { throw "MSYS2 解压失败：$bash" }
  return $bash
}

function Ensure-Msys2Tools([string]$Bash) {
  Write-Host '[2/6] 检查 make/nasm...'
  $probe = & $Bash -c 'command -v make && command -v nasm' 2>$null
  if ($LASTEXITCODE -eq 0 -and $probe) { return }
  $cmd = "sed -i '1i Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/`$arch/' /etc/pacman.d/mirrorlist.msys; pacman -Sy --noconfirm; pacman -S --noconfirm --needed make nasm diffutils"
  & $Bash -c $cmd
  if ($LASTEXITCODE -ne 0) { throw 'MSYS2 包安装失败' }
}

function Disable-MsysLink([string]$Root) {
  $link = Join-Path $Root 'usr\bin\link.exe'
  if (Test-Path $link) {
    Rename-Item -LiteralPath $link -NewName 'link.exe.msys-bak' -Force
    Write-Host '  已重命名 MSYS2 的 link.exe（避免遮蔽 MSVC 链接器）'
  }
}

$bash = Ensure-Msys2 $Msys2Root
Ensure-Msys2Tools $bash
Disable-MsysLink $Msys2Root

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$tarball = Join-Path $BuildRoot "ffmpeg-$FfmpegVersion.tar.xz"
Write-Host "[3/6] 下载 FFmpeg $FfmpegVersion 源码..."
Download-File "https://ffmpeg.org/releases/ffmpeg-$FfmpegVersion.tar.xz" $tarball
$sourceDir = Join-Path $BuildRoot "ffmpeg-$FfmpegVersion"
if (-not (Test-Path (Join-Path $sourceDir 'configure'))) {
  & tar.exe -xf $tarball -C $BuildRoot
}

$installDir = Join-Path $BuildRoot 'install'
if (Test-Path $installDir) { Remove-Item -LiteralPath $installDir -Recurse -Force }

$msysSource = ConvertTo-MsysPath $sourceDir
$msysInstall = ConvertTo-MsysPath $installDir

$configureOptions = @(
  '--toolchain=msvc', '--arch=x86_64', '--target-os=win64',
  "--prefix=$msysInstall",
  '--disable-everything', '--disable-autodetect', '--disable-programs', '--disable-doc',
  '--disable-network', '--disable-avdevice', '--disable-avfilter', '--disable-postproc',
  '--disable-debug', '--enable-shared', '--disable-static',
  '--enable-protocol=file,pipe',
  '--enable-demuxer=mov,matroska',
  '--enable-decoder=h264,hevc,vp9,av1,aac,mp3,opus,pcm_s16le,pcm_s24le,pcm_f32le,pcm_s16be,pcm_s24be,pcm_u8',
  '--enable-parser=h264,hevc,vp9,av1,aac,opus',
  '--enable-d3d11va',
  '--enable-hwaccel=h264_d3d11va,hevc_d3d11va,vp9_d3d11va,av1_d3d11va',
  '--enable-swscale', '--enable-swresample',
  '--x86asmexe=nasm'
) -join ' '

Write-Host '[4/6] configure ...'
$configureLog = Join-Path $BuildRoot 'configure.log'
Invoke-MsysBuildStep -Command "cd '$msysSource' && ./configure $configureOptions" -LogPath $configureLog -What 'configure'

$configureText = Get-Content -LiteralPath $configureLog -Raw
if ($configureText -notmatch 'h264_d3d11va') { throw 'configure 没有启用 h264_d3d11va，检查 D3D11VA 依赖' }

# 5. 改写 CC_IDENT，避免 RC 编译器在中文编译器描述上失败
$configHeader = Join-Path $sourceDir 'config.h'
$lines = Get-Content -LiteralPath $configHeader
$patched = $lines | ForEach-Object {
  if ($_ -match '^#define CC_IDENT ') { '#define CC_IDENT "MSVC x64"' } else { $_ }
}
Set-Content -LiteralPath $configHeader -Value $patched -Encoding ASCII

Write-Host '[5/6] make -j + make install（首次约 5–10 分钟）...'
$makeLog = Join-Path $BuildRoot 'make.log'
Invoke-MsysBuildStep -Command "cd '$msysSource' && make -j$Jobs && make install" -LogPath $makeLog -What 'make'

$dlls = @(Get-ChildItem (Join-Path $installDir 'bin') -Filter '*.dll' -ErrorAction SilentlyContinue)
if ($dlls.Count -eq 0) { throw "安装目录里没有 DLL：$installDir\bin" }

Write-Host '[6/6] 整理并复制到 third_party\ffmpeg\build-msvc ...'
# MSYS2 的 MSVC 安装把 .lib/.def 放在 bin/，归位到 lib/
foreach ($pattern in @('*.lib', '*.def')) {
  Get-ChildItem (Join-Path $installDir 'bin') -Filter $pattern -ErrorAction SilentlyContinue |
    ForEach-Object { Move-Item -LiteralPath $_.FullName -Destination (Join-Path $installDir 'lib') -Force }
}

$target = Join-Path (Get-RepoRoot) "third_party\ffmpeg\build-msvc\ffmpeg-$FfmpegVersion-minimal"
if (Test-Path $target) { Remove-Item -LiteralPath $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($sub in @('include', 'lib', 'bin')) {
  Copy-Item -Path (Join-Path $installDir $sub) -Destination $target -Recurse
}

$size = Get-DirSizeMB $target
Write-Host "[OK] FFmpeg $FfmpegVersion 最小构建就位：$target（$size MB）"
Write-Host '     接着运行 scripts\build-release.ps1 重新打包，check-imports 应不再有 av*.dll 的 WS2_32 例外。'
