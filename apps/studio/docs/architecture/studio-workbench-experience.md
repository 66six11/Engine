# Studio 生产工作台体验规范

状态：Implemented baseline（Slice A / #338；后续 writable Inspector、tool/session 与 asset workflow 仍是 Target）

更新日期：2026-07-28

跟踪：GitHub Epic #119；设计 Slice #337；首个实现 Slice #338

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

2026-07-28 的源码与运行态审计确认：

- `Editor.csproj` 已使用 Avalonia、compiled binding、CommunityToolkit.Mvvm 和现有图标能力；
- Shell 已有 Main Menu、Dock、Status Bar、Command Palette 和 Dialog；
- Dock 已支持 tabs、splits、floating、drop guide 和布局持久化；
- Application/Core 已有 selection、transaction、dirty、diagnostics 和 scene snapshot 基础；
- Shell-owned Workbench Bar 与窗口标题已投影明确的 project/document 占位、Edit mode、selection、task 和 diagnostic 摘要；
- 默认工作台使用 Shell-owned `Default` preset：Hierarchy 与 Project 左侧垂直分割、Scene View 居中、Inspector 右置，Diagnostics 默认折叠；
- `Compact` preset 把 Hierarchy 与 Project 合并为 tab group，仍保留 Scene View 与 Inspector；
- UI Style、Frame Debugger、Console 和 Problems 继续注册并可恢复，但不再由新默认布局创建；
- Project 当前是 compiled XAML 空状态面板；尚未连接 project/asset service；
- `Asharia.Editor.Projects` 已定义 project-open session 的 UI-neutral snapshot，Application 已能严格解析
  canonical bootstrap report；当前 Shell 尚未注入该 snapshot，也未启用 project action；
- Hierarchy 到 Inspector 与 Workbench Bar 的 selection 同步和 Command Palette 已可工作。

当前缺口：

1. Project 仍只有显式空状态，`find asset -> inspect/use` 要等待真实 project/asset snapshot service。
2. Inspector 主要是只读属性表，read-only、dirty、invalid、locked 等状态仍需随 writable property Slice 完成。
3. Scene View 缺少面板内工具栏和轻量 overlay；backend 失败仍会占用主要视区。
4. 同一诊断可同时以 Scene View 消息、Console 行和 Status 文本出现，仍需收敛 primary feedback 与重复聚合。
5. Workbench Bar 的 Play、viewport tool 与 project action 仍按现有能力保持 disabled；只有相应 service/command 落地后才能启用。

这些缺口优先通过现有系统的组合、状态投影和少量 Shell surface 解决；不以抽象通用 UI 框架作为前置条件。

## 4. 默认信息架构

### 4.1 宽屏默认布局

目标基准是 1440×900；最低支持尺寸仍由应用窗口约束决定。默认布局使用现有 Dock 表达：

