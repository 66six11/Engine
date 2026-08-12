# Studio 生产工作台体验规范

状态：Superseded（历史visual baseline；当前合同以
[ADR-0007](../adr/0007-studio-frontend-hard-cut.md)为准）

> R0已删除无真实consumer的Workbench、ProjectOpenSession checkpoint与project-launch presentation；本文相关
> 描述不再是production事实。
>
> 2026-08-04：ADR-0009 已建立不依赖旧 Workbench 的最小真实编辑面：单 SceneDocument、Hierarchy、Inspector、
> Create Entity、Save、dirty 与 Transform Undo/Redo。当时Dock、Project/Asset panel、Diagnostics panels和多文档仍是后续范围；
> 后续Dock与#381 Diagnostics current事实以下方更新为准。
>
> 2026-08-10：#363 只恢复 Hierarchy 的生产 UI 与 snapshot projection；它不恢复本文已 supersede 的旧
> Workbench/provider/tree framework，也不扩展当前 Scene ABI。
>
> 2026-08-12：#373 已由 [ADR-0013](../adr/0013-authoritative-document-transform-undo-redo.md) 固化并实现首个
> Transform Undo/Redo 与逻辑保存点纵切。
>
> 2026-08-13：#381在当前生产Dock上建立一个Diagnostics面板，内部Console读取时序日志、Problems读取可行动
> 结构化诊断；#383将同一App-owned hub加固为双预算、stream-specific subscriptions和Active/History问题生命周期，
> 不恢复本文已删除的旧Workbench/Feature框架。

更新日期：2026-08-13

跟踪：GitHub Epic #119；设计 Slice #337；首个实现 Slice #338；Hierarchy 第一纵切 #363；Transform Undo/Redo #373；Diagnostics #381

## 1. 目的

本文定义 Studio 前端的生产工作台体验合同。它回答：

- 用户进入项目后首先看到什么；
- Hierarchy、Project、Scene View、Inspector 和 Diagnostics 如何围绕同一上下文协作；
- selection、focus、dirty、read-only、locked、loading 和 invalid 如何被表达；
- 哪些反馈就地显示，哪些进入 Problems、Console 或 Status Bar；
- 现有 Avalonia、Dock 和 UI 平台能力如何继续演进，而不是再造第二套前端框架。

本文不是控件目录，也不把外部引擎的布局、素材或品牌当作产品概念。公开引擎案例和 HCI 研究只用于验证交互关系。

## 2. 目标用户与核心任务

主要用户是需要长时间停留在编辑器中的场景作者、技术美术和关卡设计者。次要用户是通过同一工作台诊断渲染、资源和工具链问题的引擎开发者。

核心闭环固定为：

```text
find -> inspect -> select -> edit -> preview -> validate -> commit/undo
```

各步骤的主表面如下：

| 步骤 | 主表面 | 必须保留的上下文 |
| --- | --- | --- |
| find | Project、Hierarchy、Command Palette | project、document、query、filter |
| inspect | Inspector、Problems | selection、revision、validation |
| select | Hierarchy、Project、Scene View | stable object/asset id、selection source |
| edit | Inspector、Viewport tool | transaction、dirty、read-only/locked |
| preview | Scene View、后续 Game View | document、camera、tool/mode |
| validate | inline feedback、Problems、Console | diagnostic id、source、recovery action |
| commit/undo | Command、transaction、save | mutation id、undo label、document revision |

最需要优先消除的风险不是“控件不够多”，而是用户无法判断当前选择、当前可编辑对象、修改是否生效以及错误应去哪里处理。

## 3. 当前实现事实与缺口

2026-08-13 的 production 源码与运行态事实：

