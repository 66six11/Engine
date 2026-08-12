# ADR-0013：Studio 采用 authoritative document Transform Undo/Redo 与逻辑保存点

状态：Accepted；由 GitHub Slice #373 实现

日期：2026-08-12

关联：GitHub Epic #16、Slice #373；延续
[ADR-0008](0008-authoritative-project-session.md) 与
[ADR-0009](0009-authoritative-scene-document.md) 的 owner 和 mutation 边界。

## 背景

Studio 已有单个 authoritative `SceneDocument`、stable `ObjectId`、expected revision、Inspector local Transform
编辑与 Save。在本决策之前，`SceneDocumentSnapshot.IsDirty` 由 `Revision != SavedRevision` 推导，也没有真实 Undo/Redo。
这足以表达“编辑后尚未保存”，但不能正确表达以下序列：

```text
open A clean -> edit B -> save B clean -> edit C -> undo to B clean -> redo to C dirty
```

Undo 本身也是一次真实 mutation，必须让 engine revision 继续单调推进；因此“当前内容是否等于已保存内容”不能再由
revision 是否相等决定。把补偿 closure 放进 ViewModel、回退 revision，或在 UI 中猜测 native mutation 的结果，都会形成
第二份 scene truth，并重复 ADR-0007 已删除的 transaction 岛问题。

本 ADR 只决定第一个可验证纵切：单个 active SceneDocument 中，Inspector 的 whole local Transform
`Apply -> Undo -> Redo -> Save/reopen`。它不先建立通用 property system 或完整 editor command framework。

## 外部引擎依据

### Unreal Engine

- [`UEditorEngine`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UEditorEngine)
  持有 editor transaction owner；Details UI 不是 history owner。
- [`UTransactor::Begin`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UTransactor/Begin)
  与 [`FScopedTransaction`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FScopedTransaction)
  表达显式 transaction scope，而不是用相邻提交的时间间隔猜测交互。
- [`UTransBuffer`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UTransBuffer)
  具有 byte budget；[`FEditorUndoClient`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorUndoClient)
  的 post-undo 回调显式携带成功结果。

Asharia 采用 owner、显式 scope、budget 与 success outcome 的思想；不复制 UObject serialization、global transactor、
嵌套 transaction 或 Unreal API/名称。

### Unity

