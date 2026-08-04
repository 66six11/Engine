# ADR-0008：Studio 采用 canonical ProjectSession 创建/打开切片

状态：Accepted

日期：2026-08-03

关联：GitHub Epic #351、Slice #352；延续 [ADR-0007](0007-studio-frontend-hard-cut.md) 的硬切边界。

## 背景

ADR-0007 完成后，production Studio 只有 Starting / No Project / No Document，旧 Project facade、
ProjectOpenSession、active ProjectSession、managed/native adapter 和无 caller smoke 均已删除。下一条真实能力必须从
当前边界重新建立 owner，不能把已删除的历史表面当作兼容合同。

`packages/project-core` 已拥有 `asharia.project.json` 的 schema、验证和 strict JSON IO。Studio 需要让用户在 No Project
状态创建或打开项目，同时保证 Avalonia、managed adapter 和 C++ Core 不各自维护一份描述符真相。

## 决策

1. `project-core` 是 canonical descriptor 与最小项目布局的唯一 owner。创建操作在目标父目录的同级 staging
   目录中写入 `asharia.project.json`、`Assets/` 和 `.asharia/cache/assets/`，重新读取验证后才以一次目录 rename
   发布；已有目标永不覆盖，失败只清理本次操作拥有的 staging。
2. `asharia-project-native` 是 project-core package 内的专用窄 adapter，不进入 `editor_native.dll`，也不依赖
   renderer、Vulkan、Scene 或 Slang。C ABI v1 使用 version/struct-size header、明确 status 和 caller-owned bounded
   UTF-8 response buffer；不返回 native-owned raw pointer，也没有按值复制后重复 release 的所有权风险。
3. `Asharia.Studio.Application.ProjectSession` 是进程内唯一活动项目 owner。persistent `ProjectId` 来自 descriptor；
   每次成功打开产生新的 runtime-only `ProjectSessionId`。create/open 串行执行，只有 adapter 返回并验证 canonical
   snapshot 后才发布 Ready；失败保留最后一次成功 snapshot，dispose 会取消并等待进行中的操作。
4. `Asharia.Studio.EngineBridge` 实现 Application descriptor port，并把 native status、绑定失败和无效 ABI 映射为
   typed failure。它不拥有 session，也不引用 Avalonia。
5. Avalonia Shell 只拥有项目名输入、folder/file picker、异步命令和 snapshot 投影。ViewModel constructor 接收
   `IProjectSession` 与 dialog port；XAML/code-behind 不读写 JSON，dialog 也不成为项目事实源。
6. `App -> StudioCompositionSession` 显式拥有 `ProjectSession`。关闭顺序先停止 development observation，再释放 Shell
   订阅/命令，最后 cancel + await ProjectSession。Release image 显式验证 managed bridge closure 与
   `asharia_project_native.dll` 的 PE/DLL/export identity，并继续拒绝 development host/protocol、Scene native、旧
   editor native 与 Slang。

## 外部引擎证据与采用项

- Unreal Project Browser：采用“选择名称/位置，创建完成后打开同一项目”和“从项目描述符打开已有项目”的基本流程。
- Godot Project Manager：采用创建/导入是进入 editor 前的项目级动作，以及已存在目录冲突必须显式失败的行为。
- O3DE Project Manager：采用项目身份、项目位置与 editor session 分离的边界。

这些案例共同支持一个小而明确的 project browser ingress，但不要求 Asharia 在本 Slice 复制完整 Hub/Manager 产品。

## 拒绝项与 Asharia 原因

- 不恢复旧 #347 Workbench/Project facade、兼容 DTO 或 `editor_project_*` API；当前 App 没有这些合同的 consumer。
- 不由 C# 复制 `asharia.project.json` schema；否则 Application/Presentation 会与 project-core 形成双真相。
- 不把 adapter 放回 renderer/Vulkan `editor_native.dll`；Project IO 不需要 GPU 闭包，独立 DLL 让部署和 ABI 可单独验证。
- 不使用 native-owned result string、release-by-value 或全局 session；caller buffer 与 Application owner 能明确 lifetime。
- 不在本 Slice 加入模板、最近项目、自动 reopen、项目构建/版本管理、asset catalog、SceneDocument 或 EditWorld；它们需要
  独立事实源、恢复策略和验收证据。

## 后果

- Studio 从 No Project 进入真实 canonical ProjectSession，但仍保持 No Document；下一 Slice 可以在该 session scope 下
  建立 SceneDocument，而不能绕过它创建 World。
- 下一 Slice 的固定验收边界是 `ProjectSession Ready -> 默认 SceneDocument -> EditWorld -> 创建/修改实体 -> Save ->
  关闭项目 -> 重开数据一致`。SceneDocument 拥有 world，EngineBridge 封装并部署 `asharia_scene_native.dll`，Avalonia
  只消费 snapshot 和命令，不持有原生句柄。
- 项目 package manifest/lock、细分加载状态与 Degraded/SafeMode 紧随其后；多模板、最近项目、完整 Asset Browser、
  完整停靠布局和 Play Mode 不得进入上述 P0 Slice。
- EngineBridge/Runtime.Contracts 重新成为 Release managed closure 的真实依赖，但 `asharia_scene_native.dll` 仍不是产品依赖。
- project-core 的新创建 API 和 C ABI 成为版本化合同，变更时必须同步 native static assertions、C header smoke、managed
  layout tests、distribution export validation 和文档。

## 验证

- project-core create/open、冲突、caller buffer、ABI 和 C compiler header smoke；
- Application success/failure/cancel/dispose tests；
- EngineBridge status/UTF-8/layout tests；
- ViewModel 与 Avalonia Headless create/open projection；
- Release publish 和 closed Editor Image identity/forbidden-artifact tests。

## 参考

- [Unreal：Creating a New Project](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-a-new-project-in-unreal-engine)
- [Unreal：Opening an Existing Project](https://dev.epicgames.com/documentation/en-us/unreal-engine/opening-an-existing-unreal-engine-project)
- [Godot Project Manager](https://docs.godotengine.org/en/latest/tutorials/editor/project_manager.html)
- [O3DE Project Manager](https://docs.o3de.org/docs/user-guide/project-config/project-manager/)
