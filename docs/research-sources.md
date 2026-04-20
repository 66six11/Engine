# 资料与依据

研究日期：2026-04-19

工程决策优先参考一手资料。社区文章可以辅助理解，但不能替代 Vulkan 规范、Khronos
仓库、GPUOpen 文档、CMake/Conan/MSVC 官方文档。

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
- Slang 命令行文档：https://shader-slang.org/slang/user-guide/command-line-slangc-reference/
- Khronos Vulkan Samples Slang shader language sample：https://docs.vulkan.org/samples/latest/samples/extensions/shader_object/slang_shaders/README.html

结论：

- Slang 是面向实时图形的 shader 语言和编译器，官方文档描述了 SPIR-V/Vulkan 目标。
- Slang 可以通过 `slangc` 输出 SPIR-V，并通过 entry point 与 stage 参数选择 shader stage。
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

结论：

- VMA 是 MIT 许可的单头文件 Vulkan 内存分配库。
- 引擎应该通过一个 allocator facade 集中管理 buffer/image allocation，避免在各模块散落
  `vkAllocateMemory`。

## CMake、Conan、MSVC

一手资料：

- CMake Visual Studio 17 2022 generator：https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html
- CMake CXX_STANDARD：https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html
- CMake presets manual：https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- Conan 2 CMakeToolchain：https://docs.conan.io/2/reference/tools/cmake/cmaketoolchain.html
- Conan 2 CMakeDeps：https://docs.conan.io/2/reference/tools/cmake/cmakedeps.html
- Conan CMakePresets workflow：https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/build_project_cmake_presets.html
- Conan extending own CMakePresets：https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/extend_own_cmake_presets.html

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
