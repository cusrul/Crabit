param(
    [Parameter(Mandatory = $true)]
    [string]$ImagePath,
    [string]$Profile = "product",
    [string]$ManifestPath,
    [switch]$StrictTarget
)

$ErrorActionPreference = "Stop"

$imageFlashOffset = [int64]0x6000
$firstReservedOffset = [int64]0xFC000
$hardMaxLength = $firstReservedOffset - $imageFlashOffset
$targetMaxLength = [int64]0xF4000

$resolvedImage = (Resolve-Path -LiteralPath $ImagePath).Path
$file = Get-Item -LiteralPath $resolvedImage
$length = [int64]$file.Length
$physicalEnd = $imageFlashOffset + $length

if ($length -gt $hardMaxLength) {
    throw ("BW16 image crosses the protected FTL/settings boundary: " +
        "length=0x{0:X}, physical_end=0x{1:X}, hard_end=0x{2:X}" -f
        $length, $physicalEnd, $firstReservedOffset)
}

if ($StrictTarget -and $length -gt $targetMaxLength) {
    throw ("BW16 image exceeds the target margin: length=0x{0:X}, target=0x{1:X}" -f
        $length, $targetMaxLength)
}

$sha256 = (Get-FileHash -LiteralPath $resolvedImage -Algorithm SHA256).Hash.ToLowerInvariant()
$status = if ($length -le $targetMaxLength) { "PASS" } else { "PASS_WITHOUT_TARGET_MARGIN" }
$manifest = [ordered]@{
    schema = "crabit-bw16-image-range-v1"
    profile = $Profile
    image = $resolvedImage
    sha256 = $sha256
    file_length = $length
    file_length_hex = ("0x{0:X}" -f $length)
    image_flash_offset = ("0x{0:X}" -f $imageFlashOffset)
    physical_end_exclusive = ("0x{0:X}" -f $physicalEnd)
    first_reserved_offset = ("0x{0:X}" -f $firstReservedOffset)
    hard_max_length = ("0x{0:X}" -f $hardMaxLength)
    target_max_length = ("0x{0:X}" -f $targetMaxLength)
    status = $status
}

if (-not $ManifestPath) {
    $ManifestPath = "$resolvedImage.bw16-range.json"
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath $ManifestPath -Encoding ascii

Write-Output ("BW16 image {0}: length=0x{1:X}, physical_end=0x{2:X}, sha256={3}" -f
    $status, $length, $physicalEnd, $sha256)
