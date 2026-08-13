# ADR-0014：Studio Resource Browser 消费有界只读 Catalog snapshot

状态：Accepted；由 GitHub Slice #385 实现

日期：2026-08-13

关联：GitHub Epic #97、Slice #385；延续
[ADR-0008](0008-authoritative-project-session.md) 的 active-project owner、
[ADR-0007](0007-studio-frontend-hard-cut.md) 的 Application/EngineBridge/Presentation 单向依赖，以及
[Asset 与 Resource 架构](../../../../docs/systems/asset-architecture.md) 的 source/product/runtime 分层。

## 背景

Studio 已有 authoritative `ProjectSession` 和可停靠的 Project panel，但此前没有真实资产数据来源。C++
Dear ImGui editor 已有 project snapshot-backed Asset Browser；其 catalog query composition 原先位于
`apps/editor` 私有源码中。若 Avalonia 再实现一套 descriptor parsing、source scan 或 catalog merge，两个 editor
前端会形成不同的资产真相；若 Project panel 直接调用 importer、`ResourceRuntime` 或 renderer，又会把浏览行为、
产品生成、运行时加载和 GPU 生命周期混成一个 owner。

本 Slice 只关闭第一条可靠纵切：活动项目的未加载资产 facts 经过一个 UI-neutral query，成为 Application-owned
immutable snapshot，再由 Project panel 以只读方式导航、搜索、筛选和选择。它不承诺模型导入、运行时 mesh、GPU
resource 或缩略图。

## 成熟引擎依据

### Unreal Engine（主要先例）

- [Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine)
  在 editor 启动时收集未加载资产信息，`FAssetData` 允许列表和筛选而不加载对象；Content Browser 是主要 consumer。
- [Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine)
  把 sources、asset view、搜索和筛选作为同一 browser 的不同投影。

Asharia 采用“浏览器查询 unloaded catalog facts，而不是先加载 runtime object”的模式。当前没有常驻 registry
或自动磁盘监听，因此只实现显式 snapshot/refresh；不把 Unreal 的 global singleton、UObject/package API、自动更新
或资产 mutation surface 机械复制进来。

### Unity