- `Editor.csproj` 使用 Avalonia、compiled binding 与 CommunityToolkit.Mvvm；
- Shell拥有当前自研Dock与单SceneDocument编辑面；它没有恢复旧Workbench/Feature框架；
- Hierarchy 投影 authoritative entity snapshot；Inspector 编辑所选 stable object ID 的名称与 local Transform；
- #363 的 Hierarchy UI 继续消费同一 authoritative snapshot，并以 `SceneEntitySnapshot.ObjectId` 在 snapshot
  替换后重映射 selection；row/control 不是 scene truth；
- Create Entity、Save、dirty 标记与关闭重开恢复经过 Application/EngineBridge/native Document 真实链；
- selection 仅由 ViewModel 按 stable ID remap，不成为 engine truth；
- Dock已恢复当前production实现；#381在一个Diagnostics tool panel中提供Console/Problems两个tab，读取同一
  bounded hub。Project/Asset数据、Command Palette、多文档、非Transform mutation Undo和Play Mode尚未实现。

当前缺口：

1. Project package manifest/lock、细分 loading/degraded 状态和 asset workspace 仍待真实 service。
2. Inspector 只覆盖名称与 local Transform；Transform 已接入 document Undo/Redo，component reflection、validation rows、
   其他 mutation Undo 与 multi-selection 未实现。
3. Scene View、render lane、工具栏和 overlay 未实现。
4. Diagnostics已接入有界projection；持久Editor log、problem report/crash artifact、typed source/target导航仍需各自owner。
5. 多文档、Play 和viewport tools只有相应owner/command落地后才能启用。
6. 当前 `SceneEntitySnapshot` 只有 `ObjectId`、`Name` 与 `Transform`；Scene ABI 尚无 `ParentId`、entity kind、
   visibility、lock 或 authoring command，因此 Hierarchy 不能宣称真实嵌套关系或这些 mutation 能力。

这些缺口优先通过现有系统的组合、状态投影和少量 Shell surface 解决；不以抽象通用 UI 框架作为前置条件。

## 4. 默认信息架构

### 4.1 宽屏默认布局

目标基准是 1440×900；最低支持尺寸仍由应用窗口约束决定。默认布局使用现有 Dock 表达：

```text
┌ Main Menu ────────────────────────────────────────────────────────────────┐
├ Project / Document │ Select Move Rotate Scale │ Snap │ Edit │ Run controls ┤
├ Project launch / recovery state · candidate · next step · diagnostics ─────┤
├─────────────────┬──────────────────────────────────────┬──────────────────┤
│ Hierarchy       │ Scene View                           │ Inspector        │
│                 │ ┌ camera / shading / overlay ─────┐ │                  │
│                 │ │                                 │ │ selected object  │
│─────────────────│ │          viewport               │ │ state + sections │
│ Project         │ │                                 │ │                  │
│ search/filter   │ └ inline status / recovery ───────┘ │                  │
├─────────────────┴──────────────────────────────────────┴──────────────────┤
│ Console | Problems | Background Tasks                         collapsed ▲ │
├──────────────────────────────────────────────────────────────────────────┤
│ active context / latest primary status             tasks · warnings · VCS │
└──────────────────────────────────────────────────────────────────────────┘
```

默认比例：

| 区域 | 默认尺寸 | 行为 |
| --- | --- | --- |
| Main Menu | 现有紧凑高度 | 只承载稳定菜单入口 |
| Workbench Bar | 32–36 px | 呈现全局 context/mode/tool，不承载面板局部选项 |
| Project launch surface | content-driven，最大 180 px | Shell-owned；有界滚动，不进入 Dock/layout persistence |
| 左列 | 260 px | Hierarchy 与 Project 垂直分割；均可成为 tab |
| 右列 | 320 px | Inspector；优先保留可读宽度 |
| 底部 Diagnostics | 打开时约 180–220 px | 当前默认布局包含一个面板；内部Console/Problems切换，warning/error不强制抢焦点 |
| Status Bar | 22–26 px | 只显示摘要、计数和可点击目标 |

Scene View 是唯一默认中心 document。compiled Avalonia UI Style、设置和其他已注册诊断工具由 Window 菜单或
Command Palette 打开，并保留用户布局持久化；Frame Debugger 在接通真实 render lane 前不提供入口。

