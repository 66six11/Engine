# Package-first 文件与包管理设计

## 目标

Asharia Engine 不采用“一个应用包含所有功能”的组织方式。核心思路接近 Unity package：
engine core 保持小而稳定，功能以 package 形式引入；不同 host，比如 sample runtime、
game app、editor、offline tools，通过依赖不同 packages 组合能力。

这能带来三个好处：

- runtime app 不需要被 editor 依赖污染。
- renderer、render graph、asset pipeline、editor UI 可以独立演进。
- 后续项目可以只引入需要的 packages，而不是复制或裁剪一个巨型工程。

## 仓库目录

```text
AshariaEngine/
  apps/
    sample-viewer/
    editor/
  engine/
    core/
    platform/
  packages/
    window-glfw/
    profiling/
    rendergraph/
    schema/
    archive/
    cpp-binding/
    persistence/
    reflection/
    serialization/
    rhi-vulkan/
    renderer-basic/
    shader-slang/
  cmake/
  docs/
  profiles/
  scripts/
  tools/
```

近期规划中的目录仍包括：

```text
AshariaEngine/
  packages/
    asset-core/
    scene-core/
    editor-core/
    input/
  package-registry/
  shaders/
  tests/
```

## Core 与 Package 边界

`engine/core` 只允许包含稳定、低层、跨 package 的基础设施：

- logging。
- error/result。
- filesystem/path。
- assertions。
- small containers 或 utility。
- build configuration。

`engine/core` 不应该依赖 Vulkan、GLFW、Slang、editor UI 或 asset importer。

package 用来承载可选能力：

- `window-glfw` 提供 GLFW host 和 Vulkan surface 创建。
- `profiling` 提供后端无关 CPU scope、frame sample、counter 和 JSONL 输出。
- `rhi-vulkan` 提供 Vulkan backend、VMA allocator、swapchain、command/sync。
- `rendergraph` 提供图声明、编译、barrier planning 和执行接口。
- `schema` 提供稳定 type/field id、value kind 和 typed metadata。
- `archive` 提供 `ArchiveValue` 和严格 JSON IO facade，不暴露第三方 JSON 类型。
- `cpp-binding` 提供 C++ object/member 与 schema field 的读写绑定。
- `persistence` 组合 schema、archive 和 binding，提供 save/load/default/migration。
- `reflection` / `serialization` 是过渡兼容 package，不再承载新 editor、script、asset 或 migration 语义。
- `renderer-basic` 提供后端无关 renderer contract 和共享 RenderGraph pass schema。
- `renderer-basic-vulkan` target 负责 Vulkan 命令录制、descriptor/pipeline/resource 绑定和 sample renderer。
- `shader-slang` 提供 Slang 编译、SPIR-V validation、metadata 和 reflection JSON。
- `asset-core` 未来提供 GUID、import settings、product/cache key、dependency 和 runtime-safe asset handle。
- `editor-core` 未来提供 editor service、selection、inspector、package browser。

## Package Manifest

每个已落地的 engine、package 和 app 入口都维护一个 manifest，命名为 `asharia.package.json`：

```json
{
  "name": "com.asharia.rendergraph",
  "version": "0.1.0",
  "displayName": "Asharia Engine Render Graph",
  "description": "Backend-agnostic frame graph, resource lifetime, and barrier planning.",
  "dependencies": [
    "com.asharia.core"
  ],
  "targets": [
    "asharia-rendergraph"
  ]
}
```

manifest 的第一阶段用途是文档化边界、shipping target、test target 和 target-level dependency；
当前 CMake 仍以显式 `CMakeLists.txt` 为准，后续再评估由脚本或 CMake helper 读取 manifest。

## CMake 组织原则

- 每个 package 映射为一个或多个 CMake target。
- target 名称使用 `asharia::<name>` alias，例如 `asharia::rendergraph`。
- app target 只链接需要的 packages。
- package 不能反向依赖 app。
- editor 只能依赖 runtime package 的 public API，不能 include package 的 private headers。

示例依赖方向：

```text
apps/sample-viewer
  -> packages/profiling
  -> packages/renderer-basic (asharia::renderer_basic_vulkan)
  -> packages/window-glfw
packages/renderer-basic
  -> packages/rendergraph
packages/renderer-basic (asharia::renderer_basic_vulkan)
  -> packages/renderer-basic
  -> packages/rhi-vulkan
  -> packages/rhi-vulkan (asharia::rhi_vulkan_rendergraph)
packages/rhi-vulkan
  -> engine/core
packages/rhi-vulkan (asharia::rhi_vulkan_rendergraph)
  -> packages/rhi-vulkan
  -> packages/rendergraph
```

其中 `packages/rhi-vulkan -> packages/rendergraph` 只适用于 RenderGraph/Vulkan 适配 target；
基础 `asharia::rhi_vulkan` target 不应公开依赖 RenderGraph。

## 文件可见性

推荐结构：

```text
packages/rendergraph/
  include/asharia/rendergraph/
  src/
  tests/
  asharia.package.json
  CMakeLists.txt
```

- `include/` 是 package public API。
- `src/` 是 private implementation。
- app 和其他 package 不允许 include 另一个 package 的 `src/` 文件。
- 跨 package 通信优先通过 interface、handle、descriptor、service registry，而不是直接访问实现对象。

## Editor 兼容性

未来 editor 不应该是 engine 的“主人”。它只是一个 host：

- editor 加载 package metadata。
- editor 读取 asset database。
- editor 通过 renderer package 提供的 public API 预览场景。
- editor 的 UI、inspector、importer、scene authoring 都放在 editor packages。
- ImGui context、ImGui Vulkan backend 和 editor texture registration 属于 editor host/integration
  层；`editor-core` 不依赖 ImGui、Vulkan 或 renderer implementation。
- runtime app 可以完全不链接 editor packages。

## MVP 取舍

第一阶段可以不实现完整 package manager，但目录和 target 边界必须提前按 package-first
组织。也就是说，MVP 仍然只跑一个窗口和三角形，但代码位置应该已经符合未来包化方向。

第一阶段必须做到：

- `apps/sample-viewer` 是唯一 executable。
- Vulkan 代码位于 `packages/rhi-vulkan`。
- 性能数据底座位于 `packages/profiling`，不依赖 Vulkan、RenderGraph 或 editor UI。
- render graph 位于 `packages/rendergraph`。
- Slang shader 构建位于 `packages/shader-slang` 或 `tools/shader-build`。
- 不出现 `src/engine_all.cpp` 这类聚合一切的巨型实现文件。
