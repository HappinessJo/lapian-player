$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$candidates = @(
    (Join-Path $repo '.secrets\bilibili_sessdata.txt'),
    (Join-Path $repo '.secrets\bili_sessdata.txt')
)
$p = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $p) {
    Write-Output 'STATUS=NO_CREDENTIAL'
    exit 2
}
$raw = [IO.File]::ReadAllText($p).Trim().TrimEnd(';')
if ($raw.StartsWith('SESSDATA=')) { $raw = $raw.Substring(9) }
$raw = $raw.Trim('"').Trim()
if ($raw.Length -lt 8) {
    Write-Output ("STATUS=BAD_CREDENTIAL len={0}" -f $raw.Length)
    exit 2
}
Write-Output ("STATUS=CREDENTIAL_PRESENT len={0} file={1}" -f $raw.Length, (Split-Path $p -Leaf))

$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
$h = @{
    'User-Agent' = $ua
    'Referer'    = 'https://www.bilibili.com/'
    'Cookie'     = "SESSDATA=$raw"
}

$nav = (Invoke-WebRequest -Uri 'https://api.bilibili.com/x/web-interface/nav' -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
Write-Output ("nav isLogin={0} code={1} vip={2} level={3}" -f $nav.data.isLogin, $nav.code, $nav.data.vipStatus, $nav.data.level_info.current_level)

$bvs = @('BV1BMnv6jE5S', 'BV1mA411m7pS', 'BV1GJ411x7h7')
foreach ($bv in $bvs) {
    $view = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/web-interface/view?bvid=$bv" -Headers $h -TimeoutSec 15 -UseBasicParsing).Content | ConvertFrom-Json
    if ($view.code -ne 0) { Write-Output ("=== {0} view_code={1}" -f $bv, $view.code); continue }
    $cid = $view.data.cid
    $title = $view.data.title
    if ($title.Length -gt 24) { $title = $title.Substring(0, 24) }
    Write-Output ("=== {0} | {1} | {2}s" -f $bv, $title, $view.data.duration)

    $d = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=120&fnval=0&fourk=1" -Headers $h -TimeoutSec 25 -UseBasicParsing).Content | ConvertFrom-Json
    $dsize = 0
    if ($d.data.durl.Count -gt 0) { $dsize = $d.data.durl[0].size }
    Write-Output ("  durl(qn=120) quality={0} format={1} size={2}MB" -f $d.data.quality, $d.data.format, [math]::Round($dsize / 1MB, 2))

    $s = (Invoke-WebRequest -Uri "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=120&fnval=4048&fourk=1" -Headers $h -TimeoutSec 25 -UseBasicParsing).Content | ConvertFrom-Json
    Write-Output ("  dash(qn=120) returned_quality={0} dash_present={1}" -f $s.data.quality, [bool]$s.data.dash)
    if ($s.data.dash) {
        $groups = $s.data.dash.video | Group-Object id | Sort-Object { [int]$_.Name }
        foreach ($g in $groups) {
            $first = $g.Group[0]
            $codecs = ($g.Group | ForEach-Object { "{0}:{1}k" -f $_.codecid, [int]($_.bandwidth / 1000) }) -join ' '
            Write-Output ("    vid={0} {1}x{2}@{3} [{4}]" -f $g.Name, $first.width, $first.height, $first.frameRate, $codecs)
        }
        $auds = ($s.data.dash.audio | ForEach-Object { "{0}:{1}k" -f $_.id, [int]($_.bandwidth / 1000) }) -join ' '
        Write-Output ("    audio [{0}]" -f $auds)
    }
    Start-Sleep -Milliseconds 700
}
