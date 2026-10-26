param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release", "MinSizeRel")]
    [string]$Configuration = "MinSizeRel"
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$stm32Root = Join-Path $repoRoot "firmware\stm32"

Push-Location $stm32Root
try {
    cmake --preset $Configuration
    if ($LASTEXITCODE -ne 0) { throw "STM32 configure failed: $LASTEXITCODE" }
    cmake --build --preset $Configuration --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "STM32 build failed: $LASTEXITCODE" }
}
finally {
    Pop-Location
}
