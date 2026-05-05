# 下一阶段开发方案

研究日期：2026-04-29

更新日期：2026-05-05

## 当前基线

第一版 MVP 已经具备可运行、可审查、可复现的稳定基线：

- 无参数 sample viewer 可以持续渲染 triangle。
- `--smoke-frame`、`--smoke-rendergraph`、`--smoke-dynamic-rendering`、`--smoke-resize`、
  `--smoke-triangle`、`--smoke-depth-triangle`、`--smoke-mesh`、`--smoke-mesh-3d`、
  `--smoke-draw-list`、`--smoke-descriptor-layout`、`--smoke-fullscreen-texture` 已作为回归入口。
- Slang shader 构建会输出 SPIR-V、执行 `spirv-val`，并生成记录工具路径和版本的 metadata。
- `packages/shader-slang/tools/slang_reflect.cpp` 已接入 Slang API reflection，triangle shader
  会生成 `basic_triangle.vert.reflection.json` 和 `basic_triangle.frag.reflection.json`。
- `packages/shader-slang` 已提供 reflection JSON 读取模型，`BasicTriangleRenderer` 会在创建时校验
  triangle shader entry/stage、vertex inputs、descriptor bindings 和 push constants 契约。
- `VulkanPipelineLayoutDesc` 已能接收 descriptor set layouts 和 push constant ranges，当前
  triangle shader 仍显式使用空 resource signature。
- `rhi-vulkan` 已提供最小 descriptor pool / descriptor set allocation / buffer + sampled image +
  sampler descriptor write helper；`--smoke-descriptor-layout` 已从 layout-only smoke 扩展为
  layout + pool + set + descriptor write 验证。
- `--smoke-fullscreen-texture` 已把 descriptor set bind、sampled image descriptor update 和 fullscreen
  dynamic-rendering draw 接入真实 Vulkan 录制路径。
- `--smoke-draw-list` 已把后端无关 `BasicDrawListItem`、`builtin.raster-draw-list` schema、
  typed params payload、transient depth attachment 和多 item indexed draw 接入真实 Vulkan 录制路径。
- RenderGraph pass 已具备可选 `type` 字段和 `RenderGraphExecutorRegistry` 执行入口。当前它是
  C++ 快速路径和 typed pass 分发点，后续会演进为脚本/工具前端可生成的 pass 声明、参数和受控
  command context 的共同入口。
- RenderGraph command context skeleton 已接入：pass 可记录 `ClearColor`、`SetShader`、
  `SetTexture`、`SetFloat/SetInt/SetVec4` 和 `DrawFullscreenTriangle` 等后端无关 command summary；
  当前只进入 compiled pass、executor context 和 debug table，不执行 Vulkan 命令。
- Conan 依赖已通过 `conan.lock` 锁定 recipe revision。

下一阶段目标不是立刻接入脚本或扩成完整 SRP，而是先把 RenderGraph 声明模型、shader
layout、资源绑定、transient 资源生命周期和同步边界做稳。未来脚本系统只应复用同一套
C++ builder / command context / compiled graph 语义，而不是引入另一套渲染路径。

2026-05-05 全量诊断结论见 `full-diagnosis-2026-05-05.md`。当前主线已通过 MSVC/ClangCL
Debug 构建和完整 smoke 清单；draw list MVP、RenderGraph 最小 dependency sort 与负向编译
smoke 已落地，下一步优先级调整为：补 RenderGraph pass culling / side-effect 标记、deferred destruction、
descriptor/transient/pipeline cache 和 multi-view 边界。
不要在下一阶段提前接入脚本 VM、完整 asset database、bindless 或 async compute。

## 一手资料结论

- Slang reflection 应通过 Slang compilation API 获取，`ProgramLayout` 通常由
  `IComponentType::getLayout()` 得到；现有 `slangc` 命令行 metadata 只能作为工具链证据，
  不能替代反射数据。
  资料：https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/09-reflection.html
