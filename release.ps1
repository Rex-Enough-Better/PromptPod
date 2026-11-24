param(
    [string]$ReleaseNotesFile = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$ver  = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()

if ($ReleaseNotesFile -and -not (Test-Path -LiteralPath $ReleaseNotesFile)) {
    throw "ReleaseNotesFile not found: $ReleaseNotesFile"
}

# 1. Build & run deterministic self-test directly in-process so any error aborts immediately
& (Join-Path $root "build.ps1")

$srcExe = Join-Path $root "dist\PromptPod.exe"
if (-not (Test-Path $srcExe)) { throw "dist\PromptPod.exe missing after build" }

$fvi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($srcExe)
if ($fvi.ProductVersion -ne $ver -or $fvi.FileVersion -ne $ver) {
    throw "Stale binary detected: VERSION is '$ver' but dist\PromptPod.exe has ProductVersion='$($fvi.ProductVersion)'."
}

# 2. Rigorous binary privacy audit (case-insensitive UTF-8 + even/odd aligned UTF-16LE + PE debug dir check)
$bytes  = [System.IO.File]::ReadAllBytes($srcExe)
$utf8   = [System.Text.Encoding]::UTF8.GetString($bytes)
$utf16E = [System.Text.Encoding]::Unicode.GetString($bytes)
$utf16O = if ($bytes.Length -gt 1) { [System.Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) } else { "" }

$rootLeaf = Split-Path -Leaf $root
$sensitivePatterns = @(
    $root,
    $env:USERPROFILE,
    "Users\$($env:USERNAME)",
    "Users/$($env:USERNAME)",
    $env:LOCALAPPDATA,
    "w64devkit",
    $rootLeaf
) | Where-Object { $_ -and $_.Length -ge 4 -and $_ -ne "PromptPod" } | Select-Object -Unique

foreach ($pat in $sensitivePatterns) {
    if ($utf8.IndexOf($pat, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
        $utf16E.IndexOf($pat, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
        $utf16O.IndexOf($pat, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Privacy audit FAILED: binary contains sensitive local pattern '$pat'"
    }
}

$objdumpPath = $null
$objCmd = Get-Command "objdump.exe" -ErrorAction SilentlyContinue
if ($objCmd) {
    $objdumpPath = $objCmd.Source
} else {
    $cacheObj = Join-Path $env:LOCALAPPDATA "PromptPod-Toolchain\w64devkit\bin\objdump.exe"
    if (Test-Path $cacheObj) { $objdumpPath = $cacheObj }
}
if ($objdumpPath) {
    $peHdr = & $objdumpPath -p $srcExe
    if ($LASTEXITCODE -ne 0) { throw "objdump -p failed during release audit." }
    if ($peHdr -match 'Entry 6\s+[0-9a-fA-F]+[1-9a-fA-F][0-9a-fA-F]*\s+.*Debug Directory') {
        throw "Privacy audit FAILED: non-empty PE Debug Directory detected!"
    }
}
Write-Host "[PromptPod] Binary privacy audit PASSED (0 local paths in UTF-8/UTF-16LE, 0 debug dir)."

# 3. Stage release artifacts (.exe + .zip)
$relDir = Join-Path $root ".tmp\release-stage\v$ver"
Remove-Item -Recurse -Force $relDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$relDir\zip" | Out-Null

$outExeAsset = Join-Path $relDir "PromptPod-$ver.exe"
$outZipAsset = Join-Path $relDir "PromptPod-$ver-win64.zip"
Copy-Item $srcExe $outExeAsset
Copy-Item $srcExe "$relDir\zip\PromptPod.exe"
Copy-Item (Join-Path $root "LICENSE") "$relDir\zip\LICENSE.txt"
$noticesFile = Join-Path $root "THIRD_PARTY_NOTICES.md"
if (Test-Path $noticesFile) { Copy-Item $noticesFile "$relDir\zip\THIRD_PARTY_NOTICES.md" }
Compress-Archive -Path "$relDir\zip\*" -DestinationPath $outZipAsset -Force

$shaExe = (Get-FileHash $outExeAsset -Algorithm SHA256).Hash.ToLower()
$shaZip = (Get-FileHash $outZipAsset -Algorithm SHA256).Hash.ToLower()
$exeSizeBytes = (Get-Item $outExeAsset).Length

$manifestNotes = "词舱 PromptPod v$ver 正式版本"
if ($ReleaseNotesFile) {
    $rawNotes = (Get-Content -LiteralPath $ReleaseNotesFile -Raw -Encoding UTF8).Trim()
    if ($rawNotes.Length -gt 2000) { $rawNotes = $rawNotes.Substring(0, 2000) }
    if ($rawNotes) { $manifestNotes = $rawNotes }
}

$manifestObj = [ordered]@{
    schemaVersion = 1
    version       = $ver
    tag           = "v$ver"
    releaseUrl    = "https://github.com/Rex-Enough-Better/PromptPod/releases/tag/v$ver"
    notes         = $manifestNotes
    assetName     = "PromptPod-$ver.exe"
    assetUrl      = "https://github.com/Rex-Enough-Better/PromptPod/releases/download/v$ver/PromptPod-$ver.exe"
    assetSize     = [int64]$exeSizeBytes
    sha256        = $shaExe
}
$outManifestAsset = Join-Path $relDir "update-manifest.json"
$manifestJson = ($manifestObj | ConvertTo-Json -Depth 4) + "`n"
[System.IO.File]::WriteAllText($outManifestAsset, $manifestJson, [System.Text.UTF8Encoding]::new($false))

Write-Host "[PromptPod] Staged assets for v$ver :"
Write-Host "  - $outExeAsset ($exeSizeBytes bytes, SHA256: $shaExe)"
Write-Host "  - $outZipAsset (SHA256: $shaZip)"
Write-Host "  - $outManifestAsset (auto-update static manifest)"

if ($ReleaseNotesFile) {
    & gh release create "v$ver" $outExeAsset $outZipAsset $outManifestAsset `
        --repo "Rex-Enough-Better/PromptPod" `
        --title "词舱 PromptPod v$ver" `
        --notes-file $ReleaseNotesFile
    if ($LASTEXITCODE -ne 0) {
        throw "gh release create failed with exit code $LASTEXITCODE"
    }
    Write-Host "[PromptPod] GitHub Release v$ver published successfully!"
    Remove-Item -Recurse -Force $relDir -ErrorAction SilentlyContinue
}
