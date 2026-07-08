# Architecture: Data Model And Persistence

## Purpose

This document describes the current data-model, archive, schema, reflection, serialization, persistence, asset metadata, project metadata, material, and shader-authoring boundaries. It answers who owns stored state, which package can transform that state, and where validation happens.

It does not describe UI workflows or renderer command recording. Future work is labeled with `future`.

## Layers

| Layer | Packages | Current responsibility |
|---|---|---|
| Archive values | `packages/archive` | JSON-like `ArchiveValue` data and JSON read/write helpers used by asset/project/material IO. |
| Schema contracts | `packages/schema` | `TypeSchema`, fields, registry, built-in schema registration, and schema document validation. |
| C++ binding contracts | `packages/cpp-binding` | C++ reflected-type metadata that binds compiled C++ types to schema fields. |
| Persistence | `packages/persistence` | Schema-aware archive loading, saving, migration, and field validation. |
| Runtime reflection | `packages/reflection` | Runtime `TypeRegistry` and type metadata independent of archive persistence. |
| Serialization | `packages/serialization` | Reflection-based text/archive serialization and migration helpers. |
| Asset/project metadata | `packages/asset-core`, `packages/project-core` | Stable asset and project identifiers, metadata documents, catalogs, and IO targets. |
| Material/shader authoring | `packages/material-core`, `packages/material-instance`, `packages/shader-authoring`, `packages/shader-slang`, `packages/shader-material-adapter` | Material signatures, material instances, `.ashader` parsing, generated Slang, Slang reflection, and reflection-to-material translation. |
| Runtime resource resolution | `packages/resource-runtime` | Runtime tickets and status derived from asset product records. |

## State Owners

| State | Owner | Format or API | Notes |
|---|---|---|---|
| Archive tree | `packages/archive` | `asharia::archive::ArchiveValue` | Used when document IO needs a JSON-like tree without reflection. |
| Schema registry | `packages/schema` | `asharia::schema::SchemaRegistry` | Owns type/field declarations and built-in schemas. |
| C++ binding table | `packages/cpp-binding` | `asharia::cpp_binding::ReflectedType<T>` specializations and binding helpers | Binds concrete C++ types to schema fields. |
| Persistent object migration | `packages/persistence` | migration API and smoke-tested migration path | Reads old archive versions and produces current object shape. |
| Runtime type registry | `packages/reflection` | `asharia::reflection::TypeRegistry` | Used by runtime serialization, not by Vulkan or RenderGraph. |
| Serialization archive | `packages/serialization` | package-local archive/text archive APIs | Separate from `packages/archive`; tied to reflection serialization. |
| Asset GUID and source metadata | `packages/asset-core` | `AssetGuid`, `AssetReference`, `AssetMetadata`, `AssetCatalog` | Does not own importer-specific texture profile interpretation. |
| Project descriptor | `packages/project-core` | project document and IO target | Tool and editor inputs read projects through this package. |
| Product manifest and product blob | `packages/asset-pipeline` | product manifest IO and product execution APIs | Owns concrete import/product execution diagnostics. |
| Runtime resource status | `packages/resource-runtime` | `RuntimeResourceRegistry` | Owns pending/ready/failed status for runtime handles. |
| Material resource signature | `packages/material-core` | `MaterialResourceSignature` and `MaterialPipelineKey` | Renderer consumes it; shader tools adapt into it. |
| Authored shader document | `packages/shader-authoring` | `.ashader` parser and generated Slang model | Does not depend on Vulkan or renderer packages. |
| Slang reflection signature | `packages/shader-slang` | reflection model and `asharia-slang-reflect` | Toolchain-facing data, consumed by adapter/tests. |

## Dependency Direction

- `archive` is a low-level document value package. `asset-core` IO, `project-core` IO, `material-instance`, `asset-pipeline`, and `persistence` may depend on it.
- `schema` owns schema contracts. `cpp-binding` and `persistence` depend on schema; schema does not depend on them.
- `persistence` depends on `archive`, `schema`, and `cpp-binding`. It does not depend on `reflection` or renderer packages.
- `serialization` depends on `reflection`; it does not depend on `schema` or `persistence` in the current manifests.
- `asset-core` owns identity and catalog state. `asset-pipeline` owns concrete import decisions and product payload construction.
- `material-core` is the common material contract. It does not depend on shader-authoring, shader-slang, RenderGraph, Vulkan, or editor hosts.
- `shader-material-adapter` is the conversion boundary from Slang reflection into material-core signatures.
- `resource-runtime` depends on asset-core product records; it does not run the asset pipeline.

