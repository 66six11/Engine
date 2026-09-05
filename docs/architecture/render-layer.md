# Render Layer Architecture

本文记录当前 render 层的真实边界和源码组织，目标是让后续 RenderView、Frame Debug、RenderGraph command stream、material/asset 接入时有清晰落点。

## Package 边界

- `packages/rendergraph` 只拥有后端无关的 graph model：resource、pass、slot、params、diagnostics 和 command summary。这里不能出现 Vulkan layout、stage、access mask 或 command buffer。
- `packages/rhi-vulkan` 拥有 Vulkan context、frame loop、swapchain、VMA resource、pipeline、descriptor、deferred deletion 和 command recording 基础设施。基础 target 不依赖 RenderGraph。
- `asharia::rhi_vulkan_rendergraph` 是 RenderGraph 到 Vulkan 的适配 target，负责把抽象 state 翻译为 Vulkan barrier/layout/stage/access。
- `packages/renderer-basic` 的 `asharia::renderer_basic` 只保留后端无关的 draw item 和 builtin graph schema。
- `asharia::renderer_basic_vulkan` 负责 basic renderer 的 graph 构建、Vulkan pass callback、debug execution event 和 offscreen viewport 路径。它可以依赖 `renderer_basic`、`rendergraph`、`rhi_vulkan` 和 `rhi_vulkan_rendergraph`，但不能反向污染这些底层 package。
- `apps/editor` 只消费 renderer 输出、diagnostics 和 sampled texture；panel 不直接创建 Vulkan pipeline、graph pass 或 barrier。

2026-05-23 审查结论：

- `packages/rendergraph/include` 和 `packages/renderer-basic/include/asharia/renderer_basic` 当前未出现
  Vulkan type；`rhi_vulkan/include` 和 `rhi_vulkan/src` 当前未 include RenderGraph，RenderGraph 只出现在
  `include-rendergraph` adapter target、CMake adapter target 和 package manifest 的 target-level metadata 中。
- `render_view.hpp` 位于 `renderer_basic_vulkan` public API，当前仍带有 `VkImage` / `VkImageView` /
  `VkFormat` / `VkExtent2D`，所以它是 Vulkan RenderView contract，不是 backend-neutral `renderer_basic`
  contract。
- `recordViewFrame()` 当前会构建 RenderGraph、记录 diagnostics 和 execution events。overlay intent 会进入
  `BasicRenderViewDiagnostics`；`BasicRenderViewOverlayDesc::worldGrid` enabled 时插入
  `builtin.render-view-world-grid`，由 `renderer_basic_vulkan` 用 fullscreen triangle、push constants 中的
  inverse view-projection / optional fade / per-view LOD / grid color 参数绘制 XZ world grid；`CameraPositionNear`
  仍保留在 RenderGraph command summary 中用于 diagnostics，但不占用 world-grid shader push constant budget。RenderView policy
  只根据 camera 到 grid plane 的垂直距离计算整帧统一的 1/2/5/10 world spacing LOD，不根据水平距离或片元距离改变 LOD；低高度会锁定 base spacing，
  shader 只消费 `GridLodSettings`，`fadeStart == fadeEnd == 0` 时不做距离淡出，避免高视角像被 depth fog
  裁掉。`BasicRenderViewOverlayDesc::worldGrid` 和 `sourceOverlayIds` 都由 renderer 复制进 diagnostics；
  Frame Debug replay 从 capture 里的 world-grid desc 恢复 overlay 参数，source id 只作为溯源元数据，
  不改变 graph/pass 执行语义。只有存在 debug-world-line 数据时才插入
  `builtin.render-view-overlay` pass，把 camera / frame / debug-world-line count 作为 typed params 与 command
  summary 进入 graph，并把 world line 投影成 line-list vertex buffer 绘制到目标 attachment。后续 scene mesh、
  selection、gizmo 或更多 debug line pass 必须继续把 per-view 数据作为 renderer-owned pass input，而不是从
  diagnostics 读取。
