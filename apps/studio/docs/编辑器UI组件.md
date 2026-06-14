游戏引擎编辑器组件 UI 功能需求表

优先级建议：

* **P0**：编辑器基础能力，第一阶段必须有。
* **P1**：标准游戏引擎编辑器应具备的能力。
* **P2**：高级功能、专业工具或可选扩展能力。

执行说明：

* 本文是长期 UI 能力目录，不代表第一阶段要直接完整实现每个功能。
* 第一阶段应优先搭建基础平台、扩展点和数据契约，具体执行方案见 `编辑器UI基础平台方案.md`。
* 平台接口与平台无关契约草案见 `编辑器UI平台契约草案.md`。
* 本清单面向游戏引擎编辑器能力设计，不绑定具体 GUI 框架、编程语言、窗口系统或渲染后端。

---

# 游戏引擎编辑器组件 UI 功能需求表

## 一、编辑器基础框架

---

## 1. 基础 UI 控件库

**优先级：P0**

编辑器所有面板都会依赖的基础控件集合。

### 功能需求

* Button
* Toggle
* Checkbox
* Radio Button
* Dropdown
* ComboBox
* Text Input
* Search Input
* Number Input
* Slider
* Drag Number
* Vector2 / Vector3 / Vector4 输入框
* Quaternion / Euler 输入框
* Color Picker

  * RGB / HSV / HEX 输入
  * Alpha 通道
  * 颜色吸管
  * 颜色预设
  * 最近使用颜色
* Curve Editor 简易控件
* Gradient Editor
* Object Picker
* Asset Picker
* File Picker
* Path Picker
* Enum Selector
* Flag Selector
* List View
* Tree View
* Table View
* Property Grid
* Splitter
* Tab
* Scroll View
* Toolbar
* Status Bar Item
* Tooltip
* Context Menu
* Popup
* Progress Bar
* Loading Spinner
* Icon Button
* Badge / Counter
* Notification / Toast

### 扩展需求

* 支持自定义控件注册。
* 支持自定义绘制。
* 支持主题样式覆盖。
* 支持快捷键焦点导航。
* 支持高 DPI 缩放。

---

## 2. 窗口 / 面板 / Docking 系统

**优先级：P0**

所有编辑器面板的承载系统。

### 功能需求

* Dockable Panel
* Tab 合并
* 面板拖拽停靠
* 面板浮动窗口
* 面板最大化 / 还原
* 面板关闭 / 打开
* 面板分屏
* 多显示器支持
* 面板激活状态
* 面板焦点状态
* 面板生命周期管理

  * 创建
  * 打开
  * 关闭
  * 激活
  * 失焦
  * 销毁
* 默认布局
* 自定义布局
* 保存布局
* 恢复布局
* 重置布局
* 导入 / 导出布局配置

### 扩展需求

* 插件可注册新窗口。
* 插件可指定默认停靠区域。
* 插件可指定窗口标题、图标、菜单路径、快捷键。
* 面板可声明是否允许多个实例。
* 面板可声明是否允许关闭、浮动、停靠。

---

## 3. 主菜单系统

**优先级：P0**

对应你原始清单中的 `menu 主菜单`。

### 功能需求

* 顶部主菜单栏。
* 支持多级菜单。
* 支持菜单项注册。
* 支持往已有菜单路径添加选项。
* 支持菜单分组。
* 支持分隔线。
* 支持菜单图标。
* 支持菜单快捷键显示。
* 支持菜单项启用 / 禁用状态。
* 支持菜单项勾选状态。
* 支持动态菜单。
* 支持最近文件菜单。
* 支持打开窗口类菜单。
* 支持工具类菜单。
* 支持帮助类菜单。

### 常见菜单结构

* File

  * New Scene
  * Open Scene
  * Save
  * Save As
  * Build Settings
  * Exit
* Edit

  * Undo
  * Redo
  * Cut
  * Copy
  * Paste
  * Delete
  * Duplicate
  * Preferences
* Assets

  * Create
  * Import
  * Reimport
  * Show in Explorer
* GameObject / Entity

  * Create Empty
  * Create Camera
  * Create Light
  * Create Mesh
  * Create UI
* Component

  * Add Component
* Window

  * Scene View
  * Game View
  * Inspector
  * Console
  * Profiler
  * Frame Debugger
* Tools

  * Command Palette
  * Diagnostics
  * Package Manager
* Help

  * Documentation
  * About
  * Shortcuts

### 扩展需求

* 菜单项应绑定统一 Command Registry。
* 插件可以注册菜单。
* 插件可以扩展已有菜单路径。
* 菜单项可绑定权限、上下文和可见性规则。

---

## 4. Command Registry / 命令系统

**优先级：P0**

菜单、快捷键、命令面板、右键菜单最好统一基于 Command 系统。

### 功能需求

* 注册命令。
* 注销命令。
* 执行命令。
* 查询命令。
* 命令分类。
* 命令显示名称。
* 命令图标。
* 命令描述。
* 命令快捷键。
* 命令启用 / 禁用状态。
* 命令上下文。
* 命令可见性。
* 命令执行前校验。
* 命令执行结果反馈。

### 扩展需求

* 插件可注册命令。
* 命令可出现在：

  * 主菜单
  * 右键菜单
  * 顶部工具栏
  * Scene View 工具栏
  * Command Palette
  * 快捷键系统
* 命令支持异步执行。
* 命令支持 Undo / Redo 接入。

---

## 5. Command Palette / 命令选择器 / Marking Menu 手势菜单

**优先级：P1**

对应你原始清单中类似 Blender / Maya 的命令选择器，建议做成全局能力。

这里不要只做成普通搜索弹窗，而是做成“文本搜索 + Maya Marking Menu 快速手势选择”的双模式：

* 新手或低频命令：通过 Command Palette 搜索、筛选、执行。
* 高频编辑命令：通过按住快捷键后拖动方向，像 Maya Marking Menu 一样快速选择。
* 熟练用户：可以不等待菜单完全展开，按下、划向目标方向、松开即可执行，形成肌肉记忆。

### 功能需求

* 快捷键呼出命令面板。
* 快捷键按住呼出 Marking Menu 手势菜单。
* 支持按下、拖动、松开即执行的快速手势选择。
* 支持短按打开搜索面板，长按打开径向手势菜单。
* 支持鼠标、触控笔、触控板的方向手势输入。
* 支持 4 向 / 8 向 / 分层径向菜单布局。
* 支持中心区域取消操作。
* 支持拖回中心或按 Esc 取消。
* 支持菜单项 hover 高亮、方向预览和即将执行命令提示。
* 支持熟练模式：手势足够明确时不必完整显示菜单即可触发。
* 支持新手模式：长按显示完整菜单标签、图标和快捷键提示。
* 支持二级 Marking Menu：划到某方向后进入子菜单继续划选。
* 支持根据上下文显示不同手势菜单。

  * Scene View
  * Hierarchy
  * Inspector
  * Asset Browser
  * Node Editor
  * Animation / Timeline
