param(
    [ValidateSet("product", "ble-mouse-lab", "ble-ppt-lab", "ble-kbd-lab", "games-lab", "wifi-tools-lab", "ota-lab", "all-lab", "network-ota-lab")]
    [string]$Profile = "product",
    [string]$BuildPath,
    [switch]$ArduinoIdeDefaults,
    [switch]$MeasureBleSpam,
    [switch]$ArduboyPorts,
    [string]$BleSpamLibrary,
    [switch]$TwoGames,
    [ValidateSet("none", "ardu3d", "hopper")]
    [string]$Game = "none"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sketch = Join-Path $repoRoot "firmware\rtl8720dn\NanoUI"
$stagingRoot = Join-Path $repoRoot "build\bw16-sketch-staging"
$stagedSketch = Join-Path $stagingRoot "NanoUI"
$authHeader = Join-Path $sketch "ble_auth.local.h"
if (-not (Test-Path -LiteralPath $authHeader -PathType Leaf)) {
    throw "Missing private BLE configuration. Copy firmware/rtl8720dn/NanoUI/ble_auth.local.example.h to ble_auth.local.h and set your own six-digit code. Do not commit it. See docs/reproduction.md."
}
$rangeGuard = Join-Path $repoRoot "tools\check-bw16-image.ps1"
$sharedProtocol = Join-Path $repoRoot "firmware\shared"
# Single staging manifest shared with tools/build_bw16_split_games.py.
# firmware/shared is the source of truth; do not add a second header list here.
$sharedManifest = Join-Path $repoRoot "tools\bw16_shared_headers.json"
$sharedNanoUi = Join-Path $repoRoot "libraries\NanoUI"
if (-not $BleSpamLibrary) {
    $bundledBleSpam = Join-Path $repoRoot "libraries\BW16_BLE_Spam"
    if (Test-Path -LiteralPath (Join-Path $bundledBleSpam "src\BW16BLESpam.h")) {
        $BleSpamLibrary = $bundledBleSpam
    }
}
$nanoUiPreflight = Join-Path $repoRoot "firmware\rtl8720dn\NanoUI\src\NanoUI_Preflight.h"
if ($ArduinoIdeDefaults -and $Profile -ne "product") {
    throw "Arduino IDE defaults validate only the sketch's default product profile."
}

if (-not (Test-Path -LiteralPath $sharedManifest -PathType Leaf)) {
    throw "Shared-header manifest missing: $sharedManifest"
}
try {
    $sharedSpec = Get-Content -LiteralPath $sharedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    throw "Shared-header manifest unreadable: $sharedManifest ($($_.Exception.Message))"
}
if ($sharedSpec -isnot [System.Management.Automation.PSCustomObject]) {
    throw "Shared-header manifest must be a JSON object"
}
$sharedAllowedKeys = @('$comment', "source_root", "staged_root", "headers")
$sharedUnexpected = @($sharedSpec.PSObject.Properties.Name | Where-Object { $sharedAllowedKeys -cnotcontains $_ })
if ($sharedUnexpected.Count -gt 0) {
    throw "Shared-header manifest has unexpected keys: $($sharedUnexpected -join ', ')"
}
if ($sharedSpec.'$comment' -isnot [string]) {
    throw "Shared-header manifest `$comment must document the source of truth"
}
# Exact, case-sensitive string match; no normalization, same rule as
# tools/build_bw16_split_games.py.
if ($sharedSpec.source_root -isnot [string] -or $sharedSpec.source_root -cne "firmware/shared" -or
    $sharedSpec.staged_root -isnot [string] -or $sharedSpec.staged_root -cne "src/crabit_shared") {
    throw "Shared-header manifest must stage firmware/shared into src/crabit_shared."
}
# @() would turn a bare string into a one-item list, so check the JSON type first.
if ($sharedSpec.headers -isnot [array] -or $sharedSpec.headers.Count -eq 0) {
    throw "Shared-header manifest must list headers as a non-empty array"
}
$sharedHeaders = @($sharedSpec.headers)
foreach ($shared in $sharedHeaders) {
    # \z, not $: $ would also match before a trailing newline; Python uses fullmatch.
    if ($shared -isnot [string] -or $shared -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*\.h\z' -or
        $shared.Contains("..") -or [System.IO.Path]::IsPathRooted($shared)) {
        throw "Shared-header manifest header must be a plain file name: $shared"
    }
}
# Case-insensitive duplicate rejection, matching Python's str.casefold() comparison.
if ((@($sharedHeaders | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique)).Count -ne $sharedHeaders.Count) {
    throw "Shared-header manifest lists a header more than once"
}
foreach ($shared in $sharedHeaders) {
    if (-not (Test-Path -LiteralPath (Join-Path $sharedProtocol $shared) -PathType Leaf)) {
        throw "Shared-header manifest header missing from firmware/shared: $shared"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $sketch "src\crabit_shared\$shared") -PathType Leaf)) {
        throw "Shared-header manifest header has no sketch target: $shared"
    }
}

if (-not $BuildPath) {
    $defaultOutput = if ($ArduinoIdeDefaults) {
        "build\arduino-bw16-ide"
    } elseif ($Profile -eq "product") {
        "build\arduino-bw16"
    } else {
        "build\arduino-bw16-$Profile"
    }
    $output = Join-Path $repoRoot $defaultOutput
} elseif ([System.IO.Path]::IsPathRooted($BuildPath)) {
    $output = $BuildPath
} else {
    $output = Join-Path $repoRoot $BuildPath
}

