param(
    [string]$Root = ".",
    [string]$BaseRef = "HEAD",
    [switch]$Staged,
    [switch]$IncludeUntracked,
    [switch]$RunCheapGates
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

function Add-Unique {
    param(
        [System.Collections.Generic.List[string]]$Items,
        [string]$Value
    )

    if (-not $Items.Contains($Value)) {
        $Items.Add($Value)
    }
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
        ForEach-Object { $_ -replace "\\", "/" } |
        Sort-Object -Unique
}

function Add-DocHints {
    param(
        [string[]]$ChangedFiles,
        [System.Collections.Generic.List[string]]$Docs
    )

    if ($ChangedFiles | Where-Object { $_ -match "^packages/rendergraph/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/rendergraph-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/api/rendergraph-api.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/rendergraph-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/api/rendergraph-api.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/rendering-and-frame-flow.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^engine/core/" -or
            $_ -match "^engine/platform/" -or
            $_ -match "^packages/window-glfw/" -or
            $_ -match "^packages/profiling/"
        }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/platform-window-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/platform-window-design.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/asset-core/" -or
            $_ -match "^packages/asset-pipeline/" -or
            $_ -match "^packages/project-core/" -or
            $_ -match "^packages/resource-runtime/" -or
            $_ -match "^tools/asset-processor/"
        }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/asset-pipeline-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/asset-and-material-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/asset-pipeline-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/asset-and-material-flow.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/material-core/" -or
            $_ -match "^packages/material-instance/" -or
            $_ -match "^packages/shader-authoring/" -or
            $_ -match "^packages/shader-slang/" -or
            $_ -match "^packages/shader-material-adapter/"
        }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/material-shader-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/asset-and-material-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/material-shader-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/asset-and-material-flow.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/archive/" -or
            $_ -match "^packages/schema/" -or
            $_ -match "^packages/cpp-binding/" -or
            $_ -match "^packages/persistence/" -or
            $_ -match "^packages/reflection/" -or
            $_ -match "^packages/serialization/"
        }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/reflection-serialization-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/reflection-serialization-design.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/scene-core/" -or
            $_ -match "^packages/resource-runtime/"
        }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/scene-resource-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/scene-resource-design.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/[^/]+/CMakeLists\.txt$" -or
            $_ -match "^packages/[^/]+/asharia\.package\.json$" -or
            $_ -match "^CMakeLists\.txt$" -or
            $_ -match "^cmake/"
        }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/overview.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/guides/add-package-guide.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/workflow/build.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/overview.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/guides/add-package-guide.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/workflow/build.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^tools/" -or $_ -match "^scripts/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/workflow/review.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/workflow/build.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/workflow/review.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/workflow/build.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^shaders/" -or $_ -match "^packages/shader-slang/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/asset-and-material-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/material-shader-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/workflow/review.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/asset-and-material-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/data-model-and-persistence.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/material-shader-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^packages/rhi-vulkan/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/rhi-vulkan-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/api/rhi-vulkan-api.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/rhi-vulkan-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/api/rhi-vulkan-api.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^packages/renderer-basic/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/renderer-basic-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/package-dependency-map.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/rendering-and-frame-flow.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/renderer-basic-design.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^apps/editor/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/editor-runtime-boundaries.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/editor-host-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/workflow/review.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/editor-runtime-boundaries.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/editor-host-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^apps/studio/" }) {
        Add-Unique $Docs "docs/developer-documentation-system/zh/architecture/editor-runtime-boundaries.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/design/studio-shell-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/zh/workflow/review.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/architecture/editor-runtime-boundaries.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/design/studio-shell-design.md"
        Add-Unique $Docs "docs/developer-documentation-system/en/workflow/review.md"
    }
}

function Get-ChangedPackageDirs {
    param([string[]]$ChangedFiles)

    return $ChangedFiles |
        Where-Object { $_ -match "^packages/([^/]+)/" } |
        ForEach-Object {
            $match = [regex]::Match($_, "^packages/([^/]+)/")
            "packages/$($match.Groups[1].Value)"
        } |
        Sort-Object -Unique
}