- Vulkan dynamic rendering 通过 `vkCmdBeginRendering` 在命令录制时指定 attachment，
  适合继续扩展 color/depth attachment，不需要回退到传统 render pass/framebuffer。
  资料：https://github.khronos.org/Vulkan-Site/features/latest/features/proposals/VK_KHR_dynamic_rendering.html
- 新增 RenderGraph 状态、transient image、depth attachment 或 texture binding 时，必须同步定义
  layout、stage、access 和 execution/memory dependency；同步问题优先用 validation 和
  synchronization2 路径验证。
  资料：https://github.khronos.org/Vulkan-Site/spec/latest/chapters/synchronization.html
- RenderGraph transient image 可以由 VMA 负责创建和绑定内存；VMA 推荐使用
  `VMA_MEMORY_USAGE_AUTO` 一类策略让 allocator 根据用途选择合适 memory type。
  资料：https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
- Descriptor indexing/bindless 能力很强，但会引入 non-uniform indexing、update-after-bind 等新约束；
  当前阶段先做固定 descriptor set/binding 契约，bindless 放到 material/texture 扩展后。
  资料：https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html
- Conan lockfile 用于依赖快照和可复现依赖解析；当前已完成，后续依赖变更时审查 lockfile diff。
  资料：https://docs.conan.io/2/tutorial/versioning/lockfiles.html
- Unity SRP/URP RenderGraph 的实用边界是：C# 在 record 阶段可以使用普通控制流创建 pass、填充
  PassData 并显式声明资源使用；RenderGraph 编译主要分析这些声明，实际 render function 在编译后
  通过受控 command context 执行。VkEngine 后续可以借鉴这个边界：脚本/工具可生成 graph 和受控命令，
  但编译优化必须基于显式 resource access，而不是解析任意脚本闭包。
  资料：https://docs.unity.cn/6000.0/Documentation/Manual/urp/render-graph-write-render-pass.html
- Unity Core RP 的 RenderGraph 执行模型是每帧 setup、compile、execute；资源通过 graph handle
  操作，pass 必须显式声明读写，graph 计算 resource lifetime，并可剔除输出未被使用的 pass。
  资料：https://docs.unity.cn/cn/Packages-cn/com.unity.render-pipelines.core%4014.1/manual/render-graph-fundamentals.html
- Unity `RenderGraphBuilder` 的 API 形态显示了需要覆盖的资源声明：`ReadTexture`、`WriteTexture`、
  `UseColorBuffer`、`UseDepthBuffer`、`Read/WriteComputeBuffer`、`UseRendererList` 和 transient
  texture/buffer；VkEngine 的 named slots 应覆盖同一类语义，而不是只提供位置参数。
  资料：https://docs.unity.cn/Packages/com.unity.render-pipelines.core%4011.0/api/UnityEngine.Experimental.Rendering.RenderGraphModule.RenderGraphBuilder.html
- Unity 的 `AddUnsafePass` 允许兼容式命令，但会降低优化能力，例如无法安全合并后续写同一 buffer 的 pass。
  VkEngine 后续如提供 unsafe/native pass，应只作为迁移和调试逃生口。
  资料：https://docs.unity.cn/6000.0/Documentation/Manual/urp/render-graph-unsafe-pass.html
- Vulkan pipeline cache 可复用 pipeline construction 结果，既可在相关 pipeline 间复用，也可跨应用运行复用；
  因此 pipeline 创建应在 backend/cache 层，不能落入每帧 RenderGraph compile。
  资料：https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCache.html

## 优秀案例借鉴

