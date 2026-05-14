# Asset-core 架构与基线计划

资料核对日期：2026-05-12

本文定义 `packages/asset-core` 的第一版边界、资料依据、数据模型和实施切片。它补齐
`docs/architecture/engine-systems.md` 中提到的 asset pipeline 前置设计，用来支撑后续 mesh/texture
resource、material、asset browser、scene persistence 和 scripting metadata，但第一版不做完整
AssetDatabase、不做 editor UI、不做 GPU resource owner。

核心结论：`asset-core` 先负责稳定身份、source metadata、import settings、product/cache key、依赖摘要和
runtime-safe asset handle。文件修改监听、source hash、metadata IO、import 调度、product cache manifest
和 dependency invalidation 后续由独立 `asset-pipeline` / `asset-processor` 承担；真实 importer、GPU upload、
shader/material pipeline key、editor browser 和热重载分别在后续 package 或工具层接入，不能反向污染
`asset-core`。

## 资料结论

| 来源 | 关键事实 | 对 Asharia Engine 的约束 |
| --- | --- | --- |
| Unity Asset Database: https://docs.unity.cn/Manual/AssetDatabase.html | Unity 把 source asset、metadata、artifact/cache 和 dependency 作为 asset pipeline 的核心边界。 | Asharia Engine 也应把用户源文件、`.ameta` 元数据和 generated product cache 分开；runtime handle 不保存 source path。 |
| Unity Asset Metadata: https://docs.unity.cn/Manual/AssetMetadata.html | Unity 使用每个 asset 对应的 metadata 文件保存 GUID 和 importer settings。 | 第一版采用 sidecar `.ameta` 保存稳定 `AssetGuid`、asset type、importer id/version 和 import settings hash。 |
| O3DE Asset Processor: https://docs.o3de.org/docs/user-guide/assets/asset-processor/ | Asset Processor 监控 source assets，生成 product assets，并维护 source/product/dependency 关系。 | `asset-core` 只定义 source record、product record 和 dependency graph 数据；未来 `tools/asset-processor` 才负责执行 import。 |
| Godot import process: https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html | Godot 保存 source 旁的 import metadata，并把 imported result 放入隐藏 cache。 | Asharia Engine 的 generated product 不提交到项目源目录；开发期可放在 `build/asset-cache/`，未来项目可放 `.asharia/cache/`。 |
| Unreal asynchronous asset loading: https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine | Unreal 使用 soft reference / path 让资源引用不等于立即加载对象。 | `AssetHandle<T>` 是稳定引用，不是 loaded pointer；加载状态和 fallback resource 由 resource/asset manager 处理。 |

## 设计目标

- 为所有可持久化 asset 提供稳定 `AssetGuid`。
- 明确 source asset、metadata、import settings、product asset 和 cache 的边界。
- 让 scene、material、script 和 editor 保存稳定 asset reference，而不是保存路径、runtime pointer 或 GPU handle。
- 允许同一 source 在不同 target platform、import settings、tool version 下生成不同 product。
- 为后续 importer、watcher、hot reload 和 dependency invalidation 留出数据模型。
- 第一版能用 package-local smoke tests 验证，不强制改 `apps/sample-viewer/src/main.cpp`。

## 非目标

第一版不做：

- 完整 editor Asset Browser。
- 文件系统 watcher 和后台导入线程。
- glTF、PNG、DDS、mesh、texture 等具体 importer。
- GPU buffer/image 创建、staging allocator 或 Vulkan upload。
- async streaming、reference counting 或 fallback resource 实现。
- 跨项目 package registry 和 marketplace。
- 二进制 cooked asset 格式。
- 自动 shader/material pipeline database。

这些能力必须等 identity、metadata、product key 和 dependency model 稳定后逐步接入。

## Package 边界

建议第一版目录：

```text
packages/asset-core/
  CMakeLists.txt
  asharia.package.json
  include/asharia/asset_core/
    asset_guid.hpp
    asset_type.hpp
    asset_handle.hpp
    asset_metadata.hpp
    asset_catalog.hpp
    product_key.hpp
    dependency.hpp
  src/
    asset_guid.cpp
    asset_catalog.cpp
    product_key.cpp
  tests/
    asset_core_smoke_tests.cpp
```

依赖原则：