* 搜索所有命令。
* 搜索菜单项。
* 搜索窗口。
* 搜索设置项。
* 搜索资源。
* 搜索场景对象。
* 执行命令。
* 显示命令快捷键。
* 显示最近使用命令。
* 显示收藏命令。
* 支持模糊搜索。
* 支持命令分类。
* 支持常用命令固定到手势菜单。
* 支持最近使用命令进入手势菜单候选。
* 支持用户自定义手势方向和命令绑定。
* 支持不同工作区保存不同 Marking Menu 配置。
* 支持与 Command Registry、快捷键系统、Undo / Redo 系统统一接入。

### 推荐默认手势分组

* Scene View 空白区域：创建对象、对齐、视图、显示模式、捕捉、选择过滤。
* Scene View 选中对象：Transform、Duplicate、Delete、Focus、Frame、Group、Prefab、Component。
* Hierarchy：创建子对象、重命名、复制、删除、设为父子关系、启用 / 禁用。
* Asset Browser：导入、重命名、复制路径、重新导入、查看引用、在资源管理器中显示。
* Node Editor：创建节点、断开连接、对齐、分组、注释、重新布局。
* Timeline：添加关键帧、剪切、复制、粘贴、吸附、循环、播放控制。

### 扩展需求

* 插件可注册命令。
* 插件可注册搜索 Provider。
* 命令结果可带图标、描述、快捷键、上下文信息。
* 插件可注册 Marking Menu 分组。
* 插件可注册上下文相关的手势菜单项。
* 插件可声明手势菜单项的优先级、方向偏好和禁用条件。
* 手势菜单配置可导入 / 导出。
* 手势使用频率可统计，用于推荐常用命令和优化默认布局。

---

## 6. 顶部工具栏

**优先级：P0**

对应你原始清单中的 `顶部工具栏`。

### 功能需求

* Play Mode Controls

  * Play
  * Pause
  * Step Frame
  * Stop
* 当前运行状态显示。
* 当前布局模式选择。
* Layout 切换。
* Layout 保存。
* Layout 重置。
* 全局搜索入口。
* Command Palette 入口。
* 版本控制状态入口。
* 历史记录入口。
* Undo / Redo 按钮。
* 账号 / 项目信息入口，可选。

### 扩展需求

* 插件可往顶部工具栏添加按钮。
* 插件可添加下拉菜单。
* 插件可添加状态显示项。
* 工具栏布局可配置。
* 工具栏按钮可绑定 Command。

---

## 7. 底部状态栏

**优先级：P0**

对应你原始清单中的 `底部状态栏`。

### 功能需求

* 全局后台任务进度。

  * Shader 编译
  * 资源导入
  * 光照贴图烘焙
  * 遮挡剔除
  * 构建任务
  * 资源缩略图生成
* 单击进度条打开 Background Tasks 窗口。
* 后台任务状态入口。
* Debug Log 单行摘要。
* 单击日志摘要打开 Console。
* 当前构建目标平台显示。
* 当前构建配置显示。
* 当前代码优化模式显示。
  
  * Debug
  * Release
  * Development
* Debug / Release / Development 切换入口。
* 当前场景保存状态。
* 当前资源导入状态。
* 当前编译状态。
* 当前版本控制状态。
* 错误数量。
* 警告数量。
* 后台任务数量。
* 内存占用摘要，可选。
* FPS 摘要，可选。

### 扩展需求

* 插件可注册状态栏项。
* 状态栏项支持：

  * 文本
  * 图标
  * 进度条
  * 按钮
  * 下拉菜单
  * Tooltip
* 状态栏项支持优先级排序。

---

## 8. 快捷键 / 输入绑定系统

**优先级：P0**

### 功能需求

* 快捷键管理窗口。
* 查看全部快捷键。
* 搜索快捷键。
* 修改快捷键。
* 恢复默认快捷键。
* 导入 / 导出快捷键配置。
* 快捷键冲突检测。
* 支持全局快捷键。
* 支持上下文快捷键。

  * Scene View
  * Game View
  * Hierarchy
  * Inspector
  * Asset Browser
  * Node Editor
  * Console
* 文本输入状态下禁用冲突快捷键。
* 支持鼠标组合键。
* 支持键盘组合键。
* 支持多平台快捷键映射。

### 扩展需求

* 插件可注册快捷键。
* 快捷键绑定到 Command。
* 支持用户自定义快捷键配置。

---

## 9. 弹窗 / 对话框 / 通知系统

**优先级：P0**

### 功能需求

* Modal Dialog。
* Non-modal Dialog。
* 确认框。
* 删除确认框。
* 覆盖文件确认框。
* 保存修改提示框。
* 进度弹窗。
* 可取消进度弹窗。
* Toast 通知。
* Error Dialog。
* Warning Dialog。
* 文件选择器。
* 目录选择器。
* 资源选择弹窗。
* 组件选择弹窗。
* 脚本类型选择弹窗。

### 扩展需求

* 支持异步任务绑定。
* 支持插件注册自定义弹窗。
* 支持统一样式。
* 支持阻塞和非阻塞两种模式。

---

## 10. 主题 / 样式 / DPI / 可访问性

**优先级：P1**

### 功能需求

* 深色主题。
* 浅色主题。
* 自定义主题。
* 图标主题。
* 字体设置。
* UI 缩放。
* 高 DPI 支持。
* 色弱辅助模式。
* 高对比度模式。
* 焦点可见。
* 键盘可操作。
* Tooltip。
* 图标文本说明。
* 错误提示不能只依赖颜色区分。

### 扩展需求

* 插件可使用统一样式 Token。
* 支持颜色 Token。
* 支持字体 Token。
* 支持间距 Token。
* 支持自定义主题包。

---

## 11. 多语言 / 本地化系统

**优先级：P1**

### 功能需求

* 编辑器语言切换。
* 文本本地化。
* 菜单本地化。
* Tooltip 本地化。
* 错误提示本地化。
* 插件本地化接口。
* 缺失翻译提示。
* 语言包加载。

### 扩展需求

* 插件可注册语言表。
* 支持运行时切换语言。
* 支持不同语言下 UI 自适应布局。

---

## 12. 剪贴板 / 拖拽 / 右键菜单基础能力

**优先级：P0**

### 功能需求

* 复制。
* 粘贴。
* 剪切。
* 删除。
* 重命名。
* Duplicate。
* 复制路径。
* 复制资源 GUID。
* 复制组件。
* 粘贴组件。
* 拖拽资源到 Scene View。
* 拖拽资源到 Inspector Slot。
* 拖拽对象到 Hierarchy。
* 拖拽改变父子关系。
* 非法拖拽反馈。
* 拖拽预览。
* 右键上下文菜单。

### 扩展需求

* 插件可扩展右键菜单。
* 插件可注册拖拽目标。
* 插件可注册拖拽数据类型。

---

# 二、核心编辑工作流

---

