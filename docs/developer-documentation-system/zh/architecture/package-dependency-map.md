# 架构：Package Dependency Map

## 目的

本文映射当前 package 和 target 依赖结构。它回答每一层拥有什么职责、哪些 target 可以依赖哪些 target、target 级例外放在哪里。

本文记录当前代码事实。未来自动化只用 `future` 标注。

## 分层

| 层 | 当前路径 | 依赖方向 |
|---|---|---|
| 基础层 | `engine/core`、`engine/platform`、`packages/profiling` | 其他层可以依赖基础层；基础层不能依赖 renderer、asset、editor 或 tool。 |
| 数据契约层 | `packages/archive`、`packages/schema`、`packages/cpp-binding`、`packages/persistence`、`packages/reflection`、`packages/serialization` | 可以依赖基础层和显式声明的数据契约前置包；不能依赖 renderer 或 editor host。 |
| 资产和材质模型层 | `packages/asset-core`、`packages/project-core`、`packages/resource-runtime`、`packages/material-core`、`packages/material-instance`、`packages/shader-authoring`、`packages/shader-slang`、`packages/shader-material-adapter`、`packages/asset-pipeline` | 可以依赖基础层和数据契约层；asset/model package 不依赖 `apps/*`。 |
| runtime rendering 层 | `packages/rendergraph`、`packages/rhi-vulkan`、`packages/renderer-basic`、`packages/window-glfw`、`packages/scene-core` | rendering package 向下依赖基础和模型层；Vulkan integration target 拥有 Vulkan-specific translation。 |
| host 和 tool 层 | `apps/sample-viewer`、`apps/editor`、`apps/studio`、`tools/asset-processor` | host 聚合 package；runtime package 不能反向依赖 host。 |

## 当前 Package 矩阵

| 路径 | 主要 target 或构建入口 | 当前 package 依赖 | 当前 owner |
|---|---|---|---|
| `engine/core` | `asharia::core` | 无 | base errors、results、logging、version data |
| `engine/platform` | `asharia::platform` | `com.asharia.core` | platform abstraction interface |
| `packages/profiling` | `asharia::profiling` | 无 | lightweight profiling helpers |
| `packages/archive` | `asharia::archive` | `com.asharia.core` | JSON-like archive value 和 JSON IO |
| `packages/schema` | `asharia::schema` | `com.asharia.core` | schema documents 和 registry |
| `packages/cpp-binding` | `asharia::cpp_binding` | `com.asharia.core`、`com.asharia.schema` | schema 上的 C++ type binding metadata |
| `packages/persistence` | `asharia::persistence` | `com.asharia.core`、`com.asharia.schema`、`com.asharia.archive`、`com.asharia.cpp-binding` | archive-backed persistence 和 migration |
| `packages/reflection` | `asharia::reflection` | `com.asharia.core` | runtime type registry |
| `packages/serialization` | `asharia::serialization` | `com.asharia.core`、`com.asharia.reflection` | reflection-based serialization |
| `packages/asset-core` | `asharia::asset_core`、`asharia::asset_core_io` | `com.asharia.core`、`com.asharia.archive` | asset identity、metadata、catalog、metadata IO |
| `packages/project-core` | `asharia::project_core`、`asharia::project_core_io` | `com.asharia.core`、`com.asharia.archive` | project descriptor 和 project IO |
| `packages/resource-runtime` | `asharia::resource_runtime` | `com.asharia.asset-core` | runtime resource handles、tickets、product record resolution |
| `packages/material-core` | `asharia::material_core` | `com.asharia.core` | material pipeline keys 和 resource signatures |
| `packages/material-instance` | `asharia::material_instance` | `com.asharia.core`、`com.asharia.archive`、`com.asharia.asset-core`、`com.asharia.shader-authoring` | `.amat` document IO 和 resolution |
| `packages/shader-authoring` | `asharia::shader_authoring` | `com.asharia.core` | `.ashader` document model、parser、generated Slang |
| `packages/shader-slang` | `asharia::shader_slang`、`asharia-slang-reflect` | `com.asharia.core` | Slang compilation helpers 和 reflection model |
| `packages/shader-material-adapter` | `asharia::shader_material_adapter` | `com.asharia.core`、`com.asharia.material-core`、`com.asharia.shader-authoring`、`com.asharia.shader-slang` | reflection 到 material signature 的转换 |
| `packages/asset-pipeline` | `asharia::asset_pipeline` | `com.asharia.archive`、`com.asharia.asset-core`、`com.asharia.material-instance`、`com.asharia.shader-authoring` | source discovery、import planning、product execution |
| `packages/rendergraph` | `asharia::rendergraph` | `com.asharia.core` | backend-agnostic graph declarations、compile result、diagnostics |
| `packages/rhi-vulkan` | `asharia::rhi_vulkan`、`asharia::rhi_vulkan_rendergraph` | `com.asharia.core`、`com.asharia.rendergraph` | Vulkan backend 和独立 RenderGraph-to-Vulkan bridge |
| `packages/renderer-basic` | `asharia::renderer_basic`、`asharia::renderer_basic_vulkan` | `com.asharia.core`、`com.asharia.material-core`、`com.asharia.rendergraph`、`com.asharia.rhi-vulkan`、`com.asharia.shader-slang` | backend-agnostic renderer schemas 和 Vulkan recorders |
| `packages/window-glfw` | `asharia::window_glfw` | `com.asharia.core`、`com.asharia.platform` | GLFW window integration |
| `packages/scene-core` | `asharia::scene_core` | `com.asharia.core` | scene world、nodes、transforms、draw packets |
| `apps/sample-viewer` | `asharia-sample-viewer` | runtime aggregate | sample host 和 runtime smoke 入口 |
| `apps/editor` | `asharia-editor`、`editor-native` | editor/runtime aggregate | C++ ImGui editor host、native bridge、editor smokes |
| `apps/studio` | `apps/studio/Editor.sln` | managed project references | Avalonia Studio shell、managed models、.NET tests |
| `tools/asset-processor` | `asharia-asset-processor` | asset tool aggregate | asset pipeline CLI |

