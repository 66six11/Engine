# Asset-core 架构与基线计划

资料核对日期：2026-05-12

本文定义 `packages/asset-core` 的第一版边界、资料依据、数据模型和实施切片。它补齐
`docs/architecture/engine-systems.md` 中提到的 asset pipeline 前置设计，用来支撑后续 mesh/texture
resource、material、asset browser、scene persistence 和 scripting metadata，但第一版不做完整
AssetDatabase、不做 editor UI、不做 GPU resource owner。

核心结论：`asset-core` 先负责稳定身份、source metadata、import settings、product/cache key、依赖摘要和
runtime-safe asset handle。文件修改监听、source hash、metadata IO、import 调度、product cache manifest
和 dependency invalidation 由独立 `asset-pipeline` / `asset-processor` 逐步承担；runtime loaded-resource
状态由独立 `resource-runtime` 承担；真实 importer、GPU upload、shader/material pipeline key、editor browser
和热重载分别在后续 package 或工具层接入，不能反向污染
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
    asset_metadata_io.hpp
    asset_catalog.hpp
    asset_product.hpp
  src/
    asset_guid.cpp
    asset_metadata_io.cpp
    asset_catalog.cpp
    asset_product.cpp
  tests/
    asset_core_smoke_tests.cpp

packages/project-core/
  CMakeLists.txt
  asharia.package.json
  include/asharia/project/
    project_descriptor.hpp
    project_descriptor_io.hpp
  src/
    project_descriptor.cpp
    project_descriptor_io.cpp
  tests/
    project_core_smoke_tests.cpp

packages/resource-runtime/
  CMakeLists.txt
  asharia.package.json
  include/asharia/resource_runtime/
    runtime_resource_registry.hpp
  src/
    runtime_resource_registry.cpp
  tests/
    resource_runtime_smoke_tests.cpp
```

依赖原则：

- `asharia::project_core` 只依赖 `asharia::core`，当前只定义 Asharia project identity、
  `assetSourceRoots`、`assetCacheRoot` 和 asset discovery ignore policy；不保存 cook/package profiles、
  target profiles、asset profiles、editor workspace 或 runtime state。
- `asharia::project_core_io` 依赖 `asharia::project_core` 和 `asharia::archive`，使用 strict JSON facade
  读写 `asharia.project.json`；JSON 类型不进入 public descriptor model。
- `asharia::asset_core` 第一阶段只依赖 `asharia::core`。
- `.ameta` 文本 IO 放在同 package 的可选 `asharia::asset_core_io` target 中；该 target 依赖
  `asharia::asset_core` 和 `asharia::archive`，但 identity/handle/catalog 头文件不强制依赖 JSON 或
  persistence 实现。
- `asharia::asset_pipeline` 第一阶段只提供显式 source/.ameta 条目的 metadata discovery 和诊断；
  public API 只依赖 `asset-core`，实现内部通过 `asset_core_io` 读取 `.ameta`，不拥有 watcher、importer、
  product cache 或 GPU upload。
- `asharia::resource_runtime` 只依赖 `asset-core`，当前提供 CPU-only runtime resource key、ticket、
  pending / ready / failed 状态和 diagnostics；不依赖 `asset-pipeline`、RenderGraph、renderer、RHI、editor
  或 ImGui。
- `asset-core` 不依赖 renderer、RHI、RenderGraph、editor、ImGui、script runtime 或具体 importer。
- `apps/editor`、`packages/scene-core`、`packages/material-core` 和未来 `packages/scripting` 可以消费
  `AssetGuid` / `AssetHandle<T>`，但不能重建自己的 asset identity 系统。

建议顶层依赖：

```mermaid
flowchart TD
    Core["engine/core"]
    Reflection["packages/schema<br/>target; reflection spike legacy"]
    Serialization["packages/persistence<br/>target; serialization spike legacy"]
    ProjectCore["packages/project-core"]
    ProjectCoreIo["packages/project-core<br/>asharia::project_core_io"]
    AssetCore["packages/asset-core"]
    AssetCoreIo["packages/asset-core<br/>asharia::asset_core_io"]
    Archive["packages/archive"]
    Scene["packages/scene-core"]
    Material["future packages/material"]
    AssetPipeline["packages/asset-pipeline"]
    ImportTool["future tools/asset-processor"]
    ResourceRuntime["packages/resource-runtime"]
    Editor["apps/editor / editor-core"]

    ProjectCore --> Core
    ProjectCoreIo --> ProjectCore
    ProjectCoreIo --> Archive
    AssetCore --> Core
    AssetCoreIo --> AssetCore
    AssetCoreIo --> Archive
    Scene --> AssetCore
    Material --> AssetCore
    AssetPipeline --> AssetCore
    ImportTool --> AssetPipeline
    ImportTool --> AssetCore
    ImportTool -.project descriptor.-> ProjectCoreIo
    ResourceRuntime --> AssetCore
    Editor --> AssetCore
    AssetPipeline -.metadata IO.-> AssetCoreIo
    ImportTool -.metadata IO.-> AssetCoreIo
    AssetCore -.optional reflected settings.-> Reflection
