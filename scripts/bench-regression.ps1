# T7.8 基准回归脚本（开发/验收用；不入分发）。
#
# 把 T7.1/T7.2/T7.4/T7.5/T7.6 的测试探针固化成一条命令：
#   冷启动 P1/P2 → 拖动 P4/P6/P7/P8/P9（1080p + 4K）→ 步进 P7（1080p + 4K）
#   → 内存 P10/P11 → T7.5 鲁棒性（可选只读 ACL）→ P12 体积/哈希。
#
# 阈值全部来自 scripts\bench\thresholds.json（spec.md v0.3 §4.1/§4.5），
# 同机复现基线来自 scripts\bench\baseline-dev.json（±10%，带噪声下限）。
#
# 明确边界：
# - 本脚本不冷缓存、不重启、不调用 RAMMap；P1/P2 是热缓存回归，
#   不能写成 F1-A1 或 T7.7 正式验收通过。
# - 素材是 media\proxy\proxy-V1.mp4 / proxy-V4.mp4（代理，不是 V1/V4 真片）。
# - 本脚本不跑 V10 2h（T7.3/P14 用 scripts\soak-bench.ps1）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1 -Quick
#   powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1 -UpdateBaseline
#   powershell -ExecutionPolicy Bypass -File scripts\bench-regression.ps1 -Suites startup,drag1080,step1080,memory,robust,package
#
# 退出码：0 通过；1 spec 阈值失败；2 基础设施失败（超时/缺日志/进程异常）；
#         3 同机基线漂移 > 容差；4 仅诊断（软解/非保证档）。

[CmdletBinding()]
param(
  [string]$Exe = '',
  [string]$Media1080 = '',
  [string]$Media4K = '',
  [string[]]$Suites = @('all'),
  [int]$Runs = 0,
  [int]$DragSamples = 0,
  [int]$DragSamples4k = 0,
  [int]$StepCount = 0,
  [int]$MemorySeconds = 0,
  [string]$SettingsRoot = '',
  [string]$LogDir = '',
  [string]$ResultDir = '',
  [string]$Thresholds = '',
  [string]$Baseline = '',
  [string]$BaselineLabel = '',
  [switch]$UpdateBaseline,
  [switch]$SkipBaseline,
  [switch]$Quick,
  [switch]$KeepSettings,
  [switch]$IncludeReadonly,
  [switch]$AllowSoftwareDecode
)

$ErrorActionPreference = 'Stop'

# 路径参数在 PowerShell 5.1 的 Start-Process 里会经过 ANSI 边界；优先用
# 工作区内的相对 ASCII 路径，必要时在 build\bench\media 下建硬链接/副本。
# 这个函数必须在下面的路径解析之前定义。
function ConvertTo-ProbeArg {
  param([string]$Path, [string]$NameHint)
  $abs = (Resolve-Path -LiteralPath $Path).Path
  $rel = ''
  try { $rel = [string](Resolve-Path -LiteralPath $abs -Relative) } catch { }
  if ($rel) { $rel = $rel -replace '^\.\\', '' }
  if ($rel -and $rel -notmatch '[^\x00-\x7F]' -and $rel -notmatch '\s') { return $rel }
  if ($abs -notmatch '[^\x00-\x7F]' -and $abs -notmatch '\s') { return $abs }
  $linkDir = Join-Path $script:repo 'build\bench\media'
  New-Item -ItemType Directory -Force -Path $linkDir | Out-Null
  $ext = [IO.Path]::GetExtension($abs)
  if (-not $ext) { $ext = '' }
  $link = Join-Path $linkDir ("probe-{0}{1}" -f ($NameHint -replace '[^A-Za-z0-9_.-]', '_'), $ext)
  if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link -Force }
  try {
    New-Item -ItemType HardLink -Path $link -Target $abs -ErrorAction Stop | Out-Null
  } catch {
    Copy-Item -LiteralPath $abs -Destination $link -Force
  }
  return (ConvertTo-ProbeArg $link $NameHint)
}

# ---------------------------------------------------------------------------
# 基础路径与参数
# ---------------------------------------------------------------------------

$script:repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $script:repo

if ([string]::IsNullOrWhiteSpace($Exe)) { $Exe = 'dist\lapian.exe' }
$script:exePath = (Resolve-Path -LiteralPath $Exe).Path
if (-not (Test-Path -LiteralPath $script:exePath -PathType Leaf)) {
  throw "找不到可执行文件：$Exe"
}
$script:exeArg = ConvertTo-ProbeArg $script:exePath 'lapian'
$script:exeHash = (Get-FileHash -LiteralPath $script:exePath -Algorithm SHA256).Hash

if ([string]::IsNullOrWhiteSpace($Media1080)) { $Media1080 = 'media\proxy\proxy-V1.mp4' }
if ([string]::IsNullOrWhiteSpace($Media4K)) { $Media4K = 'media\proxy\proxy-V4.mp4' }
foreach ($m in @($Media1080, $Media4K)) {
  if (-not (Test-Path -LiteralPath $m -PathType Leaf)) {
    throw "缺少基准素材：$m（media\ 不入库；按 docs\perf\T7.4-4k.md §2 准备代理素材）"
  }
}
$script:media1080Abs = (Resolve-Path -LiteralPath $Media1080).Path
$script:media4kAbs = (Resolve-Path -LiteralPath $Media4K).Path
$script:media1080Arg = ConvertTo-ProbeArg $script:media1080Abs 'proxy-1080p'
$script:media4kArg = ConvertTo-ProbeArg $script:media4kAbs 'proxy-4k'

if ([string]::IsNullOrWhiteSpace($Thresholds)) {
  $Thresholds = Join-Path $PSScriptRoot 'bench\thresholds.json'
}
$script:thresholdsPath = (Resolve-Path -LiteralPath $Thresholds).Path
$script:benchThresholds = Get-Content -LiteralPath $script:thresholdsPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($null -eq $script:benchThresholds.startup -or $null -eq $script:benchThresholds.drag -or
    $null -eq $script:benchThresholds.step -or $null -eq $script:benchThresholds.memory -or
    $null -eq $script:benchThresholds.robust -or $null -eq $script:benchThresholds.package) {
  throw "阈值文件缺少必需小节：$Thresholds"
}
$script:thresholdsHash = (Get-FileHash -LiteralPath $script:thresholdsPath -Algorithm SHA256).Hash

if ([string]::IsNullOrWhiteSpace($Baseline)) {
  $Baseline = Join-Path $PSScriptRoot 'bench\baseline-dev.json'
}
$script:baselinePath = if ([IO.Path]::IsPathRooted($Baseline)) { $Baseline } else { Join-Path $script:repo $Baseline }

if ($UpdateBaseline -and $SkipBaseline) {
  throw '-UpdateBaseline 与 -SkipBaseline 不能同时使用'
}
if ($UpdateBaseline -and $Quick) {
  throw '-Quick 的样本数不是 spec 口径，不能写基线'
}

# Quick 只用于调试脚本，不作为验收证据。
$script:isQuick = [bool]$Quick
if ($script:isQuick) {
  if ($Runs -le 0) { $Runs = 10 }
  if ($DragSamples -le 0) { $DragSamples = 10 }
  if ($DragSamples4k -le 0) { $DragSamples4k = 10 }
  if ($StepCount -le 0) { $StepCount = 20 }
  if ($MemorySeconds -le 0) { $MemorySeconds = 10 }
} else {
  if ($Runs -le 0) { $Runs = [int]$script:benchThresholds.startup.runs }
  if ($DragSamples -le 0) { $DragSamples = [int]$script:benchThresholds.drag.samples }
  if ($DragSamples4k -le 0) { $DragSamples4k = [int]$script:benchThresholds.drag.'4k'.samples }
  if ($StepCount -le 0) { $StepCount = [int]$script:benchThresholds.step.steps }
  if ($MemorySeconds -le 0) { $MemorySeconds = [int]$script:benchThresholds.memory.play_seconds }
}

$script:settingsRoot = if ([string]::IsNullOrWhiteSpace($SettingsRoot)) {
  Join-Path $script:repo 'build\bench\settings'
} elseif ([IO.Path]::IsPathRooted($SettingsRoot)) {
  $SettingsRoot
} else {
  Join-Path $script:repo $SettingsRoot
}
New-Item -ItemType Directory -Force -Path $script:settingsRoot | Out-Null
$script:settingsArg = ConvertTo-ProbeArg $script:settingsRoot 'settings'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:logRoot = if ([string]::IsNullOrWhiteSpace($LogDir)) {
  Join-Path $script:repo 'build\bench\logs'
} elseif ([IO.Path]::IsPathRooted($LogDir)) { $LogDir } else { Join-Path $script:repo $LogDir }
$script:logRunDir = Join-Path $script:logRoot $stamp
$script:resultRoot = if ([string]::IsNullOrWhiteSpace($ResultDir)) {
  Join-Path $script:repo 'build\bench\results'
} elseif ([IO.Path]::IsPathRooted($ResultDir)) { $ResultDir } else { Join-Path $script:repo $ResultDir }