### 4.2 紧凑布局

当窗口无法同时保持中心视区和双侧栏可读性时，不做连续复杂响应式重排，只使用确定性规则：

1. 折叠底部抽屉；
2. 左侧 Hierarchy 与 Project 合并为单个 tab group；
3. 右侧 Inspector 保留，宽度不低于可读属性行下限；
4. 更窄时把 Inspector 收为可恢复 tab，不把它永久移出 layout；
5. Workbench Bar 隐藏低优先级文字，只保留 icon、state badge 和 tooltip；
6. 不自动关闭 floating window，不改写用户已保存布局。

第一版只提供 `Default` 与 `Compact` 两个确定性 layout preset，不实现任意 breakpoint DSL。

## 5. 上下文、选择与焦点

### 5.1 全局上下文

Shell 投影一个只读 `WorkbenchContextSnapshot` 概念；它不是新的 engine truth，字段来自现有/后续 session service：

```text
project display name
active document display name
document dirty state
editor mode
active tool
selection summary
background activity summary
diagnostic summary
```

其中 project display name 与 project-open 状态由 Application 的共享
`IProjectOpenSessionSnapshotSource` 投影已有 `ProjectOpenSessionSnapshot`；Presentation 不读取 bootstrap report，
也不从 `null` 猜测 loading/error。非 Ready snapshot 按合同不包含 project summary，因此 Workbench Bar 保持
`No project`，同时用 tooltip 呈现明确的 project-open 状态。

窗口标题建议使用：

```text
<document>[*] — <project> — Asharia Studio
```

未知 project/document 使用明确占位，不回退为含糊的 `Editor`。

### 5.2 Selection

- 历史目标要求Selection service成为跨面板唯一共享truth；当前R0旧service已因无consumer删除，必须由真实Document/World/asset owner重新接入。
- Hierarchy、Project 与 Scene View 可以发起 selection，但必须携带稳定 id 和 source。
- Inspector 只消费 selection snapshot，不从控件树或 engine object 反查状态。
- 面板自己的 hover、focused row、expanded node 和 keyboard anchor 不进入全局 selection。
- Project asset selection 与 scene object selection 使用可区分的 kind；不可把文件路径当 identity。
- 面板 pin 是 Inspector 的局部状态：pin 后继续显示原对象，并明确标记，不篡改全局 selection。
- selection 变化本身不进入 document undo；由 selection 触发的 mutation 必须进入 transaction。

### 5.3 Focus

- 键盘命令由 stable command id 路由，根据 active panel、active document 和 selection 计算 enablement。
- Delete、Rename、Copy/Paste 等有歧义命令必须由 focused surface 明确解释。
- 文本输入拥有编辑快捷键时，不能被全局场景命令抢占。
- Command Palette 打开后获得搜索焦点；关闭后恢复先前有效 focus target。

## 6. Mode、Tool 与命令

全局 Mode 与局部 Tool 分离：

| 概念 | 示例 | 所有者 | 规则 |
| --- | --- | --- | --- |
| Editor Mode | Edit、Play、Preview | session/application | 影响 world 与 mutation policy |
| Viewport Tool | Select、Move、Rotate、Scale | Scene View tool service | 影响输入解释，不改变 world ownership |
| View Option | camera、shading、overlay | Scene View panel | panel-local，可布局持久化但不写 scene |
| Command | Save、Undo、Frame Selection | command registry/router | menu、shortcut、palette 共用 stable id |

未接入真实行为的按钮必须 disabled，并提供原因；不得为了“看起来完整”制造可点击但无效果的控件。

首批 Workbench Bar 只投影已有事实：

- project/document 占位与 dirty；
- active selection summary；
- Edit mode；
- tool commands 的 disabled/pending 状态；
- task 和 diagnostic 摘要。

Play/Preview、gizmo、snap 和 VCS 只有对应 service/command 存在后才启用。

