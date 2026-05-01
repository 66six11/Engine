# 技术栈决策

## 基础选择

- 平台：Windows 桌面端优先。
- 构建系统：CMake 3.28 或更新版本。
- 包管理器：Conan 2.x。
- 编译器：MSVC C++ 编译器；当前 Windows profile 固定 `compiler=msvc`、
  `compiler.version=194`、`compiler.cppstd=23`。
- 编程语言：C++23。
- 图形 API：Vulkan 1.4。
- GPU 内存管理：Vulkan Memory Allocator。
- 窗口与输入：GLFW。
- 渲染架构：render graph。
- 文件与包组织：package-first，应用、编辑器和工具作为 host 组合 packages。

## 初始依赖

运行时依赖：

- `glfw`：窗口、输入、Vulkan surface 创建。
- `vulkan-headers`：Vulkan API 声明。
- `vulkan-loader` 或系统 Vulkan SDK loader：运行时 Vulkan dispatch。
- `vulkan-memory-allocator`：GPU 内存分配。
- `glm`：可选数学库。第一阶段可以使用，后续也可以替换为项目自研 math 层。

Vulkan loader/binding 策略：

- 当前代码使用 raw Vulkan C headers，也就是直接 include `<vulkan/vulkan.h>`。
- 运行时 dispatch 依赖 Vulkan SDK 或系统 loader，CMake 通过 `Vulkan::Vulkan` 链接。
- MVP 阶段暂不引入 Volk 或 Vulkan-Hpp；如果后续切换 loader/wrapper，需要先更新 `rhi-vulkan` 边界和所有 public header 策略。

工具依赖：

- Vulkan SDK tools：`vulkaninfo`、validation layers、SPIR-V 工具。
- `slangc`：Slang 到 Vulkan SPIR-V 的主 shader 编译器。
- `glslang`、`shaderc` 或 `dxc`：fallback shader 编译器，不作为默认路线。
- `spirv-tools`：`spirv-val`、`spirv-dis` 和可选优化检查。
- `clang-format`、`clang-tidy`：格式化和静态检查。主编译器仍然是 MSVC。

## CMake 策略

- 使用 `cmake_minimum_required(VERSION 3.28)`。
- 使用 `target_compile_features(<target> PUBLIC cxx_std_23)` 表达 C++23 要求。
- 关闭非标准扩展：`CMAKE_CXX_EXTENSIONS OFF`。
- 使用 Conan 生成的 CMake presets 作为常规 configure/build 入口。
- 项目提交的 `CMakePresets.json` 不 include Conan 生成文件，避免本地生成物缺失时导致
  preset 继承链断开。
- 常规构建流程先运行 `conan install` 生成 `conan_toolchain.cmake`、CMake dependency
  config 和 Conan presets，再使用 `cmake --preset` / `cmake --build --preset`。
- Conan 生成目录和构建目录不进入源码管理。
- 每个 package 独立 CMake target，并提供 `vke::<name>` alias。
- app target 只链接需要的 package，不直接 include package 的 private `src/`。

## Package 策略

- 采用 `apps/`、`engine/`、`packages/` 三层结构。
- `engine/core` 只放稳定基础设施，不依赖 Vulkan、GLFW、Slang 或 editor。
- `packages/rhi-vulkan`、`packages/rendergraph`、`packages/shader-slang` 等功能包可以独立
  被 app/editor 引入。
- 后续每个 package 增加 `vke.package.json` manifest，记录名称、版本、依赖、CMake target。
- editor 是 host，不是 engine 核心的一部分；runtime app 不链接 editor packages。

## Conan 策略

- 使用 Conan 2 profile 区分 `Debug`、`RelWithDebInfo`、`Release`。
- 使用 `CMakeToolchain` 和 `CMakeDeps`。
- `CMakeToolchain` 保持默认 `CMakeUserPresets.json` 输出；不提交
  `CMakeUserPresets.json` 或 `build/generators/CMakePresets.json`。
- 项目级 CMake cache 选项通过 `CMakeToolchain.cache_variables` 设置。
- Conan 生成 CMake presets 后，`conanfile.py` 会补充 CLion 的 JetBrains vendor 字段，
  将 preset 绑定到名为 `Visual Studio` 的 CLion toolchain，避免 IDE 默认落到 MinGW。
- Windows MSVC profile 明确 `compiler=msvc`、`compiler.version=194`、
  `compiler.cppstd=23`、`arch=x86_64`。
- 仓库提交 `conan.lock`，`scripts/bootstrap-conan.ps1` 会在存在 lockfile 时自动用于
  `conan install`，避免依赖 recipe revision 漂移。

## Vulkan 1.4 策略

- instance/device 创建时请求 Vulkan API version 1.4。
- 查询并保存 `VkPhysicalDeviceVulkan11Features`、`VkPhysicalDeviceVulkan12Features`、
  `VkPhysicalDeviceVulkan13Features`、`VkPhysicalDeviceVulkan14Features`。
- 默认使用 synchronization2：`vkQueueSubmit2`、`VkDependencyInfo`、
  `vkCmdPipelineBarrier2`。
- MVP 使用 dynamic rendering，不使用传统 render pass/framebuffer 作为主路径。
- swapchain、present mode、surface format、timeline semaphore、dynamic rendering local
  read 都必须查询能力后再使用。

## Shader 策略

MVP 默认路线：

- shader 源码使用 Slang。
- 构建期或显式 shader build 命令通过 `slangc` 编译为 Vulkan SPIR-V。
- 运行前或构建期执行 SPIR-V validation。
- shader manifest 先记录 source、entry point、stage、target、output 与 validator；自动 reflection 等后续 render graph pass 稳定后再接入。

Slang 状态：

- Slang 作为默认 shader 语言。
- `packages/shader-slang` 提供 `vke_add_slang_shader()`，把 `slangc` 命令行、entry point、stage、
  target profile、output 路径和 `spirv-val` 检查写入构建流程。
- `vke_add_slang_shader()` 可生成 `.metadata.json`，记录 source、entry、stage、profile、
  output、`slangc` 路径/版本和 `spirv-val` 路径/版本，作为 shader metadata/reflection 的基线。
- `vke_add_slang_shader()` 可生成 `.reflection.json`，由 `vke-slang-reflect` 通过 Slang API
  读取 `ProgramLayout`，输出 entry、stage、vertex inputs、descriptor bindings、push constants
  和 Slang API version。当前 reflection 作为可审查构建产物，不自动生成 C++。
- 如果 Slang 的包管理集成暂时不顺，允许用预安装工具或 `tools/` 下的显式路径过渡，但
  不能静默依赖开发者环境。

## 构建配置

- `Debug`：validation layer、debug name、可选 GPU-assisted validation、断言。
- `RelWithDebInfo`：开发性能测试和 RenderDoc 捕获。
- `Release`：默认关闭 validation，可按需保留编译期开关。