New-Item -ItemType Directory -Force -Path $script:logRunDir | Out-Null
New-Item -ItemType Directory -Force -Path $script:resultRoot | Out-Null
New-Item -ItemType Directory -Force -Path $script:settingsRoot | Out-Null
if (-not $KeepSettings) {
  try {
    Get-ChildItem -LiteralPath $script:settingsRoot -Force -ErrorAction SilentlyContinue |
      Remove-Item -Recurse -Force -ErrorAction Stop
  } catch {
    Write-Warning "无法清空 settings 根（继续用现有设置）：$($_.Exception.Message)"
  }
}

$script:infraErrors = New-Object System.Collections.Generic.List[string]
$script:suiteErrorBaseline = 0
$script:allMetrics = [ordered]@{}
$script:allChecks = New-Object System.Collections.Generic.List[object]
$script:suiteResults = New-Object System.Collections.Generic.List[object]
$script:startTime = Get-Date

# ---------------------------------------------------------------------------
# 通用工具
# ---------------------------------------------------------------------------

function Format-ProcessArg {
  param([string]$Arg)
  if ($Arg -match '[^\x00-\x7F]' -or $Arg -match '\s' -or $Arg -match '"') {
    return '"' + ($Arg -replace '"', '\"') + '"'
  }
  return $Arg
}

function Invoke-Process {
  param(
    [string]$Name,
    [string]$FilePath,
    [string[]]$Arguments = @(),
    [int]$TimeoutSeconds = 120
  )
  $safe = $Name -replace '[^A-Za-z0-9_.-]', '_'
  $errLog = Join-Path $script:logRunDir ("$safe.err.log")
  $outLog = Join-Path $script:logRunDir ("$safe.out.log")
  $formatted = @($Arguments | ForEach-Object { Format-ProcessArg $_ })
  $started = Get-Date
  $timedOut = $false

  # Start-Process -PassThru 在 PowerShell 5.1 里异步启动后取不到 ExitCode；
  # 这里直接用 .NET Process，配合 ReadToEndAsync 排空 stdout/stderr，再用
  # WaitForExit(ms) 实现超时。日志仍落盘为 UTF-8，便于复盘。
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $FilePath
  $psi.Arguments = ($formatted -join ' ')
  $psi.WorkingDirectory = $script:repo
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.StandardOutputEncoding = New-Object System.Text.UTF8Encoding -ArgumentList $false
  $psi.StandardErrorEncoding = New-Object System.Text.UTF8Encoding -ArgumentList $false
  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  try {
    if (-not $proc.Start()) { throw 'Process.Start 返回 false' }
  } catch {
    try { $proc.Dispose() } catch { }
    [IO.File]::WriteAllText($errLog, '', (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($outLog, '', (New-Object Text.UTF8Encoding($false)))
    return [ordered]@{
      exit = $null; timed_out = $false; started = $false; seconds = 0.0
      stderr = ''; stdout = ''; err_log = $errLog; out_log = $outLog
      error = $_.Exception.Message
    }
  }

  $errTask = $proc.StandardError.ReadToEndAsync()
  $outTask = $proc.StandardOutput.ReadToEndAsync()
  $exited = $proc.WaitForExit([int]($TimeoutSeconds * 1000))
  if (-not $exited) {
    $timedOut = $true
    try { $proc.Kill() } catch { }
    try { $proc.WaitForExit() } catch { }
  } else {
    # WaitForExit(ms) 不保证异步读已完成；无参 WaitForExit 会等输出排空。
    try { $proc.WaitForExit() } catch { }
  }
  $seconds = ((Get-Date) - $started).TotalSeconds
  $exit = $null
  if ($proc.HasExited) { $exit = $proc.ExitCode }
  $stderr = ''
  $stdout = ''
  try { $stderr = $errTask.Result } catch { $stderr = '' }
  try { $stdout = $outTask.Result } catch { $stdout = '' }
  [IO.File]::WriteAllText($errLog, $stderr, (New-Object Text.UTF8Encoding($false)))
  [IO.File]::WriteAllText($outLog, $stdout, (New-Object Text.UTF8Encoding($false)))
  try { $proc.Dispose() } catch { }
  return [ordered]@{
    exit = $exit; timed_out = $timedOut; started = $true
    seconds = [Math]::Round($seconds, 2)
    stderr = $stderr; stdout = $stdout
    err_log = $errLog; out_log = $outLog; error = ''
  }
}

function Invoke-Probe {
  param([string]$Name, [string[]]$Arguments, [int]$TimeoutSeconds)
  return Invoke-Process -Name $Name -FilePath $script:exePath -Arguments $Arguments -TimeoutSeconds $TimeoutSeconds
}

function Get-AllLines {
  param([string]$Text, [string]$Pattern)
  if ([string]::IsNullOrEmpty($Text)) { return @() }
  return @($Text -split "`r?`n" | Where-Object { $_ -match $Pattern })
}

function Get-FirstLine {
  param([string]$Text, [string]$Pattern)
  $lines = @(Get-AllLines $Text $Pattern)
  if ($lines.Count -eq 0) { return $null }
  return $lines[0]
}

function Get-FieldString {
  param([string]$Line, [string]$Name)
  if ([string]::IsNullOrEmpty($Line)) { return $null }
  $m = [regex]::Match($Line, "(?:^|\s)" + [regex]::Escape($Name) + "=(\S+)")
  if ($m.Success) { return $m.Groups[1].Value }
  return $null
}

function Get-FieldDouble {
  param([string]$Line, [string]$Name)
  $raw = Get-FieldString $Line $Name
  if ($null -eq $raw) { return $null }
  try {
    return [double]::Parse($raw, [Globalization.CultureInfo]::InvariantCulture)
  } catch { return $null }
}

function Get-FieldInt {
  param([string]$Line, [string]$Name)
  $value = Get-FieldDouble $Line $Name
  if ($null -eq $value) { return $null }
  return [int]$value
}

function Get-Stats {
  param([object[]]$Values)
  $valid = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ } | Sort-Object)
  if ($valid.Count -eq 0) {
    return [ordered]@{ n = 0; min = $null; p50 = $null; p95 = $null; max = $null }
  }
  $p50i = [int][Math]::Floor(($valid.Count - 1) * 0.50)
  $p95i = [int][Math]::Floor(($valid.Count - 1) * 0.95)
  return [ordered]@{
    n = $valid.Count
    min = [Math]::Round($valid[0], 2)
    p50 = [Math]::Round($valid[$p50i], 2)
    p95 = [Math]::Round($valid[$p95i], 2)
    max = [Math]::Round($valid[$valid.Count - 1], 2)
  }
}

function New-Check {
  param(
    [string]$Id,
    $Value,
    [string]$Op,
    $Limit,
    [string]$Note = '',
    [bool]$Gating = $true
  )
  $pass = $false
  if ($null -ne $Value) {
    switch ($Op) {
      '>=' { $pass = ([double]$Value -ge [double]$Limit) }
      '<=' { $pass = ([double]$Value -le [double]$Limit) }
      '==' { $pass = ([double]$Value -eq [double]$Limit) }
      default { throw "未知比较操作：$Op" }
    }
  }
  return [ordered]@{
    id = $Id; value = $Value; op = $Op; limit = $Limit
    pass = [bool]$pass; gating = $Gating; note = $Note
  }
}

function Add-Check {
  param($List, $Check)
  [void]$List.Add($Check)
  [void]$script:allChecks.Add($Check)
}

function Set-Metric {
  param([string]$Id, $Value)
  if ($null -ne $Value) { $script:allMetrics[$Id] = $Value }
}

function Add-InfraError {
  param([string]$Message)
  [void]$script:infraErrors.Add($Message)
}

function Get-SuiteStatus {
  param($Checks)
  $gatingFails = @($Checks | Where-Object { $_.gating -and -not $_.pass })
  if ($gatingFails.Count -gt 0) { return 'fail' }
  return 'pass'
}

function Get-Prop {
  param($Object, [string]$Name)
  if ($null -eq $Object) { return $null }
  if ($Object -is [System.Collections.IDictionary]) {
    if ($Object.Contains($Name)) { return $Object[$Name] }
    return $null
  }
  $p = $Object.PSObject.Properties[$Name]
  if ($null -eq $p) { return $null }
  return $p.Value
}

function Write-JsonFile {
  param([string]$Path, $Object)
  $dir = Split-Path -Parent $Path
  if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  $json = $Object | ConvertTo-Json -Depth 12
  [IO.File]::WriteAllText($Path, $json + "`r`n", (New-Object Text.UTF8Encoding($false)))
}

function Get-GitValue {
  param([string[]]$Arguments)
  try {
    $value = & git -C $script:repo @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($value -join "`n").Trim()
  } catch { return $null }
}