## 7. Inspector 状态合同

Inspector header 固定回答“正在看什么、能否编辑、为何不能编辑”：

| 状态 | Header 表达 | Body 行为 | Mutation |
| --- | --- | --- | --- |
| no selection | `Nothing selected` | 显示选择指引 | 无 |
| loading | object + `Loading` | skeleton/保留上次稳定内容并降级 | 禁止 |
| read-only | object + `Read only` | 字段可复制，不可修改 | 禁止 |
| locked | object + `Locked` + reason | 字段禁用 | 禁止 |
| editable clean | object | 正常字段 | command + transaction |
| editable dirty | object + dirty indicator | 标记有未保存修改 | command + transaction |
| invalid | object + error count | 字段就地错误并链接 Problems | 阻止无效提交或按字段策略处理 |
| stale/missing | stable id + `Unavailable` | 保留可诊断上下文与重新定位动作 | 禁止 |

字段错误在字段附近是 primary feedback；Problems 保存结构化详情。普通 validation failure 不弹 modal。

第一版不建立通用 property-grid ABI。Transform、material reference 等真实字段按 Feature 需求实现明确 ViewModel；出现两个以上稳定 consumer 后再提取共享行/section primitive。

## 8. Project 与 Hierarchy

### 8.1 Project

Project 面板负责 active project 的 asset/product 查找与状态投影，不直接拥有 project-open lifecycle、importer 或文件 IO：

- project selection、build、restart、repair、upgrade 与 Safe Mode 属于 Shell-owned project launch/recovery surface；
- `Ready` 只表示 bootstrap inspection 通过，不表示 ProjectScope、asset catalog 或运行态 session 已激活；
- bootstrap 候选工程不得进入 active project window title/context；只有未来正式 ProjectSession 可以发布 active identity；
- 没有 application service 的 next action 只显示为非交互“下一步”文本，不渲染永久 disabled 的假按钮；
- 当前 Problems service 只有 append-only publish，没有按 source 替换/撤销语义；project-open diagnostic 先在 Shell surface
  就地显示，避免状态切换后遗留重复问题；
- 顶部只有 search、scope/filter 和 view mode；
- row/tile 使用稳定 asset id，显示 readiness、stale、missing、failed 等 product state；
- selection 与 Inspector 协作；
- 双击/Enter 通过 command 打开或聚焦适合的 document/tool；
- import、reimport、rename、move 等 mutation 后续通过 application service 和 command/transaction；
- 大集合必须增量/分页或虚拟化，不把整个 catalog 实例化为 controls。

### 8.2 Hierarchy

#363 的第一纵切采用以下当前合同：

- Hierarchy 是 active scene/document authoritative snapshot 的 presentation projection，不拥有或缓存另一份
  Scene/World truth；
- `SceneEntitySnapshot.ObjectId` 是 entity row 的稳定 identity。snapshot 更新后 selection 按 `ObjectId`
  重映射到新 snapshot；Avalonia row、控件实例与可见索引都不是 selection truth；
- 当前 ABI 没有 `ParentId`。因此 scene root 只能是 presentation-only 容器，其下 entity 是同级 projection；
  它不得被解释为已建立 engine parent/child 关系；
- filter text、root expansion、keyboard anchor、scroll position 与列宽属于 panel-local state，不写入 scene、
  dirty revision 或 undo history；filter 隐藏已选 entity 时保留 document selection；
- ViewModel 生成 flat visible-row projection，View 使用虚拟化列表。缩进、连接线与 expander 只表达该 projection，
  不通过递归 `ItemsControl` 为完整 scene 实例化控件树；
- 视觉采用已删除 Hierarchy 的目标效果，而不恢复其实现架构：紧凑 search/count toolbar、Name/Type header、
  20 px rows、12 px/layer indentation、connector、chevron、entity icon、hover/selection 与明确 empty state；
