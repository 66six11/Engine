# Asharia Engine 文档索引

这里是 Asharia Engine 的项目知识入口。根目录 `README.md` 面向快速上手；本目录按用途存放构建流程、工程规范、架构设计、RenderGraph 专项、系统设计、计划和研究资料。

## 目录约定

| 目录 | 用途 | 主要文档 |
| --- | --- | --- |
| `workflow/` | 日常工作流、构建、仓库维护工具、审查、独立 package 构建 | [build.md](workflow/build.md)、[technical-stack.md](workflow/technical-stack.md)、[review.md](workflow/review.md)、[package-standalone-build.md](workflow/package-standalone-build.md) |
| `standards/` | 编码、命名、文本编码等稳定规则 | [coding.md](standards/coding.md)、[encoding.md](standards/encoding.md)、[naming.md](standards/naming.md) |
| `architecture/` | 当前架构、真实流程、package 边界和线程边界 | [overview.md](architecture/overview.md)、[architecture-principles.md](architecture/architecture-principles.md)、[flow.md](architecture/flow.md)、[package-first.md](architecture/package-first.md)、[render-layer.md](architecture/render-layer.md)、[editor.md](architecture/editor.md)、[editor-extension-architecture.md](architecture/editor-extension-architecture.md)、[engine-systems.md](architecture/engine-systems.md)、[frame-loop-threading.md](architecture/frame-loop-threading.md)、[editor-ui-scripting.md](architecture/editor-ui-scripting.md) |
| `rendergraph/` | RenderGraph MVP、RHI 边界和专项路线图 | [mvp.md](rendergraph/mvp.md)、[rhi-boundary.md](rendergraph/rhi-boundary.md)、[roadmap.md](rendergraph/roadmap.md) |
| `systems/` | schema/持久化、资产、场景、脚本、性能诊断等系统设计 | [reflection-serialization.md](systems/reflection-serialization.md)、[reflection-serialization-plan.md](systems/reflection-serialization-plan.md)、[asset-architecture.md](systems/asset-architecture.md)、[scene-world.md](systems/scene-world.md)、[scripting.md](systems/scripting.md)、[performance-profiling.md](systems/performance-profiling.md) |
| `planning/` | GitHub Project 管理、阶段顺序、Definition of Done、专项开发方案 | [project-management.md](planning/project-management.md)、[next-development-plan.md](planning/next-development-plan.md)、[editor-development-plan.md](planning/editor-development-plan.md)、[render-layer-refactor-plan.md](planning/render-layer-refactor-plan.md) |
| `research/` | 一手资料索引和技术依据 | [sources.md](research/sources.md) |

## 快速阅读路径

新开发者建议按下面顺序阅读：

1. [workflow/build.md](workflow/build.md) - 本地 bootstrap、configure、build 和 preset 使用方式。
2. [workflow/technical-stack.md](workflow/technical-stack.md) - 平台、依赖、Vulkan、shader、CMake/Conan 策略。
3. [standards/naming.md](standards/naming.md) - Asharia Engine / 灰咏引擎的品牌、schema、文件后缀和代码命名规则。
4. [architecture/overview.md](architecture/overview.md) - 模块边界、所有权、生命周期和 RenderGraph 设计。
5. [architecture/architecture-principles.md](architecture/architecture-principles.md) - 跨系统架构思想、分层规范、数据合同和功能进入前检查。
6. [architecture/flow.md](architecture/flow.md) - 当前包依赖、启动流程、frame loop、RenderGraph/RHI 数据流。
7. [rendergraph/rhi-boundary.md](rendergraph/rhi-boundary.md) - RenderGraph 与 Vulkan RHI 的职责边界。
8. [architecture/render-layer.md](architecture/render-layer.md) - render package 边界、公共头布局和 renderer-basic Vulkan 私有源码组织。
9. [architecture/editor.md](architecture/editor.md) - 当前 `apps/editor` 架构、模块所有权、frame flow、viewport/texture lifetime 和验证入口。
10. [planning/editor-development-plan.md](planning/editor-development-plan.md) - Editor host、ImGui、panel/action/event、viewport texture registry、输入路由和详细阶段拆分。
11. [architecture/editor-extension-architecture.md](architecture/editor-extension-architecture.md) - Editor extension、tool lifecycle、viewport overlay、hot reload 和 renderer bridge 边界。
12. [architecture/editor-ui-scripting.md](architecture/editor-ui-scripting.md) - Editor UI 的 C++ 主实现、脚本扩展面、transaction 和 safe point 边界。
13. [workflow/review.md](workflow/review.md) - 每次审查、修复和提交前必须执行的门禁。
14. [planning/project-management.md](planning/project-management.md) - GitHub Project、Issue、PR、Epic/Slice、blocker 和 Done evidence 管理规则。
15. [planning/render-layer-refactor-plan.md](planning/render-layer-refactor-plan.md) - render 层整理的持久计划、合理性检查和分阶段验证入口。
16. [planning/next-development-plan.md](planning/next-development-plan.md) - 下一阶段 RenderTarget、RenderView、editor viewport、RenderGraph resource、asset/material 和 scene 路线。