| 案例 | 可借鉴点 | 对 VkEngine 的落地方式 | 暂不照搬 |
| --- | --- | --- | --- |
| Frostbite FrameGraph | pass setup 和 execution 分离；setup 阶段声明资源读写，execution 阶段只消费解析后的资源 | 继续要求 RenderGraph pass 在声明期暴露 resource access；后续 reflection/descriptor 信息也先进入声明或 metadata 层 | 不直接复制大型 world renderer/feature 系统，避免超出当前 package 边界 |
| Unreal RDG | 整帧延迟编译、dependency-sorted execute、资源 alias、barrier/memory 管理和开发期 validation | 先从 validation 和调试表开始：缺失 binding、非法 state、未声明资源访问都应在 compile/record 阶段报错 | 暂缓 async compute、alias memory 和 RDG Insights 级别工具 |
| Unity Render Graph | pass data、resource usage 和 render function 分离；record 阶段可用普通语言控制流构建图，compile 阶段依赖显式资源声明；兼容期允许 unsafe pass 但优化能力下降 | 当前先在 C++ builder 中形成 `PassParams`、named resource slots 和受控 command context；未来脚本只是这些 API 的前端 | 不把 Unity 的完整 SRP 层级、camera stack、RendererFeature 生态提前搬入 MVP |
| Granite | Vulkan 侧实践强调 render graph、deferred destruction、自动 descriptor/pipeline、linear upload allocator | 后续 mesh 阶段优先做 staging/linear upload 和 deferred destruction 规则，再扩大 descriptor 自动化 | Granite 后端偏 Vulkan-first；VkEngine 的通用 RenderGraph 层仍保持后端无关 |
| vuk | 资源通过 access-annotated pass 参数进入图；直接捕获外部资源会绕过自动同步；transient/persistent 资源区分清楚 | `--smoke-transient` 设计应区分 declare transient image 和 import/acquire persistent resource；pass callback 不直接偷用未绑定 VkImage | 暂缓 futures、多 queue 自动调度和跨 graph composition |
| Blender Vulkan render graph | 后端可把 draw/compute/transfer 命令收集成 graph，再重排并生成同步 barrier | depth/transient 阶段可把 transfer clear、dynamic rendering begin/end、draw 拆成更清楚的后端节点或调试事件 | 不把 Blender GPU module 的多线程 context 模型提前搬进当前单窗口 sample |
| Diligent Render State Packager | shader、pipeline、resource signature 可离线打包，构建期发现 shader/pipeline 问题，运行期减少编译依赖 | reflection JSON 之后，逐步把 pipeline layout/resource signature 固化为可审查的构建产物 | 暂不做跨 API archive 和完整离线 PSO packager |

## 推荐推进顺序

