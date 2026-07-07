# Asharia Engine

Asharia Engine 是一个 package-first 的 Vulkan render graph 引擎实验项目。当前目标是在 Windows 桌面端跑通 C++23、Conan 2、CMake Presets、GLFW、Vulkan 1.4、VMA、Slang shader pipeline 和最小 RenderGraph 闭环。

## 当前状态

- 已建立 `apps/`、`engine/`、`packages/` 三层目录，并为 engine、package 和 app 入口维护 `asharia.package.json`。
- 已接入 GLFW window、Vulkan context、swapchain frame loop、RenderGraph clear/transient、dynamic rendering clear、resize/recreate、triangle、depth triangle、indexed mesh、mesh 3D、draw list、MRT、descriptor layout、fullscreen texture、offscreen viewport、compute dispatch、deferred deletion 和交互式 triangle viewer。
- 已接入 `asharia-editor` Dear ImGui host：dockspace/menu、panel/action/event registry、Scene View sampled viewport、viewport overlay flags、overlay texture metadata 闭环、RenderView diagnostics snapshot、Frame Debug capture state、只读 Render Graph panel、ImGui texture registry、editor shell smoke、editor viewport smoke、frame debugger smoke 和 resize smoke。
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
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-mrt --frames 3
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-descriptor-layout
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-material-binding
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-fullscreen-texture
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-scene-draw-packet
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-render-view-grid-readback
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-offscreen-viewport
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-compute-dispatch
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-buffer-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-texture-upload
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-renderer-format-contract
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-deferred-deletion
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-registry
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-transform
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-attributes
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-roundtrip
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-json-archive
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-migration
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --bench-rendergraph --warmup 10 --frames 100 --output build\rendergraph-bench.json
```

无参数启动会打开交互式 triangle viewer；`--smoke-*` 入口用于自动验证和提交前审查。

运行 editor：

```powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --help
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --version
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --project path\to\project-dir
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --check-project path\to\project-dir
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --check-project-json path\to\project-dir
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --check-project-json path\to\asharia.project.json --product-manifest path\to\products.aproducts.json
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-native-bridge
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

无参数启动会打开 Dear ImGui editor host；`--smoke-editor-*` 入口用于验证 editor shell、Asset Browser project catalog snapshot、Lucide/custom asset icon resolver、Scene View sampled texture、viewport overlay flags、overlay metadata roundtrip、RenderView diagnostics snapshot、Frame Debug capture/pause/resume state、只读 Render Graph panel snapshot consumption 和 resize/retirement flow。

## 仓库结构

```text
AshariaEngine/
  apps/                  Host applications, including sample-viewer and editor.
  engine/                Stable engine foundation packages.
  packages/              Feature packages: window, RHI, render graph, shader, renderer.
  cmake/                 Shared CMake helpers.
  docs/                  Historical docs plus the deployable developer documentation root.
  profiles/              Conan host/build profiles.
  scripts/               Bootstrap scripts.
  tools/                 Repository maintenance tools, including encoding, doc-sync, and line-count checks.
```

## 文档入口

- [docs/developer-documentation-system/README.md](docs/developer-documentation-system/README.md)：新的开发者技术文档体系入口，也是文档站部署源目录。
- [docs/developer-documentation-system/zh/README.md](docs/developer-documentation-system/zh/README.md)：中文文档入口。
- [docs/developer-documentation-system/en/README.md](docs/developer-documentation-system/en/README.md)：English documentation entry.
- [docs/developer-documentation-system/zh/architecture/overview.md](docs/developer-documentation-system/zh/architecture/overview.md)：当前 package 分层、状态所有权和依赖方向。
- [docs/developer-documentation-system/zh/workflow/review.md](docs/developer-documentation-system/zh/workflow/review.md)：提交前验证和 smoke 命令。

## 维护约定

- 文档、脚本、配置文件使用 UTF-8 without BOM；C/C++ 源码和头文件使用 UTF-8 with BOM。详见 [docs/developer-documentation-system/zh/standards/encoding.md](docs/developer-documentation-system/zh/standards/encoding.md)。
- 修改包依赖、target 依赖、smoke 命令、RenderGraph 语义、frame loop、shader pipeline 或 Vulkan 生命周期时，必须同步相关文档。
- 构建目录、Conan 输出、生成的 toolchain/preset 不进入源码管理。