```

### asset-pipeline / asset-processor

为了避免后续 editor 文件修改更新逻辑散落到 UI 或 runtime，单独记录未来 owner：

- `packages/asset-pipeline`：当前已落地 deterministic source tree scan baseline、metadata discovery baseline、
  显式 source file snapshot/hash baseline、product manifest IO baseline、import planning baseline 和
  scan-to-planning bridge baseline，以及 deterministic product execution baseline。source scan 只遍历显式 source root，配对 `<source-file>.ameta`
  sidecar，产出 canonical sourcePath、source file path 和 metadata path；discovery 消费显式 source/.ameta
  条目，复用 `asset_core_io` 读取 `.ameta`，校验 duplicate GUID、duplicate source path、missing/malformed
  metadata 和 source path mismatch，并产出 deterministic manifest / `AssetCatalog` 输入；source snapshot
  只消费显式 sourcePath + source file path，校验缺失、非普通文件、非规范 sourcePath 和重复 source path，
  并产出确定性 v1 `sourceHash`；product manifest IO 复用 `archive` deterministic JSON facade，记录
  product key、product key hash、relative product path、product size 和 product hash；import planning 比较
  discovered source、current source snapshot、target profile 和 existing product manifest，产出 cache hit
  或 import request；scan-to-planning bridge 只顺序复用 scan、discovery、snapshot 和 planning，并保留分阶段
  diagnostics，供 dry-run CLI 报告；product execution 消费已有 import request 和显式 source bytes，写入
  deterministic placeholder product blob 与 product manifest，用作真实 importer 前的稳定 product/cache 输出基线。
  后续再扩展具体 importer、import scheduling 和 dependency invalidation 规则。
- `tools/asset-processor`：当前提供 read-only dry-run CLI 和受控 `execute` CLI。dry-run 可读取显式 source root，或读取
  `asharia.project.json` 中的 `assetSourceRoots` / `assetDiscovery.ignoredDirectories`，再使用显式
  `--target-profile` 和可选 product manifest 输出稳定文本报告；execute 可读取显式 source root、target
  profile、输出根目录和可选旧 product manifest，执行 placeholder product writer 并写出 product blob/cache
  与 product manifest。它仍不接真实 importer、watcher、后台 worker、热重载、GPU upload 或 editor UI。
  未来可扩展为开发期/后台进程或 CLI host，使用文件 watcher 调用 `asset-pipeline`，执行具体 importer，
  写入 `build/asset-cache/` 或项目 `.asharia/cache/`，并向
  editor/resource runtime 发布 product 更新通知。
- `apps/editor` / 未来 `editor-core`：只发出 reimport、rename、move、import settings 修改等命令，并展示
  pipeline 状态和诊断；不直接扫描 source tree，不直接写 product cache。
- `asset-core`：继续保持纯身份和数据模型，不拥有 watcher、后台线程、importer、product 文件写入或热更新发布。

进入条件：

- `asset-core` 至少完成 metadata model、product key、dependency 和 catalog 切片。
- `.ameta` IO facade 已有确定性读写方案；metadata discovery baseline 可依赖 `asset_core_io`。
- editor 需要真实文件变更刷新，或 mesh/texture/material product smoke 需要稳定 import/cache 流程时，再进入
  watcher/import 调度和 product cache manifest。

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

## Project descriptor

第一版 `asharia.project.json` 只描述项目身份和资产源发现入口，不是完整工程系统、打包 profile 或
editor workspace：

```json
{
  "schema": "com.asharia.project",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRoots": [
    {
      "rootName": "project-assets",
      "directory": "Assets",
      "sourcePathPrefix": "Assets"
    }
  ],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {
    "ignoredDirectories": [".git", ".asharia", "Derived"]
  }
}
```

规则：

- `assetSourceRoots` 只告诉 scanner 从哪些 project-relative 目录查找 source asset 和 `.ameta` sidecar。
- `rootName` 用于日志/诊断；`directory` 是相对 project descriptor 的真实目录；`sourcePathPrefix` 是写入
  `.ameta` 和 pipeline 诊断的 canonical source path 前缀。
- `assetCacheRoot` 只表示 generated asset cache root policy；第一版不在 descriptor 中固定
  `productManifestPath`。
- `targetProfile` 仍由 `asset-processor dry-run --target-profile` 或未来 cook/package 命令显式提供，不写入
  project descriptor。
- 不写 `targetProfiles`、`assetProfiles`、package/export/signing/staging 设置、watcher 状态、dirty queue、
  Asset Browser state、Material Editor state、editor dock layout、runtime resource handle 或 GPU upload state。

## Metadata 文件

第一版 `.ameta` 使用确定性 JSON 子集，后续可由 `packages/archive` 和 `packages/persistence` 统一读写：

```json
{
  "schema": "com.asharia.asset.metadata",
  "schemaVersion": 1,
  "guid": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetType": "com.asharia.asset.Texture2D",
  "sourcePath": "Content/Textures/Crate.png",
  "sourceHash": "1000f00d1234cafe",
  "settingsHash": "0a43b95e39b77b67",
  "importer": {
    "id": "com.asharia.importer.texture2d",
    "version": 1
  },
  "settings": {
    "colorSpace": "srgb",
    "generateMipmaps": "true",
    "compression": "auto"
  }
}
```

规则：

- `.ameta` path 与 source path 一一对应，建议命名为 `<source-file>.ameta`。
- `sourcePath` 用于诊断、catalog 查询和 relocation，不作为引用 ID；真实引用以 GUID 为准。
- `sourcePath` 必须是 project-relative generic path，例如 `Content/Textures/Crate.png`。它不能为空，
  不能是 drive/UNC/absolute path，不能包含反斜杠、`.` / `..` segment、空 segment 或尾随 slash。
  未来 source scanner 必须先把文件系统路径转换为该 canonical 字符串，再写入 `.ameta` 或交给
  `asset-pipeline` discovery。
- `sourceHash` 和 `settingsHash` 在当前 v1 IO facade 中使用 16 位小写十六进制 `uint64` 文本；完整
  SHA-256 或平台化 content hash 等后续 asset-pipeline 再扩。
- Editor/catalog snapshots must treat the current `asset-pipeline` source snapshot hash as the
  authority for active product-key comparison. The `.ameta` `sourceHash` is the metadata file's
  recorded source hash; it must not make a changed source look Ready after planning has produced a
  newer snapshot hash.
- `settings` v1 只接受 string key/value，并按文件顺序计算 deterministic settings hash；typed import
  settings 留给后续 editor/importer settings schema。
- `sourceHash`、`settingsHash` 和 `importer.version` 共同影响 product key。
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
- Catalog/browser `Ready` state must compare the active view's expected full product key, including
  dependency hash and target profile hash, when that expected key is available. A manifest product
  with the same GUID/source/settings but a different dependency hash or target profile is stale for
  the active view.
- Product record 可写入 generated manifest，不能替代 source `.ameta`。
- Product manifest v1 属于 `asset-pipeline` IO 边界，记录 `schema`、`schemaVersion` 和 `products`。
  每个 product 记录保存 GUID、asset type id、importer id/version、source/settings/dependency/target
  hash、computed product key hash、relative product path、product size 和 product hash；它不保存 editor-only
  import settings、不保存 runtime pointer 或 GPU handle。

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

`AssetHandle<T>` 不等同于 loaded resource。当前 `packages/resource-runtime` 提供第一版 CPU-only
状态合同：

```cpp
enum class RuntimeResourceState {
    Pending,
    Ready,
    Failed,
};