| 阶段 | 目标 | 主要改动 | 验收标准 |
| --- | --- | --- | --- |
| 1 | Slang reflection 基线 | 已在 `packages/shader-slang` 增加 reflection 工具，输出 `*.reflection.json` | triangle shader 生成 entry、stage、vertex inputs、descriptor set/binding、push constant 信息；现有 smoke 不退化 |
| 2 | Descriptor/Layout 契约 | 已开始用 reflection JSON 校验 triangle shader 契约，并打通 pipeline layout/resource signature 接口 | layout mismatch 能在构建或启动时报清楚错误；triangle smoke 继续通过 |
| 3 | RenderGraph 声明模型 v2 | named write slots、params type id、typed POD params payload 和最小 pass schema 已接入；schema 现在校验 slot、params type 和 allowed command kind；下一步继续补更细的 compile/execute 错误；`pass.type` 先作为执行模型 / typed pass key | `--smoke-rendergraph` 已验证 type、slot、params type、allowed command kind 和 schema 能进入 compiled pass 和 executor context；旧 callback 路径不退化 |
| 4 | RenderGraph access/state 扩展 | `ShaderRead(fragment/compute)`、`DepthAttachmentRead/Write`、`DepthSampledRead(fragment/compute)` 已接入，并同步 Vulkan layout/stage/access 翻译 | `--smoke-rendergraph` 已验证 shader-read、depth-write、depth-sampled-read transition、shader stage/domain、depth aspect barrier 和 Vulkan adapter 字段；尚不引入实际 shader sampling |
| 5 | RenderGraph transient image | `createTransientImage()`、transient lifetime plan、debug table 和 `--smoke-transient` 已接入 | `--smoke-transient` 已验证非 backbuffer image transition、first/last pass、final shader stage 和 Vulkan adapter mapping |
| 6 | PrepareBackend transient allocation | 已增加 Vulkan image/image view RAII、VMA-backed transient image 创建、usage/aspect 推导和 binding 表接入 | `--smoke-transient` 已升级为真实 transient VkImage 录制路径，validation 无 error/warning |
| 7 | Depth attachment MVP | 已增加 dynamic rendering depth attachment、`D32Sfloat` transient depth image、depth aspect binding 和 depth-enabled pipeline | `--smoke-depth-triangle` 已接入，validation 无 error/warning |
| 8 | 受控 command context skeleton | 已增加 `RenderGraphCommandList`、`RenderGraphCommand`、compiled pass command summary、executor context command span 和 debug table 输出；`--smoke-rendergraph` 已覆盖 clear、set shader、set texture、float/vec4 参数和 fullscreen draw summary | command summary 可审查；不暴露 Vulkan API；不接入脚本 VM；resource access 仍必须在 builder 上显式声明 |
| 9 | Descriptor binding / fullscreen pass | 已增加最小 descriptor pool、descriptor set allocation、uniform-buffer write、sampled-image write、sampler write、descriptor bind 和 fullscreen dynamic-rendering draw；fullscreen 路径已开始使用可复用 pass schema、allowed command kind、typed params payload 和 command-derived pipeline key；mesh / draw list 路线已开始接上同一套声明模型 | `--smoke-descriptor-layout` 已验证 descriptor set 分配和 buffer/image/sampler write；`--smoke-fullscreen-texture` 已验证 shader 真实采样 transient source image |
| 10 | Mesh asset / draw list 路线 | 已从固定顶点数据扩展到最小 indexed quad mesh、host-upload index buffer、indexed draw、独立 3D cube smoke，以及最小 draw list；当前使用固定 MVP push constants，不引入全局相机系统；后续再补 resource upload、material/pipeline key 和 asset database | `--smoke-mesh` 已渲染 indexed quad；`--smoke-mesh-3d` 已验证 3D vertex layout、depth 和 MVP push constants；`--smoke-draw-list` 已验证 typed pass + 多 item indexed cube draw |
| 11 | RenderGraph compiler v2 起步 | 已增加 pass declaration index、read/write dependency table、稳定拓扑排序和最小 producer-read 依赖推导；compiled graph 按依赖顺序执行，direct callback 通过 declaration index 找回原 pass；无 producer transient read 和缺失 schema 会在编译期失败 | `--smoke-rendergraph` 已把 transient reader 声明在 writer 前，并验证 compiled order 会把 writer 排到 reader 前；debug table 输出 dependencies；负向 smoke 验证错误不会进入 pass callback |

## RenderGraph 脚本前置原则

当前不接入脚本系统，但从现在开始避免写出未来脚本难以映射的 C++ API：

- 脚本/工具未来应运行在 build/record 阶段，用普通控制流创建资源、pass、参数和受控命令；脚本函数本身不保存进 compiled graph。
- RenderGraph compile 只依赖显式声明的 resource access、pass type、params、schema 和受控 command summary；任意脚本闭包或 native callback 都视为不可分析黑盒。
- 后端执行阶段不调用脚本语言 VM；它消费 compiled graph、barrier plan、descriptor plan 和受控 command context 产物。
- 如果未来提供 unsafe/native pass，必须显式标记并降低优化假设；unsafe pass 不参与 aggressive alias、merge、reorder 等强优化，不能作为普通 pass 的默认路径。
- `pass.type` 不等同于 RenderQueue 或 shader tag；它表示执行模型或 typed pass key。RenderQueue 和 shader pass tag 等到 mesh/material 阶段再引入。
- `pass.type` 命名优先使用执行模型，例如 `builtin.transfer-clear`、`builtin.raster-fullscreen`、
  `builtin.raster-draw-list`、`builtin.compute-dispatch`；业务语义放在 `pass.name`、params 或后续 feature
  层，避免退化成大量不可优化的业务字符串 executor。
