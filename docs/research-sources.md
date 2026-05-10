# 资料与依据

初始研究日期：2026-04-19
最近核对日期：2026-05-08

工程决策优先参考一手资料。社区文章可以辅助理解，但不能替代 Vulkan 规范、Khronos
仓库、GPUOpen 文档、CMake/Conan/MSVC 官方文档。

## 引擎系统架构与线程设计

一手资料：

- Godot architecture overview：https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html
- Godot internal rendering architecture：https://docs.godotengine.org/en/stable/engine_details/architecture/internal_rendering_architecture.html
- Godot thread-safe APIs：https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html
- Godot Object class：https://docs.godotengine.org/en/stable/engine_details/architecture/object_class.html
- Godot import process：https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html
- Godot GDExtension overview：https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/what_is_gdextension.html
- Godot editor plugins：https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html
- Unreal parallel rendering overview：https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine
- Unreal Render Dependency Graph：https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine
- Unreal Object Handling：https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-object-handling-in-unreal-engine
- Unreal asynchronous asset loading：https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine
- Unreal modules：https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules
- Unreal plugins：https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine
- Unity SRP fundamentals：https://docs.unity3d.com/Manual/ScriptableRenderPipeline.html
- Unity Job System overview：https://docs.unity3d.com/Manual/JobSystemOverview.html
- Unity script serialization：https://docs.unity3d.com/Manual/script-serialization.html
- Unity AssetDatabase API：https://docs.unity3d.com/ScriptReference/AssetDatabase.html
- Unity Input System actions：https://docs.unity.cn/Packages/com.unity.inputsystem%401.10/manual/Actions.html
- O3DE Atom RPI overview：https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/rpi/
- O3DE Pass System：https://www.docs.o3de.org/docs/atom-guide/dev-guide/passes/pass-system/
- O3DE memory allocators：https://docs.o3de.org/docs/user-guide/programming/memory/allocators/
- O3DE Behavior Context：https://www.docs.o3de.org/docs/user-guide/programming/components/reflection/behavior-context/
- O3DE Asset Processor：https://docs.o3de.org/docs/user-guide/assets/asset-processor/
- O3DE Gems：https://www.docs.o3de.org/docs/user-guide/gems/
- Bevy ECS quick start：https://bevy.org/learn/quick-start/getting-started/ecs/
- Bevy plugins quick start：https://bevy.org/learn/quick-start/getting-started/plugins/
- Bevy RenderGraph API：https://docs.rs/bevy/latest/bevy/render/render_graph/struct.RenderGraph.html
- Vulkan Guide threading：https://docs.vulkan.org/guide/latest/threading.html
- Khronos command buffer usage sample：https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html

结论：

- Godot、Unity、O3DE 和 Bevy 都把高层对象/脚本/编辑器体验与底层 renderer/backend 分开；VkEngine
  继续保持 package-first 和 `rhi_vulkan` / `rendergraph` / renderer package 边界是合理方向。
- Unreal RDG、Unity RenderGraph、O3DE Pass System 和 Bevy RenderGraph 都强调显式 pass/resource
  依赖；脚本或工具前端不应绕过 RenderGraph 的显式 resource access。
- Godot 和 Unity 的线程资料都支持“高层对象主线程、安全数据并行”的设计；Unreal 说明
  RenderThread/RHIThread 应通过 proxy/snapshot/command queue 与 gameplay 分离；Vulkan 资料要求
  command pool 和 descriptor pool 这类对象按外部同步规则隔离。
- 资产管线、反射序列化、scene/world、resource lifetime、input、event/command、editor transaction、
  material/pipeline 和 diagnostics 是完整引擎不可缺少的架构前置项，但不应全部进入当前 MVP 实现。

## Reflection / Serialization / C# metadata

一手资料：

