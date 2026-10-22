$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Net.Http | Out-Null

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$ff = Join-Path $repo 'third_party\tools\ffmpeg-gpl\bin\ffmpeg.exe'
$dir = Join-Path $env:TEMP 'lapian-spike'
$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
$h = @{ 'User-Agent' = $ua; 'Referer' = 'https://www.bilibili.com/' }
$bv = 'BV1BMnv6jE5S'

$sw = [Diagnostics.Stopwatch]::StartNew()
$view = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$bv" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
$apiView = $sw.ElapsedMilliseconds
$cid = $view.data.cid

Write-Output '=== API latency ==='
Write-Output ("view_api={0}ms" -f $apiView)

Write-Output '=== durl ceiling by qn (fnval=0) ==='
foreach ($qn in 16, 32, 64, 80, 112, 116) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $pu = "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=$qn&fnval=0"
    $p = (Invoke-WebRequest -Uri $pu -Headers $h -TimeoutSec 20 -UseBasicParsing).Content | ConvertFrom-Json
    $el = $sw.ElapsedMilliseconds
    $q = $p.data.quality
    $fmt = $p.data.format
    $cnt = $p.data.durl.Count
    $size = if ($cnt -gt 0) { $p.data.durl[0].size } else { 0 }
    Write-Output ("  qn={0} -> quality={1} format={2} durl={3} size={4} api={5}ms" -f $qn, $q, $fmt, $cnt, $size, $el)
    Start-Sleep -Milliseconds 400
}

Write-Output '=== remux timing (480p dash video+audio) ==='
$vfile = Join-Path $dir 'v.mp4'
$afile = Join-Path $dir 'a.mp4'
$out = 'E:\拉片软件开发\.secrets\_spike_remux.mp4'
for ($i = 0; $i -lt 3; $i++) {
    if (Test-Path $out) { Remove-Item $out -Force }
    $t = Measure-Command { & $ff -hide_banner -v error -y -i $vfile -i $afile -c copy -movflags +faststart $out 2>&1 | Out-Null }
    Write-Output ("  run{0} remux={1}ms exit={2}" -f ($i + 1), [math]::Round($t.TotalMilliseconds, 0), $LASTEXITCODE)
}

Write-Output '=== failure matrix (view API) ==='
$cases = @(
    @{ id = 'BV1xx411c7mD'; note = 'format-ok, probably nonexistent' },
    @{ id = 'BV1'; note = 'too short' },
    @{ id = 'BV1BMnv6jE5X'; note = 'last char changed' },
    @{ id = 'av170001'; note = 'aid form' }
)
foreach ($c in $cases) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $r = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$($c.id)" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
        $code = $r.code
        $msg = $r.message
    } catch {
        $code = 'EXC'
        $msg = $_.Exception.Message
    }
    Write-Output ("  {0} [{1}] code={2} msg={3} {4}ms" -f $c.id, $c.note, $code, $msg, $sw.ElapsedMilliseconds)
    Start-Sleep -Milliseconds 400
}

Write-Output '=== audio-less / multi-part check ==='
Write-Output ("  pages={0} cid={1} videos={2}" -f $view.data.videos, $cid, $view.data.videos)
