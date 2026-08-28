# ADR-0015：Studio 用 typed editor selection 驱动只读 Asset Inspector

状态：Accepted；由 GitHub Slice #388、#398、#402 递进实现

日期：2026-08-13

关联：GitHub Epic #97、Slice #388；延续
[ADR-0009](0009-authoritative-scene-document.md) 的 stable scene-object identity、
[ADR-0013](0013-authoritative-document-transform-undo-redo.md) 的 scene mutation owner、
[ADR-0014](0014-catalog-backed-resource-browser.md) 的只读 catalog snapshot，以及
[Asset 与 Resource 架构](../../../../docs/systems/asset-architecture.md) 的 source/product/runtime owner 分层。

## 背景

Studio 已有两个真实但此前彼此隔离的选择路径：Hierarchy 选择 scene entity 后由现有 Inspector 编辑名称与
local Transform；Resource Browser 则只在自己的 ViewModel 内保存 `AssetSelectionKey`，右侧 details 也只是
Project panel 的局部投影。让 Inspector 直接引用 Project panel、复用某个 asset row 对象，或者在收到 source path
后自行查询文件、加载 `RuntimeResource`，都会形成第二个 catalog truth，并把 panel lifetime、资产身份、运行时加载
和 GPU 生命周期混在一起。

本 Slice 只关闭一条较小的编辑器纵切：Resource Browser 把稳定资产身份发布为 Application-owned typed selection，
Inspector 再从同一 `ProjectAssetCatalog` 的 immutable snapshot 解析并显示只读 catalog facts。它不增加 import、
reimport、source/metadata 写入、runtime load、preview、thumbnail、hot reload 或新的 asset product 合同。

## 成熟引擎依据

### Unreal Engine（主要先例）

- [`FContentBrowserSelection`](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/ContentBrowser/FContentBrowserSelection)
  明确区分 selected assets、folders 与 content items；
  [Content Browser API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/ContentBrowser)
  以 delegate 提供当前 selected asset data，而不是要求 details consumer 反查 widget tree。
- [Unreal Editor Interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface)
  说明 Viewport 与 Outliner 的选择会同步到 Details，并允许 Details 锁定当前对象；
  [Level Editor Details Panel](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-editor-details-panel-in-unreal-engine)
  将其定义为当前选择的 information、utilities 与 functions surface。
- [Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine)
  允许 Content Browser 使用未加载资产 facts，而不先加载对象。

Asharia 采用“typed stable selection → details projection”和“资产 facts 可在 unloaded 状态检查”的 owner 边界；
不复制 Unreal 的全局 singleton、`UObject`/package pointer、反射 property editor、Details customization API、自动加载
或 asset mutation surface。Inspector pin/lock 仍是后续 panel-local 功能，不改变 selection truth。

### Unity