function Get-MachineInfo {
  $cpu = $env:PROCESSOR_IDENTIFIER
  $cores = $env:NUMBER_OF_PROCESSORS
  $ramGb = $null
  $gpu = ''
  $os = [Environment]::OSVersion.VersionString
  try {
    $p = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
    if ($p) { $cpu = $p.Name.Trim() }
    $cs = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    if ($cs) {
      $cores = [int]$cs.NumberOfLogicalProcessors
      $ramGb = [Math]::Round([double]$cs.TotalPhysicalMemory / 1GB, 1)
    }
    $gpuNames = @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
      ForEach-Object { $_.Name } | Where-Object { $_ } | Sort-Object)
    $gpu = $gpuNames -join '; '
    $osInfo = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    if ($osInfo) { $os = ($osInfo.Caption + ' ' + $osInfo.Version).Trim() }
  } catch { }
  if ($null -eq $ramGb) {
    try {
      $ramGb = [Math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
    } catch { $ramGb = $null }
  }
  $raw = "{0}|{1}|{2}|{3}|{4}" -f $env:COMPUTERNAME, $cpu, $cores, $ramGb, $gpu
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $hash = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($raw))
  $id = (($hash | ForEach-Object { $_.ToString('x2') }) -join '').Substring(0, 12)
  return [ordered]@{
    id = $id
    host = $env:COMPUTERNAME
    os = $os
    cpu = $cpu
    logical_cores = $cores
    ram_gb = $ramGb
    gpu = $gpu
    powershell = $PSVersionTable.PSVersion.ToString()
  }
}

# ---------------------------------------------------------------------------
# Suites
# ---------------------------------------------------------------------------

function Invoke-StartupSuite {
  $cfg = $script:benchThresholds.startup
  $limit1 = [double]$cfg.p1_limit_ms
  $limit2 = [double]$cfg.p2_limit_ms
  $minPass = [int][Math]::Ceiling($Runs * [double]$cfg.pass_fraction)
  $checks = New-Object System.Collections.Generic.List[object]
  $rows = New-Object System.Collections.Generic.List[object]
  $started = Get-Date

  for ($i = 1; $i -le $Runs; ++$i) {
    foreach ($kind in @('P1', 'P2')) {
      $args = @('--startup-test', '--settings-path', $script:settingsArg)
      if ($kind -eq 'P2') { $args += @('--file', $script:media1080Arg) }
      $r = Invoke-Probe ("startup-{0}-{1:d2}" -f $kind.ToLowerInvariant(), $i) $args 60
      $value = $null
      $stageName = if ($kind -eq 'P1') { 'ui_ready' } else { 'first_present' }
      if (-not $r.started) {
        Add-InfraError ("startup {0}#{1} 启动失败：{2}" -f $kind, $i, $r.error)
      } elseif ($r.timed_out) {
        Add-InfraError ("startup {0}#{1} 超时 60s" -f $kind, $i)
      } else {
        if ($r.exit -ne 0) {
          Add-InfraError ("startup {0}#{1} exit={2}" -f $kind, $i, $r.exit)
        }
        $m = [regex]::Match($r.stderr, ("T7\.1 startup stage=" + [regex]::Escape($stageName) + " t_ms=([0-9.]+)"))
        if ($m.Success) {
          $value = [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
        } else {
          Add-InfraError ("startup {0}#{1} 缺少 {2} 阶段日志" -f $kind, $i, $stageName)
        }
      }
      $rows.Add([pscustomobject]@{
        run = $i; kind = $kind; exit = $r.exit; timed_out = $r.timed_out
        value_ms = $value; err_log = $r.err_log
      })
    }
    if ($i % 5 -eq 0) { Write-Host ("  startup {0}/{1}" -f $i, $Runs) }
  }

  $p1Vals = @($rows | Where-Object { $_.kind -eq 'P1' } | ForEach-Object { $_.value_ms })
  $p2Vals = @($rows | Where-Object { $_.kind -eq 'P2' } | ForEach-Object { $_.value_ms })
  $p1Stats = Get-Stats $p1Vals
  $p2Stats = Get-Stats $p2Vals
  $p1Pass = @($p1Vals | Where-Object { $null -ne $_ -and ([double]$_ -le $limit1) }).Count
  $p2Pass = @($p2Vals | Where-Object { $null -ne $_ -and ([double]$_ -le $limit2) }).Count
  $badExit = @($rows | Where-Object { $_.exit -ne 0 -or $_.timed_out }).Count

  Add-Check $checks (New-Check 'startup.p1_pass_runs' $p1Pass '>=' $minPass ("≤{0}ms，{1}/{2}；spec 至少 {3}" -f $limit1, $p1Pass, $Runs, $minPass))
  Add-Check $checks (New-Check 'startup.p1_p95_ms' $p1Stats.p95 '<=' $limit1 'P1 p95')
  Add-Check $checks (New-Check 'startup.p2_pass_runs' $p2Pass '>=' $minPass ("≤{0}ms，{1}/{2}；spec 至少 {3}" -f $limit2, $p2Pass, $Runs, $minPass))
  Add-Check $checks (New-Check 'startup.p2_p95_ms' $p2Stats.p95 '<=' $limit2 'P2 p95')
  Add-Check $checks (New-Check 'startup.exit_zero' $badExit '==' 0 '非零退出/超时样本数')

  Set-Metric 'startup.p1_p50_ms' $p1Stats.p50
  Set-Metric 'startup.p1_p95_ms' $p1Stats.p95
  Set-Metric 'startup.p1_max_ms' $p1Stats.max
  Set-Metric 'startup.p1_pass_runs' $p1Pass
  Set-Metric 'startup.p2_p50_ms' $p2Stats.p50
  Set-Metric 'startup.p2_p95_ms' $p2Stats.p95
  Set-Metric 'startup.p2_max_ms' $p2Stats.max
  Set-Metric 'startup.p2_pass_runs' $p2Pass

  $status = Get-SuiteStatus $checks
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }
  return [ordered]@{
    name = 'startup'
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{
      runs = $Runs; min_pass = $minPass
      p1 = $p1Stats; p2 = $p2Stats
      p1_pass_runs = $p1Pass; p2_pass_runs = $p2Pass
      cache_state = 'hot'
    }
    checks = [object[]]$checks
    samples = [object[]]$rows
  }
}

