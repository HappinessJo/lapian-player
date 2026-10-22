# T7.7 acceptance preflight (read-only inventory; not an acceptance run).
#
# This script answers: "does this machine have everything T7.7 needs?"
# It does NOT claim T7.7/P1-P14 passed. It never runs the 2h soak and never
# changes thresholds/baselines.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\t7.7-preflight.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\t7.7-preflight.ps1 -RunCTest
#
# Expected real-material layout (media\ is not tracked by git):
#   media\acceptance\V1.mp4 ... V7.mp4, V9.mp4, V10.mp4
#   media\acceptance\V8\*.mp4|*.wav   (at least 5 corrupt/unsupported files)
# Override with -MediaRoot.
#
# Exit codes: 0 = all required resources present; 2 = blocked; 1 = infra error.

[CmdletBinding()]
param(
  [string]$MediaRoot = '',
  [string]$Exe = '',
  [string]$OutDir = '',
  [switch]$SkipProbe,
  [switch]$RunCTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo

if ([string]::IsNullOrWhiteSpace($Exe)) { $Exe = 'dist\lapian.exe' }
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
  throw "missing executable: $Exe (run scripts\build-release.ps1 first)"
}
$exePath = (Resolve-Path -LiteralPath $Exe).Path
if ([string]::IsNullOrWhiteSpace($MediaRoot)) { $MediaRoot = 'media\acceptance' }
if ([string]::IsNullOrWhiteSpace($OutDir)) { $OutDir = 'build\t7.7' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$jsonPath = Join-Path $OutDir ("preflight-" + $stamp + ".json")

function Read-TextUtf8([string]$Path) {
  return [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($Path))
}

function ConvertTo-Rate([string]$Text) {
  if ($Text -match '^\s*([0-9.]+)\s*/\s*([0-9.]+)') {
    $den = [double]$Matches[2]
    if ($den -ne 0) { return ([double]$Matches[1] / $den) }
  }
  if ($Text -match '^\s*([0-9.]+)') { return [double]$Matches[1] }
  return 0.0
}

function Get-ProbeLines([string]$ProbeExe, [string[]]$Arguments) {
  # Native stderr (FFmpeg diagnostics) is expected for corrupt V8 samples.
  # Keep it out of the console and out of $ErrorActionPreference=Stop.
  $old = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    return @(& $ProbeExe @Arguments 2>&1)
  } finally {
    $ErrorActionPreference = $old
  }
}

function Get-ProbeMap([string]$ProbeExe, [string]$Path) {
  $map = @{}
  $lines = Get-ProbeLines $ProbeExe @($Path)
  foreach ($raw in $lines) {
    $line = [string]$raw
    $idx = $line.IndexOf('=')
    if ($idx -gt 0) { $map[$line.Substring(0, $idx)] = $line.Substring($idx + 1) }
  }
  return $map
}

$probeExe = Join-Path $repo 'build\release\bin\lapian_media_probe.exe'
$seekProbeExe = Join-Path $repo 'build\release\bin\lapian_seek_probe.exe'
$syntheticDir = Join-Path $repo 'media\synthetic-suite'
$report = [ordered]@{
  schema = 1
  generated = (Get-Date).ToString('o')
  repo = $repo
  exe = $exePath
  exe_sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
  verdict = 'blocked'
  blockers = @()
  warnings = @()
  machine = [ordered]@{}
  binary = [ordered]@{}
  baseline = [ordered]@{}
  materials = [ordered]@{}
  synthetic = [ordered]@{}
  association = [ordered]@{}
  tests = [ordered]@{}
  manual = [ordered]@{}
}

# ---------------------------------------------------------------- machine
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$cs = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$gpus = @(Get-CimInstance Win32_VideoController |
  Where-Object { $_.Name -notmatch 'Basic Display|Remote Display|Microsoft Basic Render' } |
  Select-Object -ExpandProperty Name)