## 13. Selection / 选择系统

**优先级：P0**

### 功能需求

* 当前选中对象。
* 当前选中资源。
* 多选。
* 框选。
* 反选。
* 锁定选择。
* 清空选择。
* 选择历史。
* 上一个选择。
* 下一个选择。
* 按类型过滤选择。

  * Mesh
  * Light
  * Camera
  * Collider
  * UI
  * Audio
  * Physics
* Focus Selected。
* Frame Selected。
* Selection Changed 事件。
* Hierarchy / Scene View / Inspector / Asset Browser 选择同步。

### 扩展需求

* 插件可监听选择变化。
* 插件可注册自定义选择类型。
* 支持对象选择和子元素选择。

  * 顶点
  * 边
  * 面
  * 骨骼
  * 节点

---

## 14. Hierarchy / 层级树

**优先级：P0**

对应你原始清单中的 `层级树`。

### 功能需求

* 场景对象树形显示。
* 父子层级显示。
* 对象展开 / 折叠。
* 对象搜索。
* 按类型过滤。
* 多选。
* 拖拽重排。
* 拖拽改变父子关系。
* 对象重命名。
* 对象复制。
* 对象删除。
* 对象 Duplicate。
* 对象激活 / 禁用。
* 对象可见性开关。
* 对象视图可点击性开关。
* 悬浮时在行首显示可见性和可点击性按钮。
* 物体类型图标。

  * Empty
  * Camera
  * Light
  * Mesh
  * UI
  * Audio
  * Particle
  * Physics
* Prefab 实例标识。
* Missing Script 标识。
* 锁定对象标识。
* 静态对象标识。
* 场景分组显示。
* 多 Scene 显示。
* DontDestroyOnLoad 分组，可选。
* 运行时对象与编辑时对象区分。
* 右键创建对象菜单。

### 扩展需求

* 插件可注册对象类型图标。
* 插件可注册 Hierarchy 行内按钮。
* 插件可扩展右键菜单。
* 插件可注册过滤器。
* 插件可注册自定义分组。

---

## 15. Inspector / 属性检查器

**优先级：P0**

对应你原始清单中的 `inspector`。

### 功能需求

* 根据选中的场景对象显示属性。
* 根据选中的资源显示属性。
* 显示对象基础信息。

  * 名称
  * ID / GUID
  * Tag
  * Layer
  * 激活状态
* 显示 Transform。

  * Position
  * Rotation
  * Scale
* 显示 Component 列表。
* Add Component。
* Remove Component。
* Enable / Disable Component。
* Component 排序。
* Component 折叠 / 展开。
* Copy / Paste Component。
* Reset Component。
* 多对象编辑。
* 属性搜索。
* 属性分组。
* 数组 / List 编辑。
* 引用对象选择。
* 资源引用选择。
* 数值滑条。
* Min / Max 限制。
* Readonly 属性。
* Dirty 标记。
* Override 标记。
* Prefab Apply / Revert。
* Debug Inspector 模式。
* Raw Data 模式。

### 扩展需求

* 自定义属性绘制器。
* 自定义组件编辑器。
* 自定义资源 Inspector。
* 插件可注册 Inspector Drawer。
* 支持反射式属性显示。
* 支持手写 UI 属性面板。
* 支持属性变更 Undo / Redo。
* 支持属性校验和错误提示。

---

## 16. Scene View

**优先级：P0**

对应你原始清单中的 `sence view`，建议统一命名为 `Scene View`。

### 功能需求

### 固定工具栏

* 网格吸附开关。
* 网格吸附设置。
* 2D / 3D 视图切换。
* 坐标模式切换。

  * Local
  * Global
* Pivot 模式切换。

  * Pivot
  * Center
* 视图模式切换。

  * 单视图
  * 四视图
  * 顶视图
  * 前视图
  * 侧视图
  * 用户自定义视图
* 渲染模式切换。

  * Lit
  * Unlit
  * Wireframe
  * Normal
  * Albedo
  * White Model
  * Depth
  * Overdraw
  * Lightmap
  * 自定义渲染模式
* Overlay / Gizmo 可见性下拉面板。

  * Camera
  * Light
  * Collider
  * Audio
  * Particle
  * Physics
  * Navigation
  * Reflection Probe
  * Custom Gizmo

### 悬浮工具栏

* 默认位于视图左上角。
* 主工具栏默认横向排列。
* 子工具栏默认竖向排列。
* 支持多个子工具栏。
* 子工具栏可合并。
* 子工具栏可停靠在上下左右。
* 工具栏可由用户扩展。
* 根据当前工具显示不同子工具栏。

  * 选择工具
  * 移动工具
  * 旋转工具
  * 缩放工具
  * 矩形工具
  * 地形工具
  * UI 工具
  * 骨骼工具

### 视图操作

* 旋转视图。
* 平移视图。
* 缩放视图。
* Focus Selected。
* Frame Selected。
* 正交 / 透视切换。
* 摄像机书签。
* 保存当前视角。
* 恢复视角。
* 视野 FOV 设置。
* Clipping Plane 设置。
* 隔离显示选中对象。
* 隐藏未选中对象。
* X-Ray 模式。
* Hover 高亮。
* 选中轮廓高亮。
* 场景搜索结果高亮。
* 拖拽资源创建对象。
* Scene View 截图。
* Scene View 录制，可选。

### Gizmo 和 Overlay

* 左下角视图方向 XYZ 轴 Gizmo。
* 坐标轴点击切换视角。
* 可选性能分析器 Overlay。

  * 帧延迟
  * FPS
  * DrawCall
  * Triangle
  * Vertex
  * 内存
  * 自定义性能指标
* 悬浮预览视图。

  * 选中相机预览
  * 主相机预览
  * Light 预览，可选
  * Probe 预览，可选
* 命令选择器入口。

### 多视图问题

* 支持全局唯一工具栏模式。
* 支持每个 Scene View 独立工具栏模式。
* 支持用户配置。
* 支持每个 Scene View 独立摄像机状态。
* 支持每个 Scene View 独立渲染模式。
* 支持每个 Scene View 独立 Gizmo 可见性。

### 扩展需求

* 插件可注册 Scene View Overlay。
* 插件可注册渲染模式。
* 插件可注册 Gizmo。
* 插件可注册工具栏按钮。
* 插件可注册自定义编辑工具。
* 插件可注册 Scene View 快捷键。

---

## 17. Game View

**优先级：P0**

对应你原始清单中的 `game view`。

### 功能需求

* 游戏画面显示。
* Play Mode 输出。
* 相机切换。
* 多 Display 切换。
* 缩放。

  * Fit
  * 1x
  * 2x
  * 自定义缩放
* 分辨率选择。
* 自定义分辨率。
* 宽高比选择。
* 设备模拟。
* Safe Area 显示。
* DPI 模拟。
* HDR / SDR 显示切换。
* VSync / Target FPS 显示。
* Stats 面板。
* Gizmo 图标可见性下拉面板。
* 输入捕获状态提示。
* 鼠标锁定状态提示。
* 截图。
* 录屏，可选。