- command context skeleton 第一版仍主要作为 debug IR/summary；schema 已能限制 pass 允许的 command kind，
  `--smoke-fullscreen-texture` 已验证 `setTexture` / fullscreen draw 的最小 Vulkan 路径，并开始从 command
  summary 派生当前 fullscreen pipeline key；clear color 和 fullscreen tint 已通过 typed POD params payload
  进入 pass context。

近期 API 形态应先在 C++ 中验证，例如：

```cpp
graph.addPass("ClearColor", "builtin.transfer-clear")
    .writeTransfer("target", backbuffer)
    .setParams(ClearTransferParams{.color = clearColor});
```

后续 fullscreen/后处理方向可以扩展为：

```cpp
graph.addRasterPass("BloomPrefilter")
    .readTexture("source", sceneColor)
    .writeColor("target", bloomHalf)
    .record([](RenderGraphCommandList& cmd) {
        cmd.setShader("Hidden/Bloom", "Prefilter");
        cmd.setTexture("SourceTex", "source");
        cmd.setFloat("Threshold", 1.2F);
        cmd.drawFullscreenTriangle();
    });
```

第一版 compiler 不需要理解每条命令的全部语义，只要求资源访问已在 pass builder 上显式声明；
命令列表用于后续 pipeline/descriptor/debug 规划，并避免脚本直接触碰 Vulkan API。当前已有最小
fullscreen texture 真实执行 smoke；通用执行仍应等 pipeline key、typed params、resource state 和
Vulkan adapter 边界收紧后再扩大。

## RenderGraph 编译性能原则

RenderGraph 的 record/compile 路径需要按“可每帧运行”的标准设计。动态后处理、技能特效、
调试视图、相机差异和质量开关都可能让当前帧 graph topology 变化；图的价值正是让这些变化在
执行前变成可分析的 pass/resource 计划。

每帧流程拆成四段：

1. **RecordGraph**：根据 camera、quality、debug、演出/技能状态和 active predicate，创建当前帧
   resource handle、pass、named slots、typed params 和 command summary。这里可以使用普通 C++ 控制流；
   未来脚本也只应运行在这一段。
2. **CompileGraph**：校验 schema 和 resource access，剔除无用 pass，计算依赖、lifetime、resource
   state、barrier/layout plan、transient allocation plan 和 debug/profiling 表。该阶段只产生计划，不创建
   长期 GPU 对象。
3. **PrepareBackend**：根据 compiled graph 从 cache/pool 取得或创建真实 backend 对象，例如 transient
   image/buffer、descriptor set、pipeline layout、pipeline 和 per-frame parameter buffer。
4. **RecordCommands / Execute**：按 compiled pass 顺序录制 Vulkan command buffer，提交并 present。这里不再
   改 graph topology，也不调用脚本 VM。

每帧 compile 可以做的轻量工作：

- 根据当前设置、演出状态和 feature active predicate 生成 pass/resource 声明。
- 校验 pass type、schema、named resource slots、typed params 和显式 resource access。
- 计算 pass 依赖、resource lifetime、final transition 和 barrier/layout plan。
- 生成 transient resource allocation plan、command summary、debug table 和 profiling label。

每帧 compile 不应该做的重型工作：

- shader 编译、shader reflection 解析或磁盘 IO。
- descriptor set layout、pipeline layout、graphics/compute pipeline 的重复创建。
- 长期 GPU resource 创建、VMA allocation churn 或大量 heap allocation。
- 脚本源码/字节码编译、任意脚本 VM 执行期回调或 native black-box 分支解析。

因此后续需要逐步形成这些缓存/池：

- `ShaderCache`：key 至少包含 shader asset/path、entry point、stage/profile、target、compiler/toolchain
  version、defines 和 source/reflection 版本。value 包含 SPIR-V、shader module、reflection model。
- `PipelineLayoutCache`：key 来自合并后的 descriptor set/binding、descriptor type/count、stage visibility
  和 push constant ranges。value 包含 descriptor set layouts 和 pipeline layout。