$ramGb = [math]::Round(([double]$cs.TotalPhysicalMemory) / 1GB, 1)
$physicalCores = [int]$cpu.NumberOfCores
$logicalCores = [int]$cpu.NumberOfLogicalProcessors
$report.machine = [ordered]@{
  host = $env:COMPUTERNAME
  os = ("{0} {1}" -f $os.Caption, $os.Version)
  cpu = $cpu.Name.Trim()
  physical_cores = $physicalCores
  logical_cores = $logicalCores
  ram_gb = $ramGb
  gpu = ($gpus -join '; ')
}
try {
  $sysLetter = $env:SystemDrive.TrimEnd(':')
  $part = Get-Partition -DriveLetter $sysLetter -ErrorAction Stop | Select-Object -First 1
  $disk = Get-PhysicalDisk -DeviceNumber $part.DiskNumber -ErrorAction Stop
  $report.machine.system_disk = ("{0} media={1} bus={2}" -f $disk.FriendlyName, $disk.MediaType, $disk.BusType)
} catch {
  $report.machine.system_disk = 'unknown'
}

# Baseline from spec.md 4.1: 4 cores / 8GB / iGPU / SATA SSD / Windows 11.
$baselineReasons = @()
if ($physicalCores -ne 4) { $baselineReasons += "physical_cores=$physicalCores (need 4)" }
if ($ramGb -gt 9.0) { $baselineReasons += "ram_gb=$ramGb (need ~8)" }
if (($gpus -join ' ') -match 'NVIDIA|GeForce|RTX|GTX') {
  $baselineReasons += 'discrete NVIDIA GPU (baseline needs iGPU)'
}
if ($os.Version -notmatch '^10\.0\.2[2-9]') {
  $baselineReasons += ("os_version={0} (need Windows 11)" -f $os.Version)
}
$baselineOk = ($baselineReasons.Count -eq 0)
$report.machine.baseline_machine = $baselineOk
$report.machine.baseline_reasons = $baselineReasons
if (-not $baselineOk) {
  $report.blockers += ('machine is not the spec baseline: ' + ($baselineReasons -join '; '))
}

# ---------------------------------------------------------------- binary/package
$distFiles = @(Get-ChildItem -LiteralPath 'dist' -Recurse -File)
$distBytes = ($distFiles | Measure-Object -Property Length -Sum).Sum
$distMb = [math]::Round(([double]$distBytes) / 1MB, 2)
$setup = @(Get-ChildItem -LiteralPath 'out\package' -File -Filter '*-setup.exe' -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending)
$zip = @(Get-ChildItem -LiteralPath 'out\package' -File -Filter '*-portable.zip' -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending)
$setupMb = if ($setup.Count -gt 0) { [math]::Round($setup[0].Length / 1MB, 2) } else { 0 }
$zipMb = if ($zip.Count -gt 0) { [math]::Round($zip[0].Length / 1MB, 2) } else { 0 }
$report.binary = [ordered]@{
  dist_files = $distFiles.Count
  dist_mb = $distMb
  setup_mb = $setupMb
  zip_mb = $zipMb
  p12_limit_mb = 100
  p12_pass = (($distMb -le 100) -and ($setupMb -gt 0) -and ($setupMb -le 100) -and ($zipMb -gt 0) -and ($zipMb -le 100))
  package_report = (Test-Path -LiteralPath 'out\package\package-report.txt')
}
if (-not $report.binary.p12_pass) {
  $report.blockers += 'dist/setup/zip missing or over P12 100MB'
}
if (-not $report.binary.package_report) {
  $report.warnings += 'out\package\package-report.txt missing; rerun scripts\build-release.ps1 -Package'
}

