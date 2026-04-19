# 技术栈决策

## 基础选择

- 平台：Windows 桌面端优先。
- 构建系统：CMake 3.29 或更新版本。
- 包管理器：Conan 2.x。
- 编译器：MSVC v143，Visual Studio 2022。
- 编程语言：C++23。
- 图形 API：Vulkan 1.4。
- GPU 内存管理：Vulkan Memory Allocator。
- 窗口与输入：GLFW。
- 渲染架构：render graph。

## 初始依赖

运行时依赖：

- `glfw`：窗口、输入、Vulkan surface 创建。
- `vulkan-headers`：Vulkan API 声明。
- `vulkan-loader` 或系统 Vulkan SDK loader：运行时 Vulkan dispatch。
- `vulkan-memory-allocator`：GPU 内存分配。
- `glm`：可选数学库。第一阶段可以使用，后续也可以替换为项目自研 math 层。

工具依赖：

- Vulkan SDK tools：`vulkaninfo`、validation layers、SPIR-V 工具。
- `slangc`：Slang 到 Vulkan SPIR-V 的主 shader 编译器。
- `glslang`、`shaderc` 或 `dxc`：fallback shader 编译器，不作为默认路线。
- `spirv-tools`：`spirv-val`、`spirv-dis` 和可选优化检查。
- `clang-format`、`clang-tidy`：格式化和静态检查。主编译器仍然是 MSVC。

## CMake 策略

- 使用 `cmake_minimum_required(VERSION 3.29)`。
- 使用 `target_compile_features(<target> PUBLIC cxx_std_23)` 表达 C++23 要求。
- 关闭非标准扩展：`CMAKE_CXX_EXTENSIONS OFF`。
- 使用 `CMakePresets.json` 固化 configure/build/test 入口。
- Conan 生成目录和构建目录不进入源码管理。

## Conan 策略

- 使用 Conan 2 profile 区分 `Debug`、`RelWithDebInfo`、`Release`。
- 使用 `CMakeToolchain` 和 `CMakeDeps`。
- VS 2022 profile 明确 `compiler=msvc`、`compiler.cppstd=23`、`arch=x86_64`。
- 首次跑通后生成 lockfile，避免依赖版本漂移。

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
- shader reflection 等第一个 render graph pass 稳定后再接入。

Slang 状态：

- Slang 作为默认 shader 语言。
- 需要固定 compiler 版本，并把 `slangc` 命令行、entry point、stage、target profile、
  output 路径和 `spirv-val` 检查写入构建流程。
- 如果 Slang 的包管理集成暂时不顺，允许用预安装工具或 `tools/` 下的显式路径过渡，但
  不能静默依赖开发者环境。

## 构建配置

- `Debug`：validation layer、debug name、可选 GPU-assisted validation、断言。
- `RelWithDebInfo`：开发性能测试和 RenderDoc 捕获。
- `Release`：默认关闭 validation，可按需保留编译期开关。
