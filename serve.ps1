# Tiny static file server for the web build (no installs needed).
# Usage:  powershell -ExecutionPolicy Bypass -File serve.ps1 [-Port 8080]
param([int]$Port = 8080)

$root = (Resolve-Path (Join-Path $PSScriptRoot 'web')).Path
$mime = @{
  '.html' = 'text/html; charset=utf-8'; '.js' = 'text/javascript; charset=utf-8'
  '.css'  = 'text/css; charset=utf-8';  '.svg' = 'image/svg+xml'; '.json' = 'application/json'
  '.png'  = 'image/png'; '.ico' = 'image/x-icon'
}

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
Write-Host "Serving $root at http://localhost:$Port/   (Ctrl+C to stop)"
Start-Process "http://localhost:$Port/"

try {
  while ($listener.IsListening) {
    $task = $listener.GetContextAsync()
    while (-not $task.AsyncWaitHandle.WaitOne(200)) { }   # keeps Ctrl+C responsive
    $ctx = $task.Result
    $rel = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath).TrimStart('/')
    if ([string]::IsNullOrEmpty($rel)) { $rel = 'index.html' }
    $file = [IO.Path]::GetFullPath((Join-Path $root $rel))
    $res = $ctx.Response
    if ($file.StartsWith($root) -and (Test-Path $file -PathType Leaf)) {
      $bytes = [IO.File]::ReadAllBytes($file)
      $ext = [IO.Path]::GetExtension($file).ToLower()
      $res.ContentType = if ($mime.ContainsKey($ext)) { $mime[$ext] } else { 'application/octet-stream' }
      $res.Headers.Add('Cache-Control', 'no-store')
      $res.OutputStream.Write($bytes, 0, $bytes.Length)
    } else {
      $res.StatusCode = 404
    }
    $res.Close()
  }
} finally {
  $listener.Stop()
}