- `asharia::asset_core` 第一阶段只依赖 `asharia::core`。
- 如果要读写 `.ameta` 文本文件，优先放在后续 `asharia::asset_core_persistence` 或同 package 的可选源文件中，
  并按职责依赖 `asharia::archive` / `asharia::persistence`；不要让 identity/handle 头文件强制依赖 JSON 或 persistence 实现。
- `asset-core` 不依赖 renderer、RHI、RenderGraph、editor、ImGui、script runtime 或具体 importer。
- `apps/editor`、`packages/scene-core`、`packages/material` 和 `packages/scripting` 可以消费
  `AssetGuid` / `AssetHandle<T>`，但不能重建自己的 asset identity 系统。

建议顶层依赖：

```mermaid
flowchart TD
    Core["engine/core"]
    Reflection["packages/schema<br/>target; reflection spike legacy"]
    Serialization["packages/persistence<br/>target; serialization spike legacy"]
    AssetCore["packages/asset-core"]
    Scene["packages/scene-core"]
    Material["future packages/material"]
    AssetPipeline["future packages/asset-pipeline"]
    ImportTool["future tools/asset-processor"]
    ResourceRuntime["future resource runtime"]
    Editor["apps/editor / editor-core"]

    AssetCore --> Core
    Scene --> AssetCore
    Material --> AssetCore
    AssetPipeline --> AssetCore
    ImportTool --> AssetPipeline
    ImportTool --> AssetCore
    ResourceRuntime --> AssetCore
    Editor --> AssetCore
    AssetCore -.optional metadata IO.-> Serialization
    AssetCore -.optional reflected settings.-> Reflection
```

### 后续 asset-pipeline / asset-processor

为了避免后续 editor 文件修改更新逻辑散落到 UI 或 runtime，单独记录未来 owner：

- `packages/asset-pipeline`：库化的 asset import pipeline。它消费 `asset-core` 的 GUID、metadata、
  product key 和 dependency 数据，负责 source scan、source hash、metadata IO facade、import request、
  product manifest、cache hit/miss 判断和 dependency invalidation 规则。
- `tools/asset-processor`：开发期/后台进程或 CLI host。它可以使用文件 watcher 调用 `asset-pipeline`，
  执行具体 importer，写入 `build/asset-cache/` 或项目 `.asharia/cache/`，并向 editor/resource runtime
  发布 product 更新通知。
- `apps/editor` / 未来 `editor-core`：只发出 reimport、rename、move、import settings 修改等命令，并展示
  pipeline 状态和诊断；不直接扫描 source tree，不直接写 product cache。
- `asset-core`：继续保持纯身份和数据模型，不拥有 watcher、后台线程、importer、product 文件写入或热更新发布。

进入条件：

- `asset-core` 至少完成 metadata model、product key、dependency 和 catalog 切片。
- `.ameta` IO facade 已有确定性读写方案，或 `asset-pipeline` 明确依赖后续 serialization package。
- editor 需要真实文件变更刷新，或 mesh/texture/material product smoke 需要稳定 import/cache 流程。

## 数据边界

资产管线分成五类数据：

```mermaid
flowchart LR
    Source["Source asset<br/>user-authored file"]
    Meta[".ameta<br/>guid / type / importer settings"]
    Import["Importer<br/>tool execution"]
    Product["Product asset<br/>generated data"]
    Runtime["Runtime resource<br/>CPU/GPU loaded object"]

    Source --> Meta --> Import --> Product --> Runtime
    Source --> Import
```

规则：

- Source asset 是用户编辑的源文件，例如 `.png`、`.gltf`、`.fbx`、`.slang`、`.amat`、`.ascene`。
- `.ameta` 是可提交的 metadata；它保存稳定 GUID、importer 和 import settings。
- Product asset 是 generated output；它可以被删除并重新生成，默认不提交。
- Runtime resource 是加载后的 CPU/GPU 对象；它不进入 `.ameta` 或 scene 文件。
- Product cache miss 只能触发 import 或 fallback，不应让 scene/material 引用改写 source path。

## 稳定身份

建议第一版类型：

```cpp
namespace asharia::asset {

struct AssetGuid {
    std::array<std::uint8_t, 16> bytes{};
};

struct AssetTypeId {
    std::uint64_t value{};
};

struct AssetReference {
    AssetGuid guid;
    AssetTypeId expectedType;
};

template <class T>
struct AssetHandle {
    AssetGuid guid;
};

} // namespace asharia::asset
```