- `BasicRenderViewDesc::scene` 是 scene/asset/SRP 接入的当前最小入口。它只接收 `renderer_basic` 的
  backend-neutral `BasicDrawListItem` span 和 per-view `BasicSceneRasterMode`。非空 span 会插入真实
  `builtin.render-view-scene-mesh` pass；typed params 显式记录 draw item 数、indexed draw 数、view kind 和
  Solid/Wireframe policy。schema 要求 `target: ColorReadWrite`、`depth: DepthAttachmentWrite`、
  `vertices: BufferVertexRead`、`indices: BufferIndexRead`，并只允许 `SetShader`、`SetInt` 和
  `DrawIndexed` command summary。`renderer_basic_vulkan` 据此绑定 renderer-owned vertex/index buffer，使用
  camera view-projection 与每个 item 的 model matrix 录制 depth-tested `vkCmdDrawIndexed`；color attachment
  的 `LOAD` 由正式 read/write access 表达，不再伪装为 write-only。RenderGraph compiler 对进入该 pass 的
  `ColorAttachment -> ColorReadWrite` 和连续 `ColorReadWrite -> ColorReadWrite` 生成 same-layout barrier，
  Vulkan adapter 使用 color-attachment output stage 与 `READ | WRITE` access。空 span 不插入
  scene-mesh pass；仅有 Entity/Transform 的空实体不会隐式获得 mesh，也不会因为出现在 Hierarchy 就被绘制。
- `BasicDrawListItem` 携带 renderer-owned `BasicDrawPacketContext`：稳定 source object id、mesh resource key
  和 material resource key。该 context 进入 diagnostics、invalid-input error 和每个 `DrawIndexed` execution
  event，使 Frame Debug 能关联 pass、command、draw item 和 source/resource identity；它不把 `World*`、
  `Entity*`、editor pointer、source asset path、importer state 或 Vulkan handle 传进 renderer API。
- `scene-rendering` 把 authoritative local `position/quaternion/scale` 转成 row-major、column-vector 语义的
  `modelMatrix = T * R * S`；quaternion 顺序为 `(x,y,z,w)`，非均匀 scale 缩放 rotation matrix 的列。
  extraction 不静默 normalize 非单位 quaternion，也不把 finite negative/zero scale 改写为正值或 identity。
  当前 unlit validation vertex 没有 normal/tangent，因此本 slice 不新增 normal matrix、mirrored winding/culling
  policy，也不以 hierarchy/world transform 名义分解或重组该 local matrix。
- RenderGraph 对 vertex/index buffer 使用独立的 `VertexRead` / `IndexRead` state 和
  `BufferVertexRead` / `BufferIndexRead` slot access，不以 `ShaderRead` 冒充。它们不携带 shader stage；
  `rhi_vulkan_rendergraph` 分别映射为
  `VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT + VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT` 和
  `VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT + VK_ACCESS_2_INDEX_READ_BIT`。`DrawIndexed` 保留
  `indexCount / instanceCount / firstIndex / vertexOffset / firstInstance`，贯穿 compiled pass、executor
  context、diagnostics 和 debug tables。
- Solid 是每个 RenderView 的默认 raster policy。Wireframe 使用独立的
  `VK_POLYGON_MODE_LINE` pipeline；`VulkanContext` 只在调用方请求且 physical device 支持时启用
  `fillModeNonSolid`，再通过 typed `VulkanDeviceCapabilities` 告知 renderer。该 feature 不是 context
  启动硬要求；未启用时 Wireframe 返回 `VK_ERROR_FEATURE_NOT_PRESENT` typed error，同时把
  `BasicSceneWireframePath::Unavailable` 写入 diagnostics，不静默回退 Solid，也不让非法 pipeline 进入
  Vulkan validation。当前线宽固定 1 px，不查询或启用 `wideLines`。
- 当前阶段不交付 SRP 接入。SRP 只作为后续消费者约束：RenderView policy / recording 的职责划分不能把
  pipeline authoring、asset upload、script callback 或 editor state 塞进 `rendergraph`、`rhi_vulkan`
  或 `apps/editor` 的捷径；未来 SRP 应通过 renderer-owned scene/pass input 和 RenderGraph 声明接到同一
  RenderView route。

