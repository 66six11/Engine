# 详细设计：材质、Shader Authoring 与 Slang Reflection

## 背景

材质链路当前由 `material-core`、`material-instance`、`shader-authoring`、`shader-slang` 和 `shader-material-adapter` 组成。目标是让 authoring 文档、material instance override、Slang reflection 和 renderer resource signature 分层，不让 material-core 依赖 Slang、Vulkan、RenderGraph 或 editor。

## 目标

- 用 `MaterialResourceSignature` 描述 renderer 可消费的 descriptor contract。
- 用 `.ashader` 文档描述 shader authoring properties 和生成 Slang 的输入。
- 用 `.amat` 文档描述 material instance 对 material type 的 overrides。
- 用 `ShaderResourceSignature` 承接 Slang reflection。
- 用 `makeMaterialResourceSignature()` 把 shader reflection 映射为 material signature。

## 非目标

- 不在 `material-core` 中编译 shader。
- 不在 `shader-authoring` 中创建 Vulkan pipeline。
- 不在 `.amat` 中保存 GPU descriptor set。
- 不把 renderer pipeline key 放入 editor UI state。

## 当前约束

- `material-core` 只依赖 `core`。
- `material-instance` 依赖 `core`、`archive`、`asset-core`、`shader-authoring`。
- `shader-material-adapter` 依赖 `material-core`、`shader-authoring`、`shader-slang`。
- `MaterialResourceBinding` 限制 set、binding、name、kind、visibility、arrayCount。

## 总体方案

Authoring 阶段从 `.ashader` 建立 shader properties 和生成 Slang。Slang reflection 输出 `ShaderResourceSignature`。Adapter 层把 shader resource kind、set/binding/name/count/stage 映射到 `MaterialResourceSignature`，并计算 stable hash。

Material instance 通过 `AmatMaterialTypeReference` 指向 material type asset 和 expected type hash。`AmatPropertyOverride` 按 property id 和 type 存储 override。Resolver 负责比较 `.amat` 与 material type，输出 diagnostics 和 override diff。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `packages/material-core/include/asharia/material/material_resource_signature.hpp` | descriptor contract、validation、hash、compatibility |
| `packages/material-core/include/asharia/material/material_pipeline_key.hpp` | pipeline key 数据 |
| `packages/shader-authoring/include/asharia/shader_authoring/ashader_document.hpp` | `.ashader` document model |
| `packages/shader-authoring/include/asharia/shader_authoring/ashader_parser.hpp` | `.ashader` parse |
| `packages/shader-authoring/include/asharia/shader_authoring/ashader_generated_slang.hpp` | generated Slang text |
| `packages/shader-slang/include/asharia/shader_slang/reflection.hpp` | Slang reflection data model |
| `packages/shader-material-adapter/include/asharia/shader_material_adapter/reflection_to_material_signature.hpp` | reflection 到 material signature mapping |
| `packages/material-instance/include/asharia/material_instance/amat_document.hpp` | `.amat` document model |
| `packages/material-instance/include/asharia/material_instance/amat_resolver.hpp` | overrides validation 和 diagnostics |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `MaterialResourceBinding` | `set`、`binding`、`name`、`kind`、`visibility`、`arrayCount` | renderer descriptor binding contract |
| `MaterialResourceSignature` | `bindings` | material/shader resource signature |
| `AmatDocument` | `schemaVersion`、`materialType`、`properties`、`import` | material instance document |
| `AmatPropertyOverride` | `propertyId`、`type`、`value` | property override |
| `ReflectionMaterialSignature` | `signature`、`signatureHash` | adapter output |

## API 设计

- `validateMaterialResourceSignature(signature)` 检查 descriptor bounds 和重复。
- `makeMaterialResourceSignatureHash(signature)` 生成稳定 hash。
- `validateMaterialSignatureCompatibility(materialSignature, shaderSignature)` 比较 material 与 shader contract。
- `readAmatText/readAmatFile/writeAmatText/writeAmatFile()` 负责 `.amat` IO。
- `resolveAmatDocument()` 输出 resolved document、diagnostics 和 override diffs。
- `makeReflectionMaterialSignature(shaderSignature)` 输出 signature 和 hash。

## 关键流程

### 正常流程

1. 读取 `.ashader`。
2. 生成 Slang 或编译 reflection。
3. 从 `ShaderResourceSignature` 生成 `MaterialResourceSignature`。
4. 读取 `.amat`。
5. 校验 material type reference 和 property overrides。
6. renderer 使用 signature hash 参与 pipeline/material binding。

### 失败流程

- descriptor kind 不支持：adapter 返回 `Result` error。
- binding set/count 越界：material-core validation 返回 error。
- `.amat` property type 与 material type 不匹配：resolver diagnostics 标记 error。
- expected type hash 不一致：resolver diagnostics 标记 material type drift。

### 边界流程

- `material-core` 不能 include Slang、Vulkan、RenderGraph、asset-pipeline 或 editor headers。
- shader adapter 可以依赖 `shader-slang`，但输出必须是 material-core 类型。
- `.amat` import metadata 是导入记录，不是 renderer runtime state。

## 生命周期

`.ashader` 和 `.amat` 是 source documents。Reflection 和 material signatures 是导入/编译阶段产物。Renderer 可以缓存 signature hash 和 pipeline key，但不能修改 source documents。

## 错误处理

低层 validation 使用 `Result`/`VoidResult`。Resolver 使用 diagnostics，必须包含 target、code、property id 或 material type reference。Adapter error 需要保留 shader binding 上下文。

## 测试方案

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/material-core -B build/cmake/package-material-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-material-core-tests-msvc-debug && ctest --test-dir build/cmake/package-material-core-tests-msvc-debug --output-on-failure"
```

Runtime/API smokes：

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-material-binding
```

## 风险

- Reflection stage visibility 与 material visibility 不一致会产生错误 descriptor layout。缓解：adapter negative tests 覆盖 stage/kind/count。
- `.amat` override diff 若只警告不阻塞，会让 runtime 使用旧 property。缓解：resolver diagnostics 分 severity。
- material-core 一旦依赖 backend，会破坏 package-first 边界。缓解：CMake 和 include review 检查该 target。