- 当前唯一诚实的 entity type label 是 `Entity`。ABI 提供真实 kind 后再投影新的 type/icon，而不是从名称或
  控件层猜测；
- search/filter 只改变 visible projection 和计数，不改变 scene，也不因结果为空清除 selection。

明确拒绝：

- 在 Scene ABI 具备 `ParentId`、kind、visibility/lock state 且 Application 提供 command/transaction 之前，
  不伪造 reparent、visibility、lock、rename、delete 或拖放 mutation；
- 不恢复旧 fixture/provider、transient row selection 或通用 Tree framework。#363 只建立 Hierarchy 自有的最小
  projection/ViewModel，并在出现第二个真实 consumer 后再评估共享 primitive；
- 不把 panel-local expansion/filter 持久化为 scene authoring data。

## 9. Scene View

Scene View chrome 分为三层：

1. panel tab：document/panel identity；
2. panel-local toolbar：camera、shading、overlay、tool option；
3. viewport overlay：selection/tool feedback、backend state、recovery action。

Viewport backend 不可用时：

- 保留 viewport 尺寸和背景，不用大标题挤压布局；
- 在视区内显示单个轻量状态卡；
- 提供 `Open Problems`、`Retry` 或明确的不可用原因（只显示实际支持的动作）；
- 同一 structured diagnostic id 由 Scene View、Problems/Console 和 Status 引用，不生成三份独立错误。

Scene View 不直接执行 engine mutation。picking 产生 selection intent；gizmo drag 产生有明确 begin/update/commit/cancel 的 transaction intent。

## 10. Feedback 层级

一个事实可以有多个投影，但只能有一个 primary feedback surface：

| 优先级 | Surface | 使用场景 |
| --- | --- | --- |
| 1 | inline field/object/viewport | 用户能在当前上下文直接恢复的问题 |
| 2 | Problems | structured warning/error、来源、恢复动作、历史 |
| 3 | Console | 调试流、详细日志、关联 diagnostic |
| 4 | Status Bar | 最新摘要、计数、可点击目标 |
| 5 | Modal Dialog | destructive confirmation 或必须立即决策的阻塞问题 |

规则：

- diagnostic identity、severity、source 和 recovery action 由共享 snapshot 提供；
- Console按sequence/time投影时序日志；默认不折叠，显式Collapse也只合并相邻相同run，不重排时间流。Problems默认显示
  hub-owned Active inventory，可切换History查看Incident/Active/Resolved/Stale。两个tab位于同一个Diagnostics panel，
  使用同一hub的两条stream-specific subscriptions，但不合并record语义；
- Clear是当前tab的sequence barrier，只隐藏较早投影；它不删除hub记录、不重置sequence，也不影响另一个tab或进程外observer；
- collapse/search/severity/channel属于panel-local view state。折叠只改变projection，并显示repeat count；Active Problems不受
  view-only Clear隐藏，只有producer发布Resolved/Stale才从current inventory移除；
- cursor expired、drop、分页/窗口截断和字段截断必须在当前tab可见，不能用“0 items”掩盖证据缺口；
- Status Bar 不滚动长日志，不显示多行堆栈；
- warning/error 不自动抢焦点或展开底部抽屉；
- repeated diagnostic 以计数/最后时间更新，不新增视觉副本；
- background task 进入 Tasks surface，Status 只显示数量或最相关进度；
- success 只在有意义时短暂显示，不建立 toast 动画系统作为前置条件。

## 11. 视觉与交互基线

- 视觉层级来自间距、surface、border 和 typography token，不复制外部编辑器皮肤。
- 默认密度服务桌面生产场景；可点击目标不能因紧凑而失去可用尺寸。
- 图标必须有 tooltip；只有图标不足以表达 dirty、read-only、invalid 等高风险状态。
- focus、selected、hover、disabled、warning 和 error 必须可区分。
- 不以颜色作为唯一状态信号。
- 动画只用于解释状态变化，默认短且可被系统 reduced-motion 策略关闭。
- 典型面板必须提供 design-time data/preview；运行态 fake service 不进入 production composition。
- Tree/List 大集合优先虚拟化、稳定 item identity 和增量 snapshot。