$profileId = @{
    "product" = 0
    "ble-mouse-lab" = 1
    "ble-ppt-lab" = 2
    "ble-kbd-lab" = 3
    "games-lab" = 4
    "wifi-tools-lab" = 5
    "ota-lab" = 6
    "all-lab" = 7
    "network-ota-lab" = 8
}[$Profile]

$buildFlags = "-mthumb -DCORE_RTL8720DN -DBOARD_AITHINKER_BW16 " +
    "-DArduino_STD_PRINTF -DSTD_PRINTF -std=c++17 " +
    "-DCRABIT_BW16_PROFILE=$profileId -I$sharedProtocol"
if ($ArduboyPorts) {
    if ($Profile -ne "wifi-tools-lab" -or $ArduinoIdeDefaults) {
        throw "Arduboy ports require explicit wifi-tools-lab without ArduinoIdeDefaults."
    }
    $buildFlags += " -DCRABIT_BW16_ARDUBOY_PORTS=1"
}
$compilerFlags = "-include $nanoUiPreflight"
if ($Game -ne "none") {
    if ($ArduinoIdeDefaults -or $Profile -ne "wifi-tools-lab" -or $TwoGames) {
        throw "Single-game trial requires wifi-tools-lab without TwoGames."
    }
    $gameId = if ($Game -eq "ardu3d") { 1 } else { 2 }
    $buildFlags += " -DCRABIT_BW16_NETWORK_GAME=$gameId"
}
if ($TwoGames) {
    if ($ArduinoIdeDefaults -or $Profile -ne "wifi-tools-lab") {
        throw "Two-game trial requires explicit wifi-tools-lab profile."
    }
    $buildFlags += " -DCRABIT_BW16_NETWORK_TWO_GAMES=1"
}
if ($MeasureBleSpam) {
    if ($ArduinoIdeDefaults -or $Profile -ne "wifi-tools-lab") {
        throw "BLE library measurement requires explicit wifi-tools-lab profile."
    }
    if (-not $BleSpamLibrary -or -not (Test-Path -LiteralPath (Join-Path $BleSpamLibrary "src\BW16BLESpam.h"))) {
        throw "Specify the existing BLE library directory explicitly; do not rely on duplicate library discovery."
    }
    $buildFlags += " -DCRABIT_MEASURE_EXTERNAL_BLE_SPAM=1"
}
if ($BleSpamLibrary -and -not (Test-Path -LiteralPath (Join-Path $BleSpamLibrary "src\BW16BLESpam.h"))) {
    throw "The selected BLE library directory does not contain src\BW16BLESpam.h."
}

$mutex = New-Object System.Threading.Mutex($false, "Local\Crabit-BW16-Arduino-Compile")
$lockHeld = $false
try {
    $lockHeld = $mutex.WaitOne(0)
    if (-not $lockHeld) {
        throw "Another BW16 Arduino build is active; retry after it finishes."
    }

    New-Item -ItemType Directory -Force -Path $output | Out-Null
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
    Copy-Item -LiteralPath $sketch -Destination $stagingRoot -Recurse -Force
    foreach ($shared in $sharedHeaders) {
        Copy-Item -LiteralPath (Join-Path $sharedProtocol $shared) `
            -Destination (Join-Path $stagedSketch "src\crabit_shared\$shared") -Force
    }
    if ($Profile -ne "product") {
        # Generated evidence marker, not a promise that a lab image is usable.
        "COMPILE/SIZE MEASUREMENT ONLY: $Profile. Not hardware accepted. Do not flash." |
            Set-Content -LiteralPath (Join-Path $output "DO_NOT_FLASH.txt") -Encoding ascii
        Write-Warning "Lab profile: compile/size measurement only; runtime ownership/coexistence is not validated."
    }
    $compileArguments = @(
        "compile",
        "--fqbn",
        "realtek:AmebaD:Ai-Thinker_BW16",
        "--build-path",
        $output,
        "--library",
        $sharedNanoUi
    )
    if ($BleSpamLibrary) {
        $compileArguments += @("--library", $BleSpamLibrary)
    }
    # Keep all outputs in BuildPath. Exporting during compile would publish an
    # image into the sketch's IDE upload folder BEFORE the range guard runs.
    if (-not $ArduinoIdeDefaults) {
        $compileArguments += @(
            "--build-property",
            "build.extra_flags=$buildFlags",
            "--build-property",
            "compiler.cpp.extra_flags=$compilerFlags"
        )
    }
    $compileArguments += $stagedSketch
    if ($MeasureBleSpam) {
        $compileArguments += @("--build-property",
            "compiler.c.elf.extra_flags=-Wl,--undefined=crabit_ble_spam_size_probe,--undefined=_ZN14BW16BLESpammerC1Ev")
        Write-Warning "The BLE library probe is retained in addition to the profile's BLE Spam App for size comparison."
    }
    & arduino-cli @compileArguments
    $compileExitCode = $LASTEXITCODE
    if ($compileExitCode -ne 0) {
        throw "Arduino CLI compile failed with exit code $compileExitCode"
    }

    $image = Join-Path $output "km0_km4_image2.bin"
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        throw "Arduino CLI completed but km0_km4_image2.bin was not found in $output"
    }

    $guardArguments = @(
        "-ImagePath",
        $image,
        "-Profile",
        $(if ($ArduinoIdeDefaults) { "ide-default" } else { $Profile }),
        "-StrictTarget"
    )
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $rangeGuard @guardArguments
    $guardExitCode = $LASTEXITCODE
    if ($guardExitCode -ne 0) {
        throw "BW16 image range guard failed with exit code $guardExitCode"
    }
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    if ($lockHeld) {
        $mutex.ReleaseMutex()
    }
    $mutex.Dispose()
}