struct RuntimeResourceTicket {
    RuntimeResourceKey key;
    std::uint64_t generation;
};

struct RuntimeResourceRecord {
    RuntimeResourceKey key;
    RuntimeResourceState state;
    std::uint64_t generation;
    AssetProductKey expectedProductKey;
};
```

规则：

- scene、material、script 保存 `AssetHandle<T>` 或 `AssetReference`。
- runtime resource registry 只保存 GUID / asset type / product key / generation / diagnostics，不保存 source
  path 或 editor-only pending marker。
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

- `material-core` 当前提供 CPU-only material resource signature、descriptor contract、shader/signature
  compatibility validation 和 pipeline key hash；它消费的是稳定合同，不直接读 `.ameta` 或 product cache。
- Material 阶段后续消费 `asset-core` 的 GUID、product key 和 dependency data。
- Pipeline cache / Vulkan pipeline creation 仍属于 renderer/RHI 层；`asset-core` 只提供 shader/material
  source identity 和 product identity。
- GPU upload 由 resource runtime 或 renderer backend 负责，不能放进 `asset-core`。

### Editor

- Asset Browser 消费 catalog view。
- Inspector 修改 import settings 时生成 editor command，更新 `.ameta` 后触发 reimport request。
- Editor UI 不直接修改 product cache；它只请求 import、展示状态和诊断。
- 当前 Asset Browser shell 已通过 #76 / PR #77 落地面板、菜单、dock 和 Lucide icon resolver 合同。
- #78 / PR #79 已将该 shell 推进到 public `asset-core` catalog view model：editor 可消费排序稳定的
  source/product 诊断 rows，但仍不从 panel 执行扫描、读取 manifest 文件、写 product cache 或触发 import。
- #80 正在补 editor-owned read-only project catalog snapshot service：它可组合 `project-core` descriptor、
  `asset-pipeline` source scan / discovery / snapshot / import planning 和 `asset-core` catalog view；panel 仍只消费
  snapshot/view facts，不拥有 watcher、hot reload、import execution、product writes、runtime loading 或 GPU upload。
- #80 已新增 `EditorAssetCatalogStore`，Asset Browser 通过 panel draw context 消费当前 `AssetCatalogView`；无项目时使用
  deterministic fixture，交互式运行可通过 `ASHARIA_EDITOR_PROJECT` 加载静态 project snapshot；普通 editor smoke 仍走
  fixture，`--smoke-editor-asset-browser` 会加载临时 snapshot-backed project catalog 来验证启动到 panel context 的路由。
- #81 推进 texture import profile classification：`.png`、`.jpg`、`.hdr`、`.exr` 等仍只是 source file
  format，不能直接等同于 `Texture2D`。`.ameta` / importer settings 通过 `texture.profile` 描述 asset
  semantic，当前 catalog-facing canonical profiles 为 `texture2d`、`sprite-sheet`、`texture-cube` 和
  `skybox`；未知 profile 必须作为 source-metadata diagnostic 显示，而不是静默降级为 Texture2D。
- Texture profile interpretation lives in `asset-pipeline` as a source/import metadata adapter. `asset-core`
  keeps only the generic `AssetCatalogSourceFacet` view extension model and must not depend on texture importer
  constants or profile names. `tools/check-asset-boundaries.ps1` is the lightweight repository guard for this boundary.
- Sprite sheet 在当前切片只暴露稳定 sub-asset id、display name 和 role，供 Asset Browser filter/detail/icon
  resolver 使用；rect、pivot、packing、atlas bake、GPU upload、preview texture、IBL bake 和 Import Settings 编辑仍由后续
  importer/resource/runtime/editor command 切片承接。
- #86 adds the first editor command contract for import settings without turning the UI into an importer. The current
  command edits only `texture.profile`, rewrites the source `.ameta` with a recomputed `settingsHash`, supports
  undo/redo, and records a reimport request fact for a later scheduler. It does not decode PNG/JPG/HDR/EXR files, create
  Texture2D/TextureCube runtime resources, bake skyboxes, update product manifests or upload GPU textures.
- #99 / #100 now have the first Asset Browser control-surface path: selected texture rows can edit `texture.profile`
  through the editor command/history path, record pending reimport on real metadata changes, and show/clear that pending
  marker for the selected source and target profile. Catalog rows may also show a separate pending marker beside their
  product state, but this pending state is useful only for Asset Browser / Inspector status. It is not a source of truth for
  product freshness and does not refresh the catalog, execute importers, write product cache files, invalidate runtime
  handles or upload GPU resources. `--smoke-editor-asset-browser` now covers this path with a temporary snapshot-backed
  `.png` texture-profile row and verifies one reimport request, one pending reimport entry and one matching pending row.
  Pending markers are keyed for editor coordination by source GUID when available, with sourcePath as the fallback
  identity; neither key is a product cache key or runtime resource handle. The pending state can also produce a
  deterministic read-only work snapshot with sorted changed-setting keys for a future scheduler handoff, but that snapshot
  is still coordination data and does not execute imports, refresh catalog truth, write products or allocate runtime
  resources.
  #102 adds the first explicit editor catalog refresh service: it rebuilds an `EditorAssetCatalogStore` snapshot from the
  same project/product-manifest/target-profile request, so real `.ameta` changes can become read-only catalog facts while
  import execution, product manifest/blob writes, runtime resource allocation and GPU upload remain out of scope.
  The selected-row Import Settings UI can also read the canonical `texture.profile` back from the source `.ameta` after
  execute, undo and redo so the visible draft/baseline follows the persisted metadata while product truth remains owned by
  catalog planning and product manifests.
- The current answer for image sources is therefore: source format (`.png`) is separate from catalog/runtime semantic.
  `texture.profile=texture2d` means one texture semantic, `sprite-sheet` means the source owns read-only sprite sub-asset
  facts, and `texture-cube` / `skybox` describe future cube/skybox import semantics. Runtime Texture2D/TextureCube
  allocation remains a later importer/product/resource stage.
- Sprite sheet sub-asset stable id must be unique within one source asset. Duplicate ids are reported as
  source-metadata warnings and skipped from the catalog-facing sub-asset list so future single-sprite
  references and icon/filter overrides are not ambiguous.
- #83 收紧 editor-facing catalog product state：Asset Browser 的 `Ready` 不再由 raw manifest 中“同
  GUID/source/settings”的 product 推断，而是由 active import plan 产生的 expected full product key 与 manifest
  product key 完整匹配决定。官方方向对齐
  [Unity AssetDatabase](https://docs.unity3d.com/Manual/AssetDatabase.html) artifact dependencies、
  [Godot import process](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html) 和
  [Unreal Derived Data Cache](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-derived-data-cache-in-unreal-engine)：
  import/cook output 是带 source、settings、dependencies 和 target/platform context 的可再生成 derived data，而不是单靠
  source extension 或 GUID 即可确认的新鲜资源。
  `AssetCatalogViewOptions::expectedProductKeys` is therefore the readiness authority. If a caller passes product records
  without expected full keys, the catalog can expose those records as stale/unconfirmed but must not mark the row `Ready`.
- #84 明确 `.ameta` `sourceHash` 在 v1 里只是 metadata-side recorded source information，active import
  planning 以当前 `AssetSourceSnapshot` hash 作为 product key freshness authority。若两者不同，pipeline 产生
  non-blocking warning diagnostic，editor 和 asset-processor 可展示漂移；这不阻止 planning/execution 使用当前 snapshot
  hash 生成 request/product key。字段改名为 `lastImportedSourceHash` / `lastObservedSourceHash` 或移出 `.ameta`
  属于后续 schema migration slice。

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

当前状态：

- 已落地 `AssetHandle<T>`、`AssetReference`、`makeAssetReference()` 和最小
  `validateAssetReference()`。类型约束仍由后续 metadata、catalog 和 load policy 继续补全。

验收：

- smoke 覆盖 typed handle、untyped reference、type mismatch 诊断。

### 切片 D：Metadata model

交付：

- `SourceAssetRecord`。
- `ImporterId` / `ImporterVersion`。
- metadata schema/version constants。
- deterministic settings hash 输入模型。

当前状态：

- 已落地 `SourceAssetRecord`、`ImporterId`、`ImporterVersion`、metadata schema/version 常量、
  settings hash helper 和最小 source record 校验。`.ameta` IO、catalog mutation 和 product key
  仍在后续切片实现。

验收：

- smoke 覆盖 duplicate GUID/path、missing type、settings hash 稳定性。

### 切片 E：Product key 与 dependency

交付：

- `AssetProductKey`。
- `AssetProductRecord`。
- `AssetDependency`。
- key/hash 组合 helper。

当前状态：

- 已落地 `AssetProductKey`、`AssetProductRecord`、`AssetDependency`、dependency hash、
  target profile hash 和 product key hash helper。Product manifest IO 已由 `asset-pipeline`
  拥有；import planning 已由 `asset-pipeline` 负责生成 deterministic product path proposal、
  cache hit/miss 和 import request；deterministic placeholder product execution 已由 `asset-pipeline`
  负责把 import request + explicit source bytes 写成 product blob 与 manifest。真实 importer 执行和 invalidation 调度仍由后续
  asset-processor / asset-pipeline 切片实现。

验收：

- smoke 覆盖 source hash、settings hash、target profile 改变时 product key 改变。

### 切片 F：Catalog

交付：

- `AssetCatalog`。
- `findByGuid()` / `findBySourcePath()`。
- explicit add/update/remove command API。

当前状态：

- 已落地 `AssetCatalog`、`findByGuid()`、`findBySourcePath()`、`sources()` 和显式
  `addSource()` / `updateSource()` / `removeSource()`。source path relocation 必须通过
  `AssetCatalogRelocationPolicy::AllowPathChange` 显式开启；catalog 仍不做后台扫描、
  文件 watcher、`.ameta` IO 或 loaded resource 管理。

验收：

- smoke 覆盖查询、重复 GUID、重复 path、relocation policy。

### 切片 G：Metadata IO

进入条件：schema-first 的 `packages/archive` 和 `packages/persistence` 合并并稳定。

交付：

- `.ameta` read/write facade。
- strict parse diagnostics。
- byte-for-byte deterministic write。

当前状态：

- 已落地 `asharia::asset_core_io` 可选 target，提供 `.ameta` text/file read-write facade。
- IO 使用 `packages/archive` strict JSON facade；`asset_core` identity/catalog target 仍只依赖 core。
- `.ameta` v1 保存 schema、schemaVersion、guid、assetType、sourcePath、sourceHash、settingsHash、
  importer id/version 和 string settings。
- `sourcePath` validation 已收敛到 `asset-core` 的单一 canonical contract，`.ameta` IO、catalog record
  validation 和 `asset-pipeline` discovery 共用该规则。

验收：

- `asharia-asset-core-smoke-tests` 覆盖 `.ameta` deterministic round-trip、file round-trip、
  settings hash mismatch、malformed input、missing field、unknown member、non-string setting value 和
  非规范 `sourcePath`。

### 切片 H：Asset-pipeline metadata discovery baseline

进入条件：`asset-core` metadata、catalog 和 `.ameta` IO 边界稳定。

交付：

- `packages/asset-pipeline` package 骨架。
- 显式 source/.ameta 条目 discovery facade。
- deterministic manifest / `AssetCatalog` 输入和结构化 diagnostics。

当前状态：

- 已落地 `asharia::asset_pipeline` target，public API 只依赖 `asset-core`，实现内部通过 `asset_core_io`
  读取 `.ameta`。
- 已覆盖 valid discovery、missing metadata、malformed metadata、source path mismatch、duplicate GUID、
  duplicate source path、invalid entry、entry 非规范 `sourcePath` 和 metadata 非规范 `sourcePath`。
- 仍不做 watcher、import 调度、product cache manifest、具体 importer、GPU upload、Asset Browser 或
  Material Editor。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 deterministic discovery 和 discovery negative paths。

### 切片 I：Asset-pipeline source snapshot baseline

进入条件：metadata discovery baseline 和 canonical `sourcePath` 合同稳定。

交付：

- `packages/asset-pipeline` 增加显式 source file snapshot/hash facade。
- 输入为 canonical sourcePath + source file path；不扫描目录、不推断 source tree。
- 校验 sourcePath、缺失 source 文件、非普通文件和重复 source path。
- 对 source 文件字节计算确定性 v1 `sourceHash`，供后续 import plan / product key 使用。
- source snapshot 与 `.ameta` metadata IO 保持分离，不写 `.ameta`。

当前状态：

- 已落地 `AssetSourceSnapshotEntry` / `AssetSourceSnapshot` / `AssetSourceSnapshotDiagnostic` 和
  `snapshotAssetSourceFiles()`。
- 仍不做 watcher、import 调度、product cache manifest、具体 importer、GPU upload、Asset Browser 或
  Material Editor。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 deterministic hash、content change、missing source file、
  non-regular source file、invalid sourcePath 和 duplicate source path。

### 切片 J：Asset-pipeline product manifest IO baseline

进入条件：product key/dependency 数据模型、metadata discovery baseline 和 source snapshot baseline 稳定。

交付：

- `packages/asset-pipeline` 增加 product manifest document 和 text/file read-write facade。
- IO 使用 `packages/archive` deterministic JSON facade；`asset-core` identity/product headers 不依赖 JSON。
- Product manifest v1 保存 schema、schemaVersion 和 products array。
- 每个 product 记录保存 GUID、asset type id、importer id/version、source hash、settings hash、
  dependency hash、target profile hash、computed product key hash、relative product path、product size 和
  product hash。
- 校验 schema/schemaVersion、malformed input、duplicate/missing/unknown fields、non-zero key/hash 字段、
  canonical product path、duplicate product key/key-hash/path 和 product key hash mismatch。

当前状态：

- 已落地 `AssetProductManifestDocument`、`validateAssetProductPath()`、
  `validateAssetProductManifestDocument()` 和 product manifest text/file read-write facade。
- `asharia::asset_pipeline` 通过 private `asharia::archive` 依赖实现 IO，public API 仍只暴露 product
  manifest 数据和 Result，不把 JSON 类型带进 asset-core。
- 仍不做 source scan、watcher、import 调度、product blob 生成、cache hit/miss scheduling、GPU upload、
  Asset Browser 或 Material Editor。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 deterministic text/file round-trip、malformed input、
  duplicate/missing/unknown fields、duplicate product key/path、invalid product path 和 product key hash
  mismatch。

### 切片 K：Asset-pipeline import planning baseline

进入条件：metadata discovery、source snapshot/hash 和 product manifest IO baseline 稳定。

交付：

- `packages/asset-pipeline` 增加 import planning facade。
- 输入为显式 discovered source records、source snapshots、existing product manifest 和 target profile。
- 规划阶段只产出 cache hits 与 import requests；不执行 importer、不写 product blob、不做 watcher。
- Import request 保存 planned source、string settings、dependency list、product key、deterministic product
  path proposal 和 miss reason。
- Cache hit 必须匹配完整 product key，不能只按 GUID 或 source path 命中。
- 诊断 invalid target profile、invalid/duplicate source、invalid/duplicate snapshot、missing snapshot 和
  invalid product manifest。

当前状态：

- 已落地 `AssetImportPlanResult`、`AssetImportRequest`、`AssetImportCacheHit`、
  `makeAssetImportProductPath()` 和 `planAssetImports()`。
- Dependency v1 由 source file hash 和 import settings hash 组成；product key 继续携带 GUID、asset type、
  importer id/version、source hash、settings hash、dependency hash 和 target profile hash。
- 仍不做 source scan、watcher、import worker、具体 importer、product blob/cache execution、GPU upload、
  Asset Browser 或 Material Editor。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 deterministic cache hit/miss planning、source hash change、
  import settings change、missing snapshot、duplicate source、duplicate snapshot 和 invalid target profile。
- `asharia-asset-pipeline-header-tests` 覆盖 import planning public header self-contained include。

### 切片 L：Asset-pipeline source tree scan baseline

进入条件：metadata discovery、source snapshot/hash、product manifest IO 和 import planning baseline 稳定。

交付：

- `packages/asset-pipeline` 增加 deterministic source tree scan facade。
- 输入为显式 source root、可选 sourcePath prefix、metadata sidecar suffix 和 ignored directory names。
- 输出 canonical sourcePath、source file path 和 metadata path；`.ameta` 只作为 metadata sidecar，不作为 source asset。
- 诊断 invalid root、invalid sourcePath prefix / sourcePath、missing metadata、orphan metadata 和 metadata path collision。
- 保持输出按 canonical sourcePath 确定性排序。

当前状态：

- 已落地 `AssetSourceScanRequest`、`AssetSourceScanEntry`、`AssetSourceScanResult` 和 `scanAssetSourceTree()`。
- 仍不读取 `.ameta`、不 hash source bytes、不桥接 import planning、不执行 importer、不写 product blob/cache、
  不做 watcher、GPU upload、Asset Browser 或 Material Editor。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 deterministic scan、ignored directory、missing metadata、orphan
  metadata、invalid root 和 invalid sourcePath prefix。
- `asharia-asset-pipeline-header-tests` 覆盖 source scan public header self-contained include。

### 切片 M：Asset-pipeline scan-to-planning bridge baseline

进入条件：source scan、metadata discovery、source snapshot/hash、product manifest IO 和 import planning baseline 稳定。

交付：

- `packages/asset-pipeline` 增加 scan-to-planning bridge facade。
- 输入为 `AssetSourceScanRequest`、existing product manifest 和 target profile。
- 内部只顺序调用 source scan、metadata discovery、source snapshot/hash 和 import planning。
- 输出保留 scan、discovery、snapshot 和 plan 的原始 result，方便后续 CLI 按阶段报告 diagnostics。
- 前置阶段失败时停止，避免后续阶段产生噪声诊断。

当前状态：

- 已落地 `AssetScannedImportPlanRequest`、`AssetScannedImportPlanResult` 和
  `planScannedAssetImports()`。
- 仍不提供 CLI、不执行 importer、不写 product manifest/blob/cache、不做 watcher、dependency invalidation、
  GPU upload、Asset Browser 或 Material Editor。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 scan-to-planning request planning、cache hit planning、
  scan diagnostic stop、discovery diagnostic stop 和 planning diagnostic passthrough。
- `asharia-asset-pipeline-header-tests` 覆盖 scanned import planning public header self-contained include。

### 切片 N：Asset-processor dry-run CLI baseline

进入条件：source scan、metadata discovery、source snapshot/hash、product manifest IO、import planning 和
scan-to-planning bridge baseline 稳定。

交付：

- `tools/asset-processor` 增加 root build 可生成的 `asharia-asset-processor` executable。
- `dry-run` 命令接受 source root、sourcePath prefix、target profile、可选 product manifest 和 ignored
  directory names。
- dry-run 只复用 source scan、metadata discovery、source snapshot/hash 和 import planning facade，输出
  scan/discovery/snapshot/planning summary、import requests、cache hits 和 staged diagnostics。
- product manifest 缺省时使用 empty manifest；manifest read failure 带路径上下文报错。

当前状态：

- 已落地 `asharia-asset-processor dry-run ...` 和 `--smoke-dry-run`。
- `dry-run --project <path> --target-profile <name>` 已由 project descriptor slice 接入。
- 仍不做 watcher、importer execution、product manifest/blob/cache 写入、dependency invalidation、
  GPU upload、Asset Browser 或 Material Editor。

验收：

- root CMake build 生成 `asharia-asset-processor`。
- `asharia-asset-processor --smoke-dry-run` 覆盖 request/cache-hit 报告、scan diagnostic 和 product
  manifest read diagnostic。

### 切片 N.5：Project descriptor for asset source discovery

进入条件：asset-processor dry-run CLI baseline 稳定。

交付：

- `packages/project-core` 增加最小 Asharia project descriptor model 和 deterministic JSON IO facade。
- `asharia.project.json` v1 只记录 `projectName`、`projectId`、`assetSourceRoots`、`assetCacheRoot` 和
  `assetDiscovery.ignoredDirectories`。
- `assetSourceRoots` 使用清晰字段名：`rootName`、`directory`、`sourcePathPrefix`。
- `asharia-asset-processor dry-run --project <path> --target-profile <name>` 读取 descriptor，解析
  project-relative source root，并沿用 read-only dry-run 报告。

不做：

- 不在 descriptor 中加入 `targetProfiles`、`assetProfiles`、cook/package/export 设置或固定
  `productManifestPath`。
- 不做 watcher、import 调度、product manifest/blob/cache 写入、hot reload、Asset Browser、Material Editor、
  editor project browser、runtime resource loading、GPU upload、RenderGraph、RHI 或 renderer changes。

验收：

- `asharia-project-core-smoke-tests` 覆盖 descriptor round-trip、malformed input、duplicate/missing
  fields、invalid project id、invalid source root 和 duplicate root/ignored directory negative paths。
- `asharia-asset-processor --smoke-dry-run` 覆盖 project descriptor 输入模式。

### 切片 N.75：Deterministic product execution baseline

进入条件：asset-processor dry-run CLI、project descriptor discovery 和 graph-visible buffer upload baseline 稳定。

交付：

- `packages/asset-pipeline` 增加 `AssetProductExecutionRequest` / `AssetProductExecutionResult`，
  消费已有 import plan、旧 product manifest、显式 source bytes、product output root 和 manifest output path。
- 第一版只提供 deterministic placeholder product writer：输出记录 source/product/importer/settings/hash
  上下文和原始 source bytes，不接 glTF、PNG、DDS、shader、material 或 texture importer。
- Product manifest 继续使用既有 manifest IO；product path 仍由 product key 派生，写入输出根目录下的
  manifest-relative path，不保存绝对 source path 或 machine-local cache path。
- `tools/asset-processor execute ...` 写入 product blob/cache 与 product manifest；`--smoke-product-execution`
  覆盖首次写入、带 manifest 的 unchanged-input cache hit、source bytes 变化后的新 product 输出。

当前状态：

- 已落地 `executeAssetProducts()` 和 `asharia-asset-processor execute`。
- 仍不做真实 source-format importer、watcher、background worker、dependency invalidation、hot reload、
  Asset Browser、Material Editor、runtime resource loading、RenderGraph、RHI、renderer 或 GPU upload changes。

验收：

- `asharia-asset-pipeline-smoke-tests` 覆盖 deterministic product write、unchanged-input cache hit rerun
  和 source-bytes/hash mismatch diagnostic。
- `asharia-asset-processor --smoke-product-execution` 覆盖 CLI product output、manifest output、rerun cache hit
  和 source bytes change 触发的新 product 输出。

### 切片 O：Resource upload baseline

进入条件：RenderGraph storage/MRT/compute 和 resource lifetime 相关分支合并。

交付：

- 当前第一步已落地 graph-visible buffer copy baseline：`builtin.transfer-copy-buffer`、`CopyBuffer`
  command summary 和 `--smoke-buffer-upload` 覆盖显式 upload payload 经 staging buffer 到 device-local
  buffer 再到 readback buffer。
- 最小 texture product/upload smoke 已接入：`--smoke-texture-upload` 使用 `asset-pipeline` 的 deterministic
  placeholder product execution 生成 Texture2D product blob，从 product payload 上传到 Vulkan image，再通过
  RenderGraph-visible `CopyBufferToImage` / `CopyImageToBuffer` readback 验证。
- 后续仍需 Mesh product record、runtime resource handle、product cache hit/missing-product 诊断矩阵和真实
  texture importer。
- staging/upload 仍放在 RHI/resource runtime，不放在 `asset-core`、`project-core` 或 `.ameta`。

验收：

- `--smoke-buffer-upload` 证明基础 buffer upload/copy work 是 RenderGraph-visible，且输入来自显式
  upload payload，不来自 source path 或 metadata IO。
- `--smoke-texture-upload` 证明 texture upload 输入来自 deterministic product payload，最终 GPU image
  能作为 sampled view 暴露；runtime 不直接依赖 source path。
- 后续 `--smoke-mesh-resource` 证明 mesh runtime resource 不直接依赖 source path。

### 切片 P：Runtime resource handle baseline

交付：

- `packages/resource-runtime` 新增 `asharia::resource_runtime` target，target 只依赖 `asset-core`。
- `RuntimeResourceRegistry` 提供 `Pending` / `Ready` / `Failed` 状态、`RuntimeResourceTicket`
  generation、expected `AssetProductKey` 和 failure reason。
- package-local smoke 覆盖 invalid handle、pending -> ready、pending -> failed、stale generation rejection、
  product key mismatch 和 source-path-free diagnostics。

验收：

- runtime resource state 不暴露 source path，不持有 Vulkan / RenderGraph / editor 对象。
- `Ready` 必须绑定完整 `AssetProductKey`；旧 generation 或 product key mismatch 会 fail early。
- `resource-runtime` 可独立构建测试，后续 GPU texture/mesh owner 只能消费它的状态合同，不能把 loader
  逻辑塞回 `asset-core` 或 Asset Browser。

## 并行开发建议

可以现在并行：

| 工作 | 原因 | 注意事项 |
| --- | --- | --- |
| `asset-core` GUID / handle / catalog 头文件和 tests | 只依赖 `core`，不抢 RenderGraph 或 schema/archive/persistence worktree。 | 优先 package-local tests，少碰 sample-viewer。 |
| metadata schema 文档和 `.ameta` 示例 | 纯文档，可和 schema/archive/persistence 分支并行。 | 不把 runtime pointer、GPU handle 或 absolute build path 写进 `.ameta`。 |
| product key / dependency hash 数据模型 | 可独立验证 hash/key 变化规则。 | 不接真实 importer，不读写 generated product。 |
| asset-pipeline metadata discovery | 只消费显式 source/.ameta 条目，可用 package-local tests 验证。 | 不做 watcher、import 调度、product cache 或 editor UI。 |
| asset-pipeline source snapshot/hash | 只消费显式 sourcePath + source file path，可用 package-local tests 验证。 | 不做 source tree scan、watcher、import 调度、product cache 或 editor UI。 |
| asset-pipeline product manifest IO | 只读写 product manifest 文档，可用 package-local tests 验证。 | 不做 importer、product blob/cache execution、GPU upload 或 editor UI。 |
| asset-pipeline import planning | 只比较 source/snapshot/manifest 并产出 plan，可用 package-local tests 验证。 | 不做 watcher、importer 执行、product blob/cache execution、GPU upload 或 editor UI。 |
| asset-pipeline source scan | 只扫描显式 source root 并配对 `.ameta` sidecar，可用 package-local tests 验证。 | 不读取 metadata、不 hash bytes、不桥接 plan、不执行 importer、不做 watcher 或 editor UI。 |
| asset-pipeline scan-to-planning bridge | 只组合既有 scan/discovery/snapshot/planning 阶段，可用 package-local tests 验证。 | 不做 CLI、不执行 importer、不写 manifest/blob/cache、不做 watcher 或 editor UI。 |
| asset-processor dry-run CLI | 只做 read-only CLI reporting，可用 `--smoke-dry-run` 验证。 | 不执行 importer、不写 manifest/blob/cache、不做 watcher、hot reload、editor UI 或 GPU upload。 |
| asset-pipeline / asset-processor product execution | 只消费已有 import plan 和显式 source bytes，写 deterministic placeholder product blob/manifest。 | 不接真实 importer、不做 watcher、dependency invalidation、runtime resource loading 或 GPU upload。 |
| material-core signature / pipeline key | 只定义 CPU-side material resource signature、compatibility diagnostics 和 deterministic pipeline key hash，可用 package-local tests 验证。 | 不做 `.amat` IO、不执行 importer、不写 product cache、不创建 Vulkan pipeline/cache、不做 editor UI。 |

等待后再做：

| 工作 | 等待项 |
| --- | --- |
| `tools/asset-processor` / 完整 import 调度 | 等后续 slice 接入真实 importer、dependency invalidation 和调度策略。 |
| `--smoke-mesh-resource` / `--smoke-texture-upload` | 等基础 `--smoke-buffer-upload` 和 deterministic product execution 之后接入真实 mesh/texture product data、resource owner 和 lifetime。 |
| Full Asset Browser / import settings UI | 第一版 shell/icon contract 和 public catalog view model 已落地；#80 正在补 read-only project catalog snapshot service 与 Asset Browser context 接线。import settings 编辑和 reimport request 仍等 editor command/transaction 与 catalog snapshot 稳定。 |
| Material asset IO / Material Editor | 等 material-core 合同、asset product execution 和 editor transaction 稳定。 |

不建议现在做：

- 文件系统 watcher。
- 后台 import worker。
- 完整 glTF/texture importer。
- 完整 GPU upload owner；当前只允许保留 graph-visible buffer copy baseline。
- 热重载。
- 资产数据库 UI。
- package marketplace。

## 最小 smoke 建议

Package-local tests：

- `asharia-asset-core-smoke-tests --guid`：UUID parse/format、invalid GUID。
- `asharia-asset-core-smoke-tests --catalog`：add/find、重复 GUID/path 失败。
- `asharia-asset-core-smoke-tests --product-key`：source/settings/tool/target 改变会改变 key。
- `asharia-asset-core-smoke-tests --dependency`：dependency hash 和 missing dependency diagnostics。
- `asharia-asset-pipeline-smoke-tests`：source tree scan、scan-to-planning bridge、显式 source/.ameta
  discovery、source snapshot/hash、product manifest IO、import planning、缺失/坏 metadata、路径不匹配、重复
  GUID/path、缺失/非普通 source file、非规范 sourcePath、malformed product manifest、duplicate product key/path、
  product key hash mismatch、import planning diagnostics 和 deterministic product execution。

未来 CLI smoke：

- `asharia-asset-processor --smoke-dry-run`：read-only dry-run 覆盖 request/cache-hit 报告、scan diagnostic
  和 product manifest read diagnostic。
- `asharia-asset-processor --smoke-product-execution`：product execution 覆盖 product blob/manifest 写入、
  unchanged-input cache hit rerun 和 source bytes change 后的新 product 输出。
- `--smoke-asset-metadata-roundtrip`：`.ameta` deterministic round-trip。
- `--smoke-buffer-upload`：显式 upload payload 通过 RenderGraph `CopyBuffer` 进入 device-local buffer 并读回。
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