- `PipelineCache`：key 至少包含 shader pass ids、pipeline layout signature、render target formats、sample
  count、depth/stencil state、blend state、primitive topology、vertex input layout 和 dynamic state flags。
  Vulkan 的 `VkPipelineCache` 可作为 backend 的复用机制之一。
- `DescriptorAllocator`：按 frame 或 per-flight frame arena 分配 descriptor set，key 为 descriptor set layout；
  在 GPU fence 确认后回收/重置，避免每 pass 创建 descriptor pool/layout。
- `TransientResourcePool`：按 compiled lifetime plan 分配/复用 image/buffer。image key 至少包含 format、
  extent、mip/layer、sample count、usage flags 和 debug name class；buffer key 至少包含 size、usage 和
  memory domain。Vulkan 侧可用 VMA `VMA_MEMORY_USAGE_AUTO` 选择 memory type。
- `GraphTemplateCache`：暂缓；只有当 topology 稳定且 compile 成本成为瓶颈时，再按 active feature set、
  render scale、formats、quality level、injection point 和 pass topology 复用拓扑排序、schema validation
  和部分 lifetime plan。

`PassSchema` 第一版建议字段：

```cpp
struct RenderGraphPassSchema {
    std::string_view type;
    RenderGraphQueueDomain queueDomain;
    std::span<const ResourceSlotSchema> requiredReads;
    std::span<const ResourceSlotSchema> requiredWrites;
    std::span<const ResourceSlotSchema> optionalReads;
    std::span<const ResourceSlotSchema> optionalWrites;
    PassParamsTypeId paramsType;
    std::span<const RenderGraphCommandKind> allowedCommands;
    bool allowCulling;
    bool hasSideEffects;
};
```

slot schema 至少需要描述：

- slot 名称，例如 `source`、`target`、`depth`、`objects`、`visibleList`。
- resource kind，例如 texture/image、buffer、renderer/draw list。
- abstract access，例如 `ShaderRead(fragment/compute/etc.)`、`ColorAttachmentWrite`、
  `DepthAttachmentRead`、`DepthAttachmentWrite`、`DepthSampledRead`、`TransferDst`、
  `StorageReadWrite`。depth 作为 attachment 读写与 depth texture 采样必须分开建模，避免混用
  layout/stage/access。
- 是否允许 transient、imported、persistent resource。
- 是否允许多个绑定，例如 MRT color slots。

command summary 第一版建议只保存数据，不保存脚本函数或裸指针：

- `SetShader(shaderAsset, shaderPass)`：后续生成 pipeline key。
- `SetTexture(bindingName, slotName)`：`slotName` 必须引用 pass builder 已声明的 read/write slot。
- `SetFloat/SetInt/SetVec4(bindingName, valueOrParamHandle)`：值可以是当前帧 literal，也可以是
  `FrameParamHandle`、`MaterialParamHandle` 等参数句柄。
- `DrawFullscreenTriangle()`：只允许在 `builtin.raster-fullscreen` 一类 schema 中出现。
- `ClearColor(slotName, color)`：只允许在 transfer/raster clear 类型中出现。

动态参数通信规则：

- 初期允许每帧 RecordGraph，把脚本/设置系统当前值复制为 command summary literal。
- 后续为频繁变化的参数引入 `FrameParamTable`、`MaterialParamTable` 或 `PostParamBlock`；compiled graph
  保存参数句柄，PrepareBackend/RecordCommands 阶段把当前值打包到 push constants、uniform buffer 或
  descriptor。
- 后端执行阶段不得回调脚本获取参数；参数必须在 RecordGraph 或 PrepareBackend 前进入参数表。

动态 feature 的建议策略：

- 轻量、常驻且需要连续演出混合的效果可以固定在 graph 中，用参数控制强度，例如 vignette、
  color grading、exposure、debug overlay。
- 昂贵或需要额外 RT/buffer 的效果应由 active predicate 控制是否 record，例如 bloom、DOF、
  SSAO、SSR、motion blur、技能 mask composite。