### 扩展需求

* 插件可注册 Game View Overlay。
* 插件可注册分辨率预设。
* 插件可注册设备模拟配置。
* 插件可注册自定义统计项。

---

## 18. Transform 工具 / Gizmo 操作

**优先级：P0**

### 功能需求

* 选择工具。
* 移动工具。
* 旋转工具。
* 缩放工具。
* Rect 工具。
* Pivot / Center 切换。
* Local / Global 切换。
* 位置吸附。
* 旋转吸附。
* 缩放吸附。
* 网格吸附。
* 顶点吸附。
* 表面吸附。
* Gizmo 轴向锁定。
* Gizmo 尺寸设置。
* 数值输入变换。
* 对齐到地面。
* 对齐到视图。
* 对齐到对象。
* 对齐轴向。
* 分布排列。
* 批量变换。
* 多选中心计算。

### 扩展需求

* 插件可注册自定义 Gizmo。
* 插件可注册自定义编辑工具。
* 支持 Undo / Redo。
* 支持多对象编辑。

---

## 19. Preview View / 预览视图

**优先级：P0**

对应你原始清单中的 `预览视图`。

### 3D 预览视图

* 模型预览。
* 材质预览。
* Shader 预览。
* Prefab 预览。
* 粒子预览，可选。
* 可旋转。
* 可拖拽平移。
* 可缩放。
* 可切换灯光环境。
* 可切换背景。
* 可切换网格地面。
* 可重置视角。
* 可播放动画，可选。
* 可显示包围盒。
* 可显示法线 / 切线，可选。
* 可截图。

### 2D 预览视图

* 图片预览。
* Texture 预览。
* Render Target 预览。
* Sprite 预览。
* 深度图预览。
* RGBA 通道切换。
* R / G / B / A 单通道显示。
* Alpha 棋盘格背景。
* 曝光调整。
* Gamma / Linear 切换。
* MipMap 级别切换。
* 缩放。
* 拖拽平移。
* 像素信息拾取。
* 可截图。

### 扩展需求

* 插件可注册资源预览器。
* 插件可注册预览工具栏按钮。
* 插件可注册自定义预览渲染逻辑。
* 预览视图可嵌入：

  * Inspector
  * Asset Browser
  * Frame Debugger
  * 独立窗口
  * Scene View Overlay

---

## 20. Play Mode / 运行模式控制

**优先级：P0**

### 功能需求

* Enter Play Mode。
* Exit Play Mode。
* Pause。
* Step Frame。
* Step Physics。
* Reload Domain 选项。
* Reload Scene 选项。
* Play From Selected Scene。
* Play From Current Camera。
* Play From Spawn Point。
* Runtime Inspector。
* Runtime Hierarchy。
* 编辑模式与运行模式数据隔离提示。
* 退出 Play Mode 时还原场景。
* Play Mode 状态清晰提示。
* 运行时对象与编辑时对象区分。

### 扩展需求

* 插件可监听 Play Mode 状态变化。
* 插件可注册 Play Mode 初始化逻辑。
* 支持运行时调试工具扩展。

---

## 21. Undo / Redo / History 系统

**优先级：P0**

对应你原始清单中顶部工具栏的撤回 / 恢复，建议独立成系统。

### 功能需求

* Undo。
* Redo。
* 全局 Undo Stack。
* 当前可 Undo 操作名称显示。
* 当前可 Redo 操作名称显示。
* 操作合并。

  * 拖动物体合并成一次操作。
  * 连续文本输入合并。
* 操作分组。
* 按场景 / 资源隔离 Undo。
* Dirty 状态管理。

  * 场景未保存。
  * 资源未保存。
* History 面板。

  * 操作列表。
  * 操作名称。
  * 操作对象。
  * 操作时间。
  * 跳转到某一步，可选。
* 清空历史。
* 保存后压缩历史，可选。

### 扩展需求

* 插件可注册 Undo Command。
* 支持复杂对象回滚。

  * Transform
  * Component
  * Asset
  * Node Graph
  * Timeline
  * Prefab
* 支持事务式操作。
* 支持失败回滚。

---

## 22. Prefab / 模板对象系统 UI

**优先级：P1**

### 功能需求

* 创建 Prefab。
* 从对象生成 Prefab。
* 拖拽 Prefab 到 Scene View。
* Prefab 实例标识。
* 打开 Prefab 编辑模式。
* Prefab 层级视图。
* Prefab Override 显示。
* Apply 修改。
* Revert 修改。
* 嵌套 Prefab。
* Prefab Variant。
* Prefab 断开连接。
* Missing Prefab 提示。
* Prefab 引用跳转。
* Prefab 修改对比。

### 扩展需求

* 插件可参与 Prefab 序列化。
* 插件可注册 Prefab 校验规则。
* Inspector 中支持 Prefab Override 标记。

---

# 三、资源工作流

---

## 23. Asset Browser / 资源浏览器

**优先级：P0**

对应你原始清单中的 `资源浏览器`。

### 功能需求

* 文件层级视图。
* 文件夹树。
* 文件网格视图。
* 文件列表视图。
* List / Grid 切换。
* 文件预览。

  * 模型预览图。
  * 材质球预览。
  * 图片预览。
  * 音频图标 / 波形。
  * Shader 图标。
  * 场景图标。
  * Prefab 预览。
* 缩略图失败 fallback 图标。
* 网格大小调整。
* 搜索过滤。
* 按类型过滤。
* 按路径过滤。
* 资源排序。

  * 名称
  * 类型
  * 修改时间
  * 大小
* 收藏目录。
* 最近使用资源。
* 标签系统。
* 资源重命名。
* 资源移动。
* 资源复制。
* 资源删除。
* 资源 Duplicate。
* 创建资源。
* 创建文件夹。
* 显示资源 GUID。
* 复制资源路径。
* 复制资源 GUID。
* Show in Explorer / Finder。
* 批量操作。
* 资源导入状态图标。
* 资源错误状态图标。
* Meta 文件状态提示。
* 缩略图缓存。
* 缩略图异步生成。

### 扩展需求

* 插件可注册资源类型。
* 插件可注册资源图标。
* 插件可注册资源预览器。
* 插件可扩展右键菜单。
* 插件可注册搜索过滤器。
* 插件可注册资源创建菜单。

---

## 24. Asset Importer / 资源导入设置

**优先级：P0**

### 功能需求

* Texture Import Settings。
* Model Import Settings。
* Audio Import Settings。
* Shader Import Settings。
* Animation Import Settings。
* Font Import Settings。
* Scene Import Settings。
* Reimport 单个资源。
* Reimport 批量资源。
* Force Reimport。
* Import Preset。
* 保存导入预设。
* 应用导入预设。
* 根据路径自动套用导入规则。
* Import Report。

  * 导入耗时。
  * 生成的子资源。
  * Warning。
  * Error。