## 12. 所有权与实现边界

| 层 | 拥有 | 不拥有 |
| --- | --- | --- |
| Shell | Main Menu、Workbench Bar、Dock、Status、overlay host、layout preset | scene/asset truth、engine mutation |
| Features | Hierarchy、Project、Scene View、Inspector及Diagnostics内部Console/Problems的ViewModel与View | 全局 service locator、Dock 直接编排、diagnostic truth |
| UI | token、共享小型 primitive、icon、可访问状态样式 | 业务命令、engine/session state |
| Application/Core | selection、command、transaction、dirty、diagnostic、task、session snapshot | Avalonia controls |
| EngineBridge | typed native/session adapters 与 revisioned data handoff | UI state、panel lifetime、file picker |

前端继续使用现有 Avalonia + MVVM + 自有 Dock。只有出现明确缺口且已有两个以上 consumer 时才增加共享 primitive；不引入新的通用 UI framework、第二套 Dock 或 panel-local service locator。

### 12.1 UI authoring

Studio v1 只有一个 production UI runtime：Avalonia retained control tree。完整 owner、state、invalidation 和
lifecycle 合同见 [Studio 前端硬切架构](studio-frontend-hard-cut.md)。

| 场景 | 首选 authoring | 原因 |
| --- | --- | --- |
| 长期 panel、表单、列表、深度 binding | compiled XAML + typed ViewModel | compiled binding、模板、虚拟化、preview 和可访问性成熟 |
| algorithmic composition、typed code binding | code-only Avalonia + ViewModel | 与 XAML 使用同一控件树和 lifecycle |
| Viewport overlay、graph、timeline | 专用 Avalonia/custom-drawn control | 输入、绘制和性能需求由专用控件承担 |

共同规则：

- XAML 与 code-only Avalonia 是 authoring syntax，不是不同 backend；
- built-in 在 v1 静态组合，不先经过 public extension/generation framework；
- ViewModel 只投影 Application snapshot、提交 typed intent，不拥有 Document/Engine truth；
- Control 不自行创建 top-level Window、操作 native runtime 或接管 process lifetime；
- authoring 选择不改变本章的 selection、focus、diagnostics、layout 和 accessibility 合同；
- 自有 Code-first tree/host 已由 ADR-0007 安排删除，不再新增 consumer 或 primitive。

## 13. 案例与研究决策

