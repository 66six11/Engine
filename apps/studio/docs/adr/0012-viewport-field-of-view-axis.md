# ADR-0012：Viewport 显式保持水平或垂直 FOV

状态：Accepted / Implemented
日期：2026-08-12

历史状态说明（2026-09-03）：本 ADR 保留当时 V6→V7 的 FOV 设计与验证记录；production stream
已先由 #409 的 typed Translate Gizmo packet 硬切至 V8，再由 #411 的 discriminated Transform Gizmo packet 硬切至 V9，
最后由 #413 的 local-axis Scale packet rotation 硬切至 V10；
本文决定的 projection/FOV 语义未改变。

## 背景

旧 camera contract 只携 60° `VerticalFovRadians`。Scene View 宽度不变、只增加 dock 高度时，固定垂直 FOV 会扩大物体的
屏幕像素尺寸并收窄水平可见范围；用户因此感到视角被拉近。camera position/target 实际没有改变，exact surface 也没有
stretch/crop，但投影轴策略与自由编辑视图的构图预期不一致。

这个选择是 viewport-local editor state，不是 Scene 内容。把 resize 转换成 camera dolly、auto-focus 或动画 FOV 会把布局变化
写成隐式 camera mutation，也会在 resize release 时产生第二次构图跳变。

## 外部依据

- Unreal 公开 `EAspectRatioAxisConstraint` 的 `MaintainXFOV` / `MaintainYFOV`，并在 Editor viewport preference 中暴露
  aspect ratio axis constraint。Asharia 采用“显式选择保持轴”的模式，不采用其名称、模块或 `MajorAxisFOV` 默认策略。
- Godot `Camera3D.KeepAspect` 公开 `KEEP_WIDTH` / `KEEP_HEIGHT`，同样证明保持水平或垂直视野应由 camera projection
  contract 表达，而不是由 resize handler 移动相机。
- Unity `Camera.fieldOfView` 明确表示垂直视野。Asharia 因此让 Game/Preview 保持垂直 FOV，避免自由 Scene View 的编辑偏好
  改变 authored/runtime camera 的常见语义。

资料：

- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/EAspectRatioAxisConstraint
- https://dev.epicgames.com/documentation/unreal-engine/unreal-editor-preferences?lang=en-US
- https://docs.godotengine.org/en/stable/classes/class_camera3d.html
- https://docs.unity3d.com/cn/6000.0/ScriptReference/Camera-fieldOfView.html

## 决策

`ViewportCameraSnapshot` 使用 `FieldOfViewRadians` 和 `ViewportFieldOfViewAxis`：

- Scene 默认 90° `MaintainHorizontal`；固定宽度改变高度时，像素尺度不变，只增减上下可见内容。
- Game 与 Preview 默认 60° `MaintainVertical`；viewport resize 只重算 aspect-dependent projection。
- policy 随 immutable viewport request 进入 native V7 stream；native projection、picking、overlay 与 diagnostics 必须消费同一矩阵。
- policy 属于每个 `ViewportSession`，不写入 `SceneDocument`，不同 endpoint 不共享可变 camera state。

这是 V6→V7 硬切。V1–V6 stream exports、managed ABI 类型与 fallback 不保留；历史
`editor_viewport_query_runtime_stats_v2..v7` 与当前 `v10` 是 diagnostics 版本链，不属于 stream compatibility。

## 拒绝方案

- 不在 resize 中移动 camera、重新 focus selection 或插值 FOV；布局不是 document/camera edit。
- 不让 Scene policy 泄漏到 Game/Preview；二者有不同构图合同。
- 暂不增加 `MajorAxisFOV`、physical camera、gate fit 或 preference UI；当前没有第三个生产需求证明这些复杂度。
- 不把 projection policy 序列化进 Scene schema；它是 editor endpoint state。

## 影响与验证

90° horizontal 与旧 60° vertical 在 16:9 下接近，因此常用初始布局只有很小构图差异；更窄或更高的 Scene View 会稳定
保持横向构图尺度。Game/Preview 的 60° vertical 行为保持不变。

验证要求：

- native camera smoke 在固定宽度、不同高度下投影同一组顶点，Scene 像素间距误差不超过 1 px；camera pose 与 canonical
  90° FOV 不变；
- managed session/bridge tests 证明 Scene/Game/Preview default 与 V7 ABI enum/field round-trip；非法 axis fail closed；
- multi-endpoint smoke 证明同一 SceneDocument 的 Scene 与 Game session 分别保留 horizontal/vertical policy；
- 真实 Studio/Vulkan Window smoke 使用
  `--smoke-viewport-transaction-window-resize --viewport-window-pattern=height-aba --viewport-window-input-hz=60`
  `--viewport-window-input-count=12 --viewport-window-evidence=continuous`，保持 outer/client/Scene/surface 宽度不变并取得至少两个
  exact rendered 高度；它把实际 immutable requests、native leases、`LastPresentedSequence`、extent/revision 与由 90° horizontal FOV
  推导的 x/y pixel scale 写入结构化证据，要求 scale drift `<=1 px`；
- 同一 Window smoke 要求 final extent 在 release 前只有一个 exact projection request，最终呈现序列精确指向该 request，且
  `WM_EXITSIZEMOVE` 开始后 request 与 camera/projection mutation 计数均为 0；
- distribution audit 要求全部 V7 stream exports，并拒绝 V1–V6 stream exports。

2026-08-12 的 `msvc-debug-tests` 真实 GPU process gate 已通过上述 `height-aba` case。该证据止于真实 Vulkan external surface 的
Avalonia `Rendered`/presentation state；它没有采集 WGC 像素，也不声称 DWM refresh 或物理 scanout。
