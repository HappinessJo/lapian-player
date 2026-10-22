$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Net.Http | Out-Null

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$ff = Join-Path $repo 'third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe'
$dir = Join-Path $env:TEMP 'lapian-spike'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
$h = @{ 'User-Agent' = $ua; 'Referer' = 'https://www.bilibili.com/' }

$bvs = @('BV1BMnv6jE5S', 'BV1mA411m7pS', 'BV1GJ411x7h7')
foreach ($bv in $bvs) {
    $view = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$bv" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
    $cid = $view.data.cid
    $dur = $view.data.duration
    $title = $view.data.title
    if ($title.Length -gt 28) { $title = $title.Substring(0, 28) }
    Write-Output ("=== {0} | {1} | {2}s" -f $bv, $title, $dur)

    $d64 = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=64&fnval=0" -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
    $q64 = $d64.data.quality
    $s64 = 0
    if ($d64.data.durl.Count -gt 0) { $s64 = $d64.data.durl[0].size }
    Write-Output ("  durl qn=64 -> quality={0} format={1} size={2}MB" -f $q64, $d64.data.format, [math]::Round($s64 / 1MB, 2))

    $d80 = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=80&fnval=0" -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
    Write-Output ("  durl qn=80 -> quality={0} format={1}" -f $d80.data.quality, $d80.data.format)

    $sh = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=80&fnval=4048&fourk=1" -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
    $vids = ($sh.data.dash.video | Select-Object -ExpandProperty id -Unique | Sort-Object) -join ','
    Write-Output ("  dash qn=80 -> max_vid=[{0}] returned_quality={1}" -f $vids, $sh.data.quality)
    Start-Sleep -Milliseconds 500
}

Write-Output '=== 720p durl file behavior ==='
$bv = 'BV1BMnv6jE5S'
$view = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$bv" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
$cid = $view.data.cid
$d = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=64&fnval=0" -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
$url = $d.data.durl[0].url
$total = $d.data.durl[0].size

$hc = [System.Net.Http.HttpClient]::new()
$hc.DefaultRequestHeaders.Add('User-Agent', $ua)
$hc.DefaultRequestHeaders.Add('Referer', 'https://www.bilibili.com/')
$hc.Timeout = [TimeSpan]::FromSeconds(180)

$rq = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, $url)
$rq.Headers.Range = [System.Net.Http.Headers.RangeHeaderValue]::new(0, 524287)
$sw = [Diagnostics.Stopwatch]::StartNew()
$resp = $hc.SendAsync($rq, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).Result
$ttfb = $sw.ElapsedMilliseconds
$head = $resp.Content.ReadAsByteArrayAsync().Result
$sw.Stop()
Write-Output ("  head512k status={0} ttfb={1}ms recv={2}B total={3}ms content_range={4}" -f [int]$resp.StatusCode, $ttfb, $head.Length, $sw.ElapsedMilliseconds, $resp.Content.Headers.ContentRange)

$headFile = Join-Path $dir 'durl720_head.mp4'
[IO.File]::WriteAllBytes($headFile, $head)
$t = Measure-Command { & $ff -hide_banner -v error -i $headFile -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
Write-Output ("  ffmpeg first frame from 512KB: exit={0} {1}ms" -f $LASTEXITCODE, [math]::Round($t.TotalMilliseconds, 0))
$info = (& $ff -hide_banner -i $headFile 2>&1 | Select-String -Pattern 'Duration|Stream #') -join ' | '
Write-Output ("  probe: {0}" -f $info)

$rq2 = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, $url)
$sw2 = [Diagnostics.Stopwatch]::StartNew()
$resp2 = $hc.SendAsync($rq2, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).Result
$ttfb2 = $sw2.ElapsedMilliseconds
$full = $resp2.Content.ReadAsByteArrayAsync().Result
$sw2.Stop()
Write-Output ("  full status={0} ttfb={1}ms bytes={2} total={3}ms rate={4}MB/s" -f [int]$resp2.StatusCode, $ttfb2, $full.Length, $sw2.ElapsedMilliseconds, [math]::Round($full.Length / 1MB / ($sw2.ElapsedMilliseconds / 1000), 2))
$fullFile = Join-Path $dir 'durl720.mp4'
[IO.File]::WriteAllBytes($fullFile, $full)
$t2 = Measure-Command { & $ff -hide_banner -v error -i $fullFile -map 0:v:0 -frames:v 1 -f null - 2>&1 | Out-Null }
Write-Output ("  full-file first frame: {0}ms" -f [math]::Round($t2.TotalMilliseconds, 0))
$info2 = (& $ff -hide_banner -i $fullFile 2>&1 | Select-String -Pattern 'Duration|Stream #') -join ' | '
Write-Output ("  probe: {0}" -f $info2)

Write-Output '=== failure matrix v2 (find real -404) ==='
$ids = @('BV1zz411c7mD', 'BV1Q541167Qh', 'BV1AA411c7mD', 'BV1BMnv6jE5', 'BV1mA411m7pX')
foreach ($id in $ids) {
    try {
        $r = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$id" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
        Write-Output ("  {0} code={1} msg={2} title={3}" -f $id, $r.code, $r.message, $(if ($r.data) { $r.data.title } else { '-' }))
    } catch {
        Write-Output ("  {0} EXC {1}" -f $id, $_.Exception.Message)
    }
    Start-Sleep -Milliseconds 500
}
