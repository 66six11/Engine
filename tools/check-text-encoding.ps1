param(
    [string]$Root = ".",
    [switch]$Fix,
    [switch]$VerboseList
)

$ErrorActionPreference = "Stop"

$Utf8Bom = [byte[]](0xEF, 0xBB, 0xBF)
$StrictUtf8NoBom = [System.Text.UTF8Encoding]::new($false, $true)
$Utf8WithBom = [System.Text.UTF8Encoding]::new($true, $false)
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false, $false)

$RequireBomExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
    ".ps1"
) | ForEach-Object { [void]$RequireBomExtensions.Add($_) }

$ForbidBomExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    ".cmake",
    ".json",
    ".md",
    ".py",
    ".slang",
    ".vert",
    ".frag",
    ".comp",
    ".geom",
    ".tesc",
    ".tese",
    ".mesh",
    ".task",
    ".rgen",
    ".rchit",
    ".rmiss",
    ".txt"
) | ForEach-Object { [void]$ForbidBomExtensions.Add($_) }

$ForbidBomFileNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".gitattributes",
    ".gitignore",
    "CMakeLists.txt",
    "CMakePresets.json",
    "CMakeUserPresets.json",
    "conanfile.py"
) | ForEach-Object { [void]$ForbidBomFileNames.Add($_) }

$IgnoredDirectories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    ".git",
    ".idea",
    ".vs",
    "build",
    "out",
    "CMakeFiles",
    "__pycache__"
) | ForEach-Object { [void]$IgnoredDirectories.Add($_) }

function Test-IsIgnoredPath {
    param([System.IO.FileInfo]$File)

    $directory = $File.Directory
    while ($null -ne $directory) {
        if ($IgnoredDirectories.Contains($directory.Name)) {
            return $true
        }

        $directory = $directory.Parent
    }

    return $false
}

function Get-EncodingPolicy {
    param([System.IO.FileInfo]$File)

    if ($RequireBomExtensions.Contains($File.Extension)) {
        return "RequireBom"
    }

    if ($ForbidBomFileNames.Contains($File.Name) -or $ForbidBomExtensions.Contains($File.Extension)) {
        return "ForbidBom"
    }

    return "Ignore"
}

function Test-HasUtf8Bom {
    param([byte[]]$Bytes)

    return $Bytes.Length -ge 3 `
        -and $Bytes[0] -eq $Utf8Bom[0] `
        -and $Bytes[1] -eq $Utf8Bom[1] `
        -and $Bytes[2] -eq $Utf8Bom[2]
}

function Read-StrictUtf8Text {
    param([byte[]]$Bytes)

    if (Test-HasUtf8Bom -Bytes $Bytes) {
        return $StrictUtf8NoBom.GetString($Bytes, 3, $Bytes.Length - 3)
    }

    return $StrictUtf8NoBom.GetString($Bytes)
}

function Write-Utf8Bom {
    param(
        [System.IO.FileInfo]$File,
        [string]$Text
    )

    [System.IO.File]::WriteAllText($File.FullName, $Text, $Utf8WithBom)
}

function Write-Utf8NoBom {
    param(
        [System.IO.FileInfo]$File,
        [string]$Text
    )

    [System.IO.File]::WriteAllText($File.FullName, $Text, $Utf8NoBom)
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$files = Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
    Where-Object { -not (Test-IsIgnoredPath -File $_) } |
    ForEach-Object {
        [PSCustomObject]@{
            File = $_
            Policy = Get-EncodingPolicy -File $_
        }
    } |
    Where-Object { $_.Policy -ne "Ignore" }

$checked = 0
$fixed = 0
$missingBom = [System.Collections.Generic.List[string]]::new()
$unexpectedBom = [System.Collections.Generic.List[string]]::new()
$invalidUtf8 = [System.Collections.Generic.List[string]]::new()

foreach ($entry in $files) {
    $checked += 1
    $file = $entry.File
    $policy = $entry.Policy
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $hasBom = Test-HasUtf8Bom -Bytes $bytes

    try {
        $text = Read-StrictUtf8Text -Bytes $bytes

        if ($policy -eq "RequireBom" -and -not $hasBom) {
            $missingBom.Add($file.FullName)
            if ($Fix) {
                Write-Utf8Bom -File $file -Text $text
                $fixed += 1
                Write-Host "ADD-BOM    $($file.FullName)"
            } elseif ($VerboseList) {
                Write-Host "MISSING    $($file.FullName)"
            }
            continue
        }

        if ($policy -eq "ForbidBom" -and $hasBom) {
            $unexpectedBom.Add($file.FullName)
            if ($Fix) {
                Write-Utf8NoBom -File $file -Text $text
                $fixed += 1
                Write-Host "REMOVE-BOM $($file.FullName)"
            } elseif ($VerboseList) {
                Write-Host "UNEXPECTED $($file.FullName)"
            }
            continue
        }

        if ($VerboseList) {
            Write-Host "OK         $($file.FullName)"
        }
    } catch [System.Text.DecoderFallbackException] {
        $invalidUtf8.Add($file.FullName)
        Write-Host "INVALID    $($file.FullName)"
    }
}

$remainingMissingBom = if ($Fix) { $missingBom.Count - ($fixed - $unexpectedBom.Count) } else { $missingBom.Count }
$remainingUnexpectedBom = if ($Fix) { $unexpectedBom.Count - ($fixed - $missingBom.Count) } else { $unexpectedBom.Count }

if ($remainingMissingBom -lt 0) {
    $remainingMissingBom = 0
}

if ($remainingUnexpectedBom -lt 0) {
    $remainingUnexpectedBom = 0
}

Write-Host "Checked files: $checked"
Write-Host "Missing required UTF-8 BOM: $remainingMissingBom"
Write-Host "Unexpected UTF-8 BOM: $remainingUnexpectedBom"
Write-Host "Invalid UTF-8: $($invalidUtf8.Count)"

if ($Fix) {
    Write-Host "Fixed files: $fixed"
}

if ($invalidUtf8.Count -gt 0 -or $remainingMissingBom -gt 0 -or $remainingUnexpectedBom -gt 0) {
    if (-not $Fix) {
        Write-Host ""
        Write-Host "Run with -Fix to add or remove UTF-8 BOM according to the repository policy."
    }

    exit 1
}

exit 0