## 引擎先例与 Asharia 决策

- 采用 Unreal RDG 的显式 render-target load action（[`ERenderTargetLoadAction`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/ERenderTargetLoadAction)）和
  Godot `RenderingDevice` 对 keep/discard 初始内容的显式区别：保留既有 color 必须成为可编译的资源访问事实。
  Asharia 不把 Vulkan `VkAttachmentLoadOp` 泄漏到 RenderGraph public API，而以 backend-neutral
  `ColorReadWrite` / `readWriteColor` 表达 LOAD + 后续写入；拒绝把 LOAD pass 继续声明成 `ColorWrite`，因为
  这样 compiler 无法推导 producer、RAW/WAW dependency 与 access mask。
- 采用 Unreal 的 owner 分离：[`USceneComponent`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Components/USceneComponent)
  只有 transform/attachment，本身没有 rendering/collision；
  [`UStaticMeshComponent`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UStaticMeshComponent)
  才实例化 mesh，并通过 `CreateSceneProxy()` 为 render thread 建立表示。Asharia 采用“transform 与显式 mesh
  reference 分离、render 输入不可变”的方向：空 Entity 不隐式生成 mesh，schema v2 只保存 optional typed mesh GUID，
  `scene-rendering` 再从 snapshot 与显式 binding 提取 immutable draw list。拒绝照搬 UObject/component/proxy API，
  因为 Asharia 的 scene-core 必须保持 headless、package-first，renderer 才持有 GPU resource/execution state。
