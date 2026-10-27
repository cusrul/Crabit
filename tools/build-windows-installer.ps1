param(
    [Parameter(Mandatory=$true)][string]$Compiler,
    [Parameter(Mandatory=$true)][string]$Bundle,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$bundlePath = (Resolve-Path -LiteralPath $Bundle).Path
$compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
foreach ($entry in @('crabit_watch_app.exe','flutter_windows.dll','data/app.so','LICENSE','THIRD_PARTY_NOTICES.md')) {
    if (!(Test-Path -LiteralPath (Join-Path $bundlePath $entry))) { throw "Incomplete installer bundle: $entry" }
}
$forbidden = Get-ChildItem -LiteralPath $bundlePath -Recurse -Force | Where-Object {
    ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
    $_.Name -match '\.WebView2$|\.pdb$|\.log$|^ble_auth\.local\.h$|^\.ai$|^\.git$'
}
if ($forbidden) { throw 'Bundle contains runtime caches, private inputs or developer files.' }
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Choose a new output directory; existing output is preserved.' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
& $compilerPath "/DBundleDir=$bundlePath" "/DOutputDir=$outputPath" (Join-Path $PSScriptRoot 'crabit-windows.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $outputPath 'Crabit-Setup-0.2.0-preview.1-x64.exe')