* 缺失资源提示。

  * Missing Texture
  * Missing Material
  * Missing Script
  * Missing Mesh
* 导入失败 fallback。
* 导入进度显示。
* 导入任务接入状态栏。

### 扩展需求

* 插件可注册 Importer。
* 插件可注册 Import Preset。
* 插件可注册导入后处理器。
* 插件可注册导入校验规则。

---

## 25. Asset Preview / 资源预览系统

**优先级：P0**

### 功能需求

* Texture 预览。
* Model 预览。
* Material 预览。
* Shader 预览。
* Audio 预览。
* Animation Clip 预览。
* Scene 预览，可选。
* Prefab 预览。
* 预览图异步生成。
* 预览图缓存。
* 预览失败 fallback 图标。
* 预览图刷新。
* 预览图批量生成。
* 预览图尺寸配置。

### 扩展需求

* 插件可注册资源预览生成器。
* 插件可注册缩略图生成器。
* 插件可注册双击打开行为。

---

## 26. Resource Picker / Object Picker

**优先级：P0**

### 功能需求

* 在 Inspector 中选择资源。
* 在 Inspector 中选择场景对象。
* 搜索资源。
* 搜索对象。
* 类型过滤。
* 最近使用。
* 收藏。
* 拖拽赋值。
* 清空引用。
* 定位引用对象。
* 打开引用资源。
* Missing Reference 显示。

### 扩展需求

* 插件可注册可选对象类型。
* 插件可注册过滤逻辑。
* 插件可自定义选择弹窗 UI。

---

## 27. 资源依赖 / 引用关系查看器

**优先级：P1**

### 功能需求

* 查看资源引用了谁。
* 查看资源被谁引用。
* 资源引用树。
* 资源依赖图。
* 循环依赖检测。
* 未使用资源检测。
* Missing Reference 检测。
* 删除资源前提示影响范围。
* 移动资源时自动更新引用。
* 重命名资源时自动更新引用。
* 一键定位引用方。
* 资源依赖导出。
* 资源依赖缓存刷新。

### 扩展需求

* 插件可注册资源依赖解析器。
* 插件可注册资源引用关系。
* 插件可注册资源清理规则。

---

## 28. 全局搜索 / 搜索过滤器

**优先级：P0**

对应你原始清单中的 `搜索过滤器`，建议拆成局部过滤和全局搜索两层。

### 局部搜索过滤

* Hierarchy 搜索。
* Asset Browser 搜索。
* Inspector 属性搜索。
* Console 日志搜索。
* Profiler Marker 搜索。
* Frame Debugger Pass 搜索。
* Node Editor 节点搜索。

### 全局搜索

* 搜资源。
* 搜场景对象。
* 搜组件。
* 搜菜单命令。
* 搜设置项。
* 搜日志。
* 搜 Prefab。
* 搜 Shader。
* 搜材质。
* 搜引用关系。
* 搜 Missing Reference。

### 搜索语法

* `type:Texture`
* `type:Mesh`
* `name:Player`
* `path:Assets/Characters`
* `ref:xxx`
* `missing:true`
* `tag:Enemy`
* `layer:UI`
* `component:Camera`
* `modified:true`

### 扩展需求

* 插件可注册 Search Provider。
* 插件可注册搜索语法。
* 搜索结果可分组。
* 搜索结果可执行动作。

  * 打开
  * 定位
  * 删除
  * 重命名
  * 查看引用
* 支持搜索历史。
* 支持收藏搜索条件。

---

## 29. Project Settings / Editor Preferences

**优先级：P0**

### Project Settings

跟工程绑定，通常进入版本库。

功能需求：

* 渲染设置。
* 物理设置。
* 输入设置。
* 音频设置。
* 质量等级设置。
* Tag 设置。
* Layer 设置。
* Sorting Layer 设置。
* 包管理设置。
* 平台设置。
* 构建设置。
* Shader Variant 设置。
* Asset Import 默认规则。
* 序列化格式设置。
* 资源路径规则设置。
* 场景加载设置。
* Player 设置。
* 网络设置，可选。

### Editor Preferences

跟用户本地环境绑定，通常不进入版本库。

功能需求：

* 主题。
* 字体大小。
* UI 缩放。
* 快捷键。
* 鼠标操作习惯。
* Scene View 操作习惯。
* 自动保存。
* 缓存目录。
* 外部工具路径。
* 代码编辑器路径。
* Git / SVN / Perforce 客户端路径。
* 日志显示偏好。
* Inspector 显示偏好。
* 资源浏览器显示偏好。

### 扩展需求

* 插件可注册 Project Settings 页面。
* 插件可注册 Editor Preferences 页面。
* 设置项支持搜索。
* 设置项支持重置默认值。
* 设置项支持导入 / 导出。

---

## 30. Package Manager / 插件管理器

**优先级：P1**

### 功能需求

* 已安装插件列表。
* 可用插件列表。
* 插件搜索。
* 插件分类。
* 插件详情页。
* 安装插件。
* 卸载插件。
* 更新插件。
* 启用插件。
* 禁用插件。
* 插件依赖关系。
* 插件版本冲突提示。
* 插件错误日志入口。
* 本地插件导入。
* Git URL 插件导入。
* 插件设置入口。
* 插件文档入口。

### 扩展需求

* 支持官方源。
* 支持本地源。
* 支持私有源。
* 支持插件权限声明。
* 支持插件启用后重启提示。

---

# 四、调试 / 渲染 / 性能分析

---

## 31. Console / Debug Log

**优先级：P0**

对应你原始清单中的 `debug log/Console`。

### 功能需求

* 日志列表。
* 日志主信息预览。
* 打印者显示。
* 时间显示。
* 日志等级显示。

  * Info
  * Warning
  * Error
  * Fatal
* 消息颜色。
* 消息图标。
* 消息数量统计。
* Collapse 相同日志。
* 搜索日志。
* 过滤日志。
* Clear。
* Clear on Play。
* Error Pause。
* 点击日志显示详细信息。
* 详细信息面板。

  * Stack Trace
  * 调用堆栈
  * 上下文对象
  * 源文件
  * 行号
* 点击跳转源码。
* 点击定位对象。
* 日志复制。
* 日志导出。
* Stack Trace 级别设置。
* 日志来源过滤。

  * Engine
  * Editor
  * Game
  * Plugin
  * Render
  * Physics
  * Audio
* 分组 API。
* 文本颜色 API。
* 自定义跳转 API。
* 远程日志接入，可选。

### 扩展需求

* 插件可注册日志分类。
* 插件可注册日志跳转处理器。
* 插件可注册日志格式化器。
* 插件可注册自定义日志详情面板。

---

## 32. Profiler / 性能分析器（CPU / GPU / Memory）

**优先级：P1**

对应你原始清单中的 `性能分析器`、`Memory Profiler`、`GPU Profiler`。

