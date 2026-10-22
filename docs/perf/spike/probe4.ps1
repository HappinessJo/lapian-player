$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Net.Http | Out-Null

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$ff = Join-Path $repo 'third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe'
if (-not (Test-Path $ff)) { Write-Output "MISSING ffmpeg=$ff"; exit 1 }
$dir = Join-Path $env:TEMP 'lapian-spike'
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
$h = @{ 'User-Agent' = $ua; 'Referer' = 'https://www.bilibili.com/' }
$bv = 'BV1BMnv6jE5S'

$view = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$bv" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
$cid = $view.data.cid
$p = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=32&fnval=4048" -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
$dash = $p.data.dash
$vid = $dash.video | Where-Object { $_.id -eq 32 -and $_.codecid -eq 7 } | Select-Object -First 1
$aud = $dash.audio | Where-Object { $_.id -eq 30280 } | Select-Object -First 1

$initRange = [string]$vid.SegmentBase.Initialization
$indexRange = [string]$vid.SegmentBase.indexRange
Write-Output ("video init_range={0} index_range={1}" -f $initRange, $indexRange)
Write-Output ("audio init_range={0}" -f [string]$aud.SegmentBase.Initialization)

$hc = [System.Net.Http.HttpClient]::new()
$hc.DefaultRequestHeaders.Add('User-Agent', $ua)
$hc.DefaultRequestHeaders.Add('Referer', 'https://www.bilibili.com/')
$hc.Timeout = [TimeSpan]::FromSeconds(180)

function FetchAll {
    param($url)
    $rq = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, $url)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $resp = $hc.SendAsync($rq, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).Result
    $ttfb = $sw.ElapsedMilliseconds
    $b = $resp.Content.ReadAsByteArrayAsync().Result
    $sw.Stop()
    [pscustomobject]@{ bytes = $b; status = [int]$resp.StatusCode; ttfb = $ttfb; ms = $sw.ElapsedMilliseconds }
}

$rv = FetchAll $vid.baseUrl
$ra = FetchAll $aud.baseUrl
$vfile = Join-Path $dir 'v.mp4'
$afile = Join-Path $dir 'a.mp4'
[IO.File]::WriteAllBytes($vfile, $rv.bytes)
[IO.File]::WriteAllBytes($afile, $ra.bytes)
Write-Output ("video bytes={0} ttfb={1}ms total={2}ms rate={3}MB/s" -f $rv.bytes.Length, $rv.ttfb, $rv.ms, [math]::Round($rv.bytes.Length / 1MB / ($rv.ms / 1000), 2))
Write-Output ("audio bytes={0} total={1}ms" -f $ra.bytes.Length, $ra.ms)

$out = Join-Path $dir 'remux.mp4'
if (Test-Path $out) { Remove-Item $out -Force }
$t = Measure-Command { & $ff -hide_banner -v error -y -i $vfile -i $afile -c copy -movflags +faststart $out 2>&1 | Out-Null }
$rc = $LASTEXITCODE
$size = if (Test-Path $out) { [math]::Round((Get-Item $out).Length / 1MB, 2) } else { -1 }
Write-Output ("remux exit={0} {1}ms out={2}MB" -f $rc, [math]::Round($t.TotalMilliseconds, 0), $size)

if (Test-Path $out) {
    $t2 = Measure-Command { & $ff -hide_banner -v error -i $out -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
    Write-Output ("first_frame {0}ms exit={1}" -f [math]::Round($t2.TotalMilliseconds, 0), $LASTEXITCODE)
    $t3 = Measure-Command { & $ff -hide_banner -v error -ss 50 -i $out -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
    Write-Output ("seek_50s {0}ms exit={1}" -f [math]::Round($t3.TotalMilliseconds, 0), $LASTEXITCODE)
    $info = (& $ff -hide_banner -i $out 2>&1 | Select-String -Pattern 'Duration|Stream #') -join ' | '
    Write-Output ("probe: {0}" -f $info)
}

Write-Output '--- startup bytes needed for first frame ---'
foreach ($kb in 64, 128, 256, 512, 1024) {
    $n = $kb * 1024
    if ($n -ge $rv.bytes.Length) { continue }
    $part = Join-Path $dir ("v_{0}k.mp4" -f $kb)
    $buf = [byte[]]::new($n)
    [Array]::Copy($rv.bytes, 0, $buf, 0, $n)
    [IO.File]::WriteAllBytes($part, $buf)
    $t4 = Measure-Command { & $ff -hide_banner -v error -i $part -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
    Write-Output ("  {0}KB ({1}kbps ~{2}s of video): exit={3} {4}ms" -f $kb, [int]($vid.bandwidth / 1000), [math]::Round($n * 8 / $vid.bandwidth, 1), $LASTEXITCODE, [math]::Round($t4.TotalMilliseconds, 0))
}