| 来源 | 观察 | 决策 |
| --- | --- | --- |
| Unreal Editor Interface / Level Editor / Scene Outliner API | Viewport、Outliner、Details、Content 围绕选择协作；Scene Outliner 把 widget、mode、hierarchy 与稳定 `FSceneOutlinerTreeItemID` 分开 | Adopt owner/projection/稳定 identity 边界；不复制 API、外观、素材或名称 |
| Unreal Project Browser / Content Browser、Godot/O3DE Project Manager / asset dock、Unity Hub / Project window | 工程选择、版本、构建与恢复在工作台前或独立管理 surface；编辑器内 asset browser 只管理 active project 内容 | Adopt 所有权分层；当前先用 Shell surface，独立进程延后 |
| Godot Editor / Inspector Dock | Scene、FileSystem、Inspector 协作；底部面板可折叠；Inspector 有搜索、历史和恢复入口 | Adopt 可折叠诊断与明确 Inspector 状态；历史/收藏延后 |
| O3DE Editor / Entity Outliner source | Outliner + Asset Browser + Viewport + Inspector 的生产布局；widget、list model 与 sort/filter proxy 分工，selection/expansion 以 entity id 恢复 | Adopt snapshot/model/filter owner 边界与过滤期间 selection 保留；visibility、lock、拖放和批量能力延后 |
| Unity `SceneHierarchy` reference source | `TreeViewController` 消费稳定 entity id，`TreeViewState` 保存窗口局部 expansion/selection，`GetRows()` 暴露当前 visible rows | Adopt stable-id selection、panel-local tree state 与 flat visible-row projection；不把 Unity layout persistence 或 API 搬入 Scene 文档 |
| Unreal Output Log / Message Log | Output Log是category/verbosity时序记录，Message Log listing是可筛选、可执行token的结构化消息 | 作为Diagnostics主先例，采用“Console时间序列 / Problems可行动诊断”分工；合并到一个Dock panel以控制默认surface数量，不复制Slate/module/token API |
| Unity Console、Godot Output / Debugger Errors、O3DE Console/error guidance | 过滤、折叠、清除属于查看层；常规输出与问题surface可分；O3DE还把命令/CVar纳入Console | 交叉采用有界filter/collapse和被动呈现；拒绝让Clear删除truth、让warning抢焦点，或在本Slice加入命令/CVar |
| Blender HIG | 区分 scene selection、UI selection、drag 与 undo；空间上下文明确 | Adopt selection/focus/undo 边界 |
| Marking Menu 实证研究 | 可见菜单可帮助 novice 逐步形成 expert gesture | Deferred：command catalog 和快捷键冲突模型稳定后再评估 |
| Toolglass / Magic Lenses | 空间 overlay 能减少反复切换 mode 的成本 | Adapt：只用于 viewport 的非阻塞 overlay；拒绝全局浮动工具层 |

拒绝：

- 逐像素复刻任何外部编辑器；
- 用更多常驻 panel 代替清晰任务路径；
- 以 modal dialog 呈现普通命令失败；
- 让 View 或插件直接拥有 engine object、native handle 或任意文件 IO；
- 在没有真实 service 的情况下启用假按钮；
- 先设计完整 property-grid、window manager 或 runtime UI plugin ABI。

## 14. 首批实现切片

### Slice A：默认工作台 Shell context

目标：只建立生产默认组合和可观察的全局 context，不触碰 scene mutation。

状态：#338 已实现。布局 preset 由 Shell 持有，panel descriptor 继续只描述注册信息；保存布局优先于 preset，Reset 返回 `Default`。

范围：

- 新增 Shell-owned Workbench Bar；
- 默认 Dock 改为左侧 Hierarchy + Project、中心 Scene View、右侧 Inspector、底部折叠 Diagnostics；
- #338 当时将 UI Style 与 Frame Debugger 从默认 layout 移出；ADR-0007 R0 后，UI Style 已改为
  compiled Avalonia，Frame Debugger 则从 panel/action catalog 删除；
- 窗口标题投影 project/document/dirty 占位；
- Workbench Bar 只显示已有状态；未实现 tool/mode command 明确 disabled；
- 增加 design preview、ViewModel tests 和 shell smoke 断言。

该历史 Slice 当时运行的验证：

- 当时的 legacy partial solution 在 Debug / Release 通过；这不是当前完整 managed gate 的计数；
- compiled XAML build 覆盖 MainWindow 与 Project panel；
- 人工 smoke：默认尺寸、Hierarchy -> Workbench Bar/Inspector selection、Compact preset、disabled reason 的 accessibility projection；
- native editor smoke 不适用于本 Slice：改动只涉及 Avalonia Studio 托管前端，没有修改 C++、native bridge 或 native editor shell。

不做：

- 新 Dock、响应式布局 DSL、真实 Play、gizmo、selection picking；
- 可写 Inspector、scene save/load、asset mutation；
- 新的全局 service locator 或通用前端框架。

验收路径：

```text
launch Studio
-> create or open a canonical project
-> automatically create or open Default.asharia.scene.json
-> create/select an entity in Hierarchy
-> edit name and local Transform in Inspector
-> observe dirty, save, close project, reopen
-> verify authoritative data is unchanged
```

### Slice B 及以后

