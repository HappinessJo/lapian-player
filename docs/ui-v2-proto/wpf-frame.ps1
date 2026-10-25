# One-off: pull a single frame from an MP4 with WPF MediaPlayer (Media Foundation).
# ASCII-only (Windows PowerShell 5.1 reads .ps1 as ANSI).
param(
  [Parameter(Mandatory = $true)][string]$File,
  [Parameter(Mandatory = $true)][double]$Seconds,
  [Parameter(Mandatory = $true)][string]$Out
)

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName WindowsBase

$player = New-Object System.Windows.Media.MediaPlayer
$player.Volume = 0
$player.ScrubbingEnabled = $true
$player.Open([Uri]$File)

for ($i = 0; $i -lt 100; $i++) {
  Start-Sleep -Milliseconds 100
  if ($player.NaturalVideoWidth -gt 0) { break }
}
"natural=$($player.NaturalVideoWidth)x$($player.NaturalVideoHeight)"
if ($player.NaturalVideoWidth -le 0) { "open-failed"; exit 1 }

$player.Position = [TimeSpan]::FromSeconds($Seconds)
Start-Sleep -Milliseconds 1500
"position=$($player.Position.TotalSeconds)"

$w = $player.NaturalVideoWidth
$h = $player.NaturalVideoHeight
$visual = New-Object System.Windows.Media.DrawingVisual
$dc = $visual.RenderOpen()
$dc.DrawVideo($player, (New-Object System.Windows.Rect(0, 0, $w, $h)))
$dc.Close()

$rtb = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($w, $h, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
$rtb.Render($visual)

$enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
$enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($rtb))
$fs = [System.IO.File]::Create($Out)
$enc.Save($fs)
$fs.Close()

$player.Close()
"wrote $Out ($w x $h)"
