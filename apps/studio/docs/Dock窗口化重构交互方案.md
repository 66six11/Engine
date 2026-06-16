# Dock 窗口化重构交互方案

## 结论

当前 Dock 已经从固定四区 XAML 走向 layout graph，但运行时仍以 `Window` 作为激活和颜色控制中心。下一阶段建议把交互模型重构为：

```text
Workspace 只负责容纳窗口树
Window 是唯一可激活、可拖拽、可着色、可持久化的运行时 surface
Tab 只是 Window 内的内容页
Window/Split 只作为布局节点，不再承担活动面板语义
```

换句话说，去掉“活动面板”概念，保留“活动窗口”。Docked window、floating window 和后续独立 OS window 都使用同一套窗口语义，只是 host 不同。

## 调研依据

成熟编辑器的共同点不是“面板激活”，而是“窗口或标签页组织工作区”：

- Unity 支持 Editor Window 停靠为 tab、拖出为浮动窗口，并支持保存/恢复布局。参考 [Unity workspace layout](https://docs.unity3d.com/6000.4/Documentation/Manual/CustomizingYourWorkspace.html) 和 [Unity EditorWindow](https://docs.unity3d.com/6000.4/Documentation/ScriptReference/EditorWindow.html)。
- Avalonia 的复杂拖拽适合基于 Pointer capture 和状态机实现。参考 [Avalonia pointer](https://docs.avaloniaui.net/docs/input-interaction/pointer) 和 [Avalonia drag and drop](https://docs.avaloniaui.net/docs/how-to/drag-and-drop-how-to)。
- Avalonia 可复用、可换肤的 Dock chrome 后续应收敛到 `TemplatedControl`，避免 `UserControl` 承担过多样式和行为。参考 [Avalonia templated controls](https://docs.avaloniaui.net/docs/custom-controls/templated-controls)。

当前仓库事实：

- 旧的“活动面板”主路径已经迁移为 `EditorDockWorkspaceViewModel.ActiveWindow` 和 `EditorDockWindowViewModel.IsActiveWindow`。
- Float drop 已迁移为直接创建独立 `EditorDockFloatingWindow`，不再使用窗口内部 overlay floating surface。
- `EditorDockFloatingWindowViewModel` 和 `EditorDockFloatingWindow` 已存在，但目前更像预留 host。
- Dock window、tab、drop guide 和 floating preview 的样式由对应 view 文件内嵌维护，窗口本身拥有可调整的外观边界。

## 网络资料复核

本轮复核时间：2026-06-15。结论只采用官方文档和主项目资料，第三方讨论只作为问题背景，不作为设计依据。

### 成熟编辑器模式

Unity 6.4 文档明确说 Editor interface 是由 tabs 集合管理的，用户可以拖 tab 标题改变布局，悬停时显示 outline，floating window 可以包含多个 tabs，并且布局可保存、导入、删除和重置。参考 [Unity Customize your workspace layout](https://docs.unity3d.com/6000.4/Documentation/Manual/CustomizingYourWorkspace.html)。  
对我们的影响：

```text
1. 运行时核心单位应是 Window + Tab，不是活动面板。
2. 拖拽必须有 outline/drop preview，不能只靠 release 后突变。
3. Floating host 内仍允许多个 tabs，并且后续应支持内部 split。
4. Layout persistence 不是附加功能，而是 Dock 系统闭环的一部分。
```

Unreal 5.7 文档把布局自定义描述为 drag/drop tabs、dock 到主窗口、change color scheme、save layouts。参考 [Unreal Engine Interface and Navigation](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-interface-and-navigation?lang=en-US)。  
对我们的影响：

```text
1. 窗口颜色自控是合理方向，但必须服务识别和工作流，不做装饰。
2. 保存布局需要包含颜色方案，否则用户切换 layout 后识别线索会丢失。
3. Dock 交互应和 mode/tool 状态分离；布局变化不能改变编辑模式。
```

Godot 最新文档描述 docks/panels 可拖边 resize，可通过 dock 顶部菜单改位置或 Make Floating；`EditorDock` 还提供 dock layout flags、dock slot、`layout_key`、`title_color`、`open()` 与 `make_visible()` 的差异。参考 [Godot customizing editor](https://docs.godotengine.org/en/latest/tutorials/editor/customizing_editor.html) 和 [Godot EditorDock](https://docs.godotengine.org/en/latest/classes/class_editordock.html)。  
对我们的影响：

```text
1. Window 需要稳定 layout key，不能只靠显示 title 持久化。
2. Open 和 focus 要分开：显示一个 window 不一定激活它。
3. 窗口颜色可落到 title/accent 层，不需要给整个内容区染色。
4. Plugin/Feature 注册时应声明可停靠位置能力：vertical / horizontal / floating。
```

Visual Studio 文档强调 IDE 会保留窗口位置，可保存多个 window layouts；document tabs 支持 color-code、手动 tab color、left/top/right tab layout、多行 tabs 和 restore closed tab。参考 [Visual Studio window layouts and tabs](https://learn.microsoft.com/en-us/visualstudio/ide/customizing-window-layouts-in-visual-studio?view=visualstudio)。  
对我们的影响：

```text
1. 用户布局应允许多个命名 preset，不只保存最后一次状态。
2. Tab 颜色可以是内容识别线索，但应和 Window appearance 分层。
3. Tab overflow 不能长期只靠截断；至少要规划滚动、下拉或多行策略。
4. Restore closed tab/window 可以作为后续恢复能力，避免误关成本过高。
```

### Avalonia 落地依据

Avalonia pointer 文档说明 pointer capture 会把后续 pointer 事件送回捕获控件，直到 release 或 capture lost；这适合 tab drag、splitter resize 和 floating window move/resize。参考 [Avalonia pointer](https://docs.avaloniaui.net/docs/input-interaction/pointer)。

Avalonia drag/drop 文档明确提醒不要在每次 `PointerPressed` 立即启动拖拽，要加移动阈值或等到 `PointerMoved` 确认用户意图。参考 [Avalonia drag and drop](https://docs.avaloniaui.net/docs/how-to/drag-and-drop-how-to)。  
落地规则：

```text
PointerPressed  -> capture + record press state
PointerMoved    -> 超过 4-6 px 才 BeginDrag
PointerReleased -> 未 BeginDrag 则 activate；已 BeginDrag 则 complete command
CaptureLost     -> cancel drag / resize / move
```

Avalonia templated control 和 pseudoclass 文档支持自定义控件用伪类暴露 `:active`、`:dragging`、`:drop-target` 这类状态。参考 [Avalonia templated controls](https://docs.avaloniaui.net/docs/custom-controls/templated-controls) 和 [Avalonia pseudoclasses](https://docs.avaloniaui.net/docs/styling/pseudoclasses)。  
落地规则：

```text
DockWindowControl
  :active-window
  :floating
  :dragging
  :drop-target
  :drop-reject

DockTabItemControl
  :active-tab
  :drag-source
  :insert-before
  :insert-after
```

Avalonia resources 文档建议需要响应主题或用户偏好的颜色、brush 和尺寸使用 `DynamicResource`，并说明资源查找会从本地资源一路向上找。参考 [Avalonia resources](https://docs.avaloniaui.net/docs/app-development/resources)。  
落地规则：

```text
Window.Appearance 只保存 brush key 或 appearance key
Brush 实体仍放在 DeepDarkColors.axaml / window appearance resource dictionary
窗口实例可在 Resources 中覆盖同名 brush key，实现“窗口自行控制颜色”
```

Avalonia `WindowDrawnDecorations` 文档说明它可自定义 titlebar、frame、caption buttons、resize grips，并提供 `TitleBar`/`ResizeN` 等非客户区 hit-test roles。参考 [Avalonia WindowDrawnDecorations](https://docs.avaloniaui.net/controls/primitives/windowdrawndecorations)。  
对后续顶层 floating window 的影响：

```text
1. Overlay floating window 先用自研 chrome。
2. TopLevel floating window 接入时，优先研究 WindowDrawnDecorations。
3. OS window 的 resize/titlebar hit-test 不要完全复用 overlay window 的 Canvas 逻辑。
```

Dock.Avalonia 主项目把 model/layout rules/serialization 和 Avalonia controls/visual behavior 分开，并支持 floating windows、docking targets、layout persistence 和 theme customization。参考 [Dock for Avalonia](https://wieslawsoltes.github.io/Dock/) 和 [Dock GitHub](https://github.com/wieslawsoltes/Dock)。  
对我们的影响：

```text
1. 继续自研主路径是可行的，但必须保持 model/view/command 分层。
2. 不要把 layout mutation 写进 view code-behind。
3. Serialization 与 view model 绑定要保持轻量，避免保存 Control 或重对象。
```

### 复核后的设计修正

```text
1. 活动面板语义必须移除，ActiveWindow 才是唯一激活 surface。
2. Window 和 Tab 都要有稳定持久化 ID；Title 只能是展示文本。
3. Workspace edge insert 是主窗口内 dock 行为；只有离开 DockRoot bounds 才是 float。
4. Tab precision insert 要支持 index、reorder、overflow auto-scroll。
5. Window appearance 保存为 appearance key；颜色资源仍走 token。
6. OpenWindow 和 FocusWindow 分开，避免菜单显示窗口时破坏当前工作上下文。
7. Layout v2 至少保存 root tree、window list、tabs、activeWindowId、activeTabId、appearanceKey、bounds、z-order。
```

## 目标状态模型

推荐把现有类型按职责重命名或分层：

```text
EditorDockWorkspaceViewModel
  ActiveWindowId
  RootNode
  FloatingWindows
  DragState

EditorDockWindowViewModel
  Id
  Title
  Tabs
  ActiveTabId
  Bounds
  Appearance
  CanClose / CanFloat / CanDock / CanSplit

EditorDockTabViewModel
  Id
  ContentKey
  Title
  Kind
  Content
  State

EditorDockSplitNodeViewModel
  Id
  Orientation
  Children / Weights

EditorDockWindowNodeViewModel
  Window

EditorDockWorkspaceKind
  MainWindow | FloatingWindow
```

需要删除或降级的语义：

- 运行时激活语义只保留 `ActiveWindow`。
- Window view model 只保留 `IsActiveWindow`。
- Runtime dock container type is now `EditorDockWindowViewModel`.
- 内部 floating window 不再作为运行时 surface；Float drop 直接使用顶层 Avalonia Window host。
- `DockArea` 只保留默认注册 metadata，不参与运行时寻址。

## 交互设计

### 激活

激活单位只有 window：

```text
点击 window border 或 tab strip -> 激活 window
点击 tab -> 激活 window，并激活该 tab
拖拽 tab -> 源 window 保持激活，释放成功后目标 window 激活
拖拽预览经过其他 workspace -> 不改变目标 workspace 的 ActiveWindow
浮动窗口置顶 -> 同时更新 ActiveWindowId 和 z-order
```

视觉反馈：

- Active window 只显示一条细强调边，不整块染蓝。
- Active tab 仍在 tab strip 内表达，不能和 active window 混成同一个状态。
- Keyboard focus 使用 focus ring，不复用 active window 颜色。

### 拖拽

当前 `OnTabPointerPressed` 立即开始 drag preview，建议改成三态：

```text
Pressed
  左键按下，仅记录 tab/window、起点和 pointer capture

Dragging
  移动超过 4-6 px 后才显示 adorner 和 drop guide

Released
  未进入 Dragging -> 激活 tab/window
  已进入 Dragging -> 执行 drop 命令
```

这样能避免用户单击 tab 时闪现拖拽 overlay。Avalonia 官方也建议不要在每次 `PointerPressed` 立刻启动拖放，应设置最小移动阈值。

Drop 命中语义建议：

```text
Tab strip item gap -> InsertTabAtIndex / ReorderTab
Tab well           -> TabIntoWindow
Window guide       -> SplitLeft / SplitRight / SplitTop / SplitBottom
Splitter           -> SplitBetweenWindows
Workspace edge     -> InsertAtWorkspaceEdge
Outside host       -> FloatAsWindow
Invalid body       -> Reject with reason
```

注意：window body 默认不合并 tab，这个现有规则应保留。合并和精确插入只能来自真实 tab strip。

### 工作区边缘插入与 Tab 精确插入

这里要补齐两类高频交互：拖到 workspace 边缘创建新窗口，以及拖到 tab strip 指定位置插入 tab。它们都不应该走“活动面板”语义，释放后只改变 window tree 和目标 window 的 active tab。

命中优先级建议：

```text
1. Pointer 未超过拖拽阈值
   -> 不显示任何 drop preview

2. Pointer 在 workspace 外
   -> FloatAsWindow

3. Pointer 在 tab strip 内
   -> InsertTabAtIndex 或 TabIntoWindow

4. Pointer 在 workspace root edge band 内
   -> InsertAtWorkspaceEdge

5. Pointer 在 splitter hit slop 内
   -> SplitBetweenWindows

6. Pointer 在可见 window guide spoke 内
   -> SplitLeft / SplitRight / SplitTop / SplitBottom

7. Pointer 在 window body 其他区域
   -> Reject
```

Workspace edge band 只在拖拽中启用，建议尺寸：

```text
Left / Right edge band: 32-44 px 宽
Top / Bottom edge band: 28-40 px 高
Corner conflict: 角落按更接近的边处理；距离相等时优先 Left/Right，避免顶部菜单区域误触
Edge preview: 全高或全宽半透明插入带，带 1 条 accent 插入线
```

释放到 workspace edge 的语义不是“把 tab 放到 workspace”，而是：

```text
1. 从源 window 移除 tab
2. 创建一个新 EditorDockWindowViewModel
3. 新 window 只包含该 tab，并把该 tab 设为 active tab
4. 在 RootNode 外层创建或复用 split
5. 按 Left/Right/Top/Bottom 把新 window 插到整棵工作区树边缘
6. 设置 ActiveWindowId 为新 window
7. Normalize layout tree
```

伪命令：

```csharp
public interface IEditorDockWindowCommandService
{
    void InsertTabAtWorkspaceEdge(
        string tabId,
        DockWorkspaceEdge edge,
        double initialWeight = 0.22);

    void InsertTabAdjacentToWindow(
        string tabId,
        string targetWindowId,
        DockSide side,
        double initialWeight = 0.35);

    void InsertTabIntoWindow(
        string tabId,
        string targetWindowId,
        int tabIndex);
}
```

推荐默认比例：

```text
Workspace Left / Right: 新窗口占 22%-28%，或最小 260 px
Workspace Top: 新窗口占 20%-25%，但默认不推荐作为常用入口
Workspace Bottom: 新窗口占 24%-30%，或最小 210 px
Window Left / Right: 新窗口占目标区域 35%
Window Top / Bottom: 新窗口占目标区域 35%
```

Tab strip 精确插入规则：

```text
拖到 tab header 左半区  -> 插到该 tab 前
拖到 tab header 右半区  -> 插到该 tab 后
拖到 tab strip 空白区   -> 插到末尾
单 tab window           -> 仍通过真实 tab strip 插入/重排
同 window 内拖拽        -> ReorderTab，不创建新 window
跨 window 拖拽          -> MoveTab + target tabIndex
```

Tab 插入视觉：

```text
显示 2 px 竖向 insertion caret
caret 高度等于 tab strip 高度
同 window reorder 时，拖拽经过 tab 中线立即交换顺序，并用 100-140 ms 位移动画缓和跳变
tab strip 可横向滚动时，靠近左右边 24 px 自动滚动
拖拽源 tab 在源 window 中保留 ghost gap，避免 tab strip 跳动
```

Workspace edge insert 和 FloatAsWindow 的边界要清楚：

```text
Pointer 仍在 DockRoot bounds 内 -> edge insert / splitter / window guide
Pointer 离开 DockRoot bounds    -> float preview
```

不要把“靠近窗口外侧但仍在 workspace 内”的行为解释成 float；这会让用户很难把 tab dock 到左/右全局边缘。

### 窗口颜色自控

颜色所有权从 Dock 全局样式下放到 window appearance：

```csharp
public sealed record EditorDockWindowAppearance(
    string BackgroundBrushKey,
    string HeaderBrushKey,
    string HeaderActiveBrushKey,
    string BorderBrushKey,
    string AccentBrushKey,
    string TextBrushKey,
    string MutedTextBrushKey);
```

规则：

- Window 负责选择自己的 appearance。
- Dock host 只消费 appearance，不根据 window area 或 active window 硬编码颜色。
- Appearance 值必须引用 `DeepDarkColors.axaml` 中的 token 或新增稳定 token，不能在单个窗口样式里散落 hex。
- Feature window 可提供语义色，例如 Console 用 info/debug、Problems 用 warning/error、Scene 用 accent，但语义色只能用于窄边、tag、状态点，不能大面积铺底。
- ActiveWindow 只影响 border active token，不覆盖窗口自有底色。

推荐新增的窗口级 token：

```text
EditorBrushWindowSurfaceDefault
EditorBrushWindowHeaderDefault
EditorBrushWindowHeaderActiveDefault
EditorBrushWindowBorderDefault
EditorBrushWindowAccentDefault
EditorBrushWindowFloatingShadow
```

Feature 专用颜色仍走 Feature token，例如：

```text
EditorBrushWindowAccentScene
EditorBrushWindowAccentInspector
EditorBrushWindowAccentConsole
EditorBrushWindowAccentProblems
```

## 默认布局

默认布局继续保持专业编辑器框架：

```text
Left:    Hierarchy window
Center:  Scene View window
Right:   Inspector window
Bottom:  Console / Problems window
```

但持久化结果应表达为窗口树，而不是 window 树：

```json
{
  "version": 2,
  "root": {
    "type": "split",
    "id": "split-left-work",
    "orientation": "horizontal",
    "children": [
      { "weight": 0.22, "node": { "type": "window", "windowId": "hierarchy" } },
      { "weight": 0.78, "node": { "type": "split", "id": "split-work-right" } }
    ]
  },
  "windows": [
    {
      "id": "scene-view",
      "hostKind": "docked",
      "activeTabId": "scene-view",
      "appearanceKey": "scene"
    }
  ],
  "activeWindowId": "scene-view"
}
```

## 迁移路线

1. 状态改名但少改行为  
   直接使用 `ActiveWindow`/`IsActiveWindow` 作为唯一激活入口。样式 selector 使用 `.active-window`。

2. Window appearance 落地  
   在 window view model 上加入 appearance key 或 brush key，`EditorDockWindowView.axaml` 改为消费窗口自有颜色。先只支持默认/Scene/Inspector/Console/Problems 五组。

3. 拖拽阈值和 tab overflow  
   Workspace edge insert 与多 tab strip 的 tab index insert/reorder 已落地。后续 `PointerPressed` 只进入 pressed，移动超过阈值后才 `BeginTabDrag`，并补齐 tab overflow、自滚动和多行策略。

4. Floating window 接线  
   Float drop 直接创建 `EditorDockFloatingWindow`，该窗口承载独立 `EditorDockWorkspaceView`，不再走内部 overlay floating window。

5. 持久化 v2  
   保存 split tree、window list、tabs、activeWindowId、activeTabId、appearanceKey、floating bounds。旧 window snapshot 走一次迁移。

6. 顶层 Window host  
   在 overlay floating 稳定后，让 `FloatAsWindow` 可以选择创建 `EditorDockFloatingWindow`。多显示器、DPI、关闭策略和跨 workspace drop 放在这一阶段。

## 验收标准

- 代码和文档中不再把运行时激活单位称为 active panel。
- 用户单击 tab 只激活，不闪现 drag adorner。
- 拖到 workspace 左/右/上/下边缘会创建新 window 并插入整棵 root tree 边缘，不会误判为 float。
- 拖到 tab strip 可以在指定 index 插入，支持同 window 重排和跨 window 移动。
- Docked、floating overlay、top-level floating 三种 host 都使用同一种 window view model。
- 每个 window 可以声明自己的 accent/border 颜色，且颜色来自 token。
- Active window、active tab、keyboard focus、drop target 四种状态视觉上互不混淆。
- Layout save/restore 可以恢复窗口颜色、活动窗口、活动 tab、split 比例和浮动位置。
