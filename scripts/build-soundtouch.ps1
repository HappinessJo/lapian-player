[CmdletBinding()]
param(
  [switch]$Force,
  [switch]$Clean,
  [string]$Version = '2.3.3'
)

# Build the SoundTouch tempo/pitch library as a shared DLL.
# SoundTouch is LGPL-2.1; the app links it dynamically (see licenses/).
# third_party/ is gitignored: this script downloads and builds it locally.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo = Get-RepoRoot
Initialize-BuildTemp | Out-Null
$root = Join-Path $repo 'third_party\soundtouch'
$src = Join-Path $root 'soundtouch'
$tar = Join-Path $root "soundtouch-$Version.tar.gz"
$build = Join-Path $root 'build-msvc'
$dll = Join-Path $build 'Release\SoundTouch.dll'
$lib = Join-Path $build 'Release\SoundTouch.lib'
$url = "https://www.surina.net/soundtouch/soundtouch-$Version.tar.gz"
$expectedSha256 = '43B23DFAC2F64A3AFF55D64BE096FFC7B73842C3F5665CAFF44975633A975A99'

New-Item -ItemType Directory -Force -Path $root | Out-Null

if ((Test-Path $dll) -and (Test-Path $lib) -and -not $Force -and -not $Clean) {
  Write-Host "[OK] SoundTouch already built: $dll"
  exit 0
}

if (-not (Test-Path (Join-Path $src 'CMakeLists.txt'))) {
  if (-not (Test-Path $tar)) {
    Write-Host "Downloading $url"
    try {
      [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    } catch { }
    Invoke-WebRequest -Uri $url -OutFile $tar -UseBasicParsing -TimeoutSec 120
  }
  $actual = (Get-FileHash -LiteralPath $tar -Algorithm SHA256).Hash
  if ($actual -ne $expectedSha256) {
    throw "SoundTouch archive SHA256 mismatch: $actual (expected $expectedSha256)"
  }
  Write-Host "Extracting $tar"
  & tar.exe -xzf $tar -C $root
  if ($LASTEXITCODE -ne 0 -and -not (Test-Path (Join-Path $src 'CMakeLists.txt'))) {
    throw "Failed to extract SoundTouch archive (exit $LASTEXITCODE)"
  }
  # bsdtar reports the Lazarus .so symlink as invalid on Windows; source is fine.
  if (-not (Test-Path (Join-Path $src 'CMakeLists.txt'))) {
    throw "SoundTouch source not found under $src"
  }
}

if ($Clean -and (Test-Path $build)) {
  Remove-Item -LiteralPath $build -Recurse -Force
}

$cmake = Find-CMake
Write-Host '=== SoundTouch configure (shared) ==='
& $cmake -S $src -B $build -G 'Visual Studio 17 2022' -A x64 -DBUILD_SHARED_LIBS=ON
if ($LASTEXITCODE -ne 0) { throw "SoundTouch configure failed (exit $LASTEXITCODE)" }

Write-Host '=== SoundTouch build (Release) ==='
& $cmake --build $build --config Release --target SoundTouch
if ($LASTEXITCODE -ne 0) { throw "SoundTouch build failed (exit $LASTEXITCODE)" }

if (-not (Test-Path $dll) -or -not (Test-Path $lib)) {
  throw "SoundTouch output missing: $dll / $lib"
}

$size = (Get-Item -LiteralPath $dll).Length
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
Write-Host ("[OK] SoundTouch {0} built: {1} ({2} bytes, SHA256 {3})" -f $Version, $dll, $size, $hash)
$global:LASTEXITCODE = 0
exit 0
