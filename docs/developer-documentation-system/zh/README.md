# Asharia Engine 开发者技术文档体系

本目录是中文版本。它描述当前代码中的 Asharia Engine：一个以 CMake package 为边界组织的 C++23 Vulkan RenderGraph engine，包含运行时包、渲染后端、示例程序、编辑器宿主、Studio shell 和资产处理工具。

旧 `docs/` 下的资料保留为历史和迁移参考；文档站部署源目录是 `docs/developer-documentation-system`，其中 `zh/` 与 `en/` 是两套同构语言版本。

## 怎么读

| 任务 | 入口 |
|---|---|
| 了解系统分层、状态所有权、依赖方向 | [architecture/overview.md](architecture/overview.md) |
| 了解 package 和 target 依赖矩阵 | [architecture/package-dependency-map.md](architecture/package-dependency-map.md) |
| 了解 data model、schema、persistence、asset/material 数据边界 | [architecture/data-model-and-persistence.md](architecture/data-model-and-persistence.md) |
| 了解渲染 frame flow 和 RenderGraph/RHI 边界 | [architecture/rendering-and-frame-flow.md](architecture/rendering-and-frame-flow.md) |
| 了解 asset/material/shader 产物流 | [architecture/asset-and-material-flow.md](architecture/asset-and-material-flow.md) |
| 了解 Studio、native editor、runtime bridge 边界 | [architecture/editor-runtime-boundaries.md](architecture/editor-runtime-boundaries.md) |
| 设计或修改 platform/window/profiling 支撑层 | [design/platform-window-design.md](design/platform-window-design.md) |
| 设计或修改资产目录、导入计划和 product execution | [design/asset-pipeline-design.md](design/asset-pipeline-design.md) |
| 设计或修改材质、Shader authoring 和 Slang reflection | [design/material-shader-design.md](design/material-shader-design.md) |
| 设计或修改 reflection、serialization、schema、persistence | [design/reflection-serialization-design.md](design/reflection-serialization-design.md) |
| 设计或修改 scene world 和 runtime resource registry | [design/scene-resource-design.md](design/scene-resource-design.md) |
| 设计或修改 RenderGraph 功能 | [design/rendergraph-design.md](design/rendergraph-design.md) |
| 设计或修改 Vulkan RHI | [design/rhi-vulkan-design.md](design/rhi-vulkan-design.md) |
| 设计或修改 basic renderer 和 render view | [design/renderer-basic-design.md](design/renderer-basic-design.md) |
| 设计或修改 native ImGui editor host | [design/editor-host-design.md](design/editor-host-design.md) |
| 设计或修改 Avalonia Studio shell | [design/studio-shell-design.md](design/studio-shell-design.md) |
| 查询核心错误、Result、日志 API | [api/core-api.md](api/core-api.md) |
| 查询 RenderGraph API | [api/rendergraph-api.md](api/rendergraph-api.md) |
| 查询 Vulkan RHI API | [api/rhi-vulkan-api.md](api/rhi-vulkan-api.md) |
| 新增 C++ package | [guides/add-package-guide.md](guides/add-package-guide.md) |
| 新增 RenderGraph pass | [guides/add-rendergraph-pass-guide.md](guides/add-rendergraph-pass-guide.md) |
| 构建项目 | [workflow/build.md](workflow/build.md) |
| 提交前验证 | [workflow/review.md](workflow/review.md) |
| 文档站部署 | [workflow/documentation-deployment.md](workflow/documentation-deployment.md) |
| 写新文档 | [standards/documentation.md](standards/documentation.md) |
| 编码规范 | [standards/coding.md](standards/coding.md) |
| 文本编码规范 | [standards/encoding.md](standards/encoding.md) |

## 分类职责

| 分类 | 回答的问题 | 不负责 |
|---|---|---|
| `architecture/` | 系统怎么分层，谁拥有状态，谁能依赖谁 | 单个功能的逐行实现计划 |
| `design/` | 某个功能怎么落地，模块、数据结构、流程、错误处理和测试 | 长期路线图和 Issue 排期 |
| `api/` | 接口怎么调用，参数是什么，返回什么，失败时是什么 | 教程式任务流程 |
| `guides/` | 开发者怎么完成一个具体任务 | 完整 API 列表 |
| `workflow/` | 构建、测试、评审、部署怎么执行 | 模块内部设计取舍 |
| `adr/` | 为什么做一个长期技术决策，拒绝了哪些方案 | 临时任务记录 |
| `standards/` | 命名、编码、文档写作、文本编码等稳定规则 | 当前 feature 的实现细节 |

## 当前事实来源

- 顶层构建入口：`CMakeLists.txt`、`CMakePresets.json`、`conanfile.py`、`scripts/bootstrap-conan.ps1`。
- package 边界：`engine/*/CMakeLists.txt`、`packages/*/CMakeLists.txt`、`apps/*/CMakeLists.txt`、`tools/*/CMakeLists.txt`。
- package manifest：每个 `asharia.package.json`。
- public API：`engine/*/include/`、`packages/*/include/`、`packages/rhi-vulkan/include-rendergraph/`。
- smoke 和工具入口：`apps/sample-viewer/src/main.cpp`、`apps/editor/src/main.cpp`、`tools/asset-processor/src/main.cpp`。
- Studio Avalonia shell：`apps/studio/Editor.sln`、`apps/studio/Core/`、`apps/studio/Shell/`、`apps/studio/Features/`、`apps/studio/Tests/`。

## 验证方式

文档变更至少运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
```

如果改动了构建、包边界、渲染、asset pipeline 或编辑器行为，还要按 [workflow/review.md](workflow/review.md) 运行对应 gate。
