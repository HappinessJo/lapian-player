$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Net.Http | Out-Null

$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
$h = @{ 'User-Agent' = $ua; 'Referer' = 'https://www.bilibili.com/' }
$bv = 'BV1BMnv6jE5S'

$view = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$bv" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
$cid = $view.data.cid

$pu = "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=32&fnval=4048"
$p = (Invoke-WebRequest -Uri $pu -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
$dash = $p.data.dash

$vid = $dash.video | Where-Object { $_.id -eq 32 -and $_.codecid -eq 7 } | Select-Object -First 1
$aud = $dash.audio | Where-Object { $_.id -eq 30280 } | Select-Object -First 1

$m = [regex]::Match($vid.baseUrl, 'deadline=(\d+)')
$dl = [int64]$m.Groups[1].Value
$dt = [DateTimeOffset]::FromUnixTimeSeconds($dl).ToLocalTime()
$ttl = [math]::Round(($dt - [DateTimeOffset]::Now).TotalMinutes, 1)
Write-Output ("video: id={0} codec={1} {2}x{3} bw={4}k" -f $vid.id, $vid.codecid, $vid.width, $vid.height, [int]($vid.bandwidth / 1000))
Write-Output ("  init_off={0} init_len={1} indexRange={2}" -f $vid.SegmentBase.Initialization.offset, $vid.SegmentBase.Initialization.length, $vid.SegmentBase.indexRange)
Write-Output ("audio: id={0} bw={1}k init_len={2}" -f $aud.id, [int]($aud.bandwidth / 1000), $aud.SegmentBase.Initialization.length)
Write-Output ("url_deadline={0} ttl_min={1}" -f $dt.ToString('yyyy-MM-dd HH:mm:ss'), $ttl)

$hc = [System.Net.Http.HttpClient]::new()
$hc.DefaultRequestHeaders.Add('User-Agent', $ua)
$hc.DefaultRequestHeaders.Add('Referer', 'https://www.bilibili.com/')
$hc.Timeout = [TimeSpan]::FromSeconds(180)

function Fetch {
    param($url, [long]$r0, [long]$r1)
    $rq = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, $url)
    if ($r0 -ge 0) { $rq.Headers.Range = [System.Net.Http.Headers.RangeHeaderValue]::new($r0, $r1) }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $resp = $hc.SendAsync($rq, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).Result
    $ttfb = $sw.ElapsedMilliseconds
    $b = $resp.Content.ReadAsByteArrayAsync().Result
    $sw.Stop()
    [pscustomobject]@{ bytes = $b; status = [int]$resp.StatusCode; ttfb = $ttfb; ms = $sw.ElapsedMilliseconds }
}

$dir = Join-Path $env:TEMP 'lapian-spike'
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$iv = Fetch $vid.baseUrl 0 ([long]$vid.SegmentBase.Initialization.length - 1)
Write-Output ("video_init: status={0} bytes={1} ttfb={2}ms" -f $iv.status, $iv.bytes.Length, $iv.ttfb)
$rv = Fetch $vid.baseUrl -1 -1
$vrate = [math]::Round($rv.bytes.Length / 1MB / ($rv.ms / 1000), 2)
Write-Output ("video_media: status={0} bytes={1} total={2}ms rate={3}MB/s" -f $rv.status, $rv.bytes.Length, $rv.ms, $vrate)
$ia = Fetch $aud.baseUrl 0 ([long]$aud.SegmentBase.Initialization.length - 1)
$ra = Fetch $aud.baseUrl -1 -1
Write-Output ("audio: init={0}B media={1}B total={2}ms" -f $ia.bytes.Length, $ra.bytes.Length, $ra.ms)

$vfile = Join-Path $dir 'v.mp4'
$afile = Join-Path $dir 'a.mp4'
$vb = [byte[]]::new($iv.bytes.Length + $rv.bytes.Length)
[Array]::Copy($iv.bytes, 0, $vb, 0, $iv.bytes.Length)
[Array]::Copy($rv.bytes, 0, $vb, $iv.bytes.Length, $rv.bytes.Length)
[IO.File]::WriteAllBytes($vfile, $vb)
$ab = [byte[]]::new($ia.bytes.Length + $ra.bytes.Length)
[Array]::Copy($ia.bytes, 0, $ab, 0, $ia.bytes.Length)
[Array]::Copy($ra.bytes, 0, $ab, $ia.bytes.Length, $ra.bytes.Length)
[IO.File]::WriteAllBytes($afile, $ab)
Write-Output ("v.mp4={0}MB a.mp4={1}MB" -f [math]::Round((Get-Item $vfile).Length / 1MB, 2), [math]::Round((Get-Item $afile).Length / 1MB, 2))

$ff = 'E:\拉片软件开发\third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe'
$out = Join-Path $dir 'remux.mp4'
$t = Measure-Command { & $ff -hide_banner -v error -y -i $vfile -i $afile -c copy -movflags +faststart $out 2>&1 | Out-Null }
$size = if (Test-Path $out) { [math]::Round((Get-Item $out).Length / 1MB, 2) } else { -1 }
Write-Output ("remux_ms={0} exit={1} out={2}MB" -f [math]::Round($t.TotalMilliseconds, 0), $LASTEXITCODE, $size)

$t2 = Measure-Command { & $ff -hide_banner -v error -i $out -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
Write-Output ("first_frame_ms={0} exit={1}" -f [math]::Round($t2.TotalMilliseconds, 0), $LASTEXITCODE)

$t3 = Measure-Command { & $ff -hide_banner -v error -ss 50 -i $out -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
Write-Output ("seek50s_ms={0} exit={1}" -f [math]::Round($t3.TotalMilliseconds, 0), $LASTEXITCODE)