- Unity serialization rules：https://docs.unity3d.com/Manual/script-serialization-rules.html
- Unity Asset Database：https://docs.unity3d.com/Manual/AssetDatabase.html
- O3DE reflection contexts：https://www.docs.o3de.org/docs/user-guide/programming/components/reflection/
- Unreal property system reflection：https://www.unrealengine.com/blog/unreal-property-system-reflection
- Unreal Object Handling：https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-object-handling-in-unreal-engine
- Godot Object class：https://docs.godotengine.org/en/stable/engine_details/architecture/object_class.html
- WG21 P2996R13 Reflection for C++26：https://www.open-std.org/jtc1/SC22/wg21/docs/papers/2025/p2996r13.html
- .NET native hosting：https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting
- C# language versioning：https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-versioning
- Roslyn source generators：https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/#source-generators

结论：

- VkEngine 第一版反射/序列化应采用 opt-in 类型注册，不自动扫描所有 C++ 类型。
- Serialize/Edit/Script context 必须分开，不能用 public/private 或单个字段 flags 粗暴决定所有行为。
- 运行时引用不能直接保存 pointer 或 GPU handle；`EntityId`、`AssetGuid`、`AssetHandle<T>` 需要专门 serializer 和诊断。
- 当前项目使用 C++23，不能依赖 C++26 静态反射；第一版应使用手写注册表，未来可替换为 generated table。
- 后续 C# 接入应作为 scripting package，使用 .NET hosting 和 Roslyn/source generator 生成 managed metadata，不进入 `reflection` / `serialization` 底座。

## Vulkan 1.4

一手资料：

- Vulkan 最新规范：https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
- Vulkan 版本指南：https://docs.vulkan.org/guide/latest/versions.html
- Vulkan 1.4 feature proposal：https://docs.vulkan.org/features/latest/features/proposals/VK_VERSION_1_4.html
- Vulkan core revisions：https://docs.vulkan.org/spec/latest/appendices/versions.html

结论：

- Vulkan 1.4 于 2024-12-03 发布，并把一些原本基于扩展的能力提升为 core。
- 本次核对时最新规范为 Vulkan 1.4.349，日期是 2026-04-10。
- Vulkan 1.4 对现代 renderer 友好的能力包括 dynamic rendering local read、
  maintenance5、maintenance6、map memory 2、push descriptors、line rasterization、
  shader float controls 2、shader expect/assume、index type uint8 等。
- dynamic rendering local read 是部分提升到 core；depth/stencil 和 multisampled
  local read 仍然需要查询设备属性，所以引擎不能直接假设可用。

## SPIR-V 与 shader 输入

一手资料：

- Vulkan guide, ways to provide SPIR-V：https://docs.vulkan.org/guide/latest/ways_to_provide_spirv.html
- Vulkan guide, what is SPIR-V：https://github.khronos.org/Vulkan-Site/guide/latest/what_is_spirv.html
- Vulkan SPIR-V environment：https://docs.vulkan.org/spec/latest/appendices/spirvenv.html
- SPIR-V overview：https://www.khronos.org/spirv/

结论：

- Vulkan pipeline 或 shader object 最终消费的是 SPIR-V 相关输入。
- SPIR-V capability 必须和启用的 Vulkan feature/extension 对应。
- shader 编译必须可复现，生成物需要经过 SPIR-V 工具验证。

## Slang shader 语言

一手资料：

- Slang 文档：https://shader-slang.org/docs/
- Slang 用户指南：https://shader-slang.org/slang/user-guide/
- Slang 命令行文档：https://docs.shader-slang.org/en/stable/external/slang/docs/command-line-slangc-reference.html
- Slang reflection API：https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/09-reflection.html
- Khronos Vulkan Samples shader languages：https://docs.vulkan.org/samples/latest/shaders/README.html

结论：

- Slang 是面向实时图形的 shader 语言和编译器，官方文档描述了 SPIR-V/Vulkan 目标。
- Slang 可以通过 `slangc` 输出 SPIR-V，并通过 entry point 与 stage 参数选择 shader stage。
- Slang reflection 数据应通过 API 获取，例如编译链接后的 component type 可提供 `ProgramLayout`。
- Khronos Vulkan Samples 已提供 Slang shader language 示例，说明它可以用于 Vulkan 管线。
- Slang 仍然需要和 Vulkan feature/capability 对齐；输出 SPIR-V 后必须走 `spirv-val`。

