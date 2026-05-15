# Asharia Engine

Asharia Engine 是一个 package-first 的 Vulkan render graph 引擎实验项目。当前目标是在 Windows 桌面端跑通 C++23、Conan 2、CMake Presets、GLFW、Vulkan 1.4、VMA、Slang shader pipeline 和最小 RenderGraph 闭环。

## 当前状态

- 已建立 `apps/`、`engine/`、`packages/` 三层目录，并为 engine、package 和 app 入口维护 `asharia.package.json`。
- 已接入 GLFW window、Vulkan context、swapchain frame loop、RenderGraph clear/transient、dynamic rendering clear、resize/recreate、triangle、depth triangle、indexed mesh、mesh 3D、draw list、MRT、descriptor layout、fullscreen texture、offscreen viewport、compute dispatch、deferred deletion 和交互式 triangle viewer。
- Shader 默认路线是 Slang；`packages/shader-slang` 提供 `asharia_add_slang_shader()`，输出 SPIR-V 后执行 `spirv-val`，并生成 reflection JSON 供 renderer 校验 shader 契约。
- Schema-first 反射/持久化底层已拆成 `schema`、`archive`、`cpp-binding`、`persistence`，旧 `reflection` / `serialization` 仍作为过渡兼容面保留。
- `asset-core` 分支已接入最小资产身份与 metadata model：`AssetGuid`、`AssetTypeId`、`AssetHandle<T>`、`AssetReference`、`SourceAssetRecord` 和 package-local smoke tests。
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
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --help
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --version
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-window
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-transient
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-dynamic-rendering
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-depth-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mesh
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mesh-3d
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-draw-list
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mrt
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-descriptor-layout
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-fullscreen-texture
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-offscreen-viewport
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-compute-dispatch
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-registry
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-transform
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-attributes
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-roundtrip
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-json-archive
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-migration
```

无参数启动会打开交互式 triangle viewer；`--smoke-*` 入口用于自动验证和提交前审查。

## 仓库结构

```text
AshariaEngine/
  apps/                  Host applications, including sample-viewer.
  engine/                Stable engine foundation packages.
  packages/              Feature packages: window, RHI, render graph, shader, renderer.
  cmake/                 Shared CMake helpers.
  docs/                  Categorized workflow, standards, architecture, planning, and research docs.
  profiles/              Conan host/build profiles.
  scripts/               Bootstrap scripts.
  tools/                 Repository maintenance tools, including encoding, doc-sync, and line-count checks.
```

## 文档入口

- [docs/README.md](docs/README.md)：知识库导航和建议阅读路径。
- [docs/workflow/build.md](docs/workflow/build.md)：Conan、CMake Presets、Visual Studio、命令行构建和仓库维护工具。
- [docs/standards/coding.md](docs/standards/coding.md)：C++23、Vulkan、shader、同步和工程风格规范。
- [docs/architecture/flow.md](docs/architecture/flow.md)：包依赖、启动流程、frame loop 和 RenderGraph/RHI 数据流。
- [docs/rendergraph/rhi-boundary.md](docs/rendergraph/rhi-boundary.md)：RenderGraph 与 Vulkan RHI 边界。
- [docs/planning/next-development-plan.md](docs/planning/next-development-plan.md)：阶段顺序、近期重点和后续开发计划。
- [docs/workflow/review.md](docs/workflow/review.md)：审查和提交前门禁。

## 维护约定

- 文档、脚本、配置文件使用 UTF-8 without BOM；C/C++ 源码和头文件使用 UTF-8 with BOM。详见 [docs/standards/encoding.md](docs/standards/encoding.md)。
- 修改包依赖、target 依赖、smoke 命令、RenderGraph 语义、frame loop、shader pipeline 或 Vulkan 生命周期时，必须同步相关文档。
- 构建目录、Conan 输出、生成的 toolchain/preset 不进入源码管理。