- [Asset Database](https://docs.unity3d.com/Manual/AssetDatabase.html) 明确区分 source asset database 与
  import artifact database；GUID、source hash、dependency 和 artifact files 属于不同事实。
- [Project window](https://docs.unity3d.com/Manual/ProjectView.html) 使用左侧 folder hierarchy、右侧 asset pane，
  并支持按范围、文本和类型筛选；资源只在需要时加载。

Asharia 采用 source/product 分离和 folder + asset-list 投影，但 product freshness 仍来自
`AssetProductKey`/manifest 事实，不从扩展名、缩略图或 UI 状态猜测；本 Slice 不调用等价于
`LoadAssetAtPath` 或 `ImportAsset` 的能力。

### Godot

- [First look at the editor](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html)
  将 FileSystem dock 定位为项目脚本、图像、音频等文件的浏览入口。
- [Import process](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html) 将 source
  文件留在项目中，把 imported result 放入隐藏 `.godot/imported/` cache，并要求 runtime 通过 Resource Loader
  解析导入结果。
- [EditorResourcePreview](https://docs.godotengine.org/en/stable/classes/class_editorresourcepreview.html) 把 preview
  生成建模为独立、可排队的 editor service。

Asharia 采用 FileSystem-style browser projection，以及 source、derived product、runtime load 与 editor preview 分离；
Resource Browser 不直接 decode source
来生成缩略图，也不把 source file 当成 runtime object。自动 reimport、Resource Loader 和 preview queue 均留给后续
owner Slice。

### O3DE

- [Asset Cache](https://docs.o3de.org/docs/user-guide/assets/pipeline/asset-cache/) 区分 Asset Processor 专用
  Asset Database、运行时 Asset Catalog 和 product cache，并明确 product 由 processor 拥有、不能直接编辑。
- [Asset Browser](https://docs.o3de.org/docs/user-guide/editor/asset-browser/) 采用 folder navigation、asset list/table、
  search/type filter；[Asset Processor UI](https://docs.o3de.org/docs/user-guide/assets/asset-processor/interface/)
  则单独展示 source/product/jobs 和处理诊断。

Asharia 采用 Processor/Catalog/Browser 的 owner 分离和 source/product 状态可见性；不在本 Slice 引入后台进程、job
queue、自动监控或完整 asset database。

## 决策

### 1. UI-neutral query 归 `editor-content`

新增 source-boundary package `packages/editor-content`（identity `com.asharia.editor-content`，owner domain
`editor`，planned root `com.asharia.system.editor`）：

- `asharia::editor_content` 组合 `project_core_io`、`asset_core`/`asset_core_io` 与 `asset_pipeline` 的既有
  read/scan/discover/snapshot/plan API，返回 `EditorAssetCatalogSnapshot`；
- `asharia::editor_content_native` 只把该 value snapshot 投影为 C11-compatible ABI v1 和 closed JSON schema
  `com.asharia.editor.assetCatalogSnapshot` version 1；JSON 由 schema-specific bounded writer 直接写入一个有界
  response string，不先构造第二棵动态 JSON object graph；
- `apps/editor` 继续拥有 ImGui-only `EditorAssetCatalogStore`、fixture、icon/report 和 import-settings command，
  但不再私有拥有 query composition；Dear ImGui 与 Avalonia 共用同一 package query truth；
- package 不依赖 Avalonia、ImGui、`resource-runtime`、renderer、RHI 或 Vulkan。runtime app 不需要链接它。

Query 读取 canonical project descriptor、显式 source roots、`.ameta` 与可选/default product manifest，产出 resolved
source roots、source-root/folder/asset/sub-asset navigation facts、catalog rows、product state 与 diagnostics。未追踪 source
可以保留为没有伪 GUID/importer/product 的 `NotTracked` row。query 不执行 importer、不写 manifest/blob/cache，也不加载
runtime resource。catalog planning 使用 `DeclaredOnly` 工具依赖策略；refresh 不探测 `PATH`/`VULKAN_SDK`，也不读取
`slangc`/`spirv-val` 等外部工具二进制。无法由显式 facts 证明工具版本的 source 保留为可浏览 row，并产生可行动 Warning，
不会生成 provisional product key。project-relative source root 必须 canonicalize 后仍位于 canonical project root 内；
symlink/junction（包括藏在中间路径段的重定向）不能把浏览或 Asset Processor 扫描引出项目边界。

### 2. 跨语言合同必须严格且有界

`asharia_editor_content_query` 使用 `AbiHeader { abiVersion, structSize }`、fixed-width POD、caller-owned response
buffer、typed status 和 payload/message 互斥结果。native export 捕获所有 C++ 异常；C header smoke、native
`sizeof/offsetof` 与 managed explicit-layout tests 对证 ABI。

Studio 默认预算为：

| 预算 | 默认值 | hard/consumer gate |
| --- | ---: | ---: |
| source files | 10,000 | native hard max 1,000,000；managed schema safety ceiling 100,000 rows |
| source bytes | 8 GiB | native hard max 1 TiB |
| diagnostics | 10,000 | native hard max 100,000；managed aggregate max 10,000 |
| JSON payload | 16 MiB | native hard max 64 MiB |
| 单个 JSON string / failure message | 64 KiB | native 与 managed 都验证 |

路径、UTF-8、result spans、exact payload/message partition、schema/version、closed properties、row counts、navigation
parent/depth、GUID、sub-asset stable id、project id 和 query scope 都 fail closed。EngineBridge 每次只调用 native
一次，并租用固定 16 MiB 有界 response buffer；`BufferTooSmall` 视为 native 合同漂移而失败，避免 probe/fill 两次扫描
跨 source mutation 混合两个 snapshot。`Cancelled` ABI status 预留，但 v1 native query 不承诺中途
抢占；managed cancellation 只取消等待/发布，完成后的 stale generation 不得成为 current truth。
source byte budget 在同一次打开/读取/hash 中按实际字节累计，刚好命中预算成功，超出 1 字节即停止；metadata settings、
project roots/ignored directories 与 product manifest records 在 materialize/reserve 前也有显式 count/string gate。

### 3. Application owner 与 newest-request-wins

`ProjectAssetCatalog` 是 process composition 中唯一 project-catalog session owner：

```text
ProjectSession Ready / project change
  -> IAssetCatalogGateway.QueryAsync(scope)
  -> EngineBridge asset catalog ABI + strict parser
  -> ProjectAssetCatalog generation check
  -> immutable AssetCatalogSessionSnapshot
  -> KeepAlive Project panel projection
```

- scope 绑定 `ProjectSessionId + ProjectId + canonical root/file + target profile`；项目切换不跨 scope 复用
  last-known-good；
- 同 scope refresh 可在失败时保留 last-known-good 并进入 `Degraded`；首个失败进入 `Failed`；partial native snapshot
  也进入 `Degraded`，并保留 diagnostics；
- 每个 request 使用单调 generation；只有仍匹配 current scope 和 current generation 的完成结果可以发布，旧请求晚到
  必须丢弃；取消恢复同 scope 的前一稳定状态；
- event 只是 invalidation/publication，subscriber fault 被逐个隔离。App 创建 owner，
  `StudioCompositionSession` 在 `ProjectSession` 前异步销毁它；Project panel 只订阅，不拥有 service。

`Asharia.Studio.Application` 只暴露 immutable DTO 与 `IAssetCatalogGateway` port，不含 P/Invoke、JSON 或 filesystem
implementation。`Asharia.Studio.EngineBridge` 独占 native binding 和 strict JSON translation；Avalonia 不引用 ABI types。

### 4. Resource Browser 只拥有 view state

现有 Project panel 的一个 KeepAlive ViewModel 投影 `NoProject / Loading / Ready / Degraded / Failed`，并拥有：

- source-root/folder location selection；asset 和 sub-asset facts 保留在 snapshot，当前 asset list 与 details 分别投影它们；
- 150 ms debounce text search、type filter、product-state filter；
- 以 `AssetGuid` 为优先、未追踪 source path 为 fallback 的 stable selection key，refresh 后 remap；
- details expansion、local selection 和 empty/degraded/failed presentation；这些均不是 asset metadata、catalog 或 project truth；
- 两个受约束 `VirtualizingStackPanel` list，固定 22 px row；Headless gate 用 10,000 rows 证明不会按 item count
  实例化 controls。Studio production 默认同样限制 10,000 source files；100,000 只是 strict parser 的损坏输入安全
  ceiling，不是当前性能承诺。

Refresh 只请求重建 snapshot。Browser 不直接扫描 filesystem，不执行 importer，不改 `.ameta`/project/manifest/cache，
不创建 `AssetHandle`/`RuntimeResource`，不上传 GPU resource，也不从 source decode thumbnail。产品状态使用
`NotTracked / Current / Missing / Stale / Invalid`；UI 不用宽泛的 `Ready` 冒充具体 product freshness。

## 拒绝项与 Asharia 原因

- 拒绝在 `apps/studio` 重写 descriptor/source/metadata/manifest parser；否则 managed UI 会成为第二资产数据库。
- 拒绝让 `apps/editor` 的 ImGui store、fixture 或 frame context 成为跨前端 API；共享的是 UI-neutral value query，
  不是 host/panel implementation。
- 拒绝 Browser 直接 import/reimport、rename/move/delete、写 filesystem 或 product cache；mutation 必须经后续 typed
  editor command 与 asset-processor owner，并具备 undo/诊断/失效合同。
- 拒绝用 Browser row 或 thumbnail 表示 loaded runtime resource；catalog、`ResourceRuntime`、renderer-owned GPU resource
  与 preview service 是四个不同 owner。
- 拒绝从 OBJ/glTF/FBX source 直接生成临时 preview mesh 以“先显示缩略图”；缩略图必须复用版本化 cooked mesh product
  和 generation-safe runtime/render resource，不能形成旁路 importer。
- 拒绝本 Slice 启动 watcher、后台 importer worker、hot reload、持久 catalog database、全局 service locator 或
  speculative plugin interface；当前显式 refresh 已足以验证边界。

## 后果

- Dear ImGui editor 和 Avalonia Studio 现在可从同一个 package 查询未加载 project asset facts，同时保持各自 UI state、
  icon policy 和 command surface 独立。
- Studio 可在没有 runtime/GPU resource 的情况下可靠浏览 source roots、folders、assets、sub-asset摘要、product state
  和 diagnostics；missing manifest 或部分 source failure 可见且不伪造成功。
- 当前 query 仍会按显式 refresh 扫描、读取并 hash source；它是 bounded vertical slice，不是增量 asset database。
  大项目的 index、watcher、incremental invalidation 与 processor IPC 需要独立性能证据后再引入。
- C ABI 使用完整 JSON snapshot，优先换取 schema 审查与跨语言调试能力；若 16 MiB/10k 默认预算后续成为瓶颈，必须以
  profiling 证明后再设计分页或 packed table ABI，不提前提供兼容双路径。

## 明确延期

以下能力不属于 #385，必须保持边界清晰：

1. 版本化 Mesh cooked product（positions/normals/UV/indices/submeshes/bounds/material references）与首个
   glTF/OBJ importer；
2. `ResourceRuntime` 异步 artifact IO、typed CPU mesh payload、依赖加载、reload 与 generation-safe replacement；
3. renderer-owned vertex/index GPU resource、deferred destruction 和 Scene View 真实 asset-handle binding；
4. Thumbnail/Preview service：只消费同一 cooked product/runtime resource，使用独立 queue/cache/budget，不 decode
   source 建立旁路；
5. asset mutation service、watcher、增量 index、background processor/job status 和 processor IPC；
6. Asset Browser create/rename/move/delete/drag-drop、import settings、multi-select、collections、thumbnail/grid view。

## 验证

- `editor-content` package native smoke：valid/degraded/failure、aggregate file/byte/diagnostic/response/string limits、
  UTF-8、ABI/layout、C include 和 exception containment；
- `asset-pipeline` source scan limit regression 与 existing catalog/import planning tests；
- Application tests：project scope、newest wins、same-scope last-known-good、cross-project isolation、cancel/dispose、
  subscriber fault；
- EngineBridge tests：layout、single-call fixed buffer、typed failure、strict UTF-8/JSON/schema/bounds/scope；
- real DLL acceptance：canonical project -> native JSON -> managed snapshot；
- ViewModel tests：location/search/type/product filters、stable selection、refresh/degraded/empty/dispose；
- Avalonia Headless：narrow no-project state、10,000-row constrained virtualization、degraded last-known-good；
- full native/managed build、encoding、doc-sync、diff 与 repository pre-commit gates。
