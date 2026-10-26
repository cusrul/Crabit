param([switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$bundle = Join-Path $repo 'build/delivery/windows/verified'
$manifestPath = Join-Path $bundle 'bundle-manifest.json'
if (!(Test-Path -LiteralPath $manifestPath)) { throw 'Verified Windows bundle missing. See docs/windows-app-delivery.md.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$bundle = [IO.Path]::GetFullPath($bundle)
$bundlePrefix = $bundle.TrimEnd('\') + '\'
$verifiedPaths = @{}
function Assert-NoReparsePoint([string]$path) {
    $item = Get-Item -LiteralPath $path -Force
    while ($null -ne $item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not allowed in the bundle path.' }
        if ($item -is [IO.FileInfo]) { $item = $item.Directory }
        else { $item = $item.Parent }
    }
}
Assert-NoReparsePoint $manifestPath
foreach ($entry in $manifest.files) {
    if ([string]::IsNullOrWhiteSpace($entry.path) -or $entry.path -match '(^|[\\/])\.\.([\\/]|$)|:|^[\\/]') { throw 'Unsafe manifest path.' }
    $file = [IO.Path]::GetFullPath((Join-Path $bundle $entry.path))
    if (!$file.StartsWith($bundlePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes the bundle.' }
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing bundle file: $($entry.path)" }
    Assert-NoReparsePoint $file
    if ($verifiedPaths.ContainsKey($file)) { throw 'Duplicate manifest path.' }
    $stream = [IO.File]::OpenRead($file)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $actualHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $sha.Dispose() }
    if ($actualHash -ne $entry.sha256) { throw "Bundle hash mismatch: $($entry.path)" }
    $verifiedPaths[$file] = $true
}
$app = Join-Path $bundle 'crabit_watch_app.exe'
if (!$verifiedPaths.ContainsKey($app)) { throw 'Application entry is not covered by the verified manifest.' }
Write-Output "Verified bundle: $bundle"
if ($VerifyOnly) { return }
$launchMutex = [Threading.Mutex]::new($false, 'Local\CrabitVerifiedAppLaunch')
$ownsMutex = $false
try {
    try { $ownsMutex = $launchMutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (!$ownsMutex) { Write-Output 'Another launch is in progress.'; return }
    $running = Get-Process -Name crabit_watch_app -ErrorAction SilentlyContinue
    if ($running) {
    Write-Output 'Crabit App is already running. Close its window before opening another build; no process was stopped.'
    return
    }
    Start-Process -FilePath $app -WorkingDirectory $bundle -WindowStyle Normal
} finally {
    if ($ownsMutex) { $launchMutex.ReleaseMutex() }
    $launchMutex.Dispose()
}