- 采用 Godot 的 [`Node3D`](https://docs.godotengine.org/en/stable/classes/class_node3d.html) 与
  [`MeshInstance3D`](https://docs.godotengine.org/en/stable/classes/class_meshinstance3d.html) 分离，以及
  [`Viewport.DEBUG_DRAW_WIREFRAME`](https://docs.godotengine.org/en/stable/classes/class_viewport.html) 的
  per-viewport policy：Wireframe 是 view presentation/debug policy，不修改 scene mesh、material 或 source asset。拒绝
  把 Godot scene tree/node 生命周期带入 data-only SceneDocument；Scene/Game 共享 authored mesh input，但 raster 按 view 计算。
- 采用 O3DE Atom 的 [Render Component → Feature Processor → Draw Packet](https://docs.o3de.org/docs/atom-guide/dev-guide/frame-rendering/)
  owner 方向：renderer-owned packet/context 承接 simulation snapshot，RenderGraph pass 再消费资源与 draw intent；
  不让 scene/editor object 直接进入 backend callback。拒绝此阶段引入 O3DE 式全局 Feature Processor：当前可复用需求
  只需要 `scene-rendering` 的 CPU extraction 和 caller-owned explicit bindings，不需要通用 registry/service。
- local TRS 数学采用 Unreal [`TTransformSRT3`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/GeometryCore/TTransformSRT3)
  明示的 Scale→Rotate→Translate，并由 Godot [`Basis`](https://docs.godotengine.org/en/stable/classes/class_basis.html)、
  O3DE [`Transform.inl`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzCore/AzCore/Math/Transform.inl)
  与 Unity [`Matrix4x4.TRS`](https://docs.unity3d.com/ScriptReference/Matrix4x4.TRS.html) 交叉确认。Asharia 采用
  确定的 `T * R * S` 结果，不复制 Unreal `FTransform` 与 quaternion 相反的 operator 组合顺序，不采用
  O3DE core Transform 的 uniform-scale 限制，也不为测试维护第二套 matrix builder。
- 拒绝用 debug line、AABB/bounds、selection outline 或 editor proxy 冒充“真实 mesh 已渲染”。这些仍是显式
  overlay/debug pass；scene-mesh 验收必须观察 Color/Depth、VertexRead/IndexRead、`DrawIndexed` execution event
  和像素结果。
- 拒绝让 Avalonia/editor panel 持有 GPU resource、Vulkan handle、OBJ/source path 或 importer state。UI 只提交
  view policy 并消费 sampled output/typed diagnostics；source→product 属于工具/asset 层，product→GPU 属于
  renderer/resource owner。

## Public Header 布局

- `basic_renderers.hpp` 是兼容聚合头，保留旧调用方的单入口。
- `basic_renderer_descs.hpp` 放 renderer create desc。
- `basic_renderer_stats.hpp` 放 pipeline、viewport、compute 等统计结构。
- `render_view.hpp` 放 RenderView target、camera、frame params、scene input、overlay、diagnostics、execution event 和 debug preview contract。
- `descriptor_layout_smoke.hpp` 放 descriptor layout smoke 验证入口。
- `material_binding_smoke.hpp` 放 material signature 到 Vulkan descriptor/pipeline layout 的 smoke 验证入口。
- `fullscreen_texture_renderer.hpp` 放 fullscreen texture、RenderView、offscreen viewport renderer。
- `basic_scene_renderers.hpp` 放 MRT、compute、triangle、mesh 3D 和 draw-list sample renderers。

## Private Source 布局

`basic_renderers.cpp` 仍是单个 translation unit。它保留共享 helper 的匿名 namespace，然后包含 `src/basic_renderers/*.inl` 私有实现分片：

- `shader_contracts.inl`
- `pipeline_layouts.inl`
- `graph_recording.inl`
- `debug_preview.inl`：持有 Frame Debug replay preview 的候选图像、结果状态、image copy pass 和 source-pass
  after-pass cursor；该 cursor 使用原始 RenderView pass index，不把 debug copy pass 计入 captured/replay pass 对齐。
- `render_view_targets.inl`
- `render_view_diagnostics.inl`
- `render_view_pass_policy.inl`
- `render_view_recording.inl`：持有私有 `BasicRenderViewPassRecordingContext`，把 RenderView pass insertion 需要的
  graph、target、policy、frame、bindings 和 event recorder 收拢在 renderer-basic-vulkan 内部。
- `descriptor_layout_smoke.inl`
- `material_binding_smoke.inl`
- `fullscreen_texture_renderer.inl`
- `mrt_renderer.inl`
- `compute_dispatch_renderer.inl`
- `triangle_renderer.inl`
- `mesh3d_renderer.inl`
- `draw_list_renderer.inl`

这个阶段刻意不把 helper 提升成内部公共 API。`graph_recording.inl` 只覆盖 graph compile、transient resource preparation、execute 和 final transition 这条稳定路径；`debug_preview.inl` 只覆盖 Frame Debug replay preview 的候选图像、结果状态、image copy pass 和 source-pass after-pass 调度；`render_view_targets.inl` / `render_view_diagnostics.inl` 只覆盖 RenderView target 转换、target 验证、diagnostics snapshot 和 execution event recorder；`render_view_pass_policy.inl` 只覆盖 RenderView scene-mesh、world-grid / debug-line overlay pass enablement 与 typed params 计算；`render_view_recording.inl` 只覆盖把该 policy 插入 RenderGraph pass。fullscreen source/composite pass、descriptor set、pipeline readiness、debug preview candidate 定义和 compute buffer/readback 仍留在原 owner，避免抽象过早扩大。

## 当前限制

- 实现分片不是独立 translation unit，不能提升并行编译能力。
- `renderer_basic_vulkan` 仍包含 sample renderer、editor viewport renderer 和 debug preview 支撑，后续需要继续按 RenderView pipeline、scene sample renderer、debug capture support 拆分。
- Frame Debug 的 execution event 目前是 renderer diagnostics 的轻量事件流，不等价于完整 GPU command capture。
- Debug-line overlay 的 vertex upload 仍是 renderer-owned per-frame upload buffer ring，尚未建模为 RenderGraph buffer resource；
  scene-mesh vertex/index buffer 已通过独立 RenderGraph slots/states 可见，但当前仍是 validation product 对应的
  renderer-owned 持久 buffer，不是通用 runtime mesh resource registry。
- Buffer upload baseline 已新增 `builtin.transfer-copy-buffer` 和 `CopyBuffer` command summary；
  `--smoke-buffer-upload` 只验证显式 payload 经 staging buffer 复制到 device-local buffer 再复制到 readback
  buffer，不读取 source path、`.ameta`、importer 或 product cache。真实 mesh/texture runtime resource owner
  仍是后续切片。
- `assets/fixtures/scene-rendering/directional-wedge.obj` 和 sidecar metadata 只属于 repository validation
  fixture；`tools/generate_validation_mesh_product.py` 在构建时验证封闭的 OBJ 子集并生成 deterministic C++
  product header/manifest。renderer 只消费生成的 vertex/index product 数据。该工具不是通用 OBJ importer，
  生成 schema 也不是承诺给项目资产的 runtime mesh product format。
- 真实通用方向的第一条 product 合同现由 `packages/mesh-product` 与
  [Mesh Product v1 文档](../systems/mesh-product-v1.md) 拥有；`asset-pipeline` 的受限 `.glb` importer
  已能产生该 CPU product。renderer 当前仍没有消费它：scene-mesh GPU buffer/native resolver 继续只服务
  directional-wedge validation fixture，直到 ResourceRuntime typed mesh 与 renderer GPU owner Slice 接入。
- validation product native resolver 只是在 smoke/fixture 中把一个已知 asset identity 映射到显式 product binding；它不是
  importer、asset database 或 runtime resource registry。binding 缺失、type 不符、stale 或自身无效时，`scene-rendering`
  只为该 object 产生 contextual no-draw diagnostic，不能偷换为 fixture 或 fallback mesh。
- `BasicRenderViewSceneDesc::sourceRevision` 从 V10 scene packet 流入 renderer diagnostics，Frame Debug 在 capture 时冻结该值，
  JSON 与 panel 都回显同一 revision；它是 capture 溯源证据，不是 renderer 反向读取 SceneDocument 的通道。

## 下一步收敛

后续顺序由 [system-architecture-roadmap.md](../planning/system-architecture-roadmap.md)、
[next-development-plan.md](../planning/next-development-plan.md) 和 GitHub Issues / Project 决定。本文件
只保留渲染层合同：

1. 评估是否把 RenderView 路径从 `BasicFullscreenTextureRenderer` 中独立成明确的 view renderer，fullscreen composite 只消费 sampled texture。
2. 扩展 asset-backed scene mesh、selection 或 gizmo pass 时，先扩展 renderer-owned pass policy、typed input、execution event 和 smoke，再决定是否需要新的 recorder owner。
3. 保持 `renderer_basic` 后端无关，把 Vulkan 录制和资源生命周期限制在 `renderer_basic_vulkan` / `rhi_vulkan`。
4. 可编程管线必须遵守 [programmable-pipeline.md](../rendergraph/programmable-pipeline.md)，不能绕过 RenderGraph/RHI 边界。

## 后续接入门禁

- Scene mesh validation slice 已满足 renderer-owned draw packet、RenderView pass policy、Color/Depth +
  VertexRead/IndexRead、`DrawIndexed` execution event 和 `--smoke-render-view-scene-mesh` 门禁。升级为通用
  asset-backed mesh 前，Mesh Product v1/受限 GLB source→artifact reader 已满足；仍必须补 RuntimeResource typed
  payload/handle、GPU lifetime/reload/deferred deletion 和
  material compatibility；不能从 editor object、source path 或 diagnostics 读取 mesh。
- Asset upload 接入前必须定义 asset-core source/product owner、renderer/RHI resource handle 或 upload request、GPU lifetime/deferred deletion 以及失败上下文；新增上传、copy 或 buffer 写入时必须进入 RenderGraph command/diagnostics，或作为命名 external pre-pass 出现在 Frame Debug/review 输出中。
- 资源上传第一步只允许 renderer/RHI 消费显式 payload 或未来 product data；`asset-core` / `project-core`
  不得持有 GPU handle、RenderGraph pass 或 Vulkan upload state。后续 mesh/texture upload 必须继续保留同样
  的 source/product/runtime resource 分层。
- SRP 接入前必须先把 pipeline authoring 限定为 RenderGraph 声明和 renderer-owned pass input 的前端；脚本或 editor tool 不能在 execute / Vulkan command recording 阶段回调，也不能绕过现有 RenderView target、scene input 和 smoke gate。