这三类信息应该放在同一个 Profiler 面板里，而不是拆成三个互相独立的窗口。核心是同一条采样时间轴：延迟、CPU、GPU、FPS、内存曲线都能在同一张折线图里叠加、对齐和对比。

### 功能需求

* 统一性能采样时间轴。
* 主折线图同时支持延迟和内存信息。

  * Frame Latency / Frame Time。
  * CPU Frame Time。
  * GPU Frame Time。
  * FPS。
  * 总内存。
  * CPU 内存。
  * GPU 内存。
  * Texture 内存。
  * Mesh 内存。
  * Audio 内存。
  * Animation 内存。
  * Managed 内存。
  * Native 内存。
  * 脚本对象内存。
* 曲线可单独开关、叠加、分组和固定颜色。
* 支持毫秒、FPS、MB / GB 多单位坐标轴。
* 支持时间轴缩放、平移、框选时间范围。
* 支持性能尖峰标记。
* 支持预算线 / 阈值线。
* 选中某一帧或采样点后，CPU / GPU / Memory 详情同步跳转到同一时间点。
* CPU Frame Time。
* GPU Frame Time。
* FPS。
* Main Thread。
* Render Thread。
* Job Thread。
* Physics。
* Animation。
* Script。
* Rendering。
* Asset Loading。
* GC。
* Render Pass 耗时。
* DrawCall 耗时。
* Compute Dispatch 耗时。
* Texture Bandwidth。
* Render Target 使用情况。
* GPU Marker 层级树。
* GPU 事件时间线。
* Pipeline State 查看。
* Shader 查看。
* Render Target 预览。
* GPU Capture 入口，可选。
* 内存快照。
* 快照对比。
* 泄漏检测。
* 引用链查看。
* 大对象排序。
* 未释放资源检测。
* Timeline 视图。
* Hierarchy 视图。
* Marker 搜索。
* Marker 过滤。
* 采样开关。
* 暂停采样。
* 选帧查看。
* 性能数据导出。
* 性能数据导入。
* 内存数据导出。
* 截帧对比。
* 远程设备 Profiler，可选。

### 扩展需求

* 插件可注册 Profiler Module。
* 插件可注册性能 Marker。
* 插件可注册图表。
* 插件可注册详情面板。
* 插件可注册内存分类。
* 插件可注册对象引用解析器。
* 插件可注册快照分析器。
* 插件可注册 GPU Marker。
* 插件可注册渲染阶段分类。
* 插件可和 Frame Debugger 联动。

---

## 33. Frame Debugger / 帧调试器

**优先级：P1**

对应你原始清单中的 `帧调试器`。

### 功能需求

* 捕获当前帧。
* 帧事件列表。
* Pass / DrawCall 层级树。
* Render Pass 分组。
* DrawCall 分组。
* Compute Dispatch 显示。
* 选中 Pass / DrawCall 后显示信息视图。
* 支持前后逐步查看。
* 支持启用 / 禁用某个 Pass 预览。
* 支持跳转到对应资源。
* 支持跳转到对应 Shader。
* 支持跳转到对应 Mesh。
* 支持和 Scene View 联动。

### 信息视图

* 2D 预览图。
* Color Attachment 预览。
* Depth Attachment 预览。
* Stencil 预览，可选。
* MipMap 预览。
* RGBA 通道切换。
* 图像信息。

  * 宽度
  * 高度
  * 格式
  * Mip 数
  * MSAA
* Mesh 预览图。
* Mesh 信息。

  * Vertex Count
  * Index Count
  * SubMesh
* Shader 信息。
* Material 信息。
* Pipeline State。
* Blend State。
* Depth State。
* Raster State。
* Resource Binding。
* 资源依赖。

  * Texture
  * Buffer
  * Depth
  * Render Target
* 可拆分为两个面板。

  * 左侧选择 Pass / DrawCall。
  * 右侧显示详细信息。

### 扩展需求

* 插件可注册 Frame Event 类型。
* 插件可注册详情面板。
* 插件可注册附件预览器。
* 插件可接入自定义渲染管线。

---

## 34. Render Graph Viewer

**优先级：P1**

适合现代渲染管线，建议从 Frame Debugger 中拆出来。

### 功能需求

* Render Graph 节点视图。
* Pass 节点。
* Resource 节点。
* Pass 依赖关系。
* Texture / Buffer 读写关系。
* Resource 生命周期。
* Alias 可视化。
* Barrier 可视化。
* Pass 裁剪状态。
* Pass 执行顺序。
* Resource 创建 / 释放时机。
* 导出 Render Graph。
* 点击节点联动 Frame Debugger。
* 点击资源查看预览。
* 搜索 Pass。
* 搜索 Resource。
* 按类型过滤节点。

### 扩展需求

* 插件可注册 Render Graph 节点类型。
* 插件可注册资源类型。
* 插件可注册节点详情面板。

---

## 35. Diagnostics Center / 诊断中心

**优先级：P1**

Console 负责日志，Diagnostics Center 负责工程问题聚合。

### 功能需求

* Missing Script。
* Missing Material。
* Missing Texture。
* Missing Mesh。
* Shader Compile Error。
* Asset Import Error。
* Duplicate GUID。
* Invalid Reference。
* Circular Dependency。
* Deprecated API。
* Build Error。
* Performance Warning。
* 资源路径非法。
* 命名规则错误。
* 一键修复。
* 定位问题对象。
* 定位问题资源。
* 按严重程度排序。
* 按模块过滤。
* 诊断报告导出。

### 扩展需求

* 插件可注册诊断规则。
* 插件可注册自动修复逻辑。
* 插件可注册问题详情面板。

---

## 36. Physics Debugger / 物理调试器

**优先级：P1**

### 功能需求

* Collider 可视化。
* Rigidbody 可视化。
* Joint 可视化。
* Trigger 区域可视化。
* Contact Point 显示。
* 速度向量显示。
* 力向量显示。
* 射线检测调试。
* Physics Layer Collision Matrix。
* 碰撞事件日志。
* 物理步进。
* 暂停物理模拟。
* 物理 Profiler。
* Scene View Overlay 开关。

### 扩展需求

* 插件可注册物理可视化类型。
* 插件可注册物理调试面板。
* 支持自定义物理后端。

---

## 37. Navigation / AI Debugger

**优先级：P2**

### 功能需求

* NavMesh 烘焙设置。
* NavMesh 预览。
* Agent 半径 / 高度 / 坡度设置。
* 路径查询可视化。
* 障碍物显示。
* 动态 NavMesh 更新状态。
* AI 感知范围可视化。
* 行为树编辑器，可选。
* 黑板变量查看器，可选。
* AI 状态查看。
* AI 路径调试。

### 扩展需求

* 插件可注册 AI 调试 Overlay。
* 插件可注册行为树节点。
* 插件可注册寻路系统后端。

---

# 五、内容生产工具

---

## 38. Node Editor / 节点编辑器框架

**优先级：P1**

对应你原始清单中的 `节点编辑器`。

### 功能需求

