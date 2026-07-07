# 架构：总体分层

## 目的

本文说明当前代码里的系统分层、状态所有权和依赖方向。它先记录当前事实；未来工作只用 `future` 标注。

## 配套文档

更细的边界阅读这些架构文档：

- [package-dependency-map.md](package-dependency-map.md): package 和 target 依赖矩阵。
- [data-model-and-persistence.md](data-model-and-persistence.md): archive、schema、persistence、reflection、serialization、asset、material、shader 数据所有权。
- [rendering-and-frame-flow.md](rendering-and-frame-flow.md): frame loop、RenderGraph、RHI、renderer、Vulkan flow。
- [asset-and-material-flow.md](asset-and-material-flow.md): asset product 和 material/shader flow。
- [editor-runtime-boundaries.md](editor-runtime-boundaries.md): C++ editor、native bridge、Studio、runtime ownership。

## 顶层代码结构

| 路径 | 当前职责 | 构建入口 |
|---|---|---|
| `engine/core` | `Error`、`Result`、日志、版本等基础类型 | `asharia::core` |
| `engine/platform` | 平台抽象 interface target | `asharia::platform` |
| `packages/*` | 可独立构建的 engine package | `asharia::<name>` alias |
| `apps/sample-viewer` | 交互 sample viewer、runtime smoke、RenderGraph benchmark | `asharia-sample-viewer` |
| `apps/editor` | C++ ImGui editor host、native bridge、editor smoke | `asharia-editor`、`editor-native` |
| `apps/studio` | Avalonia Studio shell 和 .NET tests | `apps/studio/Editor.sln` |
| `tools/asset-processor` | asset pipeline CLI 和 smoke | `asharia-asset-processor` |
| `cmake` | package-first helper 和 compiler options | `AshariaPackage.cmake` |

## Package 边界

每个 C++ package 通过自己的 `CMakeLists.txt` 定义 target，并通过 `asharia::<name>` alias 暴露。跨 package 依赖使用 `asharia_require_package_target(target package_relative_dir)`，standalone package build 会把依赖加入 `_asharia_deps` build 目录。

当前规则：

- public API 只能从 package 的 `include/` 暴露；`src/` 是实现细节。
- 调用方依赖 CMake target，不直接包含其他 package 的 `src/`。
- `asharia_configure_target()` 统一设置 C++23 和 warning options。
- `ASHARIA_BUILD_TESTS` 为 ON 时 package-local smoke/header tests 进入构建。

## 状态所有权

| 状态 | Owner | 公开方式 |
|---|---|---|
| 错误和返回值 | `engine/core` | `asharia::Error`、`Result<T>`、`VoidResult` |
| schema registry | `packages/schema` | `asharia::schema::SchemaRegistry` |
| runtime reflection registry | `packages/reflection` | `asharia::reflection::TypeRegistry` |
| scene world | `packages/scene-core` | `asharia::scene::World` |
| asset identity/catalog metadata | `packages/asset-core` | `AssetGuid`、`SourceAssetRecord`、`AssetCatalog` |
| asset import planning/execution | `packages/asset-pipeline` | `planAssetImports()`、`executeAssetProducts()` |
| render graph declarations and compile result | `packages/rendergraph` | `RenderGraph`、`RenderGraphCompileResult` |
| Vulkan instance/device/swapchain/resources | `packages/rhi-vulkan` | `VulkanContext`、`VulkanFrameLoop` 等 RAII class |
| builtin renderer schemas and Vulkan recorders | `packages/renderer-basic` | `renderer_basic` schema 层、`renderer_basic_vulkan` 实现层 |
| editor panel/action/tool state | `apps/editor` 或 `apps/studio` | editor host API，不是 engine package API |

## 依赖方向

- `engine/core` 没有 package dependency。
- `packages/rendergraph` 只依赖 `asharia::core`，必须保持 backend-agnostic。
- `asharia::rhi_vulkan` 链接 `asharia::core`、Vulkan、Vulkan headers、VMA。
- `asharia::rhi_vulkan_rendergraph` 是独立 interface target，连接 `asharia::rhi_vulkan` 和 `asharia::rendergraph`。
- `asharia::renderer_basic` 是 backend-agnostic schema/data 层。
- `asharia::renderer_basic_vulkan` 是 Vulkan command recording 实现层。
- `apps/sample-viewer`、`apps/editor` 可以聚合 runtime packages，因为它们是 host applications。

硬约束：

- `asharia::rendergraph` 不能包含 Vulkan headers。
- Vulkan layout、stage、access、barrier、swapchain、command buffer 细节属于 `asharia::rhi_vulkan` 或 `asharia::rhi_vulkan_rendergraph`。
- `asharia::renderer_basic` 不暴露 Vulkan command buffer 类型；Vulkan command recording 属于 `asharia::renderer_basic_vulkan`。
- runtime packages 不依赖 `apps/editor` 或 `apps/studio`。

## 生命周期

1. Conan 在 `build/conan/<profile>/<config>/generators/` 生成 toolchain。
2. CMake presets 使用 Conan toolchain configure。
3. package target 统一配置 C++23 和 warning。
4. host app 创建 window/platform 状态。
5. Vulkan host 创建 `VulkanContext`，再创建 `VulkanFrameLoop`。
6. renderer 声明 `RenderGraph`，compile 后由 backend 映射和录制命令。
7. RAII owner 按反向所有权销毁 Vulkan objects；跨 submitted frame 的资源使用 deferred deletion。

## 扩展点

- 新 runtime package：新增 `packages/<name>/CMakeLists.txt`、`asharia.package.json`、public headers、implementation、tests。
- 新 RenderGraph pass type：在 backend-agnostic 层加 schema，再在 backend implementation 加 executor/recorder。
- 新 asset importer：扩展 asset pipeline planning/execution，并补 tool smoke。
- 新 editor panel/tool：在拥有 UI 的 host 中注册并测试。

`future`: manifest 和 CMake 依赖统一后，可以生成 package dependency graph；在此之前文档必须同时点名 CMake target 和 manifest package。

## 验证方式

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

检查点：

- `asharia::rhi_vulkan` 不公开链接 `asharia::rendergraph`。
- `asharia::rhi_vulkan_rendergraph` 是 `packages/rhi-vulkan` 中唯一暴露 RenderGraph integration headers 的 target。
- `asharia::renderer_basic` 和 `asharia::renderer_basic_vulkan` 保持分离。