- [`Undo.RecordObject`](https://docs.unity3d.com/ScriptReference/Undo.RecordObject.html) 记录修改前状态，并在没有
  实际变化时不产生 Undo entry。
- [`SerializedObject`](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/SerializedObject.html) 把 UI
  projection 与实际对象更新/应用分开。
- [`Undo.bindings.cs`](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Editor/Mono/Undo/Undo.bindings.cs)
  显示 history 由统一 native owner 提供；
  [`TransformRotationGUI.cs`](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Editor/Mono/Inspector/TransformRotationGUI.cs)
  继续把 Inspector Euler presentation 与内部 rotation truth 分开。

Asharia 采用 exact no-op 不入栈、presentation 不成为历史真相的行为；不复制全局 native manager、反射 property path、
隐式 group 或基于事件/时间窗口的合并。

### Godot

- [`EditorUndoRedoManager`](https://github.com/godotengine/godot/blob/master/editor/editor_undo_redo_manager.cpp)
  为 scene/resource 选择独立 history。
- [`UndoRedo`](https://github.com/godotengine/godot/blob/master/core/object/undo_redo.cpp) 维护版本与 saved version，证明
  “逻辑内容位置”和“执行 mutation 的次数”是不同概念。

Asharia 采用 per-document history 与逻辑保存点；不采用持有 mutable Object/method callback 的 entry、仅按名称或时间
合并，以及无法表达部分失败/不确定结果的执行合同。

### O3DE

- [`UndoSystem`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzToolsFramework/AzToolsFramework/Undo/UndoSystem.cpp)
  使用 command 的 before/after 状态和 batch hierarchy。
- [`ToolsApplication`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzToolsFramework/AzToolsFramework/Application/ToolsApplication.cpp)
  暴露显式 begin/end undo batch 与 interaction lifecycle。

Asharia 采用 immutable before/after 与显式 interaction 的思想；不采用无界主栈、`void` apply/revert failure，或在第一条
Transform 纵切中提前引入嵌套 batch tree。

## 决策

### 1. Owner 与依赖方向

`Asharia.Studio.Application.ProjectSession` 是当前 active SceneDocument history/savepoint 的唯一 owner。history 随
document connection 创建，在 project close、document replacement 和 session dispose 时销毁。

```text
Avalonia command/Inspector
    -> Application ProjectSession typed intent
    -> EngineBridge ISceneDocumentConnection
    -> native SceneDocument typed mutation
    -> authoritative receipt + snapshot
    -> ProjectSession history/content-state commit
    -> immutable ProjectSessionSnapshot
```

- Avalonia 只提交 intent、投影 `CanUndo`/`CanRedo`/label/dirty，不持有 entry、native handle 或补偿逻辑。
- EngineBridge 负责 ABI layout、owner lane 和 malformed receipt 验证，不拥有 history。
- native SceneDocument 负责 validate-and-apply 与 authoritative mutation receipt，不拥有 editor history 或快捷键。
- selection、Euler hint、字段草稿和 focus 都是 transient presentation state，不进入 history。

### 2. Typed Transform mutation receipt

SceneDocument Transform Document ABI v3 硬切为 typed result；磁盘 scene schema 仍为 v2。成功 result 至少携带：

- stable `SceneId` 与 `ObjectId`；
- `Changed`；
- immutable `BeforeTransform` 与 `AfterTransform`；
- `BeforeRevision` 与 `AfterRevision`。

receipt 必须满足：

- `BeforeRevision` 等于 request 的 expected revision；
- changed success 的 `AfterRevision > BeforeRevision`，且 before/after Transform 与 authoritative snapshot 一致；
- no-op success 的 before/after Transform 与 revision 均相等；
- typed failure 保证没有 mutation，不返回可被 history 接受的伪 receipt；
- target、identity、finite/unit quaternion、revision 或 receipt layout 不一致时 fail closed。

Undo/Redo 不是内存复制：它们用 entry 的 stable `ObjectId` 和 immutable Transform 再次经过同一 typed native mutation，
并使用当下 authoritative revision。当前 selection 不参与 target 解析。

### 3. Revision、ContentState 与保存点

三个概念必须分离：

| 概念 | 含义 | 规则 |
| --- | --- | --- |
| `DocumentRevision` | native scene truth 的执行顺序/stale-write fence | changed Apply、Undo、Redo 后严格单调；不回退 |
| `ContentStateId` | ProjectSession 内当前逻辑文档内容的 opaque identity | 新 authored change 产生新 ID；Undo/Redo 可返回 entry 已有的 before/after ID |
| `SavedContentStateId` | 最近一次成功 Save 所保存的逻辑内容 | Save 成功后设为当前 `ContentStateId` |

`ContentStateId` 不是 content hash、文件格式字段或跨重启 identity。打开文档时创建初始 ID，并让 current/saved 指向同一 ID。
document dirty 的唯一编辑器真相是：

```text
IsDirty = ContentStateId != SavedContentStateId
```

native `SavedRevision` 可以继续描述最近一次成功写盘对应的 engine revision，但不能再决定 Studio dirty；Undo 回到已保存
内容时，revision 已推进而 content state 可以重新等于 savepoint。

Save 不清空 history、不移动 cursor，也不产生 history entry。Save 只有在 native write 成功且仍对应同一 logical content
state 时才移动 `SavedContentStateId`；失败、取消或 stale completion 不移动保存点。

### 4. Journal 结构与预算

每个 document history 使用 `List<SceneEditHistoryEntry> + cursor`：

- `[0, cursor)` 是当前已应用 prefix；`[cursor, Count)` 是 redo tail；
- user Transform changed success 在 cursor 处提交 entry，并先截断 redo tail；
- Undo 候选是 `cursor - 1`，Redo 候选是 `cursor`；
- cursor 只在对应 native receipt 完整验证成功后移动；
- failure、cancel、revision conflict、missing target、malformed receipt 和 no-op 都不移动 cursor；
- Undo/Redo 的成功 snapshot、content state 与 cursor 作为同一次 ProjectSession publication 提交。

每个 entry 至少记录 stable scene/object identity、label、显式 `InteractionId`、before/after Transform、before/after
`ContentStateId` 和估算 byte size。本 Slice 中每次 Inspector Apply 是一个明确的一次性交互，不合并 entry；未来 gizmo
必须提供 begin/update/commit/cancel 的同一 interaction identity，不能用毫秒窗口猜测 merge。

history 同时受 256 entries 与 16 MiB 约束。超过任一预算时按完整最老 entry 淘汰，绝不截断单个 entry；cursor 与总 byte
数同步调整。淘汰只减少可 Undo 距离，不改变当前/保存 content state，也不制造 dirty。新 Slice 若需要可变 payload，必须
先定义稳定、保守且可测试的 byte 估算。

### 5. 未纳入 history 的 mutation

#373 只记录 successful changed Transform。当前 Create Entity、Create Mesh Entity 与 Rename 仍可修改文档，但不能允许
Transform history 跨越一条未记录的 mutation，因为 entry 的 before/after `ContentStateId` 表示整个文档状态。

因此，任何 successful changed、但本 Slice 不支持 Undo 的 persistent mutation 必须：

1. 分配新的 `ContentStateId`；
2. 清空该 document 的 history/redo tail；
3. 保留既有 `SavedContentStateId`；
4. 发布 `CanUndo = false`、`CanRedo = false` 与正确 dirty。

这是一条明确的安全 barrier，不是 silent history loss；后续只有在对应 operation 也具备 typed inverse/receipt 后才能移除。

### 6. Failure 与不确定结果

普通 typed failure 保证 native 未改变文档，因此 history、cursor 和 content state 保持不变，ProjectSession 发布返回的
authoritative snapshot 与 diagnostic。

ABI malformed、owner-lane interruption 或无法证明 commit outcome 的情况不能伪装成普通 failure。ProjectSession 必须先通过
document connection 的 authoritative refresh 重读 snapshot；refresh 成功时以该 snapshot 继续发布，但清除无法证明连续性的
history，并分配保守的新 content state。refresh 失败时 session fail closed 为 `NoProject`，关闭失去可信度的 document connection，
返回要求重新打开项目/文档的 typed failure。不得执行 managed compensation，也不得在结果未知时移动 cursor。

### 7. Command 与焦点

Shell 提供 document Undo/Redo command、toolbar affordance 和平台中立的 command gesture route。menu、toolbar 与 shortcut
消费同一 `ProjectSessionSnapshot` enablement/label；production 代码不读取 Win32 message、平台 key code 或 OS 分支。

当文本输入控件持有 focus 且可以处理自己的 draft Undo/Redo 时，局部文本编辑优先，document command 不得抢占。toolbar
点击是显式 document command。normalized primary-modifier shortcut 只有在 focused surface 未消费后才能路由到 active document。

## 拒绝方案

- 不恢复 closure/delegate `Apply/Revert` history，也不保存 ViewModel、Control、mutable object pointer 或 runtime `EntityId`。
- 不让 native SceneDocument、EngineBridge、Inspector 或 Shell 各自维护第二个 history/savepoint。
- 不回退 revision，不用 `SavedRevision`、content hash 或 Transform 数值近似替代 `ContentStateId`。
- 不在 native success 前 pop/push stack 或移动 cursor，不把 partial/unknown outcome 当 success。
- 不使用操作名称、连续时间或相邻 UI event 猜测 transaction merge。
- 不使用两个独立 Undo/Redo stacks；失败时它们容易在 apply 成功前丢失 entry。
- 不建立无界 history 或只按 entry count 而忽略未来 payload bytes。

## 非目标

- Create/Delete/Rename/Reparent/Add/Remove Component、asset reference 或 multi-selection Undo。
- history window、跨重启 history、branching history、宏、嵌套 transaction 与通用反射 property path。
- gizmo/live drag、snapping、preview mutation 与 merge/coalesce；它们必须在后续 Slice 定义显式 interaction lifecycle。
- selection Undo、Euler winding persistence、Inspector draft Undo 或 panel layout Undo。
- 多文档 UI、协同编辑、crash journal、autosave 或 source-control integration。

## 验证

- native/EngineBridge：changed/no-op/failure receipt、revision conflict、missing target、malformed layout 和 snapshot 一致性。
- Application：`List + cursor`、redo truncation、success-only cursor、256/16 MiB eviction、unsupported-mutation barrier。
- savepoint：`A -> B/save -> C -> Undo(B clean) -> Redo(C dirty)`，且 Apply/Undo/Redo revision 严格单调。
- UI：toolbar/command label 与 enablement；focused TextBox 的局部 Undo 优先；selection 改变不影响 history target。
- 端到端：真实 project/scene DLL 执行 Apply/Undo/Redo/Save/close/reopen，最终 Transform 与 clean state 一致。
- 回归：Inspector quaternion/Euler presentation 不因 Undo/Redo receipt 产生字段抖动；Viewport revision/presentation fence 保持通过。