function Invoke-DragSuite {
  param([string]$Label, [string]$MediaArg, [string]$Class, [int]$Samples)
  $cfg = Get-Prop $script:benchThresholds.drag $Class
  $checks = New-Object System.Collections.Generic.List[object]
  $started = Get-Date
  $args = @(
    '--file', $MediaArg,
    '--drag-test', '--drag-samples', "$Samples",
    '--drag-tag', "bench-$Label",
    '--settings-path', $script:settingsArg
  )
  if ($AllowSoftwareDecode) { $args += '--force-software-decode' }
  $r = Invoke-Probe ("drag-$Label") $args 240
  $status = 'pass'
  $begin = Get-FirstLine $r.stderr 'T7\.4 drag_begin'
  $summary = Get-FirstLine $r.stderr 'T7\.4 drag_summary'
  $details = [ordered]@{ begin = $begin; summary = $summary; distributions = @(); after_seek = $null }

  if (-not $r.started) {
    Add-InfraError ("drag {0} 启动失败：{1}" -f $Label, $r.error)
    return [ordered]@{ name = "drag$Label"; status = 'infra'; duration_s = 0; summary = $details; checks = @() }
  }
  if ($r.timed_out) { Add-InfraError ("drag {0} 超时 240s" -f $Label) }
  if ($r.exit -ne 0) { Add-InfraError ("drag {0} exit={1}" -f $Label, $r.exit) }
  if ($null -eq $summary -or $null -eq $begin) {
    Add-InfraError ("drag {0} 缺少 drag_begin/drag_summary" -f $Label)
    return [ordered]@{ name = "drag$Label"; status = 'infra'; duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2); summary = $details; checks = @() }
  }

  $hw = Get-FieldInt $begin 'hw'
  $fps = Get-FieldDouble $begin 'fps'
  $sampleCount = Get-FieldInt $begin 'samples'
  $p4Fps = Get-FieldDouble $summary 'p4_fps'
  $p8Fps = Get-FieldDouble $summary 'p8_fps'
  $p8Frames = Get-FieldInt $summary 'p8_frames'
  $p8Presents = Get-FieldInt $summary 'p8_presents'
  $p9Prefix = Get-FieldInt $summary 'p9_prefix_max'

  $dist = @{}
  foreach ($name in @('p9_jump', 'p6_release', 'p7_backward', 'p7_forward')) {
    $line = Get-FirstLine $r.stderr ("T7\.4 " + [regex]::Escape($name) + " ")
    if ($null -eq $line) {
      Add-InfraError ("drag {0} 缺少 {1} 分布行" -f $Label, $name)
      continue
    }
    $dist[$name] = [ordered]@{
      line = $line
      n = Get-FieldInt $line 'n'
      p50 = Get-FieldDouble $line 'p50_ms'
      p95 = Get-FieldDouble $line 'p95_ms'
      max = Get-FieldDouble $line 'max_ms'
      over = Get-FieldInt $line 'over'
      pass = Get-FieldInt $line 'pass'
    }
    $details.distributions += $dist[$name]
  }

  $afterSeek = Get-FirstLine $r.stderr 'T7\.4 p7_after_seek'
  $afterUp = $null
  $afterDown = $null
  if ($afterSeek -match 'spot_updown_ms=([0-9.]+)/([0-9.]+)') {
    $afterUp = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
    $afterDown = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
    $details.after_seek = $afterSeek
  }

  $p9P50 = $null
  $p9P95 = $null
  $p6P95 = $null
  $p7BackP50 = $null
  $p7BackP95 = $null
  $p7FwdP50 = $null
  $p7FwdP95 = $null
  if ($dist.ContainsKey('p9_jump')) {
    $p9P50 = $dist['p9_jump'].p50
    $p9P95 = $dist['p9_jump'].p95
  }
  if ($dist.ContainsKey('p6_release')) { $p6P95 = $dist['p6_release'].p95 }
  if ($dist.ContainsKey('p7_backward')) {
    $p7BackP50 = $dist['p7_backward'].p50
    $p7BackP95 = $dist['p7_backward'].p95
  }
  if ($dist.ContainsKey('p7_forward')) {
    $p7FwdP50 = $dist['p7_forward'].p50
    $p7FwdP95 = $dist['p7_forward'].p95
  }

  Add-Check $checks (New-Check "drag$Label.hardware_decode" $hw '==' 1 '保证档要求 D3D11VA 硬解（hw=1）' -Gating (-not [bool]$AllowSoftwareDecode))
  Add-Check $checks (New-Check "drag$Label.sample_count" $dist['p9_jump'].n '==' $Samples ("P9/P6/P7 样本数应为 {0}" -f $Samples))
  Add-Check $checks (New-Check "drag$Label.p4_fps" $p4Fps '>=' $cfg.p4_min_fps 'P4 拖动预览帧率')
  Add-Check $checks (New-Check "drag$Label.p6_p95_ms" $p6P95 '<=' $cfg.p6_max_p95_ms 'P6 松手精确 p95')
  Add-Check $checks (New-Check "drag$Label.p7_back_p95_ms" $p7BackP95 '<=' $cfg.p7_max_p95_ms 'P7 后退 p95')
  Add-Check $checks (New-Check "drag$Label.p7_fwd_p95_ms" $p7FwdP95 '<=' $cfg.p7_max_p95_ms 'P7 前进 p95')
  Add-Check $checks (New-Check "drag$Label.p8_fps" $p8Fps '>=' $cfg.p8_min_fps 'P8 长按帧率')
  Add-Check $checks (New-Check "drag$Label.p8_presents" $p8Presents '>=' $p8Frames 'P8 每帧呈现')
  Add-Check $checks (New-Check "drag$Label.p9_p95_ms" $p9P95 '<=' $cfg.p9_max_p95_ms 'P9 跳转 p95')
  if ($null -ne $fps) {
    $prefixLimit = [int][Math]::Ceiling(2.0 * [double]$fps)
    Add-Check $checks (New-Check "drag$Label.p9_prefix_frames" $p9Prefix '<=' $prefixLimit ("P9 前提：目标距前关键帧 ≤2s（{0}fps → {1} 帧）" -f $fps, $prefixLimit))
  } else {
    Add-InfraError ("drag {0} 缺少 fps，无法校验 P9 的 2s 前提" -f $Label)
  }

  # 独立复算与 runner 自报 pass 对比，避免阈值/汇总分叉。
  $calcP4 = ($null -ne $p4Fps -and $p4Fps -ge [double]$cfg.p4_min_fps)
  $calcP6 = ($null -ne $p6P95 -and $p6P95 -le [double]$cfg.p6_max_p95_ms)
  $calcP7 = ($null -ne $p7BackP95 -and $null -ne $p7FwdP95 -and
             $p7BackP95 -le [double]$cfg.p7_max_p95_ms -and $p7FwdP95 -le [double]$cfg.p7_max_p95_ms)
  $calcP8 = ($null -ne $p8Fps -and $p8Fps -ge [double]$cfg.p8_min_fps -and $null -ne $p8Frames -and $null -ne $p8Presents -and $p8Presents -ge $p8Frames)
  $calcP9 = ($null -ne $p9P95 -and $p9P95 -le [double]$cfg.p9_max_p95_ms)
  $runnerFlags = @{
    p4 = Get-FieldInt $summary 'p4_pass'
    p6 = Get-FieldInt $summary 'p6_pass'
    p7 = Get-FieldInt $summary 'p7_pass'
    p8 = Get-FieldInt $summary 'p8_pass'
    p9 = Get-FieldInt $summary 'p9_pass'
  }
  $consistent = (($runnerFlags.p4 -eq [int][bool]$calcP4) -and
                 ($runnerFlags.p6 -eq [int][bool]$calcP6) -and
                 ($runnerFlags.p7 -eq [int][bool]$calcP7) -and
                 ($runnerFlags.p8 -eq [int][bool]$calcP8) -and
                 ($runnerFlags.p9 -eq [int][bool]$calcP9))
  Add-Check $checks (New-Check "drag$Label.runner_flags" ([int][bool]$consistent) '==' 1 ("runner 自报与脚本复算不一致：{0}" -f ($runnerFlags | ConvertTo-Json -Compress)))

  $status = Get-SuiteStatus $checks
  if ($hw -ne 1) {
    if ($AllowSoftwareDecode) { $status = 'diagnostic' } else { $status = 'fail' }
  }
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }

  Set-Metric "drag$Label.p4_fps" $p4Fps
  Set-Metric "drag$Label.p6_p95_ms" $p6P95
  Set-Metric "drag$Label.p7_back_p50_ms" $p7BackP50
  Set-Metric "drag$Label.p7_back_p95_ms" $p7BackP95
  Set-Metric "drag$Label.p7_fwd_p50_ms" $p7FwdP50
  Set-Metric "drag$Label.p7_fwd_p95_ms" $p7FwdP95
  Set-Metric "drag$Label.p8_fps" $p8Fps
  Set-Metric "drag$Label.p8_frames" $p8Frames
  Set-Metric "drag$Label.p8_presents" $p8Presents
  Set-Metric "drag$Label.p9_p50_ms" $p9P50
  Set-Metric "drag$Label.p9_p95_ms" $p9P95
  Set-Metric "drag$Label.p9_prefix_max_frames" $p9Prefix
  Set-Metric "drag$Label.p7_after_seek_up_ms" $afterUp
  Set-Metric "drag$Label.p7_after_seek_down_ms" $afterDown

  return [ordered]@{
    name = "drag$Label"
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{
      hw = $hw; fps = $fps; samples = $sampleCount
      p4_fps = $p4Fps; p6_p95_ms = $p6P95
      p7_back_p50_ms = $p7BackP50; p7_back_p95_ms = $p7BackP95
      p7_fwd_p50_ms = $p7FwdP50; p7_fwd_p95_ms = $p7FwdP95
      p8_fps = $p8Fps; p8_frames = $p8Frames; p8_presents = $p8Presents
      p9_p50_ms = $p9P50; p9_p95_ms = $p9P95; p9_prefix_max_frames = $p9Prefix
      p7_after_seek_up_ms = $afterUp; p7_after_seek_down_ms = $afterDown
      runner_flags = $runnerFlags
    }
    details = $details
    checks = [object[]]$checks
  }
}