审核建议：

- 默认路线：shader 使用 Slang，编译到 Vulkan SPIR-V，再用 SPIRV-Tools 验证。
- fallback 路线：如果 Slang 工具链或 Conan 集成阻塞，短期可用 GLSL/HLSL 到 SPIR-V
  跑通 render graph，但需要保留 Slang 迁移任务。
- 实现要求：固定 Slang compiler 版本，记录 `slangc` 命令行，提交最小 shader 样例和
  validation 指令。

## Vulkan Memory Allocator

一手资料：

- VMA 产品页：https://gpuopen.com/vulkan-memory-allocator/
- VMA 仓库：https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- VMA recommended usage patterns：https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html

结论：

- VMA 是 MIT 许可的单头文件 Vulkan 内存分配库。
- 引擎应该通过一个 allocator facade 集中管理 buffer/image allocation，避免在各模块散落
  `vkAllocateMemory`。
- RenderGraph transient image 可优先使用 VMA 的自动 memory usage 策略，再按实际用途细化。

## 性能诊断与外部工具

一手资料：

- Unity Profiler window：https://docs.unity3d.com/Manual/ProfilerWindow.html
- Unity profiling applications：https://docs.unity3d.com/Manual/profiler-profiling-applications.html
- Vulkan `vkCmdWriteTimestamp2`：https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdWriteTimestamp2.html
- Vulkan timestamp queries：https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#queries-timestamps
- Khronos `VK_EXT_debug_utils` guide：https://docs.vulkan.org/guide/latest/extensions/VK_EXT_debug_utils.html
- RenderDoc documentation：https://renderdoc.org/docs/
- NVIDIA Nsight Graphics documentation：https://docs.nvidia.com/nsight-graphics/
- Windows Performance Recorder：https://learn.microsoft.com/windows-hardware/test/wpt/windows-performance-recorder
- Windows Performance Analyzer：https://learn.microsoft.com/windows-hardware/test/wpt/windows-performance-analyzer

结论：

- 性能面板应消费 profiler 数据快照，而不是直接读取 renderer 或 Vulkan 后端内部对象。
- 游戏性能和编辑器自身性能应分开建模；未来 Game View 面板默认只分析 Game/Play target。
- Vulkan GPU timestamp 必须使用 query pool 和延迟 readback，不能为了当前帧数据阻塞 GPU。
- `VK_EXT_debug_utils` labels 主要服务 RenderDoc/Nsight capture 可读性，不替代引擎内置 benchmark 数据。
- Windows Performance Recorder / Analyzer 适合定位 CPU 调度、线程等待、驱动调用和系统层开销；它是外部诊断工具，不应成为 runtime 依赖。

## 下一阶段 Vulkan 资源与同步

一手资料：

- Vulkan dynamic rendering proposal：https://github.khronos.org/Vulkan-Site/features/latest/features/proposals/VK_KHR_dynamic_rendering.html
- Vulkan synchronization spec：https://github.khronos.org/Vulkan-Site/spec/latest/chapters/synchronization.html
- Vulkan synchronization examples：https://docs.vulkan.org/guide/latest/synchronization_examples.html
- Khronos unified image layouts blog：https://www.khronos.org/blog/so-long-image-layouts-simplifying-vulkan-synchronisation
- Vulkan descriptor indexing sample：https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html
- Blender Vulkan render graph：https://developer.blender.org/docs/features/gpu/vulkan/render_graph/

结论：

- dynamic rendering 继续作为主路径，新增 color/depth attachment 时不回退到传统 render pass。
- 每新增一个 RenderGraph resource state，都必须同步定义 Vulkan layout、stage、access 和 barrier 验证。
- unified image layout 属于后续优化观察项；当前仍保持显式 layout/stage/access 映射，避免提前依赖新能力。
- descriptor indexing/bindless 暂缓；先完成固定 descriptor set/binding 和 pipeline layout 契约。
- Blender 的 Vulkan render graph 文档强调让后端重排 transfer/compute/draw 并生成同步 barrier；当前项目可先借鉴调试节点和 barrier 可视化，不急于引入多线程 context。

