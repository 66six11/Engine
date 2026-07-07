# Detailed Design: Asset Catalog, Import Planning, And Product Execution

## Background

The current asset system is split across `asset-core`, `project-core`, `asset-pipeline`, `resource-runtime`, and `tools/asset-processor`. The code separates source assets, `.ameta` metadata, project descriptors, product manifests, and runtime resource state so editor or renderer code does not directly own imported product lifetime.

## Goals

- Use `AssetCatalog` to maintain source asset records.
- Use `scanAssetSourceTree()` to discover source/metadata pairs under a source root.
- Use `planAssetImports()` to detect missing products and source/settings/importer/dependency/profile drift.
- Use `executeAssetProducts()` to write product files and product manifests.
- Use `RuntimeResourceRegistry` to track pending/ready/failed runtime resources.

## Non-Goals

- Do not read project directories from `asset-core`.
- Do not hold GPU resources in `asset-pipeline`.
- Do not let editor panels directly mutate product manifests.
- Do not let the runtime registry rerun importers.

## Current Constraints

- `asset-core` depends on `core` and `archive`; `asset-pipeline` depends on `archive`, `asset-core`, `material-instance`, and `shader-authoring`.
- `project-core` only describes project descriptors and IO.
- `resource-runtime` only depends on `asset-core`.
- `asharia-asset-processor` is the command-line entry point and supports `dry-run`, `execute`, and smoke commands.

## Overall Design

The asset flow has four stages:

1. `project-core` reads `asharia.project.json` and returns source roots and discovery settings.
2. `asset-pipeline` scans source trees, reads `.ameta`, and creates discovered sources and snapshots.
3. `planAssetImports()` compares current sources with the existing product manifest and returns import requests plus cache hits.
4. `executeAssetProducts()` reads source bytes and dependency product bytes, writes product files, and produces a new manifest.

Runtime code only consumes `AssetProductRecord`. `RuntimeResourceRegistry::request()` creates a ticket, and `markReady()` or `markFailed()` closes that ticket. The generation prevents stale completions from overwriting newer requests.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `packages/asset-core/include/asharia/asset_core/asset_metadata.hpp` | `SourceAssetRecord`, importer id, settings hash, and source path validation |
| `packages/asset-core/include/asharia/asset_core/asset_catalog.hpp` | source record add/update/remove/find |
| `packages/asset-core/include/asharia/asset_core/asset_product.hpp` | product key, dependency, and product record |
| `packages/project-core/include/asharia/project/project_descriptor.hpp` | project id, source root, and discovery descriptor |
| `packages/asset-pipeline/include/asharia/asset_pipeline/asset_source_scan.hpp` | source tree scan |
| `packages/asset-pipeline/include/asharia/asset_pipeline/asset_import_planning.hpp` | import plan and cache hit |
| `packages/asset-pipeline/include/asharia/asset_pipeline/asset_product_execution.hpp` | product write and manifest update |
| `packages/resource-runtime/include/asharia/resource_runtime/runtime_resource_registry.hpp` | runtime request/ready/failed state |
| `tools/asset-processor/src/main.cpp` | CLI argument parsing and command dispatch |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `SourceAssetRecord` | `guid`, `assetType`, `sourcePath`, `importerId`, `sourceHash`, `settingsHash` | stable source asset metadata |
| `AssetImportRequest` | `source`, `settings`, `dependencies`, `productKey`, `reason` | work item that must be imported |
| `AssetImportCacheHit` | `source`, `dependencies`, `product` | product that remains reusable |
| `AssetProductExecutionRequest` | `plan`, `sourceBytes`, `dependencyProductBytes`, `productOutputRoot` | product execution input |
| `RuntimeResourceRecord` | `key`, `state`, `generation`, `expectedProductKey`, `product`, `failure` | runtime resource state |

## API Design

Public APIs use value structs and `Result`/diagnostics instead of exceptions.

- `AssetCatalog::addSource/updateSource/removeSource()` mutates the source catalog.
- `scanAssetSourceTree(request)` returns entries and diagnostics.
- `planAssetImports(sources, snapshots, manifest, targetProfile, options)` returns a plan; `succeeded()` is true only when no error diagnostics exist.
- `executeAssetProducts(request)` returns written products, cache hits, a new manifest, and diagnostics.
- `RuntimeResourceRegistry::request/markReady/markFailed/resolveProductRecords()` maintains a generation-safe state machine.

## Key Flows

### Normal Flow

1. Read project descriptor.
2. Scan source root and `.ameta`.
3. Build `SourceAssetRecord` from metadata.
4. Read existing product manifest.
5. Create import plan.
6. Execute importers for requests.
7. Write product files and manifest.
8. Runtime requests product and marks it ready.

### Failure Flow

- Invalid source path: `validateAssetSourcePath()` or scan diagnostics reports it.
- Missing metadata or orphan metadata: `scanAssetSourceTree()` returns diagnostics.
- Invalid target profile: `planAssetImports()` returns `InvalidTargetProfile`.
- Missing source bytes or hash drift: `executeAssetProducts()` returns diagnostics.
- Stale runtime ticket: `RuntimeResourceRegistry` returns a generation mismatch error.

### Boundary Flow

- `AssetCatalogRelocationPolicy::RejectPathChange` prevents the same GUID from silently moving.
- `planAssetImports()` allows cache hits and does not force every source to reimport.
- The runtime registry does not read products from disk; callers supply product records.

## Lifetime

`AssetCatalog` is an in-memory catalog. Project descriptors, metadata, and product manifests are read/written through IO APIs. Import plans are one-shot value results. Runtime resource tickets are valid only for the current generation; ready/failed resources cannot be completed again with the same ticket.

## Error Handling

Asset planning and execution use diagnostics vectors. Catalog, descriptor, metadata, and runtime registry APIs use `Result`/`VoidResult`. Error messages should include source path, relative product path, target profile, or resource key.

## Test Plan

```powershell
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-dry-run
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-product-execution
```

Package-local tests:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/asset-core -B build/cmake/package-asset-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-asset-core-tests-msvc-debug && ctest --test-dir build/cmake/package-asset-core-tests-msvc-debug --output-on-failure"
```

## Risks

- Product manifest and source metadata drift can create stale cache hits. Mitigation: plan diagnostics must preserve the drift reason.
- Ignoring runtime generation can let an old request overwrite a new request. Mitigation: all completion APIs require a ticket.
- Editor reimport that writes manifests directly would bypass plan/execute diagnostics. Mitigation: editor should issue requests and let the pipeline execute them.
