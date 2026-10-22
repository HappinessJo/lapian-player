# T7.5 read-only directory verification (Windows ACL).
#
# Creates build\t7.5-readonly\<tag>\readonly, copies a video into it, denies
# write access for the current user with icacls, then runs the product
# --robust-test --robust-only readonly path and restores the ACL in finally.
# The video must live OUTSIDE --robust-root because the runner cleans its root.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\robust-readonly.ps1 -Exe dist\lapian.exe

[CmdletBinding()]
param(
  [string]$Exe = '',
  [string]$Media = '',
  [string]$Tag = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $repo 'dist\lapian.exe' }
if (-not $Media) { $Media = Join-Path $repo 'media\proxy\proxy-V1.mp4' }
if (-not $Tag) { $Tag = Get-Date -Format 'yyyyMMdd-HHmmss' }

if (-not (Test-Path -LiteralPath $Exe)) { throw "Missing exe: $Exe" }
if (-not (Test-Path -LiteralPath $Media)) { throw "Missing media: $Media" }

$base = Join-Path $repo "build\t7.5-readonly\$Tag"
$readonlyDir = Join-Path $base 'readonly'
$robustRoot = Join-Path $repo "build\t7.5-robust\readonly-$Tag"
$video = Join-Path $readonlyDir 'readonly.mp4'
if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
New-Item -ItemType Directory -Path $readonlyDir -Force | Out-Null
Copy-Item -LiteralPath $Media -Destination $video -Force

$identity = "$env:USERDOMAIN\$env:USERNAME"
Write-Host "[T7.5] deny write: $identity on $readonlyDir"
& icacls $readonlyDir /deny "${identity}:(W)" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls /deny failed with $LASTEXITCODE" }

$errFile = Join-Path $base 'robust.err.txt'
$outFile = Join-Path $base 'robust.out.txt'
$exitCode = 2
try {
  $stillWritable = $false
  $probe = Join-Path $readonlyDir '.write-probe'
  try {
    [IO.File]::WriteAllText($probe, 'x')
    $stillWritable = $true
    Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
  } catch {
    $stillWritable = $false
  }
  if ($stillWritable) {
    throw 'ACL did not deny write; refusing to report readonly evidence'
  }
  Write-Host '[T7.5] write denied, launching product'

  $arguments = @(
    '--robust-test',
    '--robust-only', 'readonly',
    '--robust-media', ('"' + $video + '"'),
    '--robust-root', ('"' + $robustRoot + '"'),
    '--robust-tag', $Tag,
    '--robust-readonly-dir', ('"' + $readonlyDir + '"')
  )
  $proc = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru -Wait `
    -RedirectStandardError $errFile -RedirectStandardOutput $outFile
  $exitCode = $proc.ExitCode
} finally {
  & icacls $readonlyDir /remove:d $identity | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "icacls /remove:d failed with $LASTEXITCODE"
  }
}

if (Test-Path -LiteralPath $errFile) {
  Get-Content -LiteralPath $errFile -Encoding utf8 |
    Where-Object { $_ -match '^T7\.5 ' } |
    ForEach-Object { Write-Host $_ }
}
Write-Host "[T7.5] readonly exit=$exitCode tag=$Tag"
exit $exitCode