function Get-PackageTestCommands {
    param([string[]]$PackageDirs)

    $commands = New-Object System.Collections.Generic.List[string]
    foreach ($packageDir in $PackageDirs) {
        $manifestPath = Join-Path $packageDir "asharia.package.json"
        if (-not (Test-Path -LiteralPath $manifestPath)) {
            continue
        }

        $manifest = Get-Content -Raw -Encoding utf8 -LiteralPath $manifestPath | ConvertFrom-Json
        if ($null -eq $manifest.testTargets -or $manifest.testTargets.Count -eq 0) {
            continue
        }

        $packageName = Split-Path -Leaf $packageDir
        $buildDir = "build\cmake\package-$packageName-tests-msvc-debug"
        $command = "cmd /c `"build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S $packageDir -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build $buildDir && ctest --test-dir $buildDir --output-on-failure`""
        Add-Unique $commands $command
    }

    return $commands
}

function Invoke-CheapGate {
    param(
        [string]$Label,
        [scriptblock]$Command
    )

    Write-Host ""
    Write-Host "Running $Label..."
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
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
        Write-Host "Pre-PR: no changed files."
        exit 0
    }

    $renderingPatterns = @(
        "^apps/",
        "^packages/rendergraph/",
        "^packages/rhi-vulkan/",
        "^packages/renderer-basic/",
        "^shaders/"
    )
    $designPatterns = $renderingPatterns + @(
        "^CMakeLists\.txt$",
        "^cmake/",
        "^scripts/",
        "^tools/"
    )

    $requiresRenderingSmokes = [bool]($changedFiles | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $renderingPatterns
        })
    $requiresDesignReview = [bool]($changedFiles | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $designPatterns
        })
    $assetBoundaryPatterns = @(
        "^packages/asset-core/",
        "^packages/asset-pipeline/",
        "^apps/editor/src/editor_asset_catalog",
        "^apps/editor/src/panels/asset_browser_panel",
        "^tools/check-asset-boundaries\.ps1$"
    )
    $requiresAssetBoundaryCheck = [bool]($changedFiles | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $assetBoundaryPatterns
        })

    $docHints = New-Object System.Collections.Generic.List[string]
    Add-DocHints -ChangedFiles $changedFiles -Docs $docHints

    $packageDirs = @(Get-ChangedPackageDirs -ChangedFiles $changedFiles)
    $packageTestCommands = @(Get-PackageTestCommands -PackageDirs $packageDirs)

    Write-Host "Pre-PR changed files:"
    foreach ($path in $changedFiles) {
        Write-Host "  $path"
    }

    Write-Host ""
    Write-Host "Baseline gates:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1"
    $docSyncCommand = "powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1"
    if ($IncludeUntracked) {
        $docSyncCommand += " -IncludeUntracked"
    }
    Write-Host "  $docSyncCommand"
    Write-Host "  git diff --check"
    Write-Host "  cmd /c `"build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug`""
    Write-Host "  cmd /c `"build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug`""

    if ($packageTestCommands.Count -gt 0) {
        Write-Host ""
        Write-Host "Package-local test gates:"
        foreach ($command in $packageTestCommands) {
            Write-Host "  $command"
        }
    }

    if ($requiresAssetBoundaryCheck) {
        Write-Host ""
        Write-Host "Asset boundary gate:"
        Write-Host "  powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1"
    }

    if ($requiresRenderingSmokes) {
        Write-Host ""
        Write-Host "Rendering/runtime smoke gate required for this change range:"
        Write-Host "  Run the smoke list in docs\developer-documentation-system\zh\workflow\review.md or docs\developer-documentation-system\en\workflow\review.md for both clangcl-debug and msvc-debug."
    }

    if ($requiresDesignReview) {
        Write-Host ""
        Write-Host "Design review required:"
        Write-Host "  Check package boundaries, RenderGraph/RHI separation, target dependencies, and doc sync."
    }

    if ($docHints.Count -gt 0) {
        Write-Host ""
        Write-Host "Documentation candidates to check:"
        foreach ($doc in $docHints) {
            Write-Host "  $doc"
        }
    }

    if ($RunCheapGates) {
        Invoke-CheapGate "encoding check" {
            powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
        }
        Invoke-CheapGate "doc sync check" {
            if ($IncludeUntracked) {
                powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
            } else {
                powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
            }
        }
        Invoke-CheapGate "git diff whitespace check" {
            git diff --check
        }
        if ($requiresAssetBoundaryCheck) {
            Invoke-CheapGate "asset boundary check" {
                powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
            }
        }
    }
} finally {
    Pop-Location
}
