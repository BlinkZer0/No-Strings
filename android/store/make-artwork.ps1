# Renders the Play Store artwork into ..\..\release\store\ with headless Edge:
#   icon-512.png (app icon), feature-1024x500.png (feature graphic),
#   screenshot-*.png (2400 x 1080 landscape phone screenshots of the instrument)
# Usage: powershell -ExecutionPolicy Bypass -File android\store\make-artwork.ps1
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out = Join-Path $repo 'release\store'
New-Item -ItemType Directory -Force $out | Out-Null
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge not found' }
$art = 'file:///' + ((Join-Path $PSScriptRoot 'artwork.html') -replace '\\', '/')
$web = 'file:///' + ((Join-Path $repo 'web\index.html') -replace '\\', '/')

function Shot([string]$url, [int]$w, [int]$h, [string]$file) {
    # via cmd: Edge chats on stderr, which PowerShell would otherwise treat as a failure
    cmd /c "`"$edge`" --headless=new --disable-gpu --hide-scrollbars --window-size=$w,$h --virtual-time-budget=3000 `"--screenshot=$out\$file`" `"$url`" >nul 2>nul"
    Write-Host ("{0,-28} {1} x {2}" -f $file, $w, $h)
}
Shot "$art#icon" 512 512 'icon-512.png'
Shot "$art#feature" 1024 500 'feature-1024x500.png'
Shot "$web" 2400 1080 'screenshot-1-concert-cello.png'
Shot "$web#body=6,excitation=8,sympOn=1,sympMode=0" 2400 1080 'screenshot-2-electric-ebow.png'
Shot "$web#body=1,material=0,excitation=1,sympOn=1,sympScale=6" 2400 1080 'screenshot-3-baroque-gut-drone.png'
Shot "$web#body=5,excitation=7" 2400 1080 'screenshot-4-arpeggione-mallet.png'
Write-Host "Artwork in $out"