function Invoke-StepSuite {
  param([string]$Label, [string]$MediaArg, [string]$Class)
  $cfg = Get-Prop $script:benchThresholds.step $Class
  $checks = New-Object System.Collections.Generic.List[object]
  $started = Get-Date
  $args = @(
    '--file', $MediaArg,
    '--memory-test', '--memory-mode', 'step',
    '--memory-steps', "$StepCount",
    '--memory-tag', "bench-step-$Label",
    '--settings-path', $script:settingsArg
  )
  $r = Invoke-Probe ("step-$Label") $args 240
  $back = Get-FirstLine $r.stderr 'T7\.2 step_stats direction=backward'
  $fwd = Get-FirstLine $r.stderr 'T7\.2 step_stats direction=forward'
  $summary = Get-FirstLine $r.stderr 'T7\.2 memory_summary'
  $details = [ordered]@{ backward = $back; forward = $fwd; summary = $summary }

  if (-not $r.started) {
    Add-InfraError ("step {0} 启动失败：{1}" -f $Label, $r.error)
    return [ordered]@{ name = "step$Label"; status = 'infra'; duration_s = 0; summary = $details; checks = @() }
  }
  if ($r.timed_out) { Add-InfraError ("step {0} 超时 240s" -f $Label) }
  if ($r.exit -ne 0) { Add-InfraError ("step {0} exit={1}" -f $Label, $r.exit) }
  if ($null -eq $back -or $null -eq $fwd) {
    Add-InfraError ("step {0} 缺少 step_stats" -f $Label)
    return [ordered]@{ name = "step$Label"; status = 'infra'; duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2); summary = $details; checks = @() }
  }

  $backP95 = Get-FieldDouble $back 'p95_ms'
  $fwdP95 = Get-FieldDouble $fwd 'p95_ms'
  $backP50 = Get-FieldDouble $back 'p50_ms'
  $fwdP50 = Get-FieldDouble $fwd 'p50_ms'
  $backMax = Get-FieldDouble $back 'max_ms'
  $fwdMax = Get-FieldDouble $fwd 'max_ms'
  $maxWs = Get-FieldDouble $summary 'max_ws_mb'
  $finalWs = Get-FieldDouble $summary 'final_ws_mb'

  Add-Check $checks (New-Check "step$Label.back_p95_ms" $backP95 '<=' $cfg.p7_max_p95_ms 'P7 后退 p95')
  Add-Check $checks (New-Check "step$Label.fwd_p95_ms" $fwdP95 '<=' $cfg.p7_max_p95_ms 'P7 前进 p95')
  Add-Check $checks (New-Check "step$Label.back_n" (Get-FieldInt $back 'n') '==' $StepCount ("后退步数应为 {0}" -f $StepCount))
  Add-Check $checks (New-Check "step$Label.fwd_n" (Get-FieldInt $fwd 'n') '==' $StepCount ("前进步数应为 {0}" -f $StepCount))

  $status = Get-SuiteStatus $checks
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }

  Set-Metric "step$Label.back_p50_ms" $backP50
  Set-Metric "step$Label.back_p95_ms" $backP95
  Set-Metric "step$Label.fwd_p50_ms" $fwdP50
  Set-Metric "step$Label.fwd_p95_ms" $fwdP95
  Set-Metric "step$Label.back_max_ms" $backMax
  Set-Metric "step$Label.fwd_max_ms" $fwdMax
  Set-Metric "step$Label.max_ws_mb" $maxWs

  return [ordered]@{
    name = "step$Label"
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{
      steps = $StepCount
      back_p50_ms = $backP50; back_p95_ms = $backP95; back_max_ms = $backMax
      fwd_p50_ms = $fwdP50; fwd_p95_ms = $fwdP95; fwd_max_ms = $fwdMax
      max_ws_mb = $maxWs; final_ws_mb = $finalWs
    }
    details = $details
    checks = [object[]]$checks
  }
}

function Invoke-MemoryCase {
  param(
    [string]$Name,
    [string]$Mode,
    [string]$MediaArg,
    [int]$Seconds,
    [double]$LimitMb,
    [bool]$Gating = $true
  )
  $checks = New-Object System.Collections.Generic.List[object]
  $started = Get-Date
  $args = @(
    '--memory-test', '--memory-mode', $Mode,
    '--memory-seconds', "$Seconds",
    '--memory-tag', "bench-$Name",
    '--settings-path', $script:settingsArg
  )
  if ($MediaArg) { $args = @('--file', $MediaArg) + $args }
  $r = Invoke-Probe ("memory-$Name") $args ($Seconds + 120)
  $summary = Get-FirstLine $r.stderr 'T7\.2 memory_summary'
  $details = [ordered]@{ summary = $summary; limit_mb = $LimitMb; gating = $Gating }

  if (-not $r.started) {
    Add-InfraError ("memory {0} 启动失败：{1}" -f $Name, $r.error)
    return [ordered]@{ name = Name; status = 'infra'; duration_s = 0; summary = $details; checks = @() }
  }
  if ($r.timed_out) { Add-InfraError ("memory {0} 超时 {1}s" -f $Name, ($Seconds + 120)) }
  if ($r.exit -ne 0) { Add-InfraError ("memory {0} exit={1}" -f $Name, $r.exit) }
  if ($null -eq $summary) {
    Add-InfraError ("memory {0} 缺少 memory_summary" -f $Name)
    return [ordered]@{ name = $Name; status = 'infra'; duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2); summary = $details; checks = @() }
  }

  $maxWs = Get-FieldDouble $summary 'max_ws_mb'
  $finalWs = Get-FieldDouble $summary 'final_ws_mb'
  $maxPrivate = Get-FieldDouble $summary 'max_private_mb'
  $runnerLimit = Get-FieldInt $summary 'limit_mb'
  $runnerPass = Get-FieldInt $summary 'pass'
  $display = Get-FieldString $summary 'display'
  $modeText = Get-FieldString $summary 'mode'

  if ($Gating) {
    Add-Check $checks (New-Check ("memory.{0}.max_ws_mb" -f $Name) $maxWs '<=' $LimitMb ("{0} 常驻内存上限 {1}MB" -f $Mode, $LimitMb))
    Add-Check $checks (New-Check ("memory.{0}.summary_pass" -f $Name) $runnerPass '==' 1 ("runner pass（limit={0}MB）" -f $runnerLimit))
  } else {
    Add-Check $checks (New-Check ("memory.{0}.info" -f $Name) $maxWs '>=' 0 ("诊断项，不参与 P10/P11 判定；max {0}MB" -f $maxWs) $false)
  }

  $status = Get-SuiteStatus $checks
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }

  Set-Metric ("memory.{0}.max_ws_mb" -f $Name) $maxWs
  Set-Metric ("memory.{0}.final_ws_mb" -f $Name) $finalWs
  Set-Metric ("memory.{0}.max_private_mb" -f $Name) $maxPrivate

  return [ordered]@{
    name = "memory-$Name"
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{
      mode = $modeText; display = $display; seconds = $Seconds
      max_ws_mb = $maxWs; final_ws_mb = $finalWs
      max_private_mb = $maxPrivate; runner_limit_mb = $runnerLimit
      runner_pass = $runnerPass; gating = $Gating
    }
    details = $details
    checks = [object[]]$checks
  }
}

function Invoke-MemorySuite {
  $results = New-Object System.Collections.Generic.List[object]
  $cfg = $script:benchThresholds.memory
  $emptySeconds = if ($script:isQuick) { 5 } else { [int]$cfg.empty_seconds }
  $play4kSeconds = if ($script:isQuick) { 8 } else { [int]$cfg.play4k_seconds }

  Write-Host '  memory empty（诊断基线）'
  $results.Add((Invoke-MemoryCase -Name 'empty' -Mode 'empty' -MediaArg '' `
    -Seconds $emptySeconds -LimitMb 250 -Gating $false))
  Write-Host '  memory pause 1080p（P10）'
  $results.Add((Invoke-MemoryCase -Name 'pause1080' -Mode 'pause' -MediaArg $script:media1080Arg `
    -Seconds $MemorySeconds -LimitMb ([double]$cfg.p10_max_mb) -Gating $true))
  Write-Host '  memory play 1080p（P11）'
  $results.Add((Invoke-MemoryCase -Name 'play1080' -Mode 'play' -MediaArg $script:media1080Arg `
    -Seconds $MemorySeconds -LimitMb ([double]$cfg.p11_1080p_max_mb) -Gating $true))
  Write-Host '  memory play 4K（P11 4K，proxy-V4）'
  $results.Add((Invoke-MemoryCase -Name 'play4k' -Mode 'play' -MediaArg $script:media4kArg `
    -Seconds $play4kSeconds -LimitMb ([double]$cfg.p11_4k_max_mb) -Gating $true))
  return [object[]]$results
}

function Invoke-RobustSuite {
  $cfg = $script:benchThresholds.robust
  $checks = New-Object System.Collections.Generic.List[object]
  $started = Get-Date
  $tag = "bench-$stamp"
  $root = "build\bench\robust\$tag"
  $args = @(
    '--robust-test', '--robust-tag', $tag,
    '--robust-root', $root,
    '--robust-media', $script:media1080Arg,
    '--settings-path', $script:settingsArg
  )
  $r = Invoke-Probe 'robust' $args 150
  $summary = Get-FirstLine $r.stderr 'T7\.5 robust_summary'
  $details = [ordered]@{ summary = $summary; root = $root }

  if (-not $r.started) {
    Add-InfraError ("robust 启动失败：{0}" -f $r.error)
    return [ordered]@{ name = 'robust'; status = 'infra'; duration_s = 0; summary = $details; checks = @() }
  }
  if ($r.timed_out) { Add-InfraError 'robust 超时 150s' }
  if ($r.exit -ne 0) { Add-InfraError ("robust exit={0}" -f $r.exit) }
  if ($null -eq $summary) {
    Add-InfraError 'robust 缺少 robust_summary'
    return [ordered]@{ name = 'robust'; status = 'infra'; duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2); summary = $details; checks = @() }
  }

  $checksCount = Get-FieldInt $summary 'checks'
  $skips = Get-FieldInt $summary 'skips'
  $failures = Get-FieldInt $summary 'failures'
  $modal = Get-FieldInt $summary 'modal_dismissed'
  $elapsed = Get-FieldDouble $summary 'elapsed_s'
  $passFlag = Get-FieldInt $summary 'pass'

  Add-Check $checks (New-Check 'robust.checks' $checksCount '>=' ([int]$cfg.min_checks) ("至少 {0} 项检查" -f $cfg.min_checks))
  Add-Check $checks (New-Check 'robust.failures' $failures '==' ([int]$cfg.max_failures) '失败数')
  Add-Check $checks (New-Check 'robust.skips' $skips '<=' ([int]$cfg.max_skips) '跳过数（只读目录单独跑）')
  Add-Check $checks (New-Check 'robust.elapsed_s' $elapsed '<=' ([double]$cfg.max_elapsed_s) '总耗时上界')
  Add-Check $checks (New-Check 'robust.pass' $passFlag '==' 1 'runner pass')

  $status = Get-SuiteStatus $checks
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }

  Set-Metric 'robust.checks' $checksCount
  Set-Metric 'robust.skips' $skips
  Set-Metric 'robust.failures' $failures
  Set-Metric 'robust.modal_dismissed' $modal
  Set-Metric 'robust.elapsed_s' $elapsed

  return [ordered]@{
    name = 'robust'
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{
      tag = $tag; checks = $checksCount; skips = $skips; failures = $failures
      modal_dismissed = $modal; elapsed_s = $elapsed; pass = $passFlag
    }
    details = $details
    checks = [object[]]$checks
  }
}