1. #343 已把 project-open snapshot 注入 workbench；#345 把生命周期状态收敛到 Shell launch surface，
   Project 面板恢复为 active-project asset workspace 占位；
2. 接入正式 project selection/report provider；每个动作只有在对应 application service 与 command route 存在后才渲染为控件；
3. 完成正式 ProjectSession/ProjectReady 后，Project 面板接入真实 asset/product snapshot 与 readiness；
4. #381已用同一Diagnostics面板接入Console时序日志与Problems结构化诊断；按稳定typed source替换和导航仍待source/target合同；
5. Inspector 明确 empty/read-only/dirty/invalid，并接入第一个 transaction-backed writable field；
6. Scene View toolbar/overlay 与 diagnostic deduplication；
7. Scene picking、gizmo transaction 和 Scene Authoring MVP；
8. Play/Game View 与运行态 session。

每项独立跟踪，不把后续能力并入 Slice A。

## 15. 验证

文档 Slice：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
git diff --check
```

UI 实现 Slice 至少执行：

```powershell
dotnet build apps\studio\Asharia.Studio.sln -c Release
dotnet test apps\studio\Asharia.Studio.sln -c Release --no-build --blame-hang --blame-hang-timeout 10m
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

并完成：

- `--smoke-editor-shell`；
- 无新增 binding error；
- 默认与紧凑尺寸人工 smoke；
- keyboard focus、Command Palette 恢复、panel reopen、layout persistence；
- 可视变化截图或 design preview 证据。

## 16. 参考资料

官方引擎与 UI 资料：

- [Unreal Editor Interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface)
- [Unreal Level Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-editor-in-unreal-engine)
- [Unreal Outliner](https://dev.epicgames.com/documentation/en-us/unreal-engine/outliner-in-unreal-engine)
- [Unreal SceneOutliner module API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/SceneOutliner)
- [Unreal `ISceneOutliner` API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/SceneOutliner/ISceneOutliner)
- [Unreal Opening an Existing Project](https://dev.epicgames.com/documentation/en-us/unreal-engine/opening-an-existing-unreal-engine-project)
- [Unreal Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine)
- [Godot：A first look at the editor](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html)
- [Godot Inspector Dock](https://docs.godotengine.org/en/stable/tutorials/editor/inspector_dock.html)
- [Godot Project Manager](https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html)
- [O3DE Editor](https://docs.o3de.org/docs/user-guide/editor/)
- [O3DE Entity Outliner](https://www.docs.o3de.org/docs/user-guide/editor/entity-outliner/)
- [O3DE `EntityOutlinerListModel`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzToolsFramework/AzToolsFramework/UI/Outliner/EntityOutlinerListModel.cpp)
- [O3DE `EntityOutlinerSortFilterProxyModel`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzToolsFramework/AzToolsFramework/UI/Outliner/EntityOutlinerSortFilterProxyModel.cpp)
- [O3DE Project Manager](https://www.docs.o3de.org/docs/user-guide/project-config/project-manager/)
- [O3DE Asset Browser](https://www.docs.o3de.org/docs/user-guide/editor/asset-browser/)
- [Unity Hub Manage Projects](https://docs.unity.com/en-us/hub/project-manage)
- [Unity Project window](https://docs.unity3d.com/Manual/ProjectView.html)
- [Unity `SceneHierarchy` reference source](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Editor/Mono/SceneHierarchy.cs)
- [Blender HIG：Selection](https://developer.blender.org/docs/features/interface/human_interface_guidelines/selection/)
- [Blender HIG：General Patterns](https://developer.blender.org/docs/features/interface/human_interface_guidelines/general_patterns/)

研究：

- Kurtenbach、Sellen、Buxton，*An Empirical Evaluation of Some Articulatory and Cognitive Aspects of Marking Menus*，DOI `10.1207/s15327051hci0801_1`。
- Bier 等，*Toolglass and Magic Lenses: The See-Through Interface*，DOI `10.1145/259963.260447`。