# ---------------------------------------------------------------- baseline json
$baselinePath = 'scripts\bench\baseline-dev.json'
if (Test-Path -LiteralPath $baselinePath) {
  $baselineJson = (Read-TextUtf8 (Resolve-Path -LiteralPath $baselinePath).Path) | ConvertFrom-Json
  $report.baseline = [ordered]@{
    path = $baselinePath
    label = $baselineJson.label
    commit_short = $baselineJson.commit_short
    cache_state = $baselineJson.cache_state
    quick = $baselineJson.quick
    values = @($baselineJson.values.PSObject.Properties).Count
  }
  if ($baselineJson.cache_state -ne 'hot') {
    $report.warnings += 'baseline cache_state is not hot; do not mix with cold-cache T7.7 numbers'
  }
} else {
  $report.blockers += 'scripts\bench\baseline-dev.json missing'
}

# ---------------------------------------------------------------- materials
$expected = @(
  @{ id = 'V1'; kind = 'file' },
  @{ id = 'V2'; kind = 'file' },
  @{ id = 'V3'; kind = 'file' },
  @{ id = 'V4'; kind = 'file' },
  @{ id = 'V5'; kind = 'file' },
  @{ id = 'V6'; kind = 'file' },
  @{ id = 'V7'; kind = 'file' },
  @{ id = 'V8'; kind = 'v8' },
  @{ id = 'V9'; kind = 'file' },
  @{ id = 'V10'; kind = 'file' }
)

