param(
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"

function Test-FileContainsForbiddenPattern {
    param(
        [System.IO.FileInfo]$File,
        [string]$Pattern
    )

    $matches = Select-String -LiteralPath $File.FullName -Pattern $Pattern -AllMatches
    foreach ($match in $matches) {
        [PSCustomObject]@{
            Path = $File.FullName
            Line = $match.LineNumber
            Pattern = $Pattern
            Text = $match.Line.Trim()
        }
    }
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$assetCoreRoot = Join-Path $resolvedRoot "packages\asset-core"
if (-not (Test-Path -LiteralPath $assetCoreRoot)) {
    throw "Asset boundary check could not find packages\asset-core under $resolvedRoot."
}

$forbiddenAssetCorePatterns = @(
    "asharia/asset_pipeline/",
    "asset_texture_import_profile",
    "texture\.profile",
    "kTextureImportProfile",
    "makeTextureImportCatalogSourceFacet"
)

$violations = New-Object System.Collections.Generic.List[object]
$assetCoreFiles = Get-ChildItem -LiteralPath $assetCoreRoot -Recurse -File |
    Where-Object {
        $_.Extension -in @(".cpp", ".hpp", ".h", ".cxx", ".cc", ".cmake", ".txt") -or
        $_.Name -eq "CMakeLists.txt" -or
        $_.Name -eq "asharia.package.json"
    }

foreach ($file in $assetCoreFiles) {
    foreach ($pattern in $forbiddenAssetCorePatterns) {
        $found = @(Test-FileContainsForbiddenPattern -File $file -Pattern $pattern)
        foreach ($entry in $found) {
            $violations.Add($entry)
        }
    }
}

if ($violations.Count -gt 0) {
    Write-Host "Asset boundary check: failed."
    Write-Host "asset-core must stay generic; texture profile interpretation belongs in asset-pipeline/editor adapters."
    foreach ($violation in $violations) {
        $relativePath = [System.IO.Path]::GetRelativePath($resolvedRoot, $violation.Path)
        Write-Host "$relativePath`:$($violation.Line): pattern '$($violation.Pattern)' -> $($violation.Text)"
    }
    exit 1
}

Write-Host "Asset boundary check: passed."
Write-Host "asset-core has no concrete texture profile dependency."
