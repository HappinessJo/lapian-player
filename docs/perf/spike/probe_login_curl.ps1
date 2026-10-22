$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$candidates = @(
    (Join-Path $repo '.secrets\bili_sessdata.txt'),
    (Join-Path $repo '.secrets\bilibili_sessdata.txt')
)
$p = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $p) { Write-Output 'STATUS=NO_CREDENTIAL'; exit 2 }
$sess = [IO.File]::ReadAllText($p).Trim().TrimEnd(';')
if ($sess.StartsWith('SESSDATA=')) { $sess = $sess.Substring(9) }
$sess = $sess.Trim('"').Trim()
if ($sess.Length -lt 8) { Write-Output 'STATUS=BAD_CREDENTIAL'; exit 2 }
Write-Output ("STATUS=CREDENTIAL_PRESENT len={0}" -f $sess.Length)

$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
$tmp = Join-Path $env:TEMP 'lapian-spike'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

function Get-Json {
    param([string]$Url, [string]$Name)
    $out = Join-Path $tmp ("$Name.json")
    if (Test-Path $out) { Remove-Item $out -Force }
    & curl.exe -s --max-time 25 -o $out `
        -H "User-Agent: $ua" `
        -H 'Referer: https://www.bilibili.com/' `
        -H "Cookie: SESSDATA=$sess" `
        $Url | Out-Null
    if (-not (Test-Path $out)) { return $null }
    $raw = [IO.File]::ReadAllText($out, [Text.Encoding]::UTF8)
    if ([string]::IsNullOrWhiteSpace($raw)) { return $null }
    try { return ($raw | ConvertFrom-Json) } catch { return $null }
}

$nav = Get-Json 'https://api.bilibili.com/x/web-interface/nav' 'nav'
if ($nav) {
    Write-Output ("nav code={0} isLogin={1} mid={2} vipStatus={3} level={4}" -f `
        $nav.code, $nav.data.isLogin, $nav.data.mid, $nav.data.vipStatus, $nav.data.level_info.current_level)
} else {
    Write-Output 'nav parse_failed'
    exit 1
}

$bvs = @('BV1BMnv6jE5S', 'BV1mA411m7pS', 'BV1GJ411x7h7')
foreach ($bv in $bvs) {
    $view = Get-Json "https://api.bilibili.com/x/web-interface/view?bvid=$bv" "view_$bv"
    if (-not $view -or $view.code -ne 0) { Write-Output ("=== {0} view_failed" -f $bv); continue }
    $cid = $view.data.cid
    $title = $view.data.title
    if ($title.Length -gt 26) { $title = $title.Substring(0, 26) }
    Write-Output ("=== {0} | {1} | {2}s" -f $bv, $title, $view.data.duration)

    $d = Get-Json "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=120&fnval=0&fourk=1" "durl_$bv"
    if ($d -and $d.code -eq 0) {
        $sz = 0
        if ($d.data.durl.Count -gt 0) { $sz = $d.data.durl[0].size }
        Write-Output ("  durl(qn=120) quality={0} format={1} size={2}MB" -f $d.data.quality, $d.data.format, [math]::Round($sz / 1MB, 2))
    }

    $s = Get-Json "https://api.bilibili.com/x/player/playurl?bvid=$bv&cid=$cid&qn=120&fnval=4048&fourk=1" "dash_$bv"
    if ($s -and $s.code -eq 0) {
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
    }
    Start-Sleep -Milliseconds 700
}
