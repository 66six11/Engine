# VkEngine

VkEngine 是一个 package-first 的 Vulkan render graph 引擎实验项目。当前目标是在 Windows 桌面端跑通 C++23、Conan 2、CMake Presets、GLFW、Vulkan 1.4、VMA、Slang shader pipeline 和最小 RenderGraph 闭环。

## 当前状态

- 已建立 `apps/`、`engine/`、`packages/` 三层目录。
- 已接入 GLFW window、Vulkan context、swapchain frame loop、RenderGraph clear/transient、dynamic rendering clear、resize/recreate、triangle、depth triangle、indexed mesh、mesh 3D、descriptor layout、fullscreen texture smoke 和交互式 triangle viewer。
- Shader 默认路线是 Slang；`packages/shader-slang` 提供 `vke_add_slang_shader()`，输出 SPIR-V 后执行 `spirv-val`，并生成 reflection JSON 供 renderer 校验 shader 契约。
- 日常构建以 MSVC preset 为主，提交前检查以 ClangCL preset 和 `clang-tidy` 为主。

## 环境要求

- Windows 10/11。
- Visual Studio 2022，需包含 MSVC C++ 工具链。
- CMake 3.28 或更新版本。
- Ninja。
- Conan 2.x。
- Vulkan SDK，需提供 Vulkan loader、validation layers、`slangc`、`spirv-val` 等工具。

## 快速开始

首次构建、清理 `build/` 后，或 Conan profile/依赖发生变化后，先生成 Conan toolchain 和依赖配置：

```powershell
.\scripts\bootstrap-conan.ps1
```

日常 Debug 构建：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

提交前建议至少跑一次 ClangCL Debug 检查构建：

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
```

运行 sample viewer：

```powershell
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --help
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --version
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-window
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-rendergraph
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-transient
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-dynamic-rendering
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-triangle
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-depth-triangle
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-mesh
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-mesh-3d
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-draw-list
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-descriptor-layout
build\cmake\msvc-debug\apps\sample-viewer\vke-sample-viewer.exe --smoke-fullscreen-texture
```

无参数启动会打开交互式 triangle viewer；`--smoke-*` 入口用于自动验证和提交前审查。

## 仓库结构

```text
VkEngine/
  apps/                  Host applications, including sample-viewer.
  engine/                Stable engine foundation packages.
  packages/              Feature packages: window, RHI, render graph, shader, renderer.
  cmake/                 Shared CMake helpers.
  docs/                  Architecture, workflow, review, and knowledge documents.
  profiles/              Conan host/build profiles.
  scripts/               Bootstrap scripts.
  tools/                 Repository maintenance tools.
```

## 文档入口

- [docs/README.md](docs/README.md)：知识库导航和建议阅读路径。
- [docs/build-workflow.md](docs/build-workflow.md)：Conan、CMake Presets、Visual Studio 和命令行构建流程。
- [docs/technical-stack.md](docs/technical-stack.md)：技术栈与依赖决策。
- [docs/architecture.md](docs/architecture.md)：模块边界、所有权、生命周期和 RenderGraph 设计。
- [docs/flow-architecture.md](docs/flow-architecture.md)：包依赖、启动流程、frame loop 和 RenderGraph/RHI 数据流。
- [docs/full-diagnosis-2026-05-05.md](docs/full-diagnosis-2026-05-05.md)：最近一次全量诊断、风险清单和未来开发计划。
- [docs/review-workflow.md](docs/review-workflow.md)：审查和提交前门禁。
- [docs/coding-standard.md](docs/coding-standard.md)：C++23、Vulkan、shader、同步和工程风格规范。

## 维护约定

- 文档、脚本、配置文件使用 UTF-8 without BOM；C/C++ 源码和头文件使用 UTF-8 with BOM。详见 [docs/encoding-policy.md](docs/encoding-policy.md)。
- 修改包依赖、target 依赖、smoke 命令、RenderGraph 语义、frame loop、shader pipeline 或 Vulkan 生命周期时，必须同步相关文档。
- 构建目录、Conan 输出、生成的 toolchain/preset 不进入源码管理。