function Invoke-ReadonlySuite {
  $cfg = $script:benchThresholds.robust
  $checks = New-Object System.Collections.Generic.List[object]
  $started = Get-Date
  $tag = "bench-ro-$stamp"
  $scriptPath = Join-Path $script:repo 'scripts\robust-readonly.ps1'
  $args = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath,
    '-Exe', $script:exePath, '-Media', $script:media1080Abs, '-Tag', $tag
  )
  $r = Invoke-Process -Name 'readonly' -FilePath 'powershell.exe' -Arguments $args -TimeoutSeconds 180
  $text = $r.stdout + "`n" + $r.stderr
  $summary = Get-FirstLine $text 'T7\.5 robust_summary'
  $details = [ordered]@{ summary = $summary; exit = $r.exit }

  if (-not $r.started) {
    Add-InfraError ("readonly 启动失败：{0}" -f $r.error)
    return [ordered]@{ name = 'readonly'; status = 'infra'; duration_s = 0; summary = $details; checks = @() }
  }
  if ($r.timed_out) { Add-InfraError 'readonly 超时 180s' }
  if ($r.exit -ne 0) { Add-InfraError ("readonly exit={0}" -f $r.exit) }
  if ($null -eq $summary) {
    Add-InfraError 'readonly 缺少 robust_summary'
    return [ordered]@{ name = 'readonly'; status = 'infra'; duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2); summary = $details; checks = @() }
  }

  $checksCount = Get-FieldInt $summary 'checks'
  $failures = Get-FieldInt $summary 'failures'
  Add-Check $checks (New-Check 'readonly.checks' $checksCount '>=' ([int]$cfg.readonly_min_checks) ("至少 {0} 项检查" -f $cfg.readonly_min_checks))
  Add-Check $checks (New-Check 'readonly.failures' $failures '==' 0 '失败数')
  Add-Check $checks (New-Check 'readonly.exit' $r.exit '==' 0 '脚本退出码')

  $status = Get-SuiteStatus $checks
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }
  Set-Metric 'readonly.checks' $checksCount
  Set-Metric 'readonly.failures' $failures

  return [ordered]@{
    name = 'readonly'
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{ tag = $tag; checks = $checksCount; failures = $failures }
    details = $details
    checks = [object[]]$checks
  }
}

function Invoke-PackageSuite {
  $cfg = $script:benchThresholds.package
  $maxMb = [double]$cfg.max_mb
  $checks = New-Object System.Collections.Generic.List[object]
  $started = Get-Date
  $details = [ordered]@{}

  $distDir = Join-Path $script:repo 'dist'
  if (-not (Test-Path -LiteralPath $distDir)) {
    Add-InfraError 'package: dist 目录不存在（先运行 scripts\build-release.ps1）'
    return [ordered]@{ name = 'package'; status = 'infra'; duration_s = 0; summary = $details; checks = @() }
  }
  $distFiles = @(Get-ChildItem -LiteralPath $distDir -Recurse -File)
  $distBytes = ($distFiles | Measure-Object -Property Length -Sum).Sum
  $distMb = [Math]::Round([double]$distBytes / 1MB, 2)
  Add-Check $checks (New-Check 'package.dist_mb' $distMb '<=' $maxMb ("dist {0} 文件 / P12 ≤{1}MB" -f $distFiles.Count, $maxMb))
  Set-Metric 'package.dist_mb' $distMb
  Set-Metric 'package.dist_files' $distFiles.Count
  $details.dist = [ordered]@{ files = $distFiles.Count; mb = $distMb }

  $packageDir = Join-Path $script:repo 'out\package'
  $stageDir = Join-Path $packageDir 'stage'
  if (Test-Path -LiteralPath $stageDir) {
    $stageFiles = @(Get-ChildItem -LiteralPath $stageDir -Recurse -File)
    $stageMb = [Math]::Round((($stageFiles | Measure-Object -Property Length -Sum).Sum) / 1MB, 2)
    Add-Check $checks (New-Check 'package.payload_mb' $stageMb '<=' $maxMb '分发负载 stage')
    Set-Metric 'package.payload_mb' $stageMb
    $details.payload = [ordered]@{ files = $stageFiles.Count; mb = $stageMb }
  }

  $setup = @(Get-ChildItem -LiteralPath $packageDir -Filter '*setup.exe' -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1)
  $zip = @(Get-ChildItem -LiteralPath $packageDir -Filter '*portable.zip' -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1)
  $reportPath = Join-Path $packageDir 'package-report.txt'
  $reportText = ''
  if (Test-Path -LiteralPath $reportPath) {
    $reportText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($reportPath))
  }

  $hashesOk = $true
  if ($setup.Count -gt 0) {
    $setupMb = [Math]::Round($setup[0].Length / 1MB, 2)
    Add-Check $checks (New-Check 'package.setup_mb' $setupMb '<=' $maxMb 'setup 体积')
    Set-Metric 'package.setup_mb' $setupMb
    $details.setup = [ordered]@{ name = $setup[0].Name; mb = $setupMb; sha256 = (Get-FileHash -LiteralPath $setup[0].FullName -Algorithm SHA256).Hash }
  }
  if ($zip.Count -gt 0) {
    $zipMb = [Math]::Round($zip[0].Length / 1MB, 2)
    Add-Check $checks (New-Check 'package.zip_mb' $zipMb '<=' $maxMb '便携 zip 体积')
    Set-Metric 'package.zip_mb' $zipMb
    $details.zip = [ordered]@{ name = $zip[0].Name; mb = $zipMb; sha256 = (Get-FileHash -LiteralPath $zip[0].FullName -Algorithm SHA256).Hash }
  }

  if ($reportText) {
    $setupMatch = [regex]::Match($reportText, 'Lapian-[^\r\n：]*?setup\.exe[：:]\s*([0-9.]+)\s*MB\s+SHA256=([0-9A-Fa-f]{64})')
    $zipMatch = [regex]::Match($reportText, 'Lapian-[^\r\n：]*?portable\.zip[：:]\s*([0-9.]+)\s*MB\s+SHA256=([0-9A-Fa-f]{64})')
    if ($setup.Count -gt 0 -and $setupMatch.Success) {
      $actual = (Get-FileHash -LiteralPath $setup[0].FullName -Algorithm SHA256).Hash
      $ok = ($actual -ieq $setupMatch.Groups[2].Value)
      if (-not $ok) { $hashesOk = $false }
      Add-Check $checks (New-Check 'package.setup_sha256' ([int][bool]$ok) '==' 1 'setup SHA-256 与 package-report 一致')
    }
    if ($zip.Count -gt 0 -and $zipMatch.Success) {
      $actual = (Get-FileHash -LiteralPath $zip[0].FullName -Algorithm SHA256).Hash
      $ok = ($actual -ieq $zipMatch.Groups[2].Value)
      if (-not $ok) { $hashesOk = $false }
      Add-Check $checks (New-Check 'package.zip_sha256' ([int][bool]$ok) '==' 1 'zip SHA-256 与 package-report 一致')
    }
    $headMatch = [regex]::Match($reportText, '源码 HEAD[：:]\s*([0-9a-fA-F]+)')
    if ($headMatch.Success) {
      $currentHead = Get-GitValue @('rev-parse', 'HEAD')
      $fresh = $false
      if ($currentHead) { $fresh = $currentHead.StartsWith($headMatch.Groups[1].Value, [StringComparison]::OrdinalIgnoreCase) }
      $details.report_head = $headMatch.Groups[1].Value
      $details.report_head_fresh = [int][bool]$fresh
      Add-Check $checks (New-Check 'package.report_head_fresh' ([int][bool]$fresh) '>=' 0 ("package-report HEAD={0}；当前 HEAD={1}（不阻塞，提示报告是否过期）" -f $headMatch.Groups[1].Value, $currentHead) $false)
    }
  } else {
    $details.report = 'missing'
  }
  if ($setup.Count -gt 0 -or $zip.Count -gt 0) {
    Set-Metric 'package.hashes_ok' ([int][bool]$hashesOk)
  }

  $status = Get-SuiteStatus $checks
  if ($script:infraErrors.Count -gt $script:suiteErrorBaseline) { $status = 'infra' }
  return [ordered]@{
    name = 'package'
    status = $status
    duration_s = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)
    summary = [ordered]@{
      dist_mb = $distMb; dist_files = $distFiles.Count
      has_package_artifacts = [bool]($setup.Count -gt 0 -or $zip.Count -gt 0)
    }
    details = $details
    checks = [object[]]$checks
  }
}