规则：

- `AssetGuid{}` 全零表示 invalid。
- GUID 以 canonical lowercase UUID 文本写入 `.ameta` 和用户数据，例如
  `9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21`。
- `AssetTypeId` 来自稳定 type name hash，例如 `com.asharia.asset.Texture2D`。
- `AssetHandle<T>` 只携带 GUID；类型约束由 `T`、metadata 和 load policy 共同校验。
- 错误诊断必须同时输出 GUID、source path、expected type 和 actual type，不能只输出 hash。

## Metadata 文件

第一版 `.ameta` 使用确定性 JSON 子集，后续可由 `packages/archive` 和 `packages/persistence` 统一读写：

```json
{
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "sha256:...",
  "importer": {
    "id": "com.asharia.importer.texture2d",
    "version": 1
  },
  "settings": {
    "colorSpace": "srgb",
    "generateMipmaps": true,
    "compression": "auto"
  }
}
```

规则：

- `.ameta` path 与 source path 一一对应，建议命名为 `<source-file>.ameta`。
- `sourcePath` 用于诊断和 relocation，不作为引用 ID；真实引用以 GUID 为准。
- `sourceHash`、`settings` 和 `importer.version` 共同影响 product key。
- `.ameta` 可包含 editor-only import settings，但 cooked/runtime manifest 必须剥离 editor-only 字段。
- `.ameta` 不保存 runtime pointer、GPU handle、absolute build path 或 transient cache path。

## Product key 与 cache

Product cache key 至少包含：

- source asset GUID。
- asset type。
- importer id 和 importer version。
- source content hash。
- normalized import settings hash。
- target platform / profile。
- relevant tool version，例如 mesh optimizer、texture compressor、shader compiler。
- dependency hash，例如外部 include、material graph、shader include 或 nested asset。

建议类型：

```cpp
struct AssetProductKey {
    AssetGuid guid;
    AssetTypeId assetType;
    std::uint64_t importerIdHash{};
    std::uint32_t importerVersion{};
    std::uint64_t sourceHash{};
    std::uint64_t settingsHash{};
    std::uint64_t dependencyHash{};
    std::uint64_t targetProfileHash{};
};

struct AssetProductRecord {
    AssetProductKey key;
    std::string relativeProductPath;
    std::uint64_t productSizeBytes{};
    std::uint64_t productHash{};
};
```

Cache 规则：

- 开发期 engine repo 默认使用 `build/asset-cache/`，因为 `build/` 已是 generated output。
- 未来用户项目可使用 `.asharia/cache/` 或 project-local library 目录；该目录不提交。
- Product path 由 product key 派生，避免同名 source 文件冲突。
- Cache miss 可以重新 import；cache hit 必须仍校验 product key 和 product hash。
- Product record 可写入 generated manifest，不能替代 source `.ameta`。

## Catalog 与查询

第一版 `AssetCatalog` 只做只读或显式 mutable 数据表，不做后台扫描线程：

```cpp
class AssetCatalog {
public:
    Result<void> addSource(SourceAssetRecord record);
    const SourceAssetRecord* findByGuid(AssetGuid guid) const;
    const SourceAssetRecord* findBySourcePath(std::string_view path) const;
    std::span<const SourceAssetRecord> sources() const;
};
```

校验：

- 重复 GUID 不同 path 必须失败。
- 同一路径不同 GUID 必须失败，除非显式 relocation/migration。
- unknown asset type 可以保留为 opaque source record，但不能假装可加载。
- catalog 不拥有 loaded resource。
- catalog 查询是工具/editor/runtime 的共同基础，但 mutation 只能由明确的 import/scan command 进入。

## Dependency graph

依赖分两层：

- Source dependency：source asset 依赖其他 source，例如 material 依赖 texture、shader 依赖 include。
- Product dependency：product 依赖其他 product，例如 material product 依赖 shader product 和 texture product。

建议类型：

```cpp
enum class AssetDependencyKind {
    SourceFile,
    AssetReference,
    ToolVersion,
    ImportSettings,
};

struct AssetDependency {
    AssetGuid owner;
    AssetDependencyKind kind;
    AssetGuid asset;
    std::string path;
    std::uint64_t hash{};
};
```

