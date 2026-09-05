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

    # Deleting an obsolete path is still an architecture-sensitive change.
    $gitArgs = @("diff", "--name-only", "--diff-filter=ACDMRT")

    if ($Staged) {
        $gitArgs += "--cached"
    } elseif ($BaseRef -eq "HEAD") {
        $gitArgs += "HEAD"
    } elseif ($BaseRef -match "\.\.") {
        $gitArgs += $BaseRef
    } else {
        $gitArgs += "$BaseRef...HEAD"
    }

    $files = @(& git -c core.quotepath=false @gitArgs)

    if ($IncludeUntracked -and -not $Staged) {
        $files += @(& git -c core.quotepath=false ls-files --others --exclude-standard)
    }

    return $files |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_.Trim('"') -replace "\\", "/" } |
        Sort-Object -Unique
}

function Add-DocHints {
    param(
        [string[]]$ChangedFiles,
        [System.Collections.Generic.List[string]]$Docs
    )

    if ($ChangedFiles | Where-Object { $_ -match "^packages/rendergraph/" }) {
        Add-Unique $Docs "docs/architecture/flow.md"
        Add-Unique $Docs "docs/rendergraph/mvp.md"
        Add-Unique $Docs "docs/rendergraph/rhi-boundary.md"
        Add-Unique $Docs "docs/rendergraph/programmable-pipeline.md"
        Add-Unique $Docs "docs/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^engine/core/" }) {
        Add-Unique $Docs "docs/architecture/overview.md"
        Add-Unique $Docs "docs/standards/coding.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^engine/platform/" -or
            $_ -match "^packages/window-glfw/" -or
            $_ -match "^packages/profiling/"
        }) {
        Add-Unique $Docs "docs/architecture/flow.md"
        Add-Unique $Docs "docs/architecture/frame-loop-threading.md"
        Add-Unique $Docs "docs/systems/performance-profiling.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/asset-core/" -or
            $_ -match "^packages/asset-pipeline/" -or
            $_ -match "^packages/project-core/" -or
            $_ -match "^packages/resource-runtime/" -or
            $_ -match "^tools/asset-processor/"
        }) {
        Add-Unique $Docs "docs/systems/asset-architecture.md"
        Add-Unique $Docs "packages/resource-runtime/README.md"
        Add-Unique $Docs "docs/architecture/project-build-and-launch.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/material-core/" -or
            $_ -match "^packages/material-instance/" -or
            $_ -match "^packages/shader-authoring/" -or
            $_ -match "^packages/shader-slang/" -or
            $_ -match "^packages/shader-material-adapter/" -or
            $_ -match "^shaders/"
        }) {
        Add-Unique $Docs "docs/systems/shader-material-authoring.md"
        Add-Unique $Docs "docs/specs/shader-v2.md"
        Add-Unique $Docs "docs/specs/material-runtime-products-v2.md"
        Add-Unique $Docs "docs/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/archive/" -or
            $_ -match "^packages/schema/" -or
            $_ -match "^packages/cpp-binding/" -or
            $_ -match "^packages/persistence/" -or
            $_ -match "^packages/reflection/" -or
            $_ -match "^packages/serialization/"
        }) {
        Add-Unique $Docs "docs/systems/reflection-serialization.md"
        Add-Unique $Docs "docs/standards/naming.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/scene-core/" -or
            $_ -match "^packages/resource-runtime/"
        }) {
        Add-Unique $Docs "docs/systems/scene-world.md"
        Add-Unique $Docs "packages/resource-runtime/README.md"
    }

    if ($ChangedFiles | Where-Object {
            $_ -match "^packages/[^/]+/CMakeLists\.txt$" -or
            $_ -match "^packages/[^/]+/asharia\.package\.json$" -or
            $_ -match "^CMakeLists\.txt$" -or
            $_ -match "^cmake/"
        }) {
        Add-Unique $Docs "docs/architecture/package-first.md"
        Add-Unique $Docs "docs/architecture/flow.md"
        Add-Unique $Docs "docs/workflow/build.md"
        Add-Unique $Docs "docs/workflow/package-standalone-build.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^tools/" -or $_ -match "^scripts/" }) {
        Add-Unique $Docs "docs/workflow/build.md"
        Add-Unique $Docs "docs/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^packages/rhi-vulkan/" }) {
        Add-Unique $Docs "docs/architecture/flow.md"
        Add-Unique $Docs "docs/rendergraph/rhi-boundary.md"
        Add-Unique $Docs "docs/architecture/render-layer.md"
        Add-Unique $Docs "docs/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^packages/renderer-basic/" }) {
        Add-Unique $Docs "docs/architecture/render-layer.md"
        Add-Unique $Docs "docs/architecture/flow.md"
        Add-Unique $Docs "docs/rendergraph/programmable-pipeline.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^apps/editor/" }) {
        Add-Unique $Docs "docs/architecture/editor.md"
        Add-Unique $Docs "docs/architecture/editor-ui-scripting.md"
        Add-Unique $Docs "docs/workflow/review.md"
    }

    if ($ChangedFiles | Where-Object { $_ -match "^apps/studio/" }) {
        Add-Unique $Docs "apps/studio/docs/architecture/README.md"
        Add-Unique $Docs "docs/architecture/managed-extension-model.md"
        Add-Unique $Docs "docs/architecture/project-build-and-launch.md"
        Add-Unique $Docs "docs/workflow/review.md"
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

    $nativeBuildPatterns = @(
        "^apps/editor/",
        "^apps/sample-viewer/",
        "^engine/",
        "^packages/",
        "^tools/asset-processor/",
        "^shaders/",
        "^CMakeLists\.txt$",
        "^CMakePresets\.json$",
        "^cmake/",
        "^profiles/",
        "^scripts/bootstrap-conan\.ps1$",
        "^conanfile\.py$",
        "^conan\.lock$",
        "^\.clang-tidy$",
        "^\.github/workflows/native-code-quality\.yml$"
    )
    $renderingPatterns = @(
        "^apps/sample-viewer/",
        "^packages/rendergraph/",
        "^packages/rhi-vulkan/",
        "^packages/renderer-basic/",
        "^shaders/"
    )
    $editorNativePatterns = @("^apps/editor/")
    $studioPatterns = @("^apps/studio/")
    $hostRuntimePatterns = @("^engine/host-runtime/", "^packages/project-bootstrap/")
    $targetTruthPatterns = @(
        "(^|/)asharia\.package\.json$",
        "(^|/)CMakeLists\.txt$",
        "^CMakePresets\.json$",
        "^cmake/",
        "^tools/check_target_dependency_truth\.py$"
    )
    $designPatterns = $nativeBuildPatterns + @(
        "^scripts/",
        "^tools/",
        "^docs/architecture/",
        "^docs/planning/",
        "^apps/studio/docs/architecture/",
        "^apps/studio/docs/adr/"
    )

    $nonDocumentationChanges = @($changedFiles | Where-Object {
            $_ -notmatch "\.(md|rst)$" -and $_ -notmatch "(^|/)docs/"
        })
    $requiresNativeBuild = [bool]($nonDocumentationChanges | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $nativeBuildPatterns
        })
    $requiresRenderingSmokes = [bool]($nonDocumentationChanges | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $renderingPatterns
        })
    $requiresEditorNativeSmokes = [bool]($nonDocumentationChanges | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $editorNativePatterns
        })
    $requiresStudioTests = [bool]($nonDocumentationChanges | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $studioPatterns
        })
    $requiresHostRuntimeTests = [bool]($nonDocumentationChanges | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $hostRuntimePatterns
        })
    $requiresTargetTruth = [bool]($nonDocumentationChanges | Where-Object {
            Test-MatchesAnyPattern -Path $_ -Patterns $targetTruthPatterns
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
    Write-Host "  python -m unittest discover -s tools\tests -p `"test_*.py`""
    Write-Host "  python tools\check_package_topology.py"
    Write-Host "  python tools\check_package_contracts.py"
    Write-Host "  git diff --check"

    if ($requiresNativeBuild) {
        Write-Host ""
        Write-Host "Native build gates:"
        Write-Host "  cmd /c `"build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug`""
        $tidyCommand = "python tools\run_clang_tidy.py --changed"
        if ($Staged) {
            $tidyCommand += " --staged"
        } elseif ($BaseRef -ne "HEAD") {
            $tidyCommand += " --base-ref `"$BaseRef`""
        }
        if ($IncludeUntracked -and -not $Staged) {
            $tidyCommand += " --include-untracked"
        }
        Write-Host "  cmd /c `"build\conan\clangcl-debug\Debug\generators\conanbuild.bat && $tidyCommand`""
        Write-Host "  cmd /c `"build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug`""
    }

    if ($requiresTargetTruth) {
        Write-Host ""
        Write-Host "Configured target dependency truth gate:"
        Write-Host '  python tools\check_target_dependency_truth.py --root . --prepare-query build\cmake\msvc-debug-tests'
        Write-Host '  cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests"'
        Write-Host '  $replyIndex = Get-ChildItem build\cmake\msvc-debug-tests\.cmake\api\v1\reply\index-*.json | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName'
        Write-Host '  python tools\check_target_dependency_truth.py --root . --reply-index $replyIndex --configuration Debug'
    }

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
        Write-Host "  Run the smoke list in docs\workflow\review.md for both clangcl-debug and msvc-debug."
    }

    if ($requiresEditorNativeSmokes) {
        Write-Host ""
        Write-Host "Native editor smoke gate required for this change range:"
        Write-Host "  Run the applicable editor smoke commands in docs\workflow\review.md for both clangcl-debug and msvc-debug."
    }

    if ($requiresStudioTests) {
        Write-Host ""
        Write-Host "Studio managed gates:"
        Write-Host "  dotnet build apps\studio\Asharia.Studio.sln -c Release"
        Write-Host "  dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter `"SceneView|ViewportNative|Composition`""
        Write-Host "  dotnet test apps\studio\Asharia.Studio.sln -c Release --no-build --blame-hang --blame-hang-timeout 10m"
    }

    if ($requiresHostRuntimeTests) {
        Write-Host ""
        Write-Host "Host Runtime focused gate required:"
        Write-Host "  Run the ProcessScope and project-bootstrap CTest commands in docs\workflow\review.md for both compilers."
    }

    if ($requiresDesignReview) {
        Write-Host ""
        Write-Host "Architecture/design review required:"
        Write-Host "  Record current evidence, owner/lifetime/thread/data/error contracts, Foundation prerequisite,"
        Write-Host "  earliest safe and latest required integration point, and vertical-slice exit evidence."
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
        Invoke-CheapGate "package topology check" {
            python tools\check_package_topology.py
        }
        Invoke-CheapGate "package contract check" {
            python tools\check_package_contracts.py
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