# ---------------------------------------------------------------------------
# 基线对比
# ---------------------------------------------------------------------------

function Compare-Baseline {
  param($Metrics)
  if ($script:isQuick) {
    return [ordered]@{ status = 'skipped'; reason = 'quick 模式不比较基线'; rows = @(); machine_match = $null }
  }
  if ($SkipBaseline) {
    return [ordered]@{ status = 'skipped'; reason = '已指定 -SkipBaseline'; rows = @(); machine_match = $null }
  }
  if (-not (Test-Path -LiteralPath $script:baselinePath)) {
    return [ordered]@{ status = 'skipped'; reason = "基线文件不存在：$($script:baselinePath)"; rows = @(); machine_match = $null }
  }
  try {
    $baselineDoc = Get-Content -LiteralPath $script:baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
  } catch {
    Add-InfraError ("基线 JSON 解析失败：{0}" -f $_.Exception.Message)
    return [ordered]@{ status = 'infra'; reason = '基线 JSON 解析失败'; rows = @(); machine_match = $null }
  }
  $baselineMachine = Get-Prop $baselineDoc 'machine'
  $baselineMachineId = if ($baselineMachine) { Get-Prop $baselineMachine 'id' } else { $null }
  $machineMatch = ($baselineMachineId -eq $script:machine.id)
  if (-not $machineMatch) {
    return [ordered]@{
      status = 'skipped'
      reason = ("基线机不同（baseline={0}，current={1}）；只做 spec 阈值检查" -f $baselineMachineId, $script:machine.id)
      rows = @()
      machine_match = $false
      label = Get-Prop $baselineDoc 'label'
      commit = Get-Prop $baselineDoc 'commit'
    }
  }

  $tolerance = [double]$script:benchThresholds.baseline.tolerance_pct
  $metricCfg = Get-Prop $script:benchThresholds.baseline 'metrics'
  $baselineValues = Get-Prop $baselineDoc 'values'
  $rows = New-Object System.Collections.Generic.List[object]
  $failures = 0
  if ($metricCfg) {
    foreach ($prop in $metricCfg.PSObject.Properties) {
      $key = $prop.Name
      $cfg = $prop.Value
      $base = Get-Prop $baselineValues $key
      $cur = Get-Prop $Metrics $key
      if ($null -eq $base -or $null -eq $cur) { continue }
      $pct = if ($null -ne (Get-Prop $cfg 'pct')) { [double](Get-Prop $cfg 'pct') } else { $tolerance }
      $abs = if ($null -ne (Get-Prop $cfg 'abs')) { [double](Get-Prop $cfg 'abs') } else { 0.0 }
      $delta = [double]$cur - [double]$base
      $allowed = [Math]::Max([Math]::Abs([double]$base) * $pct / 100.0, $abs)
      $ok = ([Math]::Abs($delta) -le $allowed)
      $deltaPct = if ([double]$base -ne 0) { [Math]::Round($delta / [double]$base * 100.0, 2) } else { $null }
      if (-not $ok) { ++$failures }
      $rows.Add([ordered]@{
        metric = $key; baseline = $base; current = $cur
        delta = [Math]::Round($delta, 3); delta_pct = $deltaPct
        allowed = [Math]::Round($allowed, 3); pct = $pct; abs = $abs
        pass = [bool]$ok
      })
    }
  }
  return [ordered]@{
    status = if ($failures -gt 0) { 'fail' } else { 'pass' }
    reason = ''
    rows = [object[]]$rows
    machine_match = $true
    label = Get-Prop $baselineDoc 'label'
    commit = Get-Prop $baselineDoc 'commit'
    failures = $failures
  }
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

$suiteNames = @($Suites | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.ToLowerInvariant().Trim() } | Where-Object { $_ })
$validSuites = @('startup', 'drag1080', 'drag4k', 'step1080', 'step4k', 'memory', 'robust', 'readonly', 'package', 'drag', 'step', 'all')
foreach ($name in $suiteNames) {
  if ($validSuites -notcontains $name) {
    throw "未知 suite：$name（可用：$($validSuites -join ', ')）"
  }
}
if ($suiteNames -contains 'all') {
  $suiteNames = @('startup', 'drag', 'step', 'memory', 'robust', 'package')
}
$expanded = New-Object System.Collections.Generic.List[string]
foreach ($name in $suiteNames) {
  switch ($name) {
    'drag' { $expanded.Add('drag1080'); $expanded.Add('drag4k') }
    'step' { $expanded.Add('step1080'); $expanded.Add('step4k') }
    default { $expanded.Add($name) }
  }
}
if ($IncludeReadonly -and $expanded -notcontains 'readonly') { $expanded.Add('readonly') }
$suiteNames = @($expanded)

$script:machine = Get-MachineInfo
$headFull = Get-GitValue @('rev-parse', 'HEAD')
$headShort = Get-GitValue @('rev-parse', '--short=8', 'HEAD')
$media1080Info = [ordered]@{
  path = $script:media1080Abs
  bytes = (Get-Item -LiteralPath $script:media1080Abs).Length
  sha256 = (Get-FileHash -LiteralPath $script:media1080Abs -Algorithm SHA256).Hash
}
$media4kInfo = [ordered]@{
  path = $script:media4kAbs
  bytes = (Get-Item -LiteralPath $script:media4kAbs).Length
  sha256 = (Get-FileHash -LiteralPath $script:media4kAbs -Algorithm SHA256).Hash
}

