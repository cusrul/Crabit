param(
    [switch]$CompatibilityMode,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot "apps/mobile/build/windows/x64/runner/$Configuration/crabit_watch_app.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Build Flutter Windows $Configuration before launching."
}
$previousSetting = $env:CRABIT_WINDOWS_DISABLE_ACCESSIBILITY
try {
    if ($CompatibilityMode) {
        $env:CRABIT_WINDOWS_DISABLE_ACCESSIBILITY = '1'
        Write-Host 'Windows compatibility mode: screen-reader semantics temporarily disabled for this app process.'
    }
    Start-Process -FilePath $executable -WorkingDirectory (Split-Path -Parent $executable)
} finally {
    $env:CRABIT_WINDOWS_DISABLE_ACCESSIBILITY = $previousSetting
}
