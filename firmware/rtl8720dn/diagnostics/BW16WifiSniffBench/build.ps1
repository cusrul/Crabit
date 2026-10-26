$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../../..")).Path
$output = Join-Path $repoRoot "build/bw16-wifi-sniff-bench"
$parserPath = Join-Path $repoRoot "firmware/rtl8720dn/NanoUI/app"
$stagedSketch = Join-Path $repoRoot "build/bw16-wifi-sniff-bench-ide/BW16WifiSniffBench"
$mutex = New-Object System.Threading.Mutex($false, "Local\Crabit-BW16-Arduino-Compile")
$held = $false
try {
    $held = $mutex.WaitOne(0)
    if (-not $held) { throw "Another BW16 build is active." }
    # Generated distribution snapshot only; the watch headers remain authoritative.
    $stagedApp = Join-Path $stagedSketch "src/product/app"
    New-Item -ItemType Directory -Force -Path $stagedApp | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "BW16WifiSniffBench.ino") -Destination $stagedSketch
    foreach ($header in @("wifi_hack_core.h", "wifi_hack_sniffer.h", "wifi_sniff_diagnostics.h")) {
        Copy-Item -LiteralPath (Join-Path $parserPath $header) -Destination $stagedApp
    }
    foreach ($header in @("NetManager.h", "RadioSvc.h", "packet-injection.h")) {
        Copy-Item -LiteralPath (Join-Path $parserPath "../$header") -Destination (Join-Path $stagedSketch "src/product")
    }
    & arduino-cli compile --fqbn realtek:AmebaD:Ai-Thinker_BW16 --build-path $output `
        $stagedSketch
    if ($LASTEXITCODE -ne 0) { throw "Standalone build failed." }
    & (Join-Path $repoRoot "tools/check-bw16-image.ps1") `
        -ImagePath (Join-Path $output "km0_km4_image2.bin") -Profile "receive-only-bench" -StrictTarget
} finally {
    if ($held) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
