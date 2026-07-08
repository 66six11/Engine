# Detailed Design: Materials, Shader Authoring, And Slang Reflection

## Background

The material path currently spans `material-core`, `material-instance`, `shader-authoring`, `shader-slang`, and `shader-material-adapter`. The goal is to keep authoring documents, material instance overrides, Slang reflection, and renderer resource signatures layered, so `material-core` does not depend on Slang, Vulkan, RenderGraph, or editor code.

## Goals

- Use `MaterialResourceSignature` to describe the descriptor contract consumed by renderers.
- Use `.ashader` documents to describe shader authoring properties and generated Slang input.
- Use `.amat` documents to describe material instance overrides.
- Use `ShaderResourceSignature` as the Slang reflection model.
- Use `makeMaterialResourceSignature()` to map shader reflection to a material signature.

## Non-Goals

- Do not compile shaders in `material-core`.
- Do not create Vulkan pipelines in `shader-authoring`.
- Do not store GPU descriptor sets in `.amat`.
- Do not put renderer pipeline keys in editor UI state.

## Current Constraints

- `material-core` only depends on `core`.
- `material-instance` depends on `core`, `archive`, `asset-core`, and `shader-authoring`.
- The `shader-material-adapter` library target depends on `core`, `material-core`, and `shader-slang`; `shader-authoring` is used by generated Slang reflection smoke tests, not by the adapter library target.
- `MaterialResourceBinding` constrains set, binding, name, kind, visibility, and array count.

## Overall Design

Authoring starts with `.ashader`, which provides shader properties and generated Slang input. Slang reflection emits `ShaderResourceSignature`. The adapter layer maps shader resource kind, set/binding/name/count/stage into `MaterialResourceSignature` and computes a stable hash.

Material instances use `AmatMaterialTypeReference` to point at a material type asset and expected type hash. `AmatPropertyOverride` stores override values by property id and type. The resolver compares `.amat` against the material type and returns diagnostics plus override diffs.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `packages/material-core/include/asharia/material/material_resource_signature.hpp` | descriptor contract, validation, hash, compatibility |
| `packages/material-core/include/asharia/material/material_pipeline_key.hpp` | pipeline key data |
| `packages/shader-authoring/include/asharia/shader_authoring/ashader_document.hpp` | `.ashader` document model |
| `packages/shader-authoring/include/asharia/shader_authoring/ashader_parser.hpp` | `.ashader` parsing |
| `packages/shader-authoring/include/asharia/shader_authoring/ashader_generated_slang.hpp` | generated Slang text |
| `packages/shader-slang/include/asharia/shader_slang/reflection.hpp` | Slang reflection data model |
| `packages/shader-material-adapter/include/asharia/shader_material_adapter/reflection_to_material_signature.hpp` | reflection to material signature mapping |
| `packages/material-instance/include/asharia/material_instance/amat_document.hpp` | `.amat` document model |
| `packages/material-instance/include/asharia/material_instance/amat_resolver.hpp` | override validation and diagnostics |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `MaterialResourceBinding` | `set`, `binding`, `name`, `kind`, `visibility`, `arrayCount` | renderer descriptor binding contract |
| `MaterialResourceSignature` | `bindings` | material/shader resource signature |
| `AmatDocument` | `schemaVersion`, `materialType`, `properties`, `import` | material instance document |
| `AmatPropertyOverride` | `propertyId`, `type`, `value` | property override |
| `ReflectionMaterialSignature` | `signature`, `signatureHash` | adapter output |

## API Design

- `validateMaterialResourceSignature(signature)` checks descriptor bounds and duplicates.
- `makeMaterialResourceSignatureHash(signature)` creates a stable hash.
- `validateMaterialSignatureCompatibility(materialSignature, shaderSignature)` compares material and shader contracts.
- `readAmatText/readAmatFile/writeAmatText/writeAmatFile()` handle `.amat` IO.
- `resolveAmatOverrides(document, shader, options)` compares `.amat` overrides against an `AshaderDocument` and returns override diffs plus diagnostics.
- `makeReflectionMaterialSignature(shaderSignature)` returns signature and hash.

## Key Flows

### Normal Flow

1. Read `.ashader`.
2. Generate Slang or compile reflection.
3. Convert `ShaderResourceSignature` into `MaterialResourceSignature`.
4. Read `.amat`.
5. Validate material type reference and property overrides.
6. Renderer uses signature hash for pipeline/material binding.

### Failure Flow

- Unsupported descriptor kind: adapter returns a `Result` error.
- Binding set/count out of bounds: material-core validation returns an error.
- `.amat` property type mismatches material type: resolver diagnostics mark an error.
- Expected type hash mismatch: resolver diagnostics mark material type drift.

### Boundary Flow

- `material-core` must not include Slang, Vulkan, RenderGraph, asset-pipeline, or editor headers.
- The shader adapter library may depend on `shader-slang`, but its output must be material-core types. Test targets may additionally use `shader-authoring` fixtures.
- `.amat` import metadata is an import record, not renderer runtime state.

## Lifetime

`.ashader` and `.amat` are source documents. Reflection and material signatures are import/compile products. Renderer code may cache signature hashes and pipeline keys, but it must not mutate source documents.

## Error Handling

Low-level validation uses `Result`/`VoidResult`. The resolver uses diagnostics and must include target, code, property id, or material type reference. Adapter errors need to preserve shader binding context.

## Test Plan

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/material-core -B build/cmake/package-material-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-material-core-tests-msvc-debug && ctest --test-dir build/cmake/package-material-core-tests-msvc-debug --output-on-failure"
```

Runtime/API smoke:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-material-binding
```

## Risks

- Reflection stage visibility and material visibility can diverge and create the wrong descriptor layout. Mitigation: adapter negative tests cover stage/kind/count.
- If `.amat` override diffs warn but do not block when required, runtime can use stale properties. Mitigation: resolver diagnostics separate severity.
- If `material-core` depends on a backend, package-first boundaries break. Mitigation: CMake and include review check this target.
