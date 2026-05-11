param(
    [string]$Root = ".",
    [string]$BaseRef = "HEAD",
    [switch]$Staged,
    [switch]$IncludeUntracked,
    [string]$NoDocsReason
)

$ErrorActionPreference = "Stop"

function Test-MatchesAnyPattern {
    param(
        [string]$Path,
        [string[]]$Patterns
    )

    foreach ($pattern in $Patterns) {
        if ($Path -match $pattern) {
            return $true
        }
    }

    return $false
}

function Get-ChangedFiles {
    param(
        [string]$BaseRef,
        [switch]$Staged,
        [switch]$IncludeUntracked
    )

    $gitArgs = @("diff", "--name-only", "--diff-filter=ACMRT")

    if ($Staged) {
        $gitArgs += "--cached"
    } elseif ($BaseRef -eq "HEAD") {
        $gitArgs += "HEAD"
    } elseif ($BaseRef -match "\.\.\.") {
        $gitArgs += $BaseRef
    } else {
        $gitArgs += "$BaseRef...HEAD"
    }

    $files = @(& git @gitArgs)

    if ($IncludeUntracked -and -not $Staged -and $BaseRef -eq "HEAD") {
        $files += @(& git ls-files --others --exclude-standard)
    }

    return $files |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path

Push-Location $resolvedRoot
try {
    $insideWorkTree = (& git rev-parse --is-inside-work-tree).Trim()
    if ($insideWorkTree -ne "true") {
        throw "Root is not inside a Git work tree: $resolvedRoot"
    }

    $changedFiles = @(Get-ChangedFiles -BaseRef $BaseRef -Staged:$Staged -IncludeUntracked:$IncludeUntracked)

    if ($changedFiles.Count -eq 0) {
        Write-Host "Doc sync check: no changed files."
        exit 0
    }

    $docPatterns = @(
        "^docs/",
        "^README\.md$",
        "^AGENTS\.md$",
        "^\.github/pull_request_template\.md$",
        "^\.github/PULL_REQUEST_TEMPLATE/"
    )

    $docSensitivePatterns = @(
        "^apps/",
        "^engine/",
        "^packages/",
        "^shaders/",
        "^CMakeLists\.txt$",
        "^CMakePresets\.json$",
        "^cmake/",
        "^profiles/",
        "^scripts/",
        "^tools/",
        "^conanfile\.py$",
        "^conan\.lock$",
        "^\.github/workflows/"
    )

    $docChanges = @($changedFiles | Where-Object { Test-MatchesAnyPattern -Path $_ -Patterns $docPatterns })
    $docSensitiveChanges = @($changedFiles | Where-Object { Test-MatchesAnyPattern -Path $_ -Patterns $docSensitivePatterns })

    if ($docSensitiveChanges.Count -eq 0) {
        Write-Host "Doc sync check: no doc-sensitive changes."
        exit 0
    }

    if ($docChanges.Count -gt 0) {
        Write-Host "Doc sync check: passed."
        Write-Host "Doc-sensitive changes: $($docSensitiveChanges.Count)"
        Write-Host "Documentation changes: $($docChanges.Count)"
        exit 0
    }

    if (-not [string]::IsNullOrWhiteSpace($NoDocsReason)) {
        Write-Host "Doc sync check: passed with explicit no-docs reason."
        Write-Host "Reason: $NoDocsReason"
        exit 0
    }

    Write-Host "Doc sync check: failed."
    Write-Host "Doc-sensitive files changed, but no documentation files changed."
    Write-Host ""
    Write-Host "Doc-sensitive files:"
    foreach ($path in $docSensitiveChanges) {
        Write-Host "  $path"
    }
    Write-Host ""
    Write-Host "Update the relevant docs, or rerun with -NoDocsReason when no doc update is correct."
    exit 1
} finally {
    Pop-Location
}