function Test-RealMaterial {
  param([string]$Id, [string]$Path)
  $detail = [ordered]@{ id = $Id; path = $Path; status = 'missing'; problems = @(); probe = @{}; gop = @{} }
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $detail }
  if ($SkipProbe) { $detail.status = 'unchecked'; return $detail }
  if (-not (Test-Path -LiteralPath $probeExe)) {
    $detail.status = 'probe_missing'
    $detail.problems += "missing probe: $probeExe"
    return $detail
  }
  $p = Get-ProbeMap $probeExe $Path
  $detail.probe = $p
  if ($p.OK -ne '1') {
    $detail.status = 'corrupt'
    $detail.problems += ("probe OK={0} ERROR={1}" -f $p.OK, $p.ERROR)
    return $detail
  }
  $size = [string]$p.SIZE
  $wh = $size -split 'x'
  $width = if ($wh.Count -eq 2) { [int]$wh[0] } else { 0 }
  $height = if ($wh.Count -eq 2) { [int]$wh[1] } else { 0 }
  $fps = ConvertTo-Rate ([string]$p.FPS_REAL)
  $duration = [double]$p.DURATION_US / 1000000.0
  $hasAudio = ([string]$p.HAS_AUDIO -eq '1')
  $vfr = ([string]$p.VFR_SUSPECT -eq '1')
  $codec = [string]$p.CODEC
  $problems = @()

  switch ($Id) {
    'V1' {
      if (-not ($width -eq 1920 -and $height -eq 1080)) { $problems += "size=$size (need 1920x1080)" }
      if ($codec -ne 'h264') { $problems += "codec=$codec (need h264)" }
      if ([math]::Abs($fps - 24.0) -gt 0.15) { $problems += ("fps={0:N3} (need ~24)" -f $fps) }
      if (-not ($duration -ge 180 -and $duration -le 600)) { $problems += ("duration={0:N1}s (need 3-10min)" -f $duration) }
      if (-not $hasAudio) { $problems += 'no audio (V1 needs music)' }
    }
    'V2' {
      if (-not ($width -eq 1920 -and $height -eq 1080)) { $problems += "size=$size (need 1920x1080)" }
      if ($codec -ne 'h264') { $problems += "codec=$codec (need h264)" }
      if ([math]::Abs($fps - 30.0) -gt 0.15) { $problems += ("fps={0:N3} (need ~30)" -f $fps) }
    }
    'V3' {
      if (-not ($width -eq 1920 -and $height -eq 1080)) { $problems += "size=$size (need 1920x1080)" }
      if ($codec -ne 'hevc') { $problems += "codec=$codec (need hevc)" }
      if ([math]::Abs($fps - 60.0) -gt 0.2) { $problems += ("fps={0:N3} (need ~60)" -f $fps) }
    }
    'V4' {
      if (-not ($width -eq 3840 -and $height -eq 2160)) { $problems += "size=$size (need 3840x2160)" }
      if ($codec -ne 'h264') { $problems += "codec=$codec (need h264)" }
      if ([math]::Abs($fps - 30.0) -gt 0.15) { $problems += ("fps={0:N3} (need ~30)" -f $fps) }
    }
    'V5' {
      if (-not $vfr) { $problems += 'VFR_SUSPECT=0 (need a real VFR phone recording)' }
    }
    'V6' {
      if (-not ($width -eq 1920 -and $height -eq 1080)) { $problems += "size=$size (need 1920x1080)" }
      if ($duration -lt 600) { $problems += ("duration={0:N1}s (need >=10min)" -f $duration) }
    }
    'V7' {
      if ($hasAudio) { $problems += 'HAS_AUDIO=1 (V7 must have no audio track)' }
    }
    'V9' {
      if (-not ($width -eq 1920 -and $height -eq 1080)) { $problems += "size=$size (need 1920x1080)" }
      if (-not $hasAudio) { $problems += 'no audio (V9 needs click)' }
      $report.manual.v9_click = 'verify click/white flash at frame 100 with lapian_audio_test / visual check'
    }
    'V10' {
      if (-not ($width -eq 1920 -and $height -eq 1080)) { $problems += "size=$size (need 1920x1080)" }
      if ($duration -lt 7000) { $problems += ("duration={0:N1}s (need ~7200s)" -f $duration) }
      if (-not $hasAudio) { $problems += 'no audio (V10 needs normal playback)' }
    }
  }

  if ($problems.Count -gt 0) {
    $detail.status = 'spec_mismatch'
    $detail.problems = $problems
    return $detail
  }

  $detail.status = 'ready'
  if (-not $SkipProbe -and (Test-Path -LiteralPath $seekProbeExe) -and ($Id -in @('V1', 'V3', 'V4'))) {
    $g = @{}
    $gopLines = Get-ProbeLines $seekProbeExe @('--file', $Path, '--samples', '1', '--hw', 'on')
    foreach ($raw in $gopLines) {
      $line = [string]$raw
      $idx = $line.IndexOf('=')
      if ($idx -gt 0) { $g[$line.Substring(0, $idx)] = $line.Substring($idx + 1) }
    }
    $detail.gop = $g
    if ($g.KEYFRAMES) {
      if ([double]$g.GOP_MAX_FRAMES -gt 0) {
        $gopMaxS = [double]$g.GOP_MAX_FRAMES / [math]::Max($fps, 0.001)
        $detail.gop.gop_max_seconds = [math]::Round($gopMaxS, 3)
      }
    } else {
      $detail.problems += 'seek probe did not return GOP stats'
      $detail.status = 'spec_mismatch'
    }
  } else {
    $detail.status = 'ready'
  }
  return $detail
}

