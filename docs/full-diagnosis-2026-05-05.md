# 全量诊断与未来开发计划

诊断日期：2026-05-05

本文记录一次面向全仓库的工程诊断，覆盖构建、架构边界、RenderGraph、Vulkan RHI、shader pipeline、测试门禁和后续开发顺序。它是 `next-development-plan.md` 的阶段性校准材料；日常推进仍以 `flow-architecture.md` 记录真实流程，以 `review-workflow.md` 记录提交门禁。

## 诊断范围

- 平台：Windows 桌面端。
- API：Vulkan 1.4，当前运行设备报告 Vulkan API `1.4.329`。
- 工具链：C++23、Conan 2、CMake Presets、Ninja、MSVC、ClangCL、clang-tidy。
- 图形栈：GLFW、raw Vulkan C headers、VMA、Slang、SPIR-V validation。
- 代码范围：`apps/`、`engine/`、`packages/`、`cmake/`、`scripts/`、`tools/`、`docs/`。

## 验证记录

本次诊断已通过以下本地门禁：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
python C:/Users/C66/.codex/skills/vulkan-cpp23-engineering/scripts/review_vulkan_cpp.py . --fail-on warning
```

本次诊断也在 `msvc-debug` 和 `clangcl-debug` 上跑通完整 smoke 清单：

```text
--smoke-window
--smoke-vulkan
--smoke-frame
--smoke-rendergraph
--smoke-transient
--smoke-dynamic-rendering
--smoke-resize
--smoke-triangle
--smoke-depth-triangle
--smoke-mesh
--smoke-mesh-3d
--smoke-draw-list
--smoke-descriptor-layout
--smoke-fullscreen-texture
```

结论：当前主线可构建、可运行、可审查。没有发现阻塞性构建或 smoke 回归。

## 当前健康度

### 做得好的部分

- Package-first 边界基本成立：`engine/core` 保持低层基础设施，`rhi-vulkan`、`rendergraph`、`shader-slang`、`renderer-basic` 独立成包，`sample-viewer` 是 host 和 smoke harness。
- RenderGraph public API 没有暴露 Vulkan 类型；Vulkan layout、stage、access 和 barrier 翻译集中在 `asharia::rhi_vulkan_rendergraph`。
- `asharia::rhi_vulkan` 与 `asharia::rhi_vulkan_rendergraph` 已分离，基础 Vulkan 后端没有公开依赖 RenderGraph。
- Vulkan context 查询并启用 `synchronization2`、`dynamicRendering` 和 `shaderDrawParameters`，主提交路径使用 `vkQueueSubmit2`，graph transition 使用 `vkCmdPipelineBarrier2`。
- Slang 构建链已生成 SPIR-V、执行 `spirv-val`，并通过 Slang API 生成 reflection JSON。
- Descriptor set layout、pipeline layout、descriptor pool/set allocation、buffer/image/sampler write 和 fullscreen texture bind 已有最小真实 Vulkan smoke。
- Transient image、depth attachment、indexed mesh、3D cube 和 MVP push constants 已进入 smoke 覆盖。
- 编码脚本、Conan lockfile、MSVC/ClangCL 双构建和 Vulkan/C++ 审查脚本都已形成稳定门禁。

### 主要风险

| 优先级 | 风险 | 影响 | 建议 |
| --- | --- | --- | --- |
| P2 | RenderGraph 依赖排序和显式 culling 已起步，但更强非法依赖诊断还未完成 | 后续 postprocess、draw list、multi-view 一旦出现复杂依赖，仍需要更清晰的错误报告和更细 pass 剔除策略 | 继续推进 DAG compiler v2：read-before-write 校验增强、循环/多 writer 诊断和 culling 策略扩展 |
| P2 | `sample-viewer`、`basic_triangle_renderer.cpp`、`render_graph.hpp` 开始变大 | smoke、后端执行、验证逻辑继续堆叠会降低审查效率 | 拆分 smoke helper、renderer backend executor、RenderGraph validation/debug formatting |
| P2 | 后端资源生命周期仍是 MVP 模型 | 每帧 transient allocation、单 command buffer/fence、swapchain recreate `vkQueueWaitIdle` 会限制中期扩展 | 引入 deferred destruction、per-frame arena、descriptor allocator、transient resource pool |
| P2 | pipeline/descriptor cache 还未成型 | draw list 和 material 阶段可能把 pipeline/layout/descriptor 创建推入热路径 | 建立 `ShaderCache`、`PipelineLayoutCache`、`PipelineCache`、`DescriptorAllocator` |
| P3 | 文档和实际 smoke 清单容易漂移 | 审查门禁可能漏掉新增路径 | 把 `review-workflow.md` 的 smoke 清单作为源头，新增 smoke 时同步 README、flow 和 next plan |
| P3 | Vulkan headers lockfile 与运行时 API revision 不同 | 当前不阻塞，但使用 Vulkan 1.4 新能力前可能出现头文件/SDK/驱动认知不一致 | 引入 1.4 新 feature 前更新 Vulkan SDK、`vulkan-headers` 和 `conan.lock`，并记录版本依据 |

## 设计判断

当前方向应该继续保持“小闭环、强门禁、逐步扩展”。不要为了接近 Unity SRP、Unreal RDG 或完整资产管线而提前引入大型抽象；每一步新增能力都要有 smoke、validation 和文档同步。

### RenderGraph

短期目标是把 RenderGraph 从“声明顺序执行器”推进到“可分析图编译器”：

- pass 必须显式声明 read/write slot。
- compiler 已能根据读写关系做最小稳定拓扑排序，而不是只信任 addPass 顺序。
- pass schema 继续限制 params type、slot、command kind。
- command summary 只保存数据，不保存脚本函数或裸 Vulkan 对象。
- unsafe/native pass 后续可以作为逃生口，但必须显式标记并降低优化假设。

### Vulkan RHI

Vulkan backend 当前应继续保持 single graphics queue，优先补生命周期和缓存：

- `VulkanFrameLoop` 只管理 acquire、record callback、submit、present、swapchain recreate。
- `renderer_basic_vulkan` 负责 graph construction、backend resource prepare 和 Vulkan command recording。
- `rhi_vulkan_rendergraph` 只做抽象 state 到 Vulkan layout/stage/access/barrier 的翻译。
- Deferred destruction 必须先于复杂 material/draw list 扩张。

### Shader 与 Pipeline

Slang reflection JSON 是后续 material、descriptor 和 pipeline layout 契约的基线：

- reflection 继续作为可审查构建产物。
- pipeline layout key 来自 descriptor set/binding、stage visibility 和 push constant ranges。
- pipeline key 至少包含 shader pass、layout signature、render target formats、depth state、blend state、topology、vertex input 和 dynamic state。
- `VkPipelineCache` 可作为 Vulkan 后端复用机制，但上层仍需要稳定的 engine-level key。

## 未来开发计划

### 阶段 0：文档与门禁收敛

- 同步 README、`docs/README.md`、`review-workflow.md` 和 `flow-architecture.md` 的 smoke 清单。
- 明确 `.ps1` 编码策略以 `encoding-policy.md` 和 `tools/check-text-encoding.ps1` 为准。
- 在新增 smoke 或 RenderGraph 语义时，把文档更新写入 Definition of Done。

验收：

- 编码检查、`git diff --check`、MSVC/ClangCL Debug 构建和全套 smoke 通过。
- 文档中的当前状态和 `asharia-sample-viewer --help` 一致。

### 阶段 1：Draw List MVP（最小 smoke 已落地）

- 已新增后端无关 `BasicDrawListItem`，先覆盖 draw range、instance count 和 per-draw transform；mesh handle、material/pass key 和资源上传留到后续阶段。
- RenderGraph 已增加 `builtin.raster-draw-list` 类型、target/depth slots、schema 和 typed params payload。
- 当前继续使用固定 cube/quad 数据，不先接 glTF importer。
- 保持脚本不可见，不暴露逐 object Vulkan draw loop。

验收：

- `--smoke-draw-list` 已新增。
- draw list 已通过 RenderGraph slot/schema/params 进入 compiled pass。
- Vulkan backend 消费 compiled pass transitions、backend binding 表和 renderer 持有的 draw list。

### 阶段 2：RenderGraph Compiler v2（dependency sort 与显式 culling 已落地）

- 已新增 pass/resource dependency sort，compiled pass 保留 declaration index，debug table 输出 dependency。
- `--smoke-rendergraph` 已覆盖 transient reader 声明在 writer 前的乱序声明，并验证 writer 会被排到 reader 前。
- `--smoke-rendergraph` 已新增无 producer transient read 与缺失 schema 的负向编译覆盖，确认这些错误不会进入 pass callback。
- 已新增 `allowCulling` / `hasSideEffects`，compiled pass/context 保留标记，debug table 输出 culled passes。
- `--smoke-rendergraph` 已覆盖 unused transient writer 被剔除、side-effect pass 被保留、culled pass callback 不执行。
- 后续继续补 duplicate writer、missing final state、invalid transient usage 和更细的循环/多 writer 诊断。
- buffer resource、storage read/write、MRT slot 初版。
- debug table 已增加 dependency 和 culled pass；后续继续补 lifetime 和 backend transition 视图。

验收：

- `--smoke-rendergraph` 已覆盖乱序声明、dependency table、无 producer 读取、缺失 schema 和显式 pass culling；后续继续覆盖非法 slot 和更多非法依赖。
- 当前 RenderGraph CPU smoke 不依赖手写 addPass 顺序才能正确运行；Vulkan smoke 仍保持原有顺序，后续可逐步加入乱序声明覆盖。

### 阶段 3：后端生命周期与缓存

- Deferred destruction queue，以 fence/frame index 回收 GPU object。
- Per-frame descriptor allocator，按 layout 分配并在 GPU 完成后重置。
- Transient resource pool，按 format/extent/usage/aspect 复用 image。
- Pipeline layout cache 与 graphics pipeline cache。
- 可选接入 `VkPipelineCache` 持久化。

验收：

- resize、fullscreen texture、depth、mesh、draw list smoke 在多帧运行中无 validation warning。
- 不在每帧重复创建长期 pipeline layout 或 graphics pipeline。

### 阶段 4：Asset 与 Material 基线

- Linear/staging upload allocator。
- Mesh resource manager，先支持内置 mesh，再接文件 import。
- Material resource signature 从 Slang reflection 和 manifest 生成。
- Texture upload、sampler policy 和 descriptor binding plan。

验收：

- 最小 mesh asset smoke。
- material descriptor mismatch 能在加载或启动时报清楚错误。

### 阶段 5：Multi-view 与 Editor 前置

- 引入 `RenderView`：Game、Scene、Preview、ReflectionProbe。
- 每个 view 拥有独立 graph、camera params、transient resources 和 descriptor sets。
- shader/pipeline/descriptor layout/resource pool 跨 view 共享。
- Scene View 的 grid、gizmo、selection、wire/debug overlay 作为 editor-only pass。

验收：

- 同一帧可 record 多个 view graph。
- Game View 不被 Scene View/editor-only pass 污染。

## 资料依据

一手资料优先级仍以 `research-sources.md` 为准。本次诊断重点对照：

- Vulkan versions：https://docs.vulkan.org/guide/latest/versions.html
- Vulkan synchronization：https://docs.vulkan.org/spec/latest/chapters/synchronization.html
- Vulkan dynamic rendering proposal：https://github.khronos.org/Vulkan-Site/features/latest/features/proposals/VK_KHR_dynamic_rendering.html
- Vulkan pipeline cache：https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCache.html
- VMA memory type guidance：https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/choosing_memory_type.html
- Slang reflection：https://shader-slang.org/slang/user-guide/reflection.html
- Unity URP Render Graph：https://docs.unity.cn/6000.0/Documentation/Manual/urp/render-graph-write-render-pass.html
- Blender Vulkan Render Graph：https://developer.blender.org/docs/features/gpu/vulkan/render_graph/
- Conan lockfiles：https://docs.conan.io/2/tutorial/versioning/lockfiles.html