规则：

- 依赖图用于 invalidation，不用于运行时强制同步加载。
- 循环依赖必须在 import/tool 阶段诊断。
- 缺失依赖应保留诊断，不应把 GUID 静默改成 source path。

## Runtime 引用与加载状态

`AssetHandle<T>` 不等同于 loaded resource。后续 resource runtime 可以提供：

```cpp
enum class AssetLoadState {
    Unloaded,
    Loading,
    Ready,
    Failed,
    Missing,
};

template <class T>
struct AssetLoadResult {
    AssetHandle<T> handle;
    AssetLoadState state;
    const T* resource;
};
```

规则：

- scene、material、script 保存 `AssetHandle<T>` 或 `AssetReference`。
- renderer 和 RHI 只消费已经解析好的 resource packet，不直接读 `.ameta`。
- missing asset 必须能返回 fallback resource 或明确错误；不允许崩在 render recording 阶段。
- hot reload 只能通过 asset/resource manager 发布新 product，不直接修改 live World 或 command buffer。

## 与其他系统的关系

### Schema / Persistence

- `AssetGuid`、`AssetReference` 和常见 import settings 应可被 schema/persistence 描述。
- `.ascene`、`.amat`、`.ameta` 保存 GUID 和稳定 type name，不保存 runtime pointer。
- migration 可以把旧 source path 引用迁移成 GUID，但必须保留诊断和人工修复路径。

### Scene / World

- `MeshRendererComponent` 保存 mesh/material handle。
- World snapshot 中可以携带 `AssetHandle<MeshAsset>` / `AssetHandle<MaterialAsset>`，但 renderer 接收前应解析为可用 resource 或 fallback。
- Play World 与 Edit World 可共享 GUID；不能共享 runtime pointer。

### Material / Renderer

- Material 阶段消费 `asset-core` 的 GUID、product key 和 dependency data。
- Pipeline key 仍属于 renderer/material/RHI 层；`asset-core` 只提供 shader/material source identity 和 product identity。
- GPU upload 由 resource runtime 或 renderer backend 负责，不能放进 `asset-core`。

### Editor

- Asset Browser 消费 catalog view。
- Inspector 修改 import settings 时生成 editor command，更新 `.ameta` 后触发 reimport request。
- Editor UI 不直接修改 product cache；它只请求 import、展示状态和诊断。

### Scripting

- Asset import script 可以读 source metadata、生成 import settings 或声明 dependency。
- Script runtime 使用 safe asset handle API，不获得裸 pointer 或 GPU handle。
- C# / Lua metadata 不改变 native `AssetGuid` 和 `AssetTypeId` 规则。

## 实施切片

### 切片 A：文档与 package 骨架

交付：

- 本文档。
- `packages/asset-core/CMakeLists.txt`。
- `packages/asset-core/asharia.package.json`。
- `asharia::asset_core` target。

当前状态：

- 已落地最小 `asharia::asset_core` package 骨架和 package-local smoke test target。

验收：

- package 可独立配置/构建。
- `docs/README.md` 和 `docs/research/sources.md` 包含入口。

### 切片 B：GUID 与 type identity

交付：

- `AssetGuid` parse/format。
- invalid GUID helper。
- `AssetTypeId` 和 stable asset type name helper。

当前状态：

- 已落地 `AssetGuid` parse/format、invalid all-zero GUID 拒绝、canonical lowercase 输出和
  `AssetTypeId` stable name hash helper。

验收：

- `asharia-asset-core-smoke-tests` 覆盖 valid UUID、invalid UUID、canonical lowercase 输出、invalid zero GUID。

### 切片 C：Handle 与 reference

交付：

- `AssetHandle<T>`。
- `AssetReference`。
- expected type 校验 helper。

验收：

- smoke 覆盖 typed handle、untyped reference、type mismatch 诊断。

### 切片 D：Metadata model

交付：

- `SourceAssetRecord`。
- `ImporterId` / `ImporterVersion`。
- metadata schema/version constants。
- deterministic settings hash 输入模型。

验收：

- smoke 覆盖 duplicate GUID/path、missing type、settings hash 稳定性。

### 切片 E：Product key 与 dependency

交付：

- `AssetProductKey`。
- `AssetProductRecord`。
- `AssetDependency`。
- key/hash 组合 helper。

