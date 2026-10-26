param([string]$Destination = "build/NanoUI-library.zip")
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$library = Join-Path $repoRoot "libraries/NanoUI"
$archive = if ([IO.Path]::IsPathRooted($Destination)) { $Destination } else {
    Join-Path $repoRoot $Destination
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archive) | Out-Null
# Generate a clean package, never copy previous build caches or the SDK. The
# unique staging path is cleaned only after checking its absolute boundary.
$staging = Join-Path $repoRoot ("build/arduino-package-" + [Guid]::NewGuid().ToString("N"))
$packageRoot = Join-Path $staging "NanoUI"
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $library "library.properties") -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $library "src") -Destination $packageRoot -Recurse
    foreach ($entry in @("LICENSE", "README.md", "CMakeLists.txt", "assets", "tests", "integrations", "docs")) {
        $source = Join-Path $library $entry
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination $packageRoot -Recurse
        }
    }
    # Only maintained original-framework examples; historical experiments stay out.
    New-Item -ItemType Directory -Path (Join-Path $packageRoot "examples") | Out-Null
    Copy-Item -LiteralPath (Join-Path $library "examples/FirstApp") -Destination (Join-Path $packageRoot "examples") -Recurse
    # Do not ship Python caches or generator backups.
    $toolRoot = Join-Path $library "tools"
    foreach ($file in Get-ChildItem -LiteralPath $toolRoot -File -Recurse) {
        if ($file.FullName -match '[\\/]__pycache__[\\/]' -or $file.Extension -eq '.bak') { continue }
        $relative = $file.FullName.Substring($toolRoot.Length).TrimStart('\', '/')
        $target = Join-Path (Join-Path $packageRoot "tools") $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
    Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive -Force
} finally {
    $resolvedStage = (Resolve-Path -LiteralPath $staging).Path
    $resolvedBuild = (Resolve-Path -LiteralPath (Join-Path $repoRoot "build")).Path
    $buildPrefix = $resolvedBuild.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedStage.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolvedStage) -notmatch '^arduino-package-[a-f0-9]{32}$') {
        throw "Refusing to clean unvalidated package staging path: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
Write-Output "Arduino IDE: Sketch -> Include Library -> Add .ZIP Library"
Write-Output $archive
Get-FileHash -LiteralPath $archive -Algorithm SHA256
