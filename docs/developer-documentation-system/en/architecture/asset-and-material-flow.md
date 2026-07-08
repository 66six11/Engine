# Architecture: Asset, Material, And Shader Product Flow

## Purpose

This document describes the current state ownership and data flow of the asset, material, and shader packages. It does not present older plans as current implementation.

## Current Modules

| Module | Target | Current responsibility |
|---|---|---|
| Asset core | `asharia::asset_core` | GUID, asset type, source record, product record, dependency, catalog |
| Asset core IO | `asharia::asset_core_io` | Metadata document read/write |
| Project core | `asharia::project_core`, `asharia::project_core_io` | Project descriptor and IO |
| Asset pipeline | `asharia::asset_pipeline` | Source discovery, snapshot, import plan, product execution, manifest IO |
| Asset processor | `asharia-asset-processor` | CLI wrapper for dry-run and product execution smokes |
| Material core | `asharia::material_core` | Material resource signature, pipeline key, validation/hash |
| Shader authoring | `asharia::shader_authoring` | Authored shader document model |
| Shader Slang | `asharia::shader_slang` | Slang reflection and `asharia_add_slang_shader()` helper |
| Material instance | `asharia::material_instance` | `.amat` document, override resolver, IO |
| Shader/material adapter | `asharia::shader_material_adapter` | Slang resource signature to material signature conversion |
| Resource runtime | `asharia::resource_runtime` | Runtime resource registry keyed by asset identity |

## Source Asset Ownership

`asset-core` owns stable identity and catalog facts:

- `AssetGuid` is the stable source identity.
- `AssetTypeId` identifies the type class.
- `SourceAssetRecord` stores source path, importer id/name/version, source hash, and settings hash.
- `AssetCatalog` owns an in-memory collection of source records and validates duplicate GUID/path rules.

`asset-core` does not execute import work and does not own generated product bytes.

## Import Planning

`asset-pipeline` plans import work from:

- discovered source assets,
- source snapshots,
- existing product manifest,
- target profile,
- importer/tool version dependencies.

`planAssetImports()` returns:

- `requests` for sources that need import,
- `cacheHits` for still-valid products,
- diagnostics for invalid source, duplicate source, metadata drift, invalid product manifest, or target profile problems.

Plan success is `AssetImportPlanResult::succeeded()`, which fails when any diagnostic severity is `Error`.

## Product Execution

`executeAssetProducts()` consumes:

- a completed import plan,
- source bytes,
- dependency product bytes,
- output root,
- manifest output path.

It returns:

- written product records and file paths,
- cache hits,
- updated manifest,
- diagnostics,
- `manifestWritten`.

Product execution owns generated output. Source directories must not be treated as generated cache ownership.

## Material And Shader Flow

1. `shader-authoring` represents authored shader/material intent.
2. `shader-slang` compiles Slang shader entries and can emit reflection metadata.
3. `shader-material-adapter` converts `ShaderResourceSignature` to `material::MaterialResourceSignature`.
4. `material-core` validates signatures and builds hashable `MaterialPipelineKey`.
5. `material-instance` resolves `.amat` overrides against a material type document and reports stale/mismatched overrides.
6. `renderer-basic-vulkan` consumes compiled shader outputs and material/render state facts when recording backend work.

## Dependency Constraints

- Asset identity and metadata live below importer execution.
- Asset pipeline may depend on material instance and shader authoring because import execution can produce material/shader products.
- Runtime resource lookup should depend on asset identity, not source file paths.
- Material core must not depend on Vulkan types; backend pipeline creation is owned by renderer/RHI code.

`future`: a full hot-reload resource manager should consume product manifests and runtime registry data without making editor UI own runtime resource lifetime.

## Validation

Package-local tests:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/asset-core -B build/cmake/package-asset-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-asset-core-tests-msvc-debug && ctest --test-dir build/cmake/package-asset-core-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/asset-pipeline -B build/cmake/package-asset-pipeline-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-asset-pipeline-tests-msvc-debug && ctest --test-dir build/cmake/package-asset-pipeline-tests-msvc-debug --output-on-failure"
```

Tool smokes:

```powershell
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-dry-run
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-product-execution
```

Checkpoints:

- Catalog rejects duplicate GUID/path unless the explicit relocation policy allows the path change.
- Import planning reports diagnostics instead of writing products for invalid sources.
- Product execution reports missing source bytes and invalid output roots as diagnostics.
