# 颜色 Token 指南

本文说明 Studio 前端 Deep Dark 颜色 token 的来源、命名和使用规则。完整 token 定义位于 `UI/Styles/Tokens/DeepDarkColors.axaml`。

## 1. 主题定位

当前默认主题为 `Editor Deep Dark`，面向游戏引擎编辑器外壳、Dock 面板、Inspector、Console、Asset Browser、Graph 和 Timeline 等工具界面。

设计目标：

```text
1. 用分层深色 surface 表达界面层级，不使用大面积纯黑。
2. 用冷蓝 Accent 表达主交互、选中、焦点和 Dock 拖拽反馈。
3. 用语义色表达 Success、Warning、Error、Info、Debug 等状态。
4. 重要状态不能只依赖颜色，后续控件应配合图标、文本、边线或形状。
5. 普通文本优先满足 WCAG AA 4.5:1，对焦点和控件边界优先满足 3:1。
```

## 2. 文件结构

```text
UI/Styles/Tokens/DeepDarkColors.axaml
  只放 Color 和 SolidColorBrush token。

UI/Styles/Shell/MainMenu.axaml
  使用 DynamicResource 引用 token，不直接写业务色值。

App.axaml
  在 Application.Resources 中 include DeepDarkColors.axaml。
```

新增主题 token 时，先扩展 `DeepDarkColors.axaml`，再在控件样式中引用。不要在控件样式里散落新的十六进制色值。

## 3. 命名规则

颜色 key 使用 `EditorColor...`，Brush key 使用 `EditorBrush...`。

```text
EditorColorBase00
EditorBrushBase00

EditorColorTextPrimary
EditorBrushTextPrimary

EditorColorAccent
EditorBrushAccent
```

规则：

```text
1. Color 是原始色值，Brush 是 Avalonia 控件直接使用的资源。
2. 控件 Background、Foreground、BorderBrush 优先引用 Brush。
3. 自定义绘制或需要计算颜色时引用 Color。
4. 半透明资源使用 Brush Opacity，不使用 alpha hex，避免工具链对 alpha 顺序理解不一致。
```

## 4. Token 分组

### Base / Surface

用于应用底层背景、DockHost、面板、Popup、输入框和分割线。

```text
EditorBrushBase00             主窗口最底层背景
EditorBrushBase01             菜单栏、标题栏、DockHost 背景
EditorBrushSurface01          工具面板和 Console 主背景
EditorBrushSurface02          面板内容区、卡片、属性行
EditorBrushSurface03          Hover 背景
EditorBrushSurfaceActive      激活标签页和激活面板 Header
EditorBrushSurfaceOverlay     Popup、浮动面板、命令面板
EditorBrushDivider            非强调分割线
EditorBrushBorderSubtle       普通面板边框
EditorBrushBorderDefault      Popup 和浮动窗口边框
EditorBrushBorderInteractive  输入框等可交互控件边框
```

### Text / Icon

用于所有文本和图标前景色。

```text
EditorBrushTextPrimary        主要正文、激活标题、重要属性名
EditorBrushTextSecondary      次级文本、未激活标签
EditorBrushTextMuted          时间戳、路径、弱说明
EditorBrushTextDisabled       禁用文本
EditorBrushIconPrimary        主要图标
EditorBrushIconSecondary      普通工具图标
EditorBrushIconDisabled       禁用图标
```

`Text Disabled` 只用于真正不可交互内容，不要用于正文或重要提示。

### Interaction

用于主操作、焦点、选中和 Dock 拖拽反馈。

```text
EditorBrushAccent             主按钮、当前工具、激活边
EditorBrushAccentHover        可点击元素 Hover
EditorBrushAccentPressed      Pressed 或拖动中状态
EditorBrushFocusRing          键盘焦点、输入框焦点、Dock Drop Target 焦点
EditorBrushSelectionActive    激活列表选中项
EditorBrushSelectionInactive  非激活面板选中项
EditorBrushSelectionSoft      搜索匹配和弱选区
EditorBrushDropPreview        Dock 停靠预览区域
EditorBrushDragTargetFill     Dock 拖拽目标填充
```

Dock 拖拽相关颜色不要复用普通按钮 hover 色。Drop Target、Drop Preview 和 Focus Ring 是独立语义。

### Semantic

用于状态和反馈。

```text
EditorBrushSuccess
EditorBrushWarning
EditorBrushError
EditorBrushFatal
EditorBrushInfo
EditorBrushDebug
EditorBrushModified
EditorBrushBreakpoint
EditorBrushRunning
EditorBrushPaused
EditorBrushStopped
```

语义色只表达状态，不作为装饰色大面积使用。

## 5. 使用示例

XAML 样式中：

```xml
<Style Selector="Menu.editor-main-menu">
    <Setter Property="Background" Value="{DynamicResource EditorBrushBase01}" />
    <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextSecondary}" />
</Style>
```

控件中：

```xml
<Border Background="{DynamicResource EditorBrushSurface01}"
        BorderBrush="{DynamicResource EditorBrushBorderSubtle}">
    <TextBlock Foreground="{DynamicResource EditorBrushTextPrimary}" />
</Border>
```

自定义绘制中需要 `Color` 时再引用 `EditorColor...`。

## 6. 扩展规则

新增 token 前先判断它属于哪类：

```text
Base / Surface      界面层级和容器
Text / Icon         内容前景
Interaction         点击、选中、焦点、拖拽
Semantic            成功、警告、错误、运行状态
Feature             某类编辑器面板的稳定语义
Graph / Code        图、节点、脚本编辑器专用语义
```

如果一个颜色只服务某个临时控件状态，优先使用现有 token。只有当它表达新的稳定语义时，才新增 token。

## 7. 禁止事项

```text
1. 不在新增样式中直接写散落色值，除非是临时 Spike。
2. 不用低对比分割线表达重要状态。
3. 不用 Warning / Error / Success 做普通装饰。
4. 不在大列表 ItemTemplate 中通过 converter 动态计算颜色。
5. 不只靠颜色表达错误、锁定、修改、断点等重要状态。
```

## 8. 后续工作

后续实现 Dock、Inspector、Console、Asset Browser、Graph 等界面时，应先复用本指南里的 token。若发现 token 不足，先更新 `DeepDarkColors.axaml` 和本文，再修改控件样式。