- [Inspector window reference](https://docs.unity3d.com/Manual/UsingTheInspector.html) 明确同一 Inspector 可以根据当前选择
  显示 GameObject、asset 或 component，并可把一个 Inspector 固定到单个 item。
- [Introduction to importing assets](https://docs.unity3d.com/Manual/ImportingAssets.html) 把 project source 与编辑器导入流程
  放在 asset pipeline，而不是把源文件解释责任交给 Inspector。

Asharia 采用一个可区分 scene/entity 与 asset 的 editor selection snapshot，以及 panel-local pin 的目标语义；当前拒绝
Unity 式通用 serialized-object/property reflection、selection object reference 和在 Asset Inspector 内直接改 importer
settings。`AssetSelectionKey` 仍以 GUID 为优先，未追踪 source path 只作受限 fallback，不升级成通用资产 identity。

### Godot

- [Inspector Dock](https://docs.godotengine.org/en/stable/tutorials/editor/inspector_dock.html) 会随 Scene Tree 的 node 选择
  或 FileSystem 中打开的 resource 切换所投影对象，并把 history、search、resource open/save 和 property editing
  作为 Inspector 自己的后续能力。

Asharia 采用“一个 Inspector shell 根据 typed selection 切换明确 presentation”的模式；当前不采用 resource open/save、
内存 resource 创建、通用 property list、history 或隐式即时写回。Godot 的 per-property revert 也不能替代 Asharia
未来 import-settings draft 的整体 Apply/Revert 与 expected-revision gate。

### O3DE

- [O3DE Editor tour](https://docs.o3de.org/docs/welcome-guide/tours/editor-tour/) 明确 Asset Browser 在选中 asset 时显示
  thumbnail/information，而 Entity Inspector 显示当前 entity 的 components；Asset Processor job 状态另由状态栏和
  processor surface 呈现。
- [Asset Browser](https://docs.o3de.org/docs/user-guide/editor/asset-browser/) 保持 asset navigation/preview 与 processor
  生成的 source/product facts 分层。

Asharia 采用 Browser、selection、Inspector projection 与 processor/runtime owner 分离；当前只把 catalog information
移入统一 Inspector，不复制 thumbnail preview、Scene Settings、component editor 或后台 Asset Processor job UI。

## 决策

### 1. typed selection 由 Application 拥有

Application 的 `Asharia.Studio.Application.Selection` 提供 project-scoped immutable editor selection snapshot。
当前不用可组合错误的 enum + optional fields，而以 closed target hierarchy 区分：

```text
null Primary
SceneObjectSelectionTarget(SessionId, SceneId, ObjectId)
AssetSelectionTarget(SessionId, ProjectId, TargetProfile, AssetSelectionKey)
```

`EditorSelectionSnapshot` 携带单调 `Revision`、optional `Primary` 与 `EditorSelectionChangeReason`。target 自身必须
携带有效 scope 和唯一 identity：

- `SceneObjectSelectionTarget` 使用 authoritative `SceneDocument` 的 session/scene/stable object identity；
- `AssetSelectionTarget` 使用 session/project/target profile 与既有 `AssetSelectionKey`，GUID 优先，只有未追踪 source
  才以 catalog source path fallback；
- no selection 由 `Primary == null` 表达，不创建伪 target。

发布者通过 `IEditorSelectionService.Replace/Clear` 提交 intent，Application service 验证 scope/target/identity 后才发布
新 snapshot；`EditorSelectionChangeReason` 区分 user、project scope 改变、scene target 删除与 asset target 删除。Resource Browser、
Hierarchy 与 Scene View 不互相引用；Inspector 只订阅 snapshot。hover、focused row、folder location、filter、
expansion、keyboard anchor 和 pin 都是 panel-local state，不进入共享 selection。selection 变化不进入 scene/document
Undo，也不写 project、scene、`.ameta` 或 product manifest。

Project close/switch 在新 project scope publication 时使旧 selection 失效；consumer 只持有稳定ID，禁止在scope切换期间解引用旧owner。catalog refresh 后，Resource Browser 仍用
`AssetSelectionKey` remap；找不到 identity 时清除 asset selection。同步 `Replace/Clear` publication 保持单调 revision；
未来异步 producer 仍必须保证旧 scope 或旧 revision 的晚到结果不能覆盖 current selection。subscriber failure 必须隔离，
不能阻止 owner 完成 publication 或 shutdown。

#### Scene View model bounds 与 Transform proxy picking

#398 的首个 Scene View producer 只命中当前帧实际绘制的有界 Transform XYZ 轴代理。#402将当前唯一可解析并真实绘制的
directional-wedge validation mesh source-controlled local AABB纳入同一Application pick snapshot：model body先以screen ray
和inverse local TRS执行ray-OBB slab hit；只有没有model hit时，才检查没有model proxy的entity Transform axes。这样可见模型
是主要入口，空entity仍有诚实回退；unknown/not-ready authored mesh不获得伪造bounds。

`ViewportSession`在同一锁内捕获session、scene、document revision、camera、model bounds与bounded debug proxy set。model
重叠按camera depth、stable `ObjectId`排序；axis重叠按camera depth、screen distance、stable `ObjectId`排序。rotation、
non-uniform/negative scale保留，退化scale fail closed。fallback复用native overlay的投影轴、quaternion旋转和正`clipW`规则；
当前debug-line overlay不按camera near/far裁切端点，因此fallback也不擅自增加不同的near/far可见性语义。

Avalonia `ViewportCompositionControl` 只在 endpoint 为 Ready、未 degraded、当前 physical extent 与 front frame完全一致，
且 frame sequence/revision仍可呈现时，暴露最小 presented interaction context；它不引用 selection。Shell View只保存
单次pointer gesture并把DIP坐标和容差按`RenderScaling`变成physical pixels；ViewModel复验current project/document与
pick snapshot后，才发布 `SceneObjectSelectionTarget`，空白命中则 `Clear`。过期、closed、resize mismatch、未呈现或
degraded画面保持原selection不变。该路径不修改SceneDocument、dirty/savepoint或Undo/Redo。

采用 [Unreal `FEditorViewportClient::ProcessClick`](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient/ProcessClick)
与 [`FViewport`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FViewport) 的viewport click context、
presented hit proxy/typed element与stable identity边界，但拒绝其GPU hit-proxy实现；采用
[Godot `Node3DEditorViewport`](https://github.com/godotengine/godot/blob/master/editor/scene/3d/node_3d_editor_viewport.h)
公开的screen ray、AABB distance、depth sort与stable `ObjectID`路径，以及
[O3DE `EditorPickEntitySelectionHelper`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzToolsFramework/AzToolsFramework/ViewportSelection/EditorPickEntitySelection.cpp)
的“viewport resolve → shared editor selection”和导航/选择输入分离。

Asharia采用当前validation product已证明的真实local bounds，但不把这一绑定冒充通用mesh bounds/geometry hit合同。明确拒绝
PhysicsWorld、collider raycast、mesh triangle/BVH、GPU ID buffer和readback作为本切片前置；完整asset bounds provider必须由未来
resource/product owner提供逐项ready identity与bounds后再替换该窄绑定。outline、hover、gizmo、camera navigation、多选与通用
viewport tool framework继续独立演进。

### 2. Asset Inspector 只投影同一 catalog snapshot 的 immutable facts

Shell `StudioInspectorPanelViewModel` 订阅 current `EditorSelectionSnapshot` 与 `IProjectAssetCatalog`，当 Primary 是
`AssetSelectionTarget` 时，按其 `AssetSelectionKey` 从 current `AssetCatalogSessionSnapshot.Catalog.Entries` 查找
`AssetCatalogEntry` 并构造只读 presentation。这里不增加 speculative Application inspection/projector type；Application
仍只拥有 selection 与 catalog snapshots，Shell 只负责组合 presentation。Inspector 不接收 Project panel row/ViewModel，
也不持有 catalog owner。首个 asset presentation 只显示 catalog 已经证明的 facts：

- display name、source path、tracked/untracked 与 GUID；
- asset type、extension、importer id/name 与 importer version；
- import profile、asset role；
- `NotTracked / Current / Missing / Stale / Invalid` product state，以及 current/stale product counts；
- sub-asset stable id、display name 与 role；
- asset-local structured diagnostics。

Inspector 必须显式投影 `Nothing selected`、scene entity 或 resolved asset。完整 current catalog 找不到 selected key、
scope 不匹配，或 failed catalog 没有 last-known-good 时，selection owner 清为 `Primary == null`；Loading/Degraded 若仍有
同 scope last-known-good且其中仍有目标entry，则继续投影该稳定entry。不得保留另一资产的内容或用path/name猜测替代项。
资产字段当前只读显示且不可编辑；复制动作与diagnostic detail到Problems的typed target均后置，本Slice不伪造command。

现有 scene entity Inspector 的名称/local Transform mutation 继续走 `ProjectSession` command、expected revision、
authoritative receipt 与 document Undo/Redo。typed selection 只统一“正在检查什么”，不建立通用 property grid、
统一 asset/scene mutation provider 或第二个 scene truth。

### 3. 当前 Inspector 不进入 asset pipeline 或 runtime

Asset Inspector 当前明确禁止：

- 读取或 decode source file、`.ameta`、product blob、thumbnail 或 preview；
- 写 source、metadata、manifest、cache 或 project descriptor；
- 执行 import/reimport、创建 processor job 或声称 refresh 已更新 product；
- 创建 `AssetHandle`、`RuntimeResourceTicket`、CPU mesh/texture payload、GPU buffer/image 或 renderer handle；
- 订阅 watcher、持有 runtime generation、替换 live resource 或销毁旧 GPU resource。

Catalog `Current` 只表示 active expected product key 与 manifest facts 匹配，不表示 product blob 已读取、runtime resource
已 Ready、GPU upload 已完成或 thumbnail 已生成。Inspector 的 `Refresh`（若展示）只能路由既有 catalog refresh；不能
成为 reimport/hot-reload 的同义词。

### 4. import settings 使用未来独立的 staged Apply/Revert 边界

可写 Asset Inspector 延后到存在真实 metadata mutation owner、import scheduler、diagnostic 与 revision contract 以后。
届时采用以下边界，而不是在字段变化时直接写回：

1. Inspector 从 authoritative metadata/catalog facts 创建 project + asset + metadata revision-scoped draft；
2. 编辑只改变 draft，并显式呈现 dirty/invalid；切换 selection、project close 和外部 revision 改变必须经过明确的
   discard/reload/conflict policy；
3. `Revert` 只丢弃当前 draft并重新投影 authoritative facts，不等同于 document Undo，也不回滚已发布 product；
4. `Apply` 通过 Application typed command 校验 scope、identity、expected metadata revision 和完整 settings，再由
   metadata owner 做原子写入并提交 reimport request；
5. Apply success 只证明 settings commit/reimport request 已被接受，不证明 cook、runtime load 或 GPU replacement 成功。

asset metadata history 是否进入独立 editor transaction、如何合并连续字段编辑，以及 selection 离开时是否提示，均在
可写 Slice 中以真实 owner 决定；本 ADR 不预设 scene document Undo 可跨域回滚 `.ameta`。

### 5. hot reload 采用未来的 last-known-good staged publication

hot reload 不由 selection 或 Inspector 默认启用。等真实 product/runtime/GPU 闭环存在后，按独立 Slice 分阶段加入：

```text
source/metadata invalidation or explicit reimport
  -> deterministic candidate cook + dependency validation
  -> atomic product/catalog publication
  -> ResourceRuntime async blob read + typed CPU decode in a new generation
  -> renderer-owned GPU candidate creation
  -> safe-point generation swap
  -> deferred retirement of the old CPU/GPU generation
```

任一 candidate 阶段失败都保留 last-known-good active product/resource，并发布结构化诊断；不能先破坏 live resource 再
尝试恢复。Inspector 将来只投影 authored settings、processor state、active/candidate generation 与 failure facts，并通过
typed commands 请求操作；watcher、scheduler、`ResourceRuntime`、renderer 和 deferred destruction 仍各自拥有生命周期。

完整 runtime/Profiler 可以消费 reload/load/upload 的时序事件，但 profiler 是否启用不能决定 correctness，也不能成为
asset hot reload 的 owner。常规 runtime 只保留有界 milestone/diagnostic 事件；高频 trace 仍由按需 capture 控制。

## Adopt / Reject / Defer 记录

| 决策 | 结论 | Asharia 原因 |
| --- | --- | --- |
| closed typed selection target + stable identity | Adopt | 防止 scene id、asset GUID 与 source path 混用，并让 panel 解耦 |
| Inspector 消费 selection snapshot | Adopt | 一个检查面可切换 scene/asset presentation，不反查 controls |
| unloaded catalog facts | Adopt | 检查资产不应触发 runtime load 或 GPU allocation |
| panel-local pin/history/search | Defer | 需要真实多 Inspector/persistence 需求；不能污染 selection truth |
| 通用 reflection/property-grid | Reject for current Slice | 当前只有 scene Transform 与 catalog facts两个真实 consumer，语义和 mutation owner 不同 |
| asset field immediate write-through | Reject | 绕过 draft、validation、revision、atomic metadata write 与 reimport receipt |
| Inspector-owned import/runtime/preview | Reject | 破坏 source/product/runtime/renderer owner 分层 |
| thumbnail/model preview | Defer | 必须复用 cooked product、generation-safe runtime resource 与独立 preview service |
| staged Apply/Revert | Defer and constrain | 等 metadata command 与 processor owner 存在后按本 ADR 边界实现 |
| automatic hot reload | Defer and constrain | 等 product/runtime/GPU 闭环后以 candidate + last-known-good + safe swap 实现 |

## 后果

- Resource Browser selection 与 Inspector 不再通过 panel reference 或 copied row 形成隐式耦合；Application snapshot 是
  唯一跨面板 selection handoff。
- 用户可在不加载 runtime asset 的前提下查看资源身份、导入分类、产品新鲜度、sub-assets 与诊断；“可检查”不会被
  误解为“可运行”或“可预览”。
- 统一 Inspector shell 不等于统一 mutation system。scene edit 继续由 SceneDocument/ProjectSession 拥有，asset edit
  必须等待 metadata/import command owner。
- 后续加入 importer、model thumbnail 和 hot reload 时已有明确接缝，但本 Slice 不为未来 owner 提前增加 service locator、
  plugin API 或 speculative runtime handle。

## 验证

- Application contract tests：target/identity/scope invariants、monotonic revision、duplicate/no-op policy、project close/switch
  invalidation、subscriber fault isolation；
- Application catalog/selection tests：scene/asset stable identity、project close/switch、catalog scope/current-generation、
  Loading/Degraded last-known-good、missing identity与catalog unavailable；catalog DTO自身测试继续覆盖immutable facts；
- ViewModel tests：Resource Browser select/clear/filter/refresh remap 到 typed selection；Inspector 按 current catalog
  解析 scene/asset/none presentation，不保留前一资产内容；
- Avalonia Headless：选择Resource Browser row后Inspector显示只读catalog facts、sub-assets与diagnostics，窄Dock保持可用；
  返回scene selection后现有Transform workflow保持工作。Apply/Revert/import控件属于后续mutation Slice；
- Scene View picker/session tests：FOV axis、high-DPI physical extent、behind-camera、native overlay near/far行为、
  重叠tie-break、bounded/truncated proxy set与stale/closed snapshot；ViewModel tests证明hit/blank/stale selection语义且
  document revision、dirty/savepoint、Undo/Redo不变；
- architecture/source gates：Inspector 不引用 Project panel ViewModel、EngineBridge、native ABI、filesystem、
  `resource-runtime`、renderer/RHI/Vulkan；Application 不引用 Avalonia；
- encoding、doc-sync、diff 与相关 managed build/tests。
