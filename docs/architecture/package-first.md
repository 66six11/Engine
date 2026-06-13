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
    project-core/
    asset-core/
    asset-pipeline/
    material-core/
    shader-authoring/
    scene-core/
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
- `scene-core` 提供 headless World、runtime EntityId 和 local Transform baseline。
- `project-core` 提供最小 Asharia project descriptor。当前只保存 project identity、asset source roots、
  asset cache root policy 和 discovery ignore policy；不保存 target profiles、asset profiles、
  package/export 设置、editor workspace 或 runtime/GPU state。
- `reflection` / `serialization` 是过渡兼容 package，不再承载新 editor、script、asset 或 migration 语义。
- `renderer-basic` 提供后端无关 renderer contract 和共享 RenderGraph pass schema。
- `renderer-basic-vulkan` target 负责 Vulkan 命令录制、descriptor/pipeline/resource 绑定和 sample renderer。
- `shader-slang` 提供 Slang 编译、SPIR-V validation、metadata 和 reflection JSON。
- `asset-core` 提供最小资产身份、asset type、runtime-safe handle/reference、source metadata model、
  product/cache key、dependency 和 catalog；`.ameta` metadata IO 位于可选 `asharia::asset_core_io`
  target，依赖 `archive` facade，不把 JSON/persistence 依赖强加给 identity/handle API。
- `asset-pipeline` 第一阶段提供 CPU-only metadata discovery baseline：读取显式 source/.ameta 条目，
  产出 deterministic manifest / catalog 输入和诊断；不拥有 watcher、import 调度、product cache、
  GPU upload 或 editor UI。
- `material-core` 提供 CPU-only material resource signature、descriptor contract 和 pipeline key 数据模型；
  当前只依赖 `core`，不拥有 `.amat` IO、asset import、GPU upload、Vulkan pipeline/cache 或 editor UI。
- `shader-authoring` 提供 CPU-only `.ashader` document model、parser、source spans 和 authoring diagnostics；
  它只依赖 `core`，不调用 Slang compiler，不生成 SPIR-V，不进入 renderer、RHI、asset-pipeline 或 editor。
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

当前 manifest 已包含 `targetDependencies` 字段。审查时需要同时看两层：

- `dependencies` 是 package-level 粗粒度边界，表达“这个 package 中至少一个 target 需要这些 package”。
- `targetDependencies` 才能表达多 target package 内的真实 target 边界。例如
  `packages/rhi-vulkan` 的 package-level dependencies 可以包含 `com.asharia.rendergraph`，但
  `asharia-rhi-vulkan` target 仍只能依赖 `asharia-core`；RenderGraph 只属于
  `asharia-rhi-vulkan-rendergraph` adapter target。
- CMake 的 `target_link_libraries()` 和 `add_dependencies()` 仍是构建真相；manifest 更新滞后时，以
  CMake 为准并修正文档或 manifest，不能用 package-level dependency 推断 base target 已经合法依赖上层。

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

当前 `apps/editor` 已经是 Dear ImGui host。它不是 engine 的“主人”，只是组合
runtime/rendering packages 的 editor-only executable：

- 当前 editor 负责 ImGui runtime、dockspace、main menu、panel/action/event state、Scene View
  viewport request 和 ImGui texture descriptor registration。
- 当前 editor 通过 `renderer_basic_vulkan` 输出 sampled RenderView，并通过 `rhi-vulkan`
  frame loop 提交/present；panel 代码不录制 Vulkan commands。
- 未来 editor 加载 package metadata、读取 asset database，并通过 renderer package 提供的
  public API 预览场景。
- 未来 editor 的 inspector、importer、scene authoring 放在 editor-only packages 或
  `apps/editor` integration 层。
- ImGui context、ImGui Vulkan backend 和 editor texture registration 属于 editor host/integration
  层；`editor-core` 不依赖 ImGui、Vulkan 或 renderer implementation。
- Editor UI 的 C++ / 脚本协作边界以 [editor-ui-scripting.md](editor-ui-scripting.md) 为准；脚本不拥有
  第一版 editor shell、dockspace、viewport 或 backend integration。
- runtime app 可以完全不链接 editor packages。

## MVP 取舍

当前仍不实现完整 package manager，但目录和 target 边界已经按 package-first 组织。
Host 可以继续扩展 smoke 和 editor integration；新增能力必须先放进正确 package 或
app integration 层，不能为了方便把 package/private 实现直接并进 app。

当前已经落地：

- `apps/sample-viewer` 是 sample host 和 runtime smoke harness。
- `apps/editor` 是 editor host 和 editor smoke harness。
- Vulkan 代码位于 `packages/rhi-vulkan`。
- 性能数据底座位于 `packages/profiling`，不依赖 Vulkan、RenderGraph 或 editor UI。
- render graph 位于 `packages/rendergraph`。
- Slang shader 构建位于 `packages/shader-slang` 或 `tools/shader-build`。
- `.ashader` authoring parser 位于 `packages/shader-authoring`，与 Slang 编译和 material runtime model 分离。
- 不出现 `src/engine_all.cpp` 这类聚合一切的巨型实现文件。