* 节点画布。
* 节点创建。
* 节点删除。
* 节点复制。
* 节点粘贴。
* 节点拖拽。
* 节点连线。
* 断开连线。
* 框选节点。
* 节点分组。
* 节点注释。
* 节点搜索创建菜单。
* 右键菜单。
* 小地图。
* 缩放。
* 平移。
* 自动布局。
* 节点错误提示。
* 节点编译状态。
* 节点预览。
* Graph 保存。
* Graph Dirty 状态。
* Undo / Redo。
* Copy / Paste 跨图。
* 节点参数 Inspector。

### 可承载图类型

* Shader Graph。
* Material Graph。
* Animation State Machine。
* Behavior Tree。
* Visual Scripting。
* Render Graph。
* Dialogue Graph。
* VFX Graph，可选。

### 扩展需求

* 插件可注册节点类型。
* 插件可注册 Pin 类型。
* 插件可注册连线规则。
* 插件可注册 Graph 类型。
* 插件可注册节点编译器。
* 插件可注册节点预览器。

---

## 39. Material / Shader 编辑器

**优先级：P1**

### 功能需求

* 材质 Inspector。
* Shader 选择。
* 材质参数编辑。
* Texture Slot。
* Color 参数。
* Float / Vector 参数。
* Render State。

  * Blend Mode
  * Cull Mode
  * Depth Test
  * Depth Write
  * Stencil
* Render Queue。
* 材质预览球。
* 材质预览模型切换。
* Shader Graph。
* Shader 编译状态。
* Shader 编译错误面板。
* Shader Variant 查看。
* Shader Keyword 管理。
* Pass 列表查看。
* Shader Include 跳转。
* Shader Property 搜索。

### 扩展需求

* 插件可注册材质面板。
* 插件可注册 Shader 参数绘制器。
* 插件可注册 Shader Graph 节点。
* 插件可注册预览模型。

---

## 40. Animation / Timeline 编辑器

**优先级：P1**

### 功能需求

* Animation Clip 浏览。
* Animation Timeline。
* 关键帧编辑。
* 曲线编辑器。
* Dope Sheet。
* 播放 / 暂停。
* 单帧步进。
* Loop / Ping-pong 预览。
* 骨骼层级视图。
* 骨骼预览。
* Avatar / Skeleton 映射。
* Animation Event。
* 动画状态机。
* Blend Tree。
* Timeline / Sequencer。
* Camera Track。
* Audio Track。
* Animation Track。
* Event Track。
* Transform Track。
* Signal Track。
* Clip 裁剪。
* Clip 混合。
* Timeline 缩放和平移。
* 当前时间轴指针。
* 时间码显示。

### 扩展需求

* 插件可注册 Track 类型。
* 插件可注册 Clip 类型。
* 插件可注册动画曲线类型。
* 插件可注册事件类型。

---

## 41. UI / Canvas 编辑器

**优先级：P1**

### 功能需求

* Canvas 层级树。
* Rect Transform 编辑。
* Anchor 可视化。
* Pivot 可视化。
* UI 对齐工具。
* UI 分布工具。
* UI 自动布局。
* 图片控件预览。
* 文本控件预览。
* 按钮控件预览。
* 滚动视图预览。
* 多分辨率预览。
* Safe Area 预览。
* DPI / 缩放模拟。
* UI 动画预览。
* UI 事件调试。
* UI Raycast 调试。
* UI DrawCall 统计。

### 扩展需求

* 插件可注册 UI 控件。
* 插件可注册 UI 属性绘制器。
* 插件可注册布局组件。
* 插件可注册 UI 调试 Overlay。

---

## 42. Terrain / 地形编辑器

**优先级：P2**

### 功能需求

* 地形创建。
* 高度刷。
* 平滑刷。
* 平整刷。
* 贴图刷。
* 植被刷。
* 树木刷。
* 笔刷大小。
* 笔刷强度。
* 笔刷 Falloff。
* 笔刷预设。
* 地形 Layer 管理。
* 地形贴图管理。
* 地形 LOD 预览。
* 地形切块。
* 地形碰撞生成。
* 地形导入 / 导出。
* 高度图导入 / 导出。

### 扩展需求

* 插件可注册地形笔刷。
* 插件可注册地形 Layer 类型。
* 插件可注册地形生成器。

---

## 43. Lighting / 烘焙 / GI 面板

**优先级：P1**

### 功能需求

* Light Explorer。
* 场景中所有灯光列表。
* 灯光类型显示。
* 强度显示。
* 阴影状态显示。
* 环境光设置。
* 天空盒设置。
* 反射探针设置。
* Light Probe 设置。
* Lightmap 分辨率。
* GI 设置。
* Lightmap 预览。
* 烘焙控制。

  * 开始
  * 暂停
  * 取消
  * 清除缓存
* 烘焙进度。
* 烘焙结果查看。

  * Lightmap 图集
  * UV 占用
  * Texel Density
* 烘焙问题诊断。

  * UV 重叠
  * 光照泄漏
  * 采样不足
* 烘焙任务接入状态栏。

### 扩展需求

* 插件可注册光照后端。
* 插件可注册烘焙器。
* 插件可注册光照诊断规则。

---

## 44. Audio 编辑 / 预览面板

**优先级：P1**

### 功能需求

* Audio Clip 预览。
* 波形显示。
* 播放。
* 暂停。
* 停止。
* Loop 开关。
* 音量调节。
* 采样率显示。
* 声道信息显示。
* 压缩格式信息显示。
* Audio Mixer。
* Bus / Group 管理。
* 实时音量 Meter。
* Spatial Audio 预览，可选。
* 音频导入设置入口。

### 扩展需求

* 插件可注册音频效果器 UI。
* 插件可注册音频分析工具。
* 插件可注册自定义音频预览器。

---

## 45. 数据表 / 配置表编辑器

**优先级：P1**

### 功能需求

* 表格视图。
* 单元格编辑。
* 类型校验。
* 枚举下拉。
* 资源引用选择。
* 对象引用选择。
* 批量编辑。
* 搜索过滤。
* 排序。
* 分页，可选。
* 导入 CSV。
* 导入 JSON。
* 导入 Excel，可选。
* 导出 CSV。
* 导出 JSON。
* 数据校验。
* 错误定位。
* 修改历史。
* Undo / Redo。

### 扩展需求

* 插件可注册数据类型。
* 插件可注册字段校验器。
* 插件可注册导入 / 导出格式。

---

## 46. Test Runner / 自动化测试面板

**优先级：P2**

### 功能需求

* Unit Test 列表。
* Integration Test 列表。
* Play Mode Test。
* Editor Test。
* 运行全部测试。
* 运行选中测试。
* 测试结果显示。
* 失败日志。
* 失败堆栈。
* 测试耗时。
* 测试过滤。
* 覆盖率报告，可选。
* 自动化录制，可选。
* CI 结果查看，可选。

### 扩展需求

* 插件可注册测试类型。
* 插件可注册测试结果面板。
* 插件可注册自动化任务。

