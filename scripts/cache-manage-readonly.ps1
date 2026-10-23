# T8.3.1 read-only cache root verification (Windows ACL).
#
# Creates a directory, denies write for the current user with icacls, then runs
# the product --online-cache-manage-test against it. The probe must return a
# readable "不可写（只读或权限不足）" error and must not change its current root.
# The ACL is restored in finally.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\cache-manage-readonly.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\cache-manage-readonly.ps1 `
#     -Exe "$env:LOCALAPPDATA\Programs\拉片软件\lapian.exe"

[CmdletBinding()]
param(
  [string]$Exe = '',
  [string]$Tag = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $repo 'build\release\bin\lapian.exe' }
if (-not $Tag) { $Tag = Get-Date -Format 'yyyyMMdd-HHmmss' }
if (-not (Test-Path -LiteralPath $Exe)) { throw "Missing exe: $Exe" }

$base = Join-Path $repo "build\t8.3.1-readonly\$Tag"
$readonlyDir = Join-Path $base 'readonly'
$cacheDir = Join-Path $base 'cache'
$recordsDir = Join-Path $base 'records'
$settingsDir = Join-Path $base 'settings'
$outFile = Join-Path $base 'probe.out.txt'
$errFile = Join-Path $base 'probe.err.txt'
if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
New-Item -ItemType Directory -Path $readonlyDir -Force | Out-Null

$identity = "$env:USERDOMAIN\$env:USERNAME"
Write-Host "[T8.3.1] deny write: $identity on $readonlyDir"
& icacls $readonlyDir /deny "${identity}:(W)" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls /deny failed with $LASTEXITCODE" }

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
  Write-Host '[T8.3.1] write denied, launching probe'

  $arguments = @(
    '--online-cache-manage-test',
    '--manage-root', ('"' + $cacheDir + '"'),
    '--manage-records-root', ('"' + $recordsDir + '"'),
    '--manage-settings-root', ('"' + $settingsDir + '"'),
    '--manage-readonly-root', ('"' + $readonlyDir + '"')
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

if (Test-Path -LiteralPath $outFile) {
  Get-Content -LiteralPath $outFile -Encoding utf8 |
    Where-Object { $_ -match 'readonly|verdict|checks|failures|intensity|integrity' } |
    ForEach-Object { Write-Host $_ }
}
if ($exitCode -ne 0 -and (Test-Path -LiteralPath $errFile)) {
  Get-Content -LiteralPath $errFile -Encoding utf8 | Select-Object -Last 20 | Write-Host
}
Write-Host "[T8.3.1] readonly exit=$exitCode tag=$Tag"
exit $exitCode