- 技能/演出类临时效果在淡入、激活、淡出期间 record pass；淡出结束且权重低于阈值若干帧后移除。
- 为避免首次触发 hitch，技能或后处理 feature 可以预热 shader、reflection、pipeline layout 和
  pipeline cache，但不需要长期执行对应 pass 或常驻 transient RT。

示例：黑白闪技能可以在激活期间临时加入 `SkillFlashMask` draw-list pass 和
`SkillBlackWhiteFlash` fullscreen composite pass；mask RT 是 transient image，`weight`、`phase`
和受影响对象列表作为每帧参数/输入更新。技能未激活时，这些 pass 和 mask RT 不进入 graph。

这类 feature 可以拆成：

- `SkillFlashState`：由 gameplay/timeline 更新，包含 `active`、`weight`、`phase`、`affectedObjects`、
  `cooldownFrames`。
- `SkillFlashMask`：`type = builtin.raster-draw-list`，写 `mask` transient RT，draw list 由
  `affectedObjects` 生成，shader tag 后续可为 `MaskOnly`。
- `SkillBlackWhiteFlash`：`type = builtin.raster-fullscreen`，读 `sceneColor` 和 `mask`，写 post target，
  参数为 `weight`、`phase`。
- `active predicate`：`active || weight > epsilon || cooldownFrames > 0`；predicate 为 false 时不 record
  这两个 pass，mask RT 也不会进入 transient allocation plan。

## 多视图 / 多相机原则

Unity SRP 在 Editor 中会为可见 Game View、Scene View、preview camera 等 view/camera 调用渲染；URP
也会处理 game camera、Scene view camera、reflection probe 和 inspector preview 等不同 camera。
VkEngine 后续 editor 不应假设一帧只有一个 RenderGraph。

建议模型：

```cpp
enum class RenderViewKind {
    Game,
    Scene,
    Preview,
    ReflectionProbe,
};

struct RenderView {
    RenderViewKind kind;
    CameraData camera;
    RenderTarget target;
    RenderViewFlags flags;
};
```

每帧流程：

```cpp
for (const RenderView& view : frameViews) {
    RenderGraph graph;
    renderer.recordViewGraph(graph, view);
    RenderGraphCompileResult compiled = graph.compile();
    backend.prepareAndRecord(compiled, view);
}
```

Game View 和 Scene View 共享 renderer package、RenderGraph、Vulkan backend、shader/pipeline/descriptor
cache 和 resource pools，但 graph topology 可以不同：

- Game View 通常使用游戏 camera、game volume/postprocess、UI/camera stack，输出到 swapchain 或 game
  viewport texture。
- Scene View 使用 editor camera，可能关闭或替换部分 game postprocess，并额外加入 grid、gizmos、
  selection outline、wire overlay、debug overlay 等 editor-only pass。
- Preview View / asset inspector 可以使用更小的 target 和专用 lighting/背景 pass，但仍复用 backend caches。

工程约束：

- RenderGraph resource handle 只在单个 graph/view 内有效；跨 view 共享的 GPU resource 必须由 resource
  manager 拥有，再作为 imported resource 进入各自 graph。
- transient resources 默认只在单个 graph/view 内分配和复用；跨 view alias 以后再考虑。
- pipeline、shader、descriptor layout 等 cache 应跨 view 共享；per-view dynamic params、camera buffer、
  descriptor sets 和 transient resources 按 view/frame 隔离。
- Scene View 的 editor-only pass 必须有独立 feature flag，不能污染 Game View graph。
- 未来如支持 camera stacking，camera stack 是单个 view 内的多 camera composition；它不同于
  Scene/Game/Preview 这些 view target。

## 阶段 1 状态

Slang reflection 基线已接入，原因是它对后续 descriptor、pipeline layout、material、mesh
输入都提供契约，而且不会扩大 RenderGraph/Vulkan resource 生命周期风险。

当前范围：

