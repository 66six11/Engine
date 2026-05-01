# 下一阶段开发方案

研究日期：2026-04-29

更新日期：2026-05-01

## 当前基线

第一版 MVP 已经具备可运行、可审查、可复现的稳定基线：

- 无参数 sample viewer 可以持续渲染 triangle。
- `--smoke-frame`、`--smoke-rendergraph`、`--smoke-dynamic-rendering`、`--smoke-resize`、
  `--smoke-triangle` 已作为回归入口。
- Slang shader 构建会输出 SPIR-V、执行 `spirv-val`，并生成记录工具路径和版本的 metadata。
- `packages/shader-slang/tools/slang_reflect.cpp` 已接入 Slang API reflection，triangle shader
  会生成 `basic_triangle.vert.reflection.json` 和 `basic_triangle.frag.reflection.json`。
- `packages/shader-slang` 已提供 reflection JSON 读取模型，`BasicTriangleRenderer` 会在创建时校验
  triangle shader entry/stage、vertex inputs、descriptor bindings 和 push constants 契约。
- `VulkanPipelineLayoutDesc` 已能接收 descriptor set layouts 和 push constant ranges，当前
  triangle shader 仍显式使用空 resource signature。
- Conan 依赖已通过 `conan.lock` 锁定 recipe revision。

下一阶段目标不是立刻扩成完整 renderer，而是先把 shader layout、资源绑定、transient
资源生命周期和同步边界做稳，再进入 mesh asset 路线。

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

## 优秀案例借鉴

| 案例 | 可借鉴点 | 对 VkEngine 的落地方式 | 暂不照搬 |
| --- | --- | --- | --- |
| Frostbite FrameGraph | pass setup 和 execution 分离；setup 阶段声明资源读写，execution 阶段只消费解析后的资源 | 继续要求 RenderGraph pass 在声明期暴露 resource access；后续 reflection/descriptor 信息也先进入声明或 metadata 层 | 不直接复制大型 world renderer/feature 系统，避免超出当前 package 边界 |
| Unreal RDG | 整帧延迟编译、dependency-sorted execute、资源 alias、barrier/memory 管理和开发期 validation | 先从 validation 和调试表开始：缺失 binding、非法 state、未声明资源访问都应在 compile/record 阶段报错 | 暂缓 async compute、alias memory 和 RDG Insights 级别工具 |
| Granite | Vulkan 侧实践强调 render graph、deferred destruction、自动 descriptor/pipeline、linear upload allocator | 后续 mesh 阶段优先做 staging/linear upload 和 deferred destruction 规则，再扩大 descriptor 自动化 | Granite 后端偏 Vulkan-first；VkEngine 的通用 RenderGraph 层仍保持后端无关 |
| vuk | 资源通过 access-annotated pass 参数进入图；直接捕获外部资源会绕过自动同步；transient/persistent 资源区分清楚 | `--smoke-transient` 设计应区分 declare transient image 和 import/acquire persistent resource；pass callback 不直接偷用未绑定 VkImage | 暂缓 futures、多 queue 自动调度和跨 graph composition |
| Blender Vulkan render graph | 后端可把 draw/compute/transfer 命令收集成 graph，再重排并生成同步 barrier | depth/transient 阶段可把 transfer clear、dynamic rendering begin/end、draw 拆成更清楚的后端节点或调试事件 | 不把 Blender GPU module 的多线程 context 模型提前搬进当前单窗口 sample |
| Diligent Render State Packager | shader、pipeline、resource signature 可离线打包，构建期发现 shader/pipeline 问题，运行期减少编译依赖 | reflection JSON 之后，逐步把 pipeline layout/resource signature 固化为可审查的构建产物 | 暂不做跨 API archive 和完整离线 PSO packager |

## 推荐推进顺序

| 阶段 | 目标 | 主要改动 | 验收标准 |
| --- | --- | --- | --- |
| 1 | Slang reflection 基线 | 已在 `packages/shader-slang` 增加 reflection 工具，输出 `*.reflection.json` | triangle shader 生成 entry、stage、vertex inputs、descriptor set/binding、push constant 信息；现有 smoke 不退化 |
| 2 | Descriptor/Layout 契约 | 已开始用 reflection JSON 校验 triangle shader 契约，并打通 pipeline layout/resource signature 接口 | layout mismatch 能在构建或启动时报清楚错误；triangle smoke 继续通过 |
| 3 | RenderGraph transient image | 增加 transient image 声明、Vulkan image/image view/VMA allocation 和 binding 表接入 | 新增 `--smoke-transient`，验证非 backbuffer image transition 和 binding |
| 4 | Depth attachment MVP | 增加 depth 抽象状态、Vulkan depth layout/stage/access 翻译、dynamic rendering depth attachment | 新增 `--smoke-depth-triangle`，validation 无 error/warning |
| 5 | Mesh asset 路线 | 从固定顶点数据扩展到最小 mesh 数据、index buffer、staging upload | 新增 `--smoke-mesh`，渲染 indexed triangle 或 quad |

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

Descriptor/Layout 契约已开始消费 reflection JSON。当前实现保持 Vulkan pipeline layout 不变，
只在 `BasicTriangleRenderer::create()` 期间校验 triangle shader 契约，避免在 descriptor 系统
尚未完整前引入资源绑定行为变化。

当前范围：

- `packages/shader-slang` 提供 `readShaderReflection()`，读取 entry、stage、vertex inputs、
  descriptor binding 数量和 push constant 数量。
- `renderer-basic-vulkan` 校验 triangle vertex shader 的 `POSITION0` 和 `COLOR0` 输入。
- `renderer-basic-vulkan` 校验 triangle vertex/fragment shader 当前没有 descriptor binding 和
  push constant。
- `shader-slang` 可合并 shader reflection 得到 resource signature 计数。
- `VulkanPipelineLayoutDesc` 可接收 descriptor set layouts 和 push constant ranges，triangle
  renderer 当前显式创建空 layout。
- 缺失 reflection JSON 或字段不匹配会返回 `ErrorDomain::Shader`，并带具体字段上下文。

后续范围：

- 用 reflection descriptor bindings 定义固定 descriptor set/layout 契约。
- 增加固定 descriptor set layout RAII wrapper，并由 reflection descriptor bindings 创建或校验。
- 在 material/resource binding 路线稳定前，继续暂缓 bindless 和自动 C++ codegen。

## 暂缓事项

- 暂缓 bindless/descriptor indexing，等固定 descriptor 契约稳定后再进入。
- 暂缓 editor、asset database 和 package registry。
- 暂缓 glTF/mesh importer，先做最小 mesh buffer 和 index buffer。
- 暂缓多 queue/async transfer，当前仍保持 single graphics queue，降低同步复杂度。