$mediaRootAbs = if (Test-Path -LiteralPath $MediaRoot) { (Resolve-Path -LiteralPath $MediaRoot).Path } else { (Join-Path $repo $MediaRoot) }
$report.materials.root = $mediaRootAbs
$materialResults = @()
foreach ($item in $expected) {
  if ($item.kind -eq 'v8') {
    $v8Files = @()
    $v8Dir = Join-Path $mediaRootAbs 'V8'
    if (Test-Path -LiteralPath $v8Dir -PathType Container) {
      $v8Files = @(Get-ChildItem -LiteralPath $v8Dir -File | Where-Object { $_.Extension -in @('.mp4', '.mkv', '.mov', '.wav', '.txt') })
    } else {
      $v8Files = @(Get-ChildItem -LiteralPath $mediaRootAbs -File -Filter 'V8-*' -ErrorAction SilentlyContinue)
    }
    $detail = [ordered]@{ id = 'V8'; path = $v8Dir; status = 'missing'; files = @(); problems = @() }
    if ($v8Files.Count -lt 5) {
      $detail.problems += ("found {0} V8 files (need >=5 corrupt/unsupported)" -f $v8Files.Count)
    } elseif (-not $SkipProbe) {
      foreach ($f in $v8Files) {
        $p = Get-ProbeMap $probeExe $f.FullName
        $entry = [ordered]@{ file = $f.Name; ok = $p.OK; error = $p.ERROR }
        $detail.files += $entry
        if ($p.OK -ne '0') { $detail.problems += ("{0}: OK={1} (need 0)" -f $f.Name, $p.OK) }
      }
      if ($detail.problems.Count -eq 0) { $detail.status = 'ready' }
      else { $detail.status = 'spec_mismatch' }
    } else {
      $detail.status = 'unchecked'
    }
    $materialResults += $detail
  } else {
    $detail = Test-RealMaterial -Id $item.id -Path (Join-Path $mediaRootAbs ($item.id + '.mp4'))
    $materialResults += $detail
  }
}
$report.materials.items = $materialResults
foreach ($m in $materialResults) {
  if ($m.status -eq 'missing') { $report.blockers += ("real material {0} missing" -f $m.id) }
  elseif ($m.status -eq 'spec_mismatch' -or $m.status -eq 'corrupt') {
    $report.blockers += ("real material {0}: {1}" -f $m.id, ($m.problems -join '; '))
  }
}

# ---------------------------------------------------------------- synthetic suite
$synExpected = @('V1.mp4', 'V2.mp4', 'V3.mp4', 'V4.mp4', 'V5.mp4', 'V6.mp4', 'V7.mp4', 'V9.mp4', 'V10.mp4')
$synPresent = @()
foreach ($name in $synExpected) {
  if (Test-Path -LiteralPath (Join-Path $syntheticDir $name) -PathType Leaf) { $synPresent += $name }
}
$synV8 = @(Get-ChildItem -LiteralPath $syntheticDir -File -Filter 'V8-*' -ErrorAction SilentlyContinue)
$synV8Ok = 0
if (-not $SkipProbe) {
  foreach ($f in $synV8) {
    $p = Get-ProbeMap $probeExe $f.FullName
    if ($p.OK -eq '0') { $synV8Ok++ }
  }
}
$report.synthetic = [ordered]@{
  dir = $syntheticDir
  present = $synPresent.Count
  expected = $synExpected.Count
  v8_files = $synV8.Count
  v8_ok0 = $synV8Ok
  ready = (($synPresent.Count -eq $synExpected.Count) -and ($synV8.Count -ge 5) -and ($synV8Ok -eq $synV8.Count))
}
if (-not $report.synthetic.ready) {
  $report.warnings += 'synthetic suite incomplete; prechecks that depend on it will be skipped'
}

# ---------------------------------------------------------------- association
$progIdKey = 'HKCU:\Software\Classes\Lapian.Video'
$exts = @('.mp4', '.mov', '.mkv', '.webm')
$assoc = [ordered]@{ progid = (Test-Path -LiteralPath $progIdKey); extensions = [ordered]@{} }
foreach ($ext in $exts) {
  $key = "HKCU:\Software\Classes\$ext\OpenWithProgids"
  $has = $false
  if (Test-Path -LiteralPath $key) {
    $value = Get-ItemProperty -LiteralPath $key -Name 'Lapian.Video' -ErrorAction SilentlyContinue
    $has = ($null -ne $value)
  }
  $assoc.extensions[$ext] = $has
}
$assoc.ready = $assoc.progid -and (($assoc.extensions.Values | Where-Object { $_ }).Count -eq $exts.Count)
$report.association = $assoc
if (-not $assoc.ready) {
  $report.warnings += 'ProgID/OpenWithProgids not installed; install setup before F1-A1'
}

