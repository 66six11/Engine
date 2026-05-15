param(
    [string]$Root = ".",
    [switch]$IncludeDocs,
    [switch]$IncludeAllText
)

$ErrorActionPreference = "Stop"

$CodeExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
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
    ".cmake",
    ".json",
    ".ps1",
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
    ".rmiss"
) | ForEach-Object { [void]$CodeExtensions.Add($_) }

$DocExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    ".md",
    ".txt"
) | ForEach-Object { [void]$DocExtensions.Add($_) }

$TextFileNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".gitattributes",
    ".gitignore",
    "CMakeLists.txt",
    "conanfile.py"
) | ForEach-Object { [void]$TextFileNames.Add($_) }

function Get-LineCount {
    param([string]$Path)

    $count = 0
    foreach ($null in [System.IO.File]::ReadLines($Path)) {
        ++$count
    }
    return $count
}

function Get-LineGroup {
    param([string]$Path)

    $name = [System.IO.Path]::GetFileName($Path)
    if ($name -ieq "CMakeLists.txt") {
        return "cmake"
    }
    if ($name.StartsWith(".", [System.StringComparison]::Ordinal)) {
        return $name
    }

    $extension = [System.IO.Path]::GetExtension($Path)
    if ([string]::IsNullOrWhiteSpace($extension)) {
        return "(no extension)"
    }
    return $extension.TrimStart(".").ToLowerInvariant()
}

function Test-ShouldCount {
    param([string]$Path)

    $name = [System.IO.Path]::GetFileName($Path)
    $extension = [System.IO.Path]::GetExtension($Path)

    if ($TextFileNames.Contains($name) -or $CodeExtensions.Contains($extension)) {
        return $true
    }
    if ($IncludeDocs -and $DocExtensions.Contains($extension)) {
        return $true
    }
    if ($IncludeAllText -and ($DocExtensions.Contains($extension) -or $TextFileNames.Contains($name))) {
        return $true
    }
    return $false
}

$RepoRoot = (Resolve-Path -LiteralPath $Root).Path
$trackedOutput = & git -C $RepoRoot ls-files -z
if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed for '$RepoRoot'"
}

$relativePaths = $trackedOutput -split "`0" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$rows = foreach ($relativePath in $relativePaths) {
    if (-not (Test-ShouldCount $relativePath)) {
        continue
    }

    $fullPath = Join-Path $RepoRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        continue
    }

    [pscustomobject]@{
        Group = Get-LineGroup $relativePath
        File = $relativePath
        Lines = Get-LineCount $fullPath
    }
}

$summary = $rows |
    Group-Object Group |
    Sort-Object @{ Expression = { ($_.Group | Measure-Object Lines -Sum).Sum }; Descending = $true }, Name |
    ForEach-Object {
        $lineSum = ($_.Group | Measure-Object Lines -Sum).Sum
        [pscustomobject]@{
            Group = $_.Name
            Files = $_.Count
            Lines = $lineSum
        }
    }

$summary | Format-Table -AutoSize

$totalFiles = ($rows | Measure-Object).Count
$totalLines = ($rows | Measure-Object Lines -Sum).Sum
Write-Host ""
Write-Host "Total files: $totalFiles"
Write-Host "Total lines: $totalLines"