验收：

- smoke 覆盖 source hash、settings hash、target profile 改变时 product key 改变。

### 切片 F：Catalog

交付：

- `AssetCatalog`。
- `findByGuid()` / `findBySourcePath()`。
- explicit add/update/remove command API。

验收：

- smoke 覆盖查询、重复 GUID、重复 path、relocation policy。

### 切片 G：Metadata IO

进入条件：schema-first 的 `packages/archive` 和 `packages/persistence` 合并并稳定。

交付：

- `.ameta` read/write facade。
- strict parse diagnostics。
- byte-for-byte deterministic write。

验收：

- `--smoke-asset-metadata-roundtrip` 或 package-local test 覆盖 `.ameta` round-trip 和 malformed input。

### 切片 H：Resource upload baseline

进入条件：RenderGraph storage/MRT/compute 和 resource lifetime 相关分支合并。

交付：

- Mesh/texture product record 到 runtime resource request 的桥接。
- staging/upload 仍放在 RHI/resource runtime，不放在 `asset-core`。

验收：

- `--smoke-mesh-resource` 和 `--smoke-texture-upload` 证明 runtime 不直接依赖 source path。

## 并行开发建议

可以现在并行：

| 工作 | 原因 | 注意事项 |
| --- | --- | --- |
| `asset-core` GUID / handle / catalog 头文件和 tests | 只依赖 `core`，不抢 RenderGraph 或 schema/archive/persistence worktree。 | 优先 package-local tests，少碰 sample-viewer。 |
| metadata schema 文档和 `.ameta` 示例 | 纯文档，可和 schema/archive/persistence 分支并行。 | 不实现 parser 前不要承诺最终 JSON facade。 |
| product key / dependency hash 数据模型 | 可独立验证 hash/key 变化规则。 | 不接真实 importer，不读写 generated product。 |

等待后再做：

| 工作 | 等待项 |
| --- | --- |
| `.ameta` read/write | 等 `packages/archive` / `packages/persistence` 分支完成 migration/text archive 细节。 |
| `packages/asset-pipeline` / `tools/asset-processor` | 等 `asset-core` metadata、product key、dependency、catalog 和 `.ameta` IO 边界稳定。 |
| `--smoke-mesh-resource` / `--smoke-texture-upload` | 等 rendering 分支完成 storage/MRT/compute 和上传路径边界。 |
| Asset Browser / import settings UI | 等 `editor-core` transaction 和 catalog view 稳定。 |
| Material asset / pipeline key | 等 material signature 和 descriptor contract 进入计划阶段。 |

不建议现在做：

- 文件系统 watcher。
- 后台 import worker。
- 完整 glTF/texture importer。
- GPU upload owner。
- 热重载。
- 资产数据库 UI。
- package marketplace。

## 最小 smoke 建议

Package-local tests：

- `asharia-asset-core-smoke-tests --guid`：UUID parse/format、invalid GUID。
- `asharia-asset-core-smoke-tests --catalog`：add/find、重复 GUID/path 失败。
- `asharia-asset-core-smoke-tests --product-key`：source/settings/tool/target 改变会改变 key。
- `asharia-asset-core-smoke-tests --dependency`：dependency hash 和 missing dependency diagnostics。

未来 CLI smoke：

- `--smoke-asset-metadata-roundtrip`：`.ameta` deterministic round-trip。
- `--smoke-mesh-resource`：mesh product 解析为 runtime draw resource，不依赖 source path。
- `--smoke-texture-upload`：texture product 解析为 sampled runtime resource，不依赖 source path。

## 审查清单

新增或修改 `asset-core` 时检查：

- 是否没有依赖 Vulkan、RenderGraph、renderer、editor UI 或 script runtime。
- GUID 是否稳定，诊断是否包含原始文本和 source path。
- `.ameta` 是否只保存 source metadata 和 import settings，不保存 runtime resource。
- Product key 是否包含 source hash、settings hash、importer/tool version、target profile 和 dependency hash。
- Generated product/cache 是否不提交。
- `AssetHandle<T>` 是否只是引用，不假装资源已加载。
- Missing asset/type mismatch 是否有结构化错误。
- Editor-only import settings 是否不会进入 runtime manifest。
- 新增 tests 是否不依赖当前两个并行 worktree 的未合并实现。