Write-Host ''
Write-Host '=== T7.8 bench-regression ========================================='
Write-Host ("时间：{0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Host ("exe：{0}" -f $script:exePath)
Write-Host ("exe SHA256：{0}" -f $script:exeHash)
Write-Host ("HEAD：{0}（{1}）" -f $headShort, $headFull)
Write-Host ("机器：{0} / {1} / {2} 核 / {3} GB / {4}" -f $script:machine.host, $script:machine.cpu, $script:machine.logical_cores, $script:machine.ram_gb, $script:machine.gpu)
Write-Host ("模式：{0}；P1/P2 缓存：热（不是冷缓存/F1-A1）" -f $(if ($script:isQuick) { 'quick（非验收口径）' } else { 'full' }))
Write-Host ("素材：1080p={0}；4K={1}（代理，不是 V1/V4）" -f $script:media1080Abs, $script:media4kAbs)
Write-Host ("Suites：{0}" -f ($suiteNames -join ', '))
Write-Host '==================================================================='

$index = 0
$total = $suiteNames.Count
foreach ($name in $suiteNames) {
  ++$index
  Write-Host ''
  Write-Host ("[{0}/{1}] {2}" -f $index, $total, $name)
  $script:suiteErrorBaseline = $script:infraErrors.Count
  $suite = $null
  switch ($name) {
    'startup' { $suite = Invoke-StartupSuite }
    'drag1080' { $suite = Invoke-DragSuite -Label '1080' -MediaArg $script:media1080Arg -Class '1080p' -Samples $DragSamples }
    'drag4k' { $suite = Invoke-DragSuite -Label '4k' -MediaArg $script:media4kArg -Class '4k' -Samples $DragSamples4k }
    'step1080' { $suite = Invoke-StepSuite -Label '1080' -MediaArg $script:media1080Arg -Class '1080p' }
    'step4k' { $suite = Invoke-StepSuite -Label '4k' -MediaArg $script:media4kArg -Class '4k' }
    'memory' { $suite = Invoke-MemorySuite }
    'robust' { $suite = Invoke-RobustSuite }
    'readonly' { $suite = Invoke-ReadonlySuite }
    'package' { $suite = Invoke-PackageSuite }
  }
  if ($suite -is [System.Collections.IEnumerable] -and -not ($suite -is [System.Collections.IDictionary])) {
    foreach ($item in $suite) {
      $script:suiteResults.Add($item)
      $icon = switch ($item.status) { 'pass' { 'PASS' } 'fail' { 'FAIL' } 'infra' { 'INFRA' } 'diagnostic' { 'DIAG' } default { $item.status.ToUpperInvariant() } }
      Write-Host ("  -> {0} {1} ({2:N2}s)" -f $icon, $item.name, $item.duration_s)
    }
  } else {
    $script:suiteResults.Add($suite)
    $icon = switch ($suite.status) { 'pass' { 'PASS' } 'fail' { 'FAIL' } 'infra' { 'INFRA' } 'diagnostic' { 'DIAG' } default { $suite.status.ToUpperInvariant() } }
    Write-Host ("  -> {0} ({1:N2}s)" -f $icon, $suite.duration_s)
  }
}

# 基线对比（-UpdateBaseline 时不比较）。
$drift = $null
if ($UpdateBaseline) {
  $drift = [ordered]@{ status = 'update'; reason = '本次运行写入基线'; rows = @(); machine_match = $true }
} else {
  $drift = Compare-Baseline $script:allMetrics
}

$allChecks = $script:allChecks.ToArray()
$gatingFails = [object[]]($allChecks | Where-Object { $_.gating -and -not $_.pass })
$infraCount = $script:infraErrors.Count
$diagnostic = ([object[]]($script:suiteResults | Where-Object { $_.status -eq 'diagnostic' })).Count -gt 0
$driftFail = ($drift.status -eq 'fail')

$overall = 'pass'
$exitCode = 0
if ($infraCount -gt 0) { $overall = 'infra'; $exitCode = 2 }
elseif ($gatingFails.Count -gt 0) { $overall = 'fail'; $exitCode = 1 }
elseif ($diagnostic) { $overall = 'diagnostic'; $exitCode = 4 }
elseif ($driftFail) { $overall = 'baseline_drift'; $exitCode = 3 }

# -UpdateBaseline：只有全量 spec 阈值通过才写。
if ($UpdateBaseline) {
  if ($overall -ne 'pass') {
    Write-Host ''
    Write-Host ("[拒绝写基线] 本次结果 {0}，先修正失败再更新基线。" -f $overall)
    $UpdateBaseline = $false
  }
}
$baselineDocOut = $null
if ($UpdateBaseline) {
  if ([string]::IsNullOrWhiteSpace($BaselineLabel)) { $BaselineLabel = 'dev-' + (Get-Date -Format 'yyyy-MM-dd') }
  $baselineDocOut = [ordered]@{
    schema = 1
    label = $BaselineLabel
    created = (Get-Date).ToString('o')
    commit = $headFull
    commit_short = $headShort
    cache_state = 'hot'
    quick = $false
    machine = $script:machine
    exe = [ordered]@{ path = $script:exePath; sha256 = $script:exeHash; bytes = (Get-Item -LiteralPath $script:exePath).Length }
    media = [ordered]@{ '1080p' = $media1080Info; '4k' = $media4kInfo }
    thresholds_sha256 = $script:thresholdsHash
    values = $script:allMetrics
  }
  Write-JsonFile $script:baselinePath $baselineDocOut
  Write-Host ("基线已写入：{0}" -f $script:baselinePath)
}

$duration = [Math]::Round(((Get-Date) - $script:startTime).TotalSeconds, 2)
$failureRows = New-Object System.Collections.Generic.List[object]
foreach ($f in $gatingFails) {
  [void]$failureRows.Add([ordered]@{
    id = $f.id; value = $f.value; op = $f.op; limit = $f.limit; note = $f.note
  })
}
$resultDoc = [ordered]@{
  schema = 1
  tool = 'bench-regression.ps1'
  tool_version = '1.0'
  timestamp = (Get-Date).ToString('o')
  duration_s = $duration
  quick = $script:isQuick
  spec_grade = (-not $script:isQuick)
  result = $overall
  exit_code = $exitCode
  head = $headFull
  head_short = $headShort
  machine = $script:machine
  cache_state = 'hot'
  exe = [ordered]@{ path = $script:exePath; sha256 = $script:exeHash; bytes = (Get-Item -LiteralPath $script:exePath).Length }
  media = [ordered]@{ '1080p' = $media1080Info; '4k' = $media4kInfo }
  thresholds = [ordered]@{ path = $script:thresholdsPath; sha256 = $script:thresholdsHash; source = $script:benchThresholds.source }
  baseline = [ordered]@{
    path = $script:baselinePath
    status = $drift.status
    reason = $drift.reason
    machine_match = $drift.machine_match
    label = Get-Prop $drift 'label'
    commit = Get-Prop $drift 'commit'
    rows = [object[]]$drift.rows
  }
  suites = [object[]]$script:suiteResults
  metrics = $script:allMetrics
  checks = $allChecks
  failures = $failureRows.ToArray()
  infra_errors = [object[]]$script:infraErrors
  readonly = [bool]$IncludeReadonly
  allow_software_decode = [bool]$AllowSoftwareDecode
}
$latestPath = Join-Path $script:resultRoot 'bench-latest.json'
$stampPath = Join-Path $script:resultRoot ("bench-{0}.json" -f $stamp)
Write-JsonFile $stampPath $resultDoc
Write-JsonFile $latestPath $resultDoc

# ---------------------------------------------------------------------------
# 汇总输出
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host '=== 结果 =========================================================='
foreach ($suite in $script:suiteResults) {
  $line = "  {0,-10} {1}" -f $suite.status.ToUpperInvariant(), $suite.name
  $summary = Get-Prop $suite 'summary'
  if ($summary) {
    if ($suite.name -eq 'startup') {
      $line += ("  P1 p50={0}ms p95={1}ms {2}/{3}；P2 p50={4}ms p95={5}ms {6}/{7}" -f `
        $summary.p1.p50, $summary.p1.p95, $summary.p1_pass_runs, $summary.runs,
        $summary.p2.p50, $summary.p2.p95, $summary.p2_pass_runs, $summary.runs)
    } elseif ($suite.name -like 'drag*') {
      $line += ("  P4={0}fps P6={1}ms P7={2}/{3}ms P8={4}fps {5}/{6} P9={7}ms hw={8}" -f `
        $summary.p4_fps, $summary.p6_p95_ms, $summary.p7_back_p95_ms, $summary.p7_fwd_p95_ms,
        $summary.p8_fps, $summary.p8_presents, $summary.p8_frames, $summary.p9_p95_ms, $summary.hw)
    } elseif ($suite.name -like 'step*') {
      $line += ("  P7 back p50/p95/max={0}/{1}/{2}ms；fwd={3}/{4}/{5}ms" -f `
        $summary.back_p50_ms, $summary.back_p95_ms, $summary.back_max_ms,
        $summary.fwd_p50_ms, $summary.fwd_p95_ms, $summary.fwd_max_ms)
    } elseif ($suite.name -like 'memory-*') {
      $line += ("  max={0}MB final={1}MB limit={2}MB" -f $summary.max_ws_mb, $summary.final_ws_mb, $summary.runner_limit_mb)
    } elseif ($suite.name -eq 'robust') {
      $line += ("  {0} checks / {1} fail / {2} skip / {3}s" -f $summary.checks, $summary.failures, $summary.skips, $summary.elapsed_s)
    } elseif ($suite.name -eq 'readonly') {
      $line += ("  {0} checks / {1} fail" -f $summary.checks, $summary.failures)
    } elseif ($suite.name -eq 'package') {
      $line += ("  dist={0}MB/{1} files" -f $summary.dist_mb, $summary.dist_files)
    }
  }
  Write-Host $line
  foreach ($c in @(Get-Prop $suite 'checks')) {
    if ($c.gating -and -not $c.pass) {
      Write-Host ("         FAIL {0}: {1} {2} {3}  {4}" -f $c.id, $c.value, $c.op, $c.limit, $c.note)
    }
  }
}
Write-Host ''
if ($drift.rows.Count -gt 0) {
  $badDrift = @($drift.rows | Where-Object { -not $_.pass })
  Write-Host ("基线对比（同机 ±{0}%，含噪声下限）：{1} 项，超差 {2} 项" -f $script:benchThresholds.baseline.tolerance_pct, $drift.rows.Count, $badDrift.Count)
  foreach ($row in $badDrift) {
    Write-Host ("  DRIFT {0}: baseline={1} current={2} delta={3} ({4}%) allowed={5}" -f `
      $row.metric, $row.baseline, $row.current, $row.delta, $row.delta_pct, $row.allowed)
  }
} elseif ($drift.status -ne 'update') {
  Write-Host ("基线对比：{0}（{1}）" -f $drift.status, $drift.reason)
}
if ($infraCount -gt 0) {
  Write-Host ''
  Write-Host '基础设施错误：'
  foreach ($e in $script:infraErrors) { Write-Host ("  - {0}" -f $e) }
}
Write-Host ''
Write-Host ("检查：{0} 项，gating 失败 {1} 项，infra {2} 项" -f $allChecks.Count, $gatingFails.Count, $infraCount)
Write-Host ("总耗时：{0:N1}s" -f $duration)
Write-Host ("结果：{0}（exit {1}）" -f $overall.ToUpperInvariant(), $exitCode)
Write-Host ("报告：{0}" -f $stampPath)
Write-Host ("最新：{0}" -f $latestPath)
if ($script:isQuick) {
  Write-Host '注意：quick 模式的样本数不是 spec 口径，不能作为 T7.8 验收证据。'
}
Write-Host '注意：P1/P2 是热缓存回归；F1-A1 仍需冷缓存 + Explorer 真双击 + 基线机。'
Write-Host '注意：本脚本不跑 V10 2h；P14 仍用 scripts\soak-bench.ps1（-NoLoop）。'
Write-Host '==================================================================='

exit $exitCode