## 当前门禁状态

- Scope：Windows 桌面端，Vulkan 1.4，C++23，GLFW，VMA，CMake，Conan，MSVC/ClangCL。
- Research：已有官方资料索引；涉及最新 SDK、扩展、工具行为时仍需重新核对一手资料。
- Design：已形成 package-first、RenderGraph/RHI 边界、frame loop 和 shader pipeline 初版设计。
- Implementation：已接入窗口、Vulkan context、swapchain frame loop、RenderGraph clear、transient image、dynamic rendering clear、resize/recreate、triangle、depth triangle、indexed mesh、mesh 3D、draw list、MRT、descriptor layout、fullscreen texture、offscreen viewport、compute dispatch、deferred deletion、asset-core identity/metadata model、无参数交互式 triangle viewer，以及 `asharia-editor` Dear ImGui host、Scene View sampled viewport、viewport overlay flags、overlay texture metadata 闭环、RenderView diagnostics snapshot、Frame Debug capture/pause state、Live RG View、FrameDebuggerPanel Frame/RenderGraph views 和 editor smokes。
- Validation：提交前按 [workflow/review.md](workflow/review.md) 运行编码检查、构建、smoke 和 Vulkan/C++ 审查脚本。

## 文档维护规则

- 根目录 `README.md` 只保留项目简介、快速开始、常用命令和文档入口。
- 当前真实流程以 [architecture/flow.md](architecture/flow.md) 为准；新增 smoke、包依赖、frame loop、RenderGraph 语义或 Vulkan 生命周期变化时必须同步更新。
- 提交门禁以 [workflow/review.md](workflow/review.md) 为准；新增 smoke 或审查规则时优先改这里。
- GitHub Project、Issue、PR、Epic/Slice、blocker 和 Done evidence 管理以 [planning/project-management.md](planning/project-management.md) 为准；AI/agent 行为入口同步在根目录 `AGENTS.md`。
- 全局阶段顺序以 [planning/next-development-plan.md](planning/next-development-plan.md) 为准；专项文档只能补约束，不维护第二套总路线。Editor 子阶段以 [planning/editor-development-plan.md](planning/editor-development-plan.md) 细化，不改全局阶段编号。
- Markdown 文件按 [standards/encoding.md](standards/encoding.md) 使用 UTF-8 without BOM。
- 维护脚本入口记录在 [workflow/build.md](workflow/build.md)；新增 `tools/` 脚本时必须同步工具用途、默认统计范围或门禁语义。

## 清理记录

- `full-diagnosis-2026-05-05.md` 是一次性诊断快照，结论已合并到 [architecture/flow.md](architecture/flow.md)、[workflow/review.md](workflow/review.md) 和 [planning/next-development-plan.md](planning/next-development-plan.md)，不再单独维护。
