$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../../..")).Path
$product = Join-Path $repoRoot "firmware/rtl8720dn/NanoUI"
$stage = Join-Path $repoRoot "build/bw16-wifi-app-bench-ide/BW16WifiAppBench"
$output = Join-Path $repoRoot "build/bw16-wifi-app-bench"
$mutex = New-Object System.Threading.Mutex($false, "Local\Crabit-BW16-Arduino-Compile")
$held = $false
try {
    $held = $mutex.WaitOne(0)
    if (-not $held) { throw "Another BW16 build is active." }
    $stageProduct = Join-Path $stage "src/product"
    New-Item -ItemType Directory -Force -Path (Join-Path $stageProduct "app") | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "BW16WifiAppBench.ino") -Destination $stage
    foreach ($header in @("app_wifi_sniff.h", "wifi_hack_core.h", "wifi_hack_sniffer.h", "wifi_sniff_diagnostics.h")) {
        Copy-Item -LiteralPath (Join-Path $product "app/$header") -Destination (Join-Path $stageProduct "app")
    }
    foreach ($header in @("NetManager.h", "RadioSvc.h", "packet-injection.h")) {
        Copy-Item -LiteralPath (Join-Path $product $header) -Destination $stageProduct
    }
    # Only product headers; no hardware driver .cpp or other applications.
    $sourceHeaders = Join-Path $product "src"
    foreach ($file in Get-ChildItem -LiteralPath $sourceHeaders -Recurse -Filter *.h) {
        $relative = $file.FullName.Substring($sourceHeaders.Length + 1)
        $destination = Join-Path $stageProduct "src/$relative"
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    foreach ($file in @("font_engine.cpp", "nanoui_assets.cpp")) {
        Copy-Item -LiteralPath (Join-Path $sourceHeaders $file) -Destination (Join-Path $stageProduct "src")
    }
    & arduino-cli compile --fqbn realtek:AmebaD:Ai-Thinker_BW16 --build-path $output `
        --library (Join-Path $repoRoot "libraries/NanoUI") $stage
    if ($LASTEXITCODE -ne 0) { throw "App bench build failed." }
    & (Join-Path $repoRoot "tools/check-bw16-image.ps1") -ImagePath (Join-Path $output "km0_km4_image2.bin") `
        -Profile "receive-app-bench" -StrictTarget
} finally {
    if ($held) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
