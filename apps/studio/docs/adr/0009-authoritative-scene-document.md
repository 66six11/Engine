# ADR-0009：Studio 采用 authoritative SceneDocument 编辑闭环

状态：Accepted

日期：2026-08-04

关联：GitHub Epic #351、Slice #353；延续 [ADR-0008](0008-authoritative-project-session.md) 的活动项目边界。

## 背景

ADR-0008 让 Studio 能创建或打开 canonical project，但成功后仍停留在 No Document。用户无法创建实体、编辑
名称或 Transform，也无法证明保存后重新打开的数据一致性。下一条主线需要建立真实 Document/World owner，而不是
先扩充 Asset Browser、Dock、Viewport、Play Mode 或程序集系统。

公开引擎合同显示，编辑器文档需要拥有可编辑世界的生命周期和持久化边界：Unreal 把 `UWorld` 作为 level/actor
集合及其生命周期 owner；Godot 明确区分 editor-time scene 修改与运行时状态；O3DE 的 Entity Inspector 与 editor
automation 都通过编辑器拥有的实体/组件操作修改场景。Asharia 采用这些所有权与 authoring intent 模式，但不复制它们的
对象 API、序列化格式或 UI 框架。

## 决策

1. `asharia::scene::SceneDocument` 是 scene identity、revision/savepoint、stable object ID 与 `World` 的唯一 native
   owner。默认文档固定为 `Assets/Scenes/Default.asharia.scene.json`；新项目首次打开时创建一个包含默认实体的场景，
   后续打开只加载既有文件。
2. `scene-core` 的 strict JSON IO 拥有 schema `com.asharia.scene` version 1。文档读取验证 UTF-8、字段、GUID、有限
   Transform、单位四元数、重复 object ID、64 MiB 文件上限与 10,000 实体上限；写入使用 sibling staging 和 replace，
   不把 JSON 真相复制到 managed 层。
3. 每个 mutation 携带 expected revision。成功后 revision 单调推进并由 authoritative snapshot 决定 dirty；失败或
   revision conflict 不发布猜测状态。Save 只在同一文档 revision 上推进 saved revision。本 Slice 不伪造 undo/redo；
   后续 history 必须继续通过相同 mutation boundary。
4. `asharia_scene_native.dll` 新增专用 SceneDocument C ABI。registry 使用 generation-safe opaque token，所有文档操作
   强制在创建线程执行，响应采用 caller-owned bounded buffer，snapshot 用固定布局 entry 与 UTF-8 span 表达；raw
   `World*`、entity handle 和 native-owned string 均不跨 ABI。
5. `Asharia.Studio.EngineBridge` 为每个文档连接建立专用 owner lane，所有 native open/mutate/snapshot/save/close 都在
   同一线程执行。它验证 ABI 布局、状态、长度和 UTF-8，只向 Application 暴露 typed command/result/snapshot，不向
   Avalonia 暴露句柄或 P/Invoke。
6. `ProjectSession` 先取得 canonical project descriptor，再创建或打开默认 SceneDocument；两者都成功才发布 `Ready`。
   关闭顺序为停止新命令、关闭文档连接、最后清除项目状态。创建实体、修改名称/Transform 与保存均由 session 串行化。
7. 最小 Avalonia 编辑面只投影 authoritative snapshot：Hierarchy 显示实体，Inspector 编辑所选实体的名称和 local
   Transform，工具栏提供 Create Entity 与 Save，标题显示 dirty `*`。selection 是 UI 状态，并在每次 snapshot 后按
   stable object ID remap；它不是 native truth。
8. Release Editor Image 必须精确包含 `bin/asharia_scene_native.dll`，验证 PE/DLL identity 和全部 SceneDocument exports；
   缺失、错名、嵌套、副产物或 export 不完整都 fail closed。

## 拒绝项与 Asharia 原因

- 不让 C# 直接读写 scene JSON；否则 native Document 与 managed UI 会形成双真相。
- 不让 Avalonia 持有原生句柄或直接调用 ABI；线程、关闭和 stale-handle 规则只能由 EngineBridge 统一维护。
- 不把旧逐实体 World ABI 当作文档协议；它没有 stable object ID、revision、snapshot 或 savepoint。
- 不返回 native-owned字符串或可长期借用的 snapshot；caller-owned response 明确了跨语言生命周期。
- 不在本 Slice 建立完整 transaction/undo/redo、hierarchy parenting、组件反射、Asset Browser、Dock、Viewport、Play
  Mode 或 package manifest/lock。它们需要后续独立垂直切片与恢复策略。

## 后果

- Studio 的最小真实编辑链成为：

  ```text
  创建项目 -> 创建/打开默认 SceneDocument -> 创建/修改实体 -> 保存 -> 关闭项目 -> 重新打开 -> 数据一致
  ```

- `ProjectSessionSnapshot.Ready` 现在同时要求活动项目和活动文档；No Project 与 document-open failure 不会冒充 Ready。
- SceneDocument 当前只有 revision/savepoint dirty 模型；Undo/Redo 仍是明确缺口，不能由 ViewModel compensation 替代。
- P1 紧随其后接入 project package manifest/lock，并扩充 Opening、ResolvingPackages、LoadingDocument、Degraded/
  SafeMode 等加载状态；当前 typed failure 先保留可诊断的 document/native/IO 分类。

## 验证

- scene-core create/load/save、schema/UTF-8/duplicate/size/revision failure 和 C/C++ header smoke；
- native document ABI lifecycle、owner-thread、stale handle、buffer sizing、mutation 与 persistence smoke；
- Application session ownership、failure、revision、dirty、save、close/dispose tests；
- EngineBridge ABI layout、owner lane、status/UTF-8/response-bound tests；
- ViewModel、Avalonia Headless Hierarchy/Inspector/dirty/Save projection；
- 使用真实 project/scene DLL 的创建、编辑、保存、关闭、重开端到端验收；
- Release image required artifact/export/forbidden-shape 验证。

## 参考

- [Unreal Engine：UWorld API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UWorld)
- [Unreal Engine：Working with Levels](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-levels-in-unreal-engine)
- [Godot：Running code in the editor](https://docs.godotengine.org/en/stable/tutorials/plugins/running_code_in_the_editor.html)
- [O3DE：Entity Inspector](https://docs.o3de.org/docs/user-guide/editor/entity-inspector/)
- [O3DE：Editor automation](https://docs.o3de.org/docs/user-guide/editor/editor-automation/)