## Target 级例外

Manifest dependency list 记录 package 级事实。CMake target dependency 更严格，是边界检查的事实源。

- `packages/rhi-vulkan` 的 package 级依赖包含 `com.asharia.rendergraph`，原因是同一个 package 内有 `asharia::rhi_vulkan_rendergraph`。基础 target `asharia::rhi_vulkan` 只链接 `asharia::core` 和 Vulkan 依赖。
- `packages/renderer-basic` 同时包含 `asharia::renderer_basic` 和 `asharia::renderer_basic_vulkan`。backend-agnostic 的 `asharia::renderer_basic` 只链接 `asharia::core`、`asharia::rendergraph`、`asharia::shader_slang`；Vulkan command recording 隔离在 `asharia::renderer_basic_vulkan`。
- `packages/asset-core` 和 `packages/project-core` 把 model target 与 IO target 拆开。model target 保持更小；IO target 引入 `asharia::archive`。
- `apps/editor` 构建供 Studio interop 使用的 shared `editor-native` bridge，也构建 native ImGui host 的 `asharia-editor` executable。

## 按层的状态所有权

| 状态 | Owner | 可以观察 | 不能修改 |
|---|---|---|---|
| Error category 和返回状态 | `engine/core` | 所有 package | host app 不能为 public package API 发明不兼容 error carrier |
| Schema definitions | `packages/schema` | `cpp-binding`、`persistence`、tests | renderer、RHI、editor UI package |
| Runtime type registry | `packages/reflection` | `serialization`、sample smokes | asset pipeline 和 Vulkan backend |
| Asset GUID、source record、product key | `packages/asset-core` | asset pipeline、tools、editors、resource runtime | renderer 和 RHI package |
| Asset import decisions 和 product payload | `packages/asset-pipeline` | tools 和 editor hosts | `asset-core`、renderer、RHI |
| Runtime resource ticket status | `packages/resource-runtime` | host applications、scene/resource systems | asset pipeline execution |
| Material resource signatures | `packages/material-core` | renderer-basic、shader-material-adapter | shader compiler 和 editor hosts |
| Render graph declarations 和 compile diagnostics | `packages/rendergraph` | renderer、RHI bridge、sample/editor smokes | Vulkan backend base target |
| Vulkan device、swapchain、GPU resources | `packages/rhi-vulkan` | renderer-basic-vulkan、host apps | RenderGraph 和 backend-agnostic renderer packages |
| Native viewport packet 和 frame-debug snapshot | `apps/editor` 的 `editor-native` | Studio interop adapters | managed Studio view models |
| Studio dock、command、panel、viewport model state | `apps/studio` | Studio features 和 tests | C++ runtime packages |

## 依赖规则

- package 通过 `include/` 或 package-specific public include directory 暴露 public API；其他 package 不包含它的 `src/`。
- `asharia::rendergraph` 是 backend-agnostic，不能包含 Vulkan headers。
- `asharia::rhi_vulkan` 不能依赖 RenderGraph。RenderGraph integration 属于 `asharia::rhi_vulkan_rendergraph`。
- `asharia::renderer_basic` 不能录制 Vulkan command。Vulkan command recording 属于 `asharia::renderer_basic_vulkan`。
- asset import 和 decoder policy 留在 `packages/asset-pipeline`；`asset-core` 拥有 identity 和 metadata，不拥有具体 importer 行为。
- `material-core` 不能依赖 Slang、Vulkan、RenderGraph、asset-pipeline 或 editor host。`shader-material-adapter` 负责 shader reflection 到 material signature 的转换。
- runtime package 不依赖 `apps/editor`、`apps/studio`、`apps/sample-viewer` 或 `tools/asset-processor`。

## 生命周期

1. Package manifest 声明 package-level dependencies 和 target dependency intent。
2. CMake 通过根 `CMakeLists.txt` 或 standalone package build 中的 `asharia_require_package_target()` 按拓扑加载 package。
3. 每个 package 声明一个或多个 `asharia::<name>` alias。
4. Host application 聚合 package target，并拥有 process-level lifetime。
5. RAII 或显式 `create()` function 拥有 failable runtime resources；host app 按反向所有权顺序销毁。

`future`: 生成式 graph 应该对比 `asharia.package.json` target dependencies 和 `target_link_libraries()`，在漂移时失败。在此之前 review 同时使用两个文件作为证据。

## 验证方式

运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked -RunCheapGates
```

边界检查：

```powershell
rg -n "rendergraph" packages/rhi-vulkan/CMakeLists.txt packages/rhi-vulkan/include packages/rhi-vulkan/src
rg -n "vulkan|Vk[A-Z]|vk[A-Z]" packages/rendergraph packages/renderer-basic/include/asharia/renderer_basic
rg -n "#include .*src/" engine packages apps tools -g "*.hpp" -g "*.cpp" -g "*.inl"
```

预期结果：

- 第一条命令可以找到 `asharia-rhi-vulkan-rendergraph`、`include-rendergraph` 和 bridge 文件，但不能出现 `asharia-rhi-vulkan` 对 `asharia::rendergraph` 的 public link。
- 第二条命令不应在 `packages/rendergraph` 或 backend-agnostic `renderer_basic` public headers 中显示 Vulkan API usage。
- 第三条命令不应显示跨 package `src/` include。