## 优秀案例

一手资料和成熟项目资料：

- Frostbite FrameGraph GDC Vault：https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-RenderingArc
- Frostbite FrameGraph slides：https://www.slideshare.net/slideshow/framegraph-extensible-rendering-architecture-in-frostbite/72795495
- Unity URP Render Graph introduction：https://docs.unity3d.com/Manual/urp/render-graph-introduction.html
- Unity URP custom render pass：https://docs.unity3d.com/Manual/urp/render-graph-write-render-pass.html
- Unity URP unsafe pass：https://docs.unity3d.com/Manual/urp/render-graph-unsafe-pass.html
- Unreal Rendering Dependency Graph：https://dev.epicgames.com/documentation/en-us/unreal-engine/rendering-dependency-graph?application_version=4.27
- Unreal Render Dependency Graph in Unreal Engine：https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine
- Granite renderer：https://github.com/Themaister/Granite
- Granite render graph deep dive：https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/
- vuk render graph：https://vuk.readthedocs.io/en/latest/topics/rendergraph.html
- Diligent Render State Packager：https://diligentgraphics.github.io/docs/d6/d8b/DiligentSamples_Tutorials_Tutorial25_StatePackager_readme.html

结论：

- Frostbite、Unreal RDG 和 Unity Render Graph 都强调整帧图、延迟执行、资源生命周期、pass culling 和 barrier 由 graph 编译阶段统一管理。
- Granite 和 vuk 对 Vulkan 工程更贴近：transient/imported resource、access annotation、deferred destruction、upload allocator 都适合作为 VkEngine 中期参考。
- Diligent 的 Render State Notation/Packager 说明 shader、pipeline 和 resource signature 可以逐步转成离线可审查产物；这支持 VkEngine 先做 reflection JSON，再做 pipeline layout 契约。
- 这些案例都不意味着下一步要直接引入大型 renderer、async compute、bindless 或多线程 context；当前阶段仍以小 smoke 闭环验证每个抽象。

## CMake、Conan、MSVC

一手资料：

- CMake Visual Studio 17 2022 generator：https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html
- CMake CXX_STANDARD：https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html
- CMake presets manual：https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- Conan 2 CMakeToolchain：https://docs.conan.io/2/reference/tools/cmake/cmaketoolchain.html
- Conan 2 CMakeDeps：https://docs.conan.io/2/reference/tools/cmake/cmakedeps.html
- Conan CMakePresets workflow：https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/build_project_cmake_presets.html
- Conan extending own CMakePresets：https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/extend_own_cmake_presets.html
- Conan 2 lockfiles：https://docs.conan.io/2/tutorial/versioning/lockfiles.html

结论：

- CMake 支持 Visual Studio 17 2022 generator，默认使用 v143 toolset。
- Conan 2 推荐 `CMakeToolchain` 搭配 `CMakeDeps` 使用。
- Conan 生成的 presets 和 toolchain 文件应该被视为构建产物，不手写、不提交。
- 项目共享配置保留在 `CMakePresets.json`；开发者本地配置使用
  `CMakeUserPresets.json` 且不提交。
- 本项目采用 Conan 官方默认 workflow：`conan install` 生成 `CMakeUserPresets.json`
  和 `build/generators/CMakePresets.json`，再用 `cmake --preset conan-default`、
  `cmake --build --preset conan-debug`、`cmake --build --preset conan-release`。
- 不让项目提交的 `CMakePresets.json` include Conan 生成文件，避免生成物缺失时
  preset 继承链断开。
- CLion 导入 preset 时默认绑定默认 toolchain；需要通过
  `vendor["jetbrains.com/clion"].toolchain` 指定项目 toolchain。
