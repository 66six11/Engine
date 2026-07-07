# 架构：Asset、Material 与 Shader 产物流

## 目的

本文描述当前 asset/material/shader packages 的状态所有权和数据流。它不把旧计划写成当前实现。

## 当前模块

| 模块 | Target | 当前职责 |
|---|---|---|
| Asset core | `asharia::asset_core` | GUID、asset type、source record、product record、dependency、catalog |
| Asset core IO | `asharia::asset_core_io` | metadata document read/write |
| Project core | `asharia::project_core`、`asharia::project_core_io` | project descriptor 和 IO |
| Asset pipeline | `asharia::asset_pipeline` | source discovery、snapshot、import plan、product execution、manifest IO |
| Asset processor | `asharia-asset-processor` | CLI wrapper for dry-run and product execution smokes |
| Material core | `asharia::material_core` | material resource signature、pipeline key、validation/hash |
| Shader authoring | `asharia::shader_authoring` | authored shader document model |
| Shader Slang | `asharia::shader_slang` | Slang reflection 和 `asharia_add_slang_shader()` helper |
| Material instance | `asharia::material_instance` | `.amat` document、override resolver、IO |
| Shader/material adapter | `asharia::shader_material_adapter` | Slang resource signature 到 material signature 的转换 |
| Resource runtime | `asharia::resource_runtime` | 以 asset identity 为 key 的 runtime resource registry |

## Source Asset Ownership

`asset-core` 拥有稳定身份和 catalog facts：

- `AssetGuid` 是稳定 source identity。
- `AssetTypeId` 标识类型类别。
- `SourceAssetRecord` 保存 source path、importer id/name/version、source hash、settings hash。
- `AssetCatalog` 拥有内存中的 source records，并验证 duplicate GUID/path rules。

`asset-core` 不执行 import，也不拥有 generated product bytes。

## Import Planning

`asset-pipeline` 从 discovered sources、source snapshots、existing product manifest、target profile、importer/tool version dependencies 生成 import plan。

`planAssetImports()` 返回 requests、cacheHits 和 diagnostics。`AssetImportPlanResult::succeeded()` 在没有 `Error` severity diagnostic 时为 true。

## Product Execution

`executeAssetProducts()` 消费 import plan、source bytes、dependency product bytes、output root 和 manifest output path，返回 written products、cache hits、updated manifest、diagnostics 和 `manifestWritten`。

Product execution 拥有 generated output；source directories 不应被当作 generated cache owner。

## Material And Shader Flow

1. `shader-authoring` 表示 authored shader/material intent。
2. `shader-slang` 编译 Slang shader entries，并生成 reflection metadata。
3. `shader-material-adapter` 把 `ShaderResourceSignature` 转成 `material::MaterialResourceSignature`。
4. `material-core` 验证 signatures，并生成可 hash 的 `MaterialPipelineKey`。
5. `material-instance` 根据 material type document 解析 `.amat` overrides，并报告 stale/mismatched overrides。
6. `renderer-basic-vulkan` 在 backend recording 时消费 shader outputs 和 material/render state facts。

## 依赖约束

- Asset identity 和 metadata 位于 importer execution 之下。
- Asset pipeline 可以依赖 material instance 和 shader authoring，因为 import execution 可生成 material/shader products。
- Runtime resource lookup 应依赖 asset identity，而不是 source file path。
- Material core 不依赖 Vulkan types；backend pipeline creation 由 renderer/RHI code 拥有。

`future`: 完整 hot-reload resource manager 应消费 product manifests 和 runtime registry data，但不能让 editor UI 拥有 runtime resource lifetime。

## 验证方式

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/asset-core -B build/cmake/package-asset-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-asset-core-tests-msvc-debug && ctest --test-dir build/cmake/package-asset-core-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/asset-pipeline -B build/cmake/package-asset-pipeline-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-asset-pipeline-tests-msvc-debug && ctest --test-dir build/cmake/package-asset-pipeline-tests-msvc-debug --output-on-failure"
```

Tool smokes:

```powershell
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-dry-run
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-product-execution
```