---

# 六、工程化 / 构建 / 协作

---

## 47. Build Settings / 构建发布窗口

**优先级：P0**

### 功能需求

* 平台选择。

  * Windows
  * macOS
  * Linux
  * Android
  * iOS
  * Web
  * Console，可选
* 场景列表。

  * 添加场景
  * 删除场景
  * 排序
  * 是否参与构建
* 构建配置。

  * Debug
  * Development
  * Release
* 构建选项。

  * 是否开启 Profiler
  * 是否压缩资源
  * 是否启用符号文件
  * 是否增量构建
  * 是否打包资源
  * 是否生成日志
* 构建进度。
* 构建日志。
* 构建错误显示。
* 构建警告显示。
* 构建结果摘要。

  * 体积
  * 耗时
  * 资源数量
  * 错误数量
  * 警告数量
* 构建后操作。

  * 打开目录
  * 运行程序
  * 上传平台
* 构建任务接入状态栏。

### 扩展需求

* 插件可注册构建平台。
* 插件可注册构建步骤。
* 插件可注册构建后处理。
* 插件可注册平台设置页面。

---

## 48. Version Control / 版本控制面板

**优先级：P1**

对应你原始清单中顶部工具栏的版本控制。

### 功能需求

* 文件状态显示。

  * Modified
  * Added
  * Deleted
  * Renamed
  * Conflicted
  * Ignored
* 提交面板。
* Commit Message。
* Pull。
* Push。
* Fetch。
* Branch 切换。
* Branch 创建。
* Commit History。
* Diff 视图。

  * 文本 Diff
  * 资源 Diff
  * 场景 Diff，可选
  * Prefab Diff，可选
* Conflict Resolver。
* Lock / Unlock 文件。
* 文件占用状态。
* 忽略文件配置。
* 当前分支状态栏显示。

### 扩展需求

* 支持 Git。
* 支持 SVN。
* 支持 Perforce。
* 支持自定义版本控制后端。
* 插件可注册 Diff 查看器。
* 插件可注册冲突解决器。

---

## 49. Collaboration / 多人协作

**优先级：P2**

### 功能需求

* 当前文件被谁占用。
* 场景对象锁定。
* 资源锁定。
* 在线用户列表。
* 协作修改提示。
* 冲突提示。
* 评论 / 标注，可选。
* Review 面板，可选。
* 实时协同状态，可选。
* 场景对象修改历史，可选。

### 扩展需求

* 可接入版本控制锁文件系统。
* 可接入云端协作服务。
* 插件可注册协作状态 Overlay。

---

## 50. Background Tasks / 后台任务窗口

**优先级：P0**

### 功能需求

* 显示所有后台任务。
* 显示任务名称。
* 显示任务状态。

  * Waiting
  * Running
  * Paused
  * Completed
  * Failed
  * Canceled
* 显示总进度。
* 显示子任务进度。
* 显示任务日志。
* 任务取消。
* 任务暂停，可选。
* 任务重试。
* 任务完成通知。
* 任务失败通知。
* 从状态栏打开。

### 常见任务

* Shader 编译。
* Asset Import。
* Thumbnail 生成。
* Lightmap 烘焙。
* Occlusion Culling。
* Build。
* Package Download。
* Source Control Sync。

### 扩展需求

* 插件可注册后台任务。
* 插件可注册任务详情 UI。
* 插件可注册任务取消逻辑。

---

## 51. Crash Recovery / 自动保存与崩溃恢复

**优先级：P1**

### 功能需求

* 自动保存。
* 自动保存间隔设置。
* 崩溃恢复提示。
* 可恢复场景列表。
* 可恢复资源列表。
* 恢复前差异预览。
* 恢复。
* 放弃恢复。
* 手动恢复文件位置。
* 崩溃报告窗口。
* 崩溃日志查看。
* 上传诊断信息，可选。

### 扩展需求

* 插件可参与恢复流程。
* 插件可注册自定义资源恢复逻辑。
* 支持恢复前备份。

---

## 52. Documentation / Help / 新手引导

**优先级：P1**

### 功能需求

* Help 菜单。
* 文档入口。
* API 文档入口。
* 快捷键说明。
* Tooltip 文档。
* 组件文档链接。
* Shader 文档链接。
* 示例工程入口。
* 新手引导。
* What's New。
* About 窗口。
* 插件帮助入口。
* 错误码帮助跳转。

### 扩展需求

* 插件可注册帮助文档入口。
* 插件可注册新手引导步骤。
* 插件可注册错误码说明。

---

# 七、建议的最终模块清单

如果只看顶层模块，完整编辑器 UI 可以整理成下面这份目录：

1. 基础 UI 控件库
2. Window / Docking 系统
3. 主菜单系统
4. Command Registry
5. Command Palette / Marking Menu
6. 顶部工具栏
7. 底部状态栏
8. 快捷键系统
9. Dialog / Toast / Progress 系统
10. Theme / DPI / Accessibility
11. Localization
12. Clipboard / Drag & Drop / Context Menu
13. Selection System
14. Hierarchy
15. Inspector
16. Scene View
17. Game View
18. Transform Gizmo
19. Preview View
20. Play Mode Controls
21. Undo / Redo / History
22. Prefab UI
23. Asset Browser
24. Asset Importer
25. Asset Preview
26. Resource / Object Picker
27. Resource Dependency Viewer
28. Global Search
29. Project Settings / Editor Preferences
30. Package Manager
31. Console
32. Profiler / CPU / GPU / Memory
33. Frame Debugger
34. Render Graph Viewer
35. Diagnostics Center
36. Physics Debugger
37. Navigation / AI Debugger
38. Node Editor
39. Material / Shader Editor
40. Animation / Timeline Editor
41. UI / Canvas Editor
42. Terrain Editor
43. Lighting / GI Panel
44. Audio Panel
45. Data Table Editor
46. Test Runner
47. Build Settings
48. Version Control
49. Collaboration
50. Background Tasks
51. Crash Recovery
52. Documentation / Help

---

# 八、第一阶段 P0 建议范围

如果要先做一个可用 MVP 编辑器，建议 P0 优先做这些：

1. 基础 UI 控件库
2. Docking / Window Layout
3. 主菜单系统
4. Command Registry
5. 顶部工具栏
6. 底部状态栏
7. 快捷键系统
8. Dialog / Toast / Progress
9. Selection System
10. Hierarchy
11. Inspector
12. Scene View
13. Game View
14. Transform Gizmo
15. Preview View
16. Play Mode Controls
17. Undo / Redo
18. Asset Browser
19. Asset Importer
20. Asset Preview
21. Resource / Object Picker
22. Global Search 基础版
23. Project Settings / Editor Preferences
24. Console
25. Build Settings
26. Background Tasks

这 26 项是编辑器的“地基”。后续的 Profiler、Frame Debugger、Shader Graph、Animation、Terrain、Version Control 都可以在这套地基上继续扩展。