```text
┌ Main Menu ────────────────────────────────────────────────────────────────┐
├ Project / Document │ Select Move Rotate Scale │ Snap │ Edit │ Run controls ┤
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
| 左列 | 260 px | Hierarchy 与 Project 垂直分割；均可成为 tab |
| 右列 | 320 px | Inspector；优先保留可读宽度 |
| 底部抽屉 | 打开时约 180–220 px | 默认折叠，warning/error/running task 可提示但不强制展开 |
| Status Bar | 22–26 px | 只显示摘要、计数和可点击目标 |

Scene View 是唯一默认中心 document。UI Style、Frame Debugger、设置和其他诊断工具由 Window 菜单或 Command Palette 打开，并保留用户布局持久化。

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

其中 project display name 与 project-open 状态应由 Application 投影已有
`ProjectOpenSessionSnapshot`；Presentation 不读取 bootstrap report，也不从 `null` 猜测 loading/error。

窗口标题建议使用：

```text
<document>[*] — <project> — Asharia Studio
```

未知 project/document 使用明确占位，不回退为含糊的 `Editor`。

### 5.2 Selection

- Selection service 是跨面板唯一共享 selection truth。
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

Project 面板负责 asset/product 的查找与状态投影，不直接拥有 importer 或文件 IO：

- 顶部只有 search、scope/filter 和 view mode；
- row/tile 使用稳定 asset id，显示 readiness、stale、missing、failed 等 product state；
- selection 与 Inspector 协作；
- 双击/Enter 通过 command 打开或聚焦适合的 document/tool；
- import、reimport、rename、move 等 mutation 后续通过 application service 和 command/transaction；
- 大集合必须增量/分页或虚拟化，不把整个 catalog 实例化为 controls。

### 8.2 Hierarchy

- 默认显示 active scene/document snapshot；
- selection 双向同步；
- search/filter 不改变 scene；
- visible、locked、dirty/prefab-like state 需要稳定 icon/badge 和 tooltip；
- expansion、column width 和 local filter 是 panel-local state；
- reparent、rename、delete 尚未接入 transaction 前不得伪装为可用。

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
| Features | Hierarchy、Project、Scene View、Inspector、Problems/Console 的 ViewModel 与 View | 全局 service locator、Dock 直接编排 |
| UI | token、共享小型 primitive、icon、可访问状态样式 | 业务命令、engine/session state |
| Application/Core | selection、command、transaction、dirty、diagnostic、task、session snapshot | Avalonia controls |
| EngineBridge | typed native/session adapters 与 revisioned data handoff | UI state、panel lifetime、file picker |

前端继续使用现有 Avalonia + MVVM + 自有 Dock。只有出现明确缺口且已有两个以上 consumer 时才增加共享 primitive；不引入新的通用 UI framework、第二套 Dock 或 panel-local service locator。

### 12.1 UI authoring backend

工作台体验合同不要求所有面板使用同一种 authoring 方式。完整的 contribution、action、tool、state、invalidation
和 lifecycle 合同见 [Studio 前端框架](studio-frontend-framework.md)。现有
[Code-first UI 设计](../Code-first%20UI设计.md)和
[Avalonia/XAML Editor 扩展规范](editor-extension-avalonia.md)继续共同生效：

| 场景 | 首选 backend | 原因 |
| --- | --- | --- |
| 低频、小规模、标准按钮/过滤/只读详情 | Code-first UI | UI-neutral、开发快，复用 Host theme、command、state 和 lifecycle |
| 复杂长期面板、深度数据绑定、模板、动画、自定义控件 | Avalonia + compiled XAML/ViewModel | 直接使用成熟 retained UI、compiled binding 和虚拟化能力 |
| algorithmic composition、typed code binding | code-only Avalonia + ViewModel | 与 XAML 使用同一控件运行时，不需要第二套 backend |
| Viewport overlay、graph、timeline | 专用 Avalonia control + 公共 Editor contract | 输入、绘制和性能需求不应被塞进通用 Code-first primitive |

两种 backend 的共同规则：

- 它们属于同一个 `EditorModule` / contribution / panel lifecycle，不是两套扩展 SDK；
- 同一 extension 可以贡献不同 backend 的不同 panel，但单个 panel 选择一种 backend；
- Code-first 是类似 IMGUI 的顺序 authoring API；当前 Host 把 node tree 重建为 retained Avalonia content subtree，
  keyed reconcile 尚未实现；它不建立第二个 immediate-mode renderer；
- XAML 与 code-only Avalonia 共享同一 content backend/lease，不自行创建 `Window`、操作 Dock 或接管 application lifetime；
- ViewModel 与持久 mutation 继续只依赖公共 Editor service，并走 command/transaction/dirty/validation；
- backend 选择不改变本章的 selection、focus、diagnostics、layout 和 accessibility 合同。

## 13. 案例与研究决策

| 来源 | 观察 | 决策 |
| --- | --- | --- |
| Unreal Editor Interface / Level Editor / Outliner | Viewport、Outliner、Details、Content 与底部诊断围绕选择和任务协作 | Adopt 关系；不复制外观、素材或名称 |
| Godot Editor / Inspector Dock | Scene、FileSystem、Inspector 协作；底部面板可折叠；Inspector 有搜索、历史和恢复入口 | Adopt 可折叠诊断与明确 Inspector 状态；历史/收藏延后 |
| O3DE Editor / Entity Outliner | Outliner + Asset Browser + Viewport + Inspector 的生产布局；搜索、过滤、锁定和可见性状态明确 | Adopt 默认信息架构与状态可见性；复杂列/批量能力延后 |
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
- UI Style 与 Frame Debugger 从默认 layout 移出，但继续可从 Window/Command Palette 打开；
- 窗口标题投影 project/document/dirty 占位；
- Workbench Bar 只显示已有状态；未实现 tool/mode command 明确 disabled；
- 增加 design preview、ViewModel tests 和 shell smoke 断言。

已运行验证：

- Debug / Release `dotnet test apps\studio\Editor.sln`：各 508 tests passed；
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
-> see project/document context and production default panels
-> select fixture entity in Hierarchy
-> observe Inspector and Workbench Bar selection update
-> open Project and Diagnostics tabs
-> open UI Style / Frame Debugger through Window or Command Palette
-> resize to compact width without losing the center document
```

### Slice B 及以后

1. Application 把 project-open snapshot 注入 Shell，Project 面板先呈现状态、诊断和真实可执行动作；
2. Project 面板接入真实 asset/product snapshot 与 readiness；
3. Inspector 明确 empty/read-only/dirty/invalid，并接入第一个 transaction-backed writable field；
4. Scene View toolbar/overlay 与 diagnostic deduplication；
5. Scene picking、gizmo transaction 和 Scene Authoring MVP；
6. Play/Game View 与运行态 session。

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
dotnet test apps\studio\Editor.sln -c Release
dotnet test apps\studio\Editor.sln
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
- [Godot：A first look at the editor](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html)
- [Godot Inspector Dock](https://docs.godotengine.org/en/stable/tutorials/editor/inspector_dock.html)
- [O3DE Editor](https://docs.o3de.org/docs/user-guide/editor/)
- [O3DE Entity Outliner](https://www.docs.o3de.org/docs/user-guide/editor/entity-outliner/)
- [Blender HIG：Selection](https://developer.blender.org/docs/features/interface/human_interface_guidelines/selection/)
- [Blender HIG：General Patterns](https://developer.blender.org/docs/features/interface/human_interface_guidelines/general_patterns/)

研究：

- Kurtenbach、Sellen、Buxton，*An Empirical Evaluation of Some Articulatory and Cognitive Aspects of Marking Menus*，DOI `10.1207/s15327051hci0801_1`。
- Bier 等，*Toolglass and Magic Lenses: The See-Through Interface*，DOI `10.1145/259963.260447`。