- 新增 `packages/shader-slang/tools/slang_reflect.cpp`，使用 Slang API 读取 shader。
- 输入继续沿用 `*.shader.json`：source、entry、stage、profile、target。
- 输出 `*.reflection.json`：entry、stage、vertex inputs、descriptor bindings、push constants、
  compiler version、source path。
- `vke_add_slang_shader()` 增加可选 `REFLECTION_OUTPUT`，和当前 `METADATA_OUTPUT` 并行。
- 暂不自动生成 C++ 代码，先作为构建产物和校验输入。

验收：

- `basic_triangle.vert.reflection.json` 和 `basic_triangle.frag.reflection.json` 可重复生成。
- `--smoke-triangle` 继续通过。
- 文档更新 `flow-architecture.md`、`technical-stack.md` 和本文件。

## 阶段 2 状态

Descriptor/Layout 契约已开始消费 reflection JSON。当前实现会在
`BasicTriangleRenderer::create()` 期间校验 triangle shader 契约，并把合并后的
`ShaderResourceSignature` 映射为固定 Vulkan descriptor set layout / push constant ranges，
再创建 pipeline layout。当前 triangle signature 仍为空，因此尚未引入 descriptor set 绑定行为。

当前范围：

- `packages/shader-slang` 提供 `readShaderReflection()`，读取 entry、stage、vertex inputs、
  descriptor binding 数量和 push constant 数量。
- `renderer-basic-vulkan` 校验 triangle vertex shader 的 `POSITION0` 和 `COLOR0` 输入。
- `renderer-basic-vulkan` 校验 triangle vertex/fragment shader 当前没有 descriptor binding 和
  push constant。
- `shader-slang` 可合并 shader reflection 得到 resource signature 明细；descriptor binding
  按 `(set,binding)` 合并，push constant 按 `(offset,size)` 合并，并保留 stage visibility。
- `rhi-vulkan` 提供 `VulkanDescriptorSetLayout` RAII wrapper。
- `VulkanPipelineLayoutDesc` 可接收 descriptor set layouts 和 push constant ranges，triangle
  renderer 通过 reflection-derived signature 创建 pipeline layout。
- `--smoke-descriptor-layout` 已验证非空 descriptor signature：`descriptor_layout.slang`
  反射出 set 0 / binding 0 / `constantBuffer`，并能创建固定 descriptor set layout
  和 pipeline layout。
- 缺失 reflection JSON 或字段不匹配会返回 `ErrorDomain::Shader`，并带具体字段上下文。

后续范围：

- descriptor pool / descriptor set allocation / uniform-buffer / sampled-image / sampler write 已有
  最小 RHI wrapper；descriptor bind、fullscreen texture、draw list smoke 和 dependency-sorted
  RenderGraph smoke 已接入。进入 material/resource binding 前，下一步先补 pipeline/layout/descriptor
  cache 与 deferred destruction。
- RenderGraph pass 已拥有 named resource slots 和 typed params；后续 fullscreen/postprocess/draw-list
  继续沿用这套声明模型，避免依赖位置参数或 ad hoc callback capture。
- `ShaderRead`、depth attachment read/write 和 sampled depth state 已扩展到抽象 state 与 Vulkan
  access/layout 翻译；后续新增采样或 depth 路径时继续校验 image usage flags 与目标 layout/access 匹配。
- 在 material/resource binding 路线稳定前，继续暂缓 bindless 和自动 C++ codegen。

## 暂缓事项

- 暂缓 bindless/descriptor indexing，等固定 descriptor 契约稳定后再进入。
- 暂缓 editor、asset database 和 package registry。
- 暂缓脚本 VM 接入；当前只做脚本可映射的 C++ builder、typed params 和受控 command context。
- 暂缓完整 Unity SRP 风格的 RenderPipelineAsset、RendererFeature、RendererList 和 ShaderLab metadata。
- 暂缓 glTF/mesh importer，先做最小 mesh buffer 和 index buffer。
- 暂缓多 queue/async transfer，当前仍保持 single graphics queue，降低同步复杂度。