# ---------------------------------------------------------------- tests
$report.tests.ctest = 'not_run'
if ($RunCTest) {
  $env:TMP = Join-Path $repo '.deps\tmp'
  $env:TEMP = $env:TMP
  $env:PATH = (Join-Path $repo 'third_party\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
  $ctestOut = & ctest --test-dir build\release -C Release --output-on-failure 2>&1
  $ctestExit = $LASTEXITCODE
  $report.tests.ctest_exit = $ctestExit
  $report.tests.ctest = if ($ctestExit -eq 0) { 'pass' } else { 'fail' }
  $report.tests.ctest_tail = @($ctestOut | Select-Object -Last 8)
  if ($ctestExit -ne 0) { $report.blockers += 'ctest failed' }
}

# ---------------------------------------------------------------- manual gates
$report.manual = [ordered]@{
  cold_cache_p1_p2 = 'manual: reboot, clear OS file cache (RAMMap), then run startup-bench; hot-cache runs are not F1-A1'
  explorer_double_click = 'manual: install, associate, double-click a 1080p MP4 from Explorer, measure to first frame'
  dpi_matrix = 'manual: 100/125/150/200% real display scaling (QT_SCALE_FACTOR only prechecks layout)'
  zero_network = 'runtime: poll TCP/UDP during install/open/playback; structural check: scripts\check-imports.ps1'
  physical_device_loss = 'manual: unplug audio device during playback; simulated path is not physical evidence'
  reachable_smb = 'manual: open a share over a reachable SMB path; unreachable UNC is a different case'
  installer_ui = 'manual: English UI, unsigned, no icon are known gaps; per-user install must not require admin'
}

# ---------------------------------------------------------------- verdict
$report.verdict = if ($report.blockers.Count -eq 0) { 'ready_for_t7.7' } else { 'blocked' }
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

Write-Host ("T7.7 preflight verdict={0}" -f $report.verdict)
Write-Host ("machine={0} cores={1}/{2} ram={3}GB gpu={4} baseline={5}" -f `
  $report.machine.host, $physicalCores, $logicalCores, $ramGb, ($gpus -join '; '), $baselineOk)
Write-Host ("binary: dist={0} files/{1}MB setup={2}MB zip={3}MB p12={4}" -f `
  $distFiles.Count, $distMb, $setupMb, $zipMb, $report.binary.p12_pass)
Write-Host ("baseline: label={0} commit={1} cache={2}" -f `
  $report.baseline.label, $report.baseline.commit_short, $report.baseline.cache_state)
foreach ($m in $materialResults) {
  if ($m.status -eq 'ready') {
    Write-Host ("[READY] {0} {1}" -f $m.id, $m.path)
  } elseif ($m.status -eq 'missing') {
    Write-Host ("[MISSING] {0} {1}" -f $m.id, $m.path)
  } else {
    Write-Host ("[BLOCKED] {0} {1}: {2}" -f $m.id, $m.path, ($m.problems -join '; '))
  }
}
Write-Host ("synthetic: {0}/{1} + V8 ok0 {2}/{3} ready={4}" -f `
  $synPresent.Count, $synExpected.Count, $synV8Ok, $synV8.Count, $report.synthetic.ready)
Write-Host ("association: progid={0} extensions={1} ready={2}" -f `
  $assoc.progid, (($assoc.extensions.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '), $assoc.ready)
if ($RunCTest) { Write-Host ("ctest: {0}" -f $report.tests.ctest) }
if ($report.blockers.Count -gt 0) {
  Write-Host 'blockers:' -ForegroundColor Yellow
  foreach ($b in $report.blockers) { Write-Host ("  - " + $b) -ForegroundColor Yellow }
}
Write-Host ("report: {0}" -f $jsonPath)
if ($report.verdict -ne 'ready_for_t7.7') { exit 2 }
exit 0
