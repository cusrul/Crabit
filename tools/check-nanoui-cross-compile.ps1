param(
    [Parameter(Mandatory = $true)][string]$Stm32Compiler,
    [Parameter(Mandatory = $true)][string]$Bw16Compiler,
    [string]$BuildPath = "build/nanoui-cross-probe",
    [ValidateSet("c++11", "c++17")][string]$Standard = "c++11"
)

# Compile-only SDK probes. A pass is not firmware linking or hardware acceptance.
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$libraryRoot = Join-Path $repoRoot "libraries/NanoUI/src"
$output = if ([IO.Path]::IsPathRooted($BuildPath)) { $BuildPath } else {
    Join-Path $repoRoot $BuildPath
}
$compilers = @{
    stm32 = @{ path = $Stm32Compiler; cpu = "cortex-m4" }
    bw16 = @{ path = $Bw16Compiler; cpu = "cortex-m33" }
}
$sources = Get-ChildItem -LiteralPath $libraryRoot -Recurse -Filter *.cpp
foreach ($target in @("stm32", "bw16")) {
    $compiler = $compilers[$target]
    if (-not (Test-Path -LiteralPath $compiler.path -PathType Leaf)) {
        throw "Missing $target compiler: $($compiler.path)"
    }
    $targetOutput = Join-Path $output $target
    New-Item -ItemType Directory -Force -Path $targetOutput | Out-Null
    & $compiler.path --version
    if ($LASTEXITCODE -ne 0) { throw "$target compiler version check failed" }
    foreach ($source in $sources) {
        $relative = $source.FullName.Substring($libraryRoot.Length + 1)
        $object = Join-Path $targetOutput ($relative.Replace('\', '_').Replace('/', '_') + '.o')
        & $compiler.path "-std=$Standard" -fno-exceptions -fno-rtti -mthumb "-mcpu=$($compiler.cpu)" `
            -Wall -Wextra -Werror "-I$libraryRoot" -c $source.FullName -o $object
        if ($LASTEXITCODE -ne 0) { throw "$target core compile failed: $relative" }
    }
    Write-Output "$target PASS: $($sources.Count) translation units (compile only)"
}