## Read And Write Flow

Current asset/project/material authoring flow:

1. Source files and metadata are discovered by `asset-pipeline` or read by editor/tool code.
2. `asset-core` owns stable asset identity, source records, product keys, catalog entries, and metadata IO.
3. `project-core` owns project descriptors used by tools and hosts.
4. `.ashader` documents are parsed by `shader-authoring`, then generated Slang and metadata are consumed by shader tooling.
5. `.amat` material instances are read by `material-instance`, using asset references and shader-authoring contracts.
6. `asset-pipeline` plans imports and executes products. Diagnostics stay with the plan/execution result.
7. `resource-runtime` resolves runtime handles against product records and produces pending/ready/failed status for host code.

Current schema/persistence flow:

1. `schema` registers or loads type declarations.
2. `cpp-binding` maps C++ field access to schema fields.
3. `persistence` reads an archive object, validates type/version/fields, applies registered migrations when needed, and writes into bound C++ storage.
4. Failure returns project error types with context; tests cover unsupported fields, unknown versions, missing required fields, and migration.
5. MVP persistence supports `Bool`, `Integer`, `Float`, `String`, `Object`, and `InlineStruct` fields. `Enum`, `Array`, `AssetReference`, and `EntityReference` fields intentionally fail until their file and binding contracts are implemented.

## Lifecycle

- Document model packages own parsed values as ordinary C++ objects. Failable IO returns `Result<T>` or package-specific diagnostics.
- Schema and reflection registries are explicit objects, not implicit global state.
- Asset catalog data is built from source records and product records, then exposed through catalog views.
- Runtime resource registry tickets advance by resolving product records; stale generation or mismatched product keys return `Result<T>` errors keyed by `RuntimeResourceDiagnosticCode` instead of silently producing a ready handle.
- Material/shader contracts are created before renderer binding. Renderer code consumes signatures and pipeline keys; it does not parse `.amat` or `.ashader` documents.

## Error Handling

| Failure | Owner | Expected behavior |
|---|---|---|
| Malformed JSON/archive input | IO package that reads the file | Return a contextual error or diagnostic; do not create partial ready state. |
| Unsupported schema version | `packages/persistence` | Attempt registered migration when available; otherwise fail with version context. |
| Unknown or unsupported field kind | `packages/persistence` or `packages/schema` | Reject during validation or load. |
| Missing asset product record | `packages/asset-core` view or `packages/resource-runtime` | Mark missing/stale/failed status; runtime registry failures carry `RuntimeResourceDiagnosticCode` context. |
| Product key mismatch | `packages/resource-runtime` | Reject stale product records and keep handle unresolved. |
| Shader reflection mismatch | `packages/shader-material-adapter` | Fail before producing an incompatible material signature. |

## Future Work

`future`: asset, project, material, shader, and schema documents can share a generated document-schema index after schemas become the single source for document validation. Until that exists, package-local tests and smoke commands remain the verification source.

`future`: reflection serialization and schema persistence may converge only if the new boundary preserves the current separation: `schema/persistence` validates stored document shape; `reflection/serialization` describes runtime C++ metadata.

## Validation

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked -RunCheapGates
```

Package-local gates when these areas change:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\persistence -B build\cmake\package-persistence-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-persistence-tests-msvc-debug && ctest --test-dir build\cmake\package-persistence-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\asset-pipeline -B build\cmake\package-asset-pipeline-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-asset-pipeline-tests-msvc-debug && ctest --test-dir build\cmake\package-asset-pipeline-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\resource-runtime -B build\cmake\package-resource-runtime-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-resource-runtime-tests-msvc-debug && ctest --test-dir build\cmake\package-resource-runtime-tests-msvc-debug --output-on-failure"
```

Checkpoints:

- `material-core` does not link Slang, Vulkan, RenderGraph, asset-pipeline, or editor targets.
- `asset-core` does not contain texture importer or decoder-specific policy.
- `resource-runtime` status transitions are tested for pending, ready, failed, stale generation, and product key mismatch.
- `persistence` tests cover migration and negative validation paths.
