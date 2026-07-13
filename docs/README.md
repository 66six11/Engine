# Asharia Engine 文档索引

本目录是仓库唯一的工程文档源。这里保留当前合同、长期设计依据、可执行工作流和目标架构；
实际进度、阻塞、负责人及 Done evidence 只维护在 GitHub Issues / Project。

## 权威范围

| 范围 | 权威文档 | 状态 |
| --- | --- | --- |
| 当前 target graph、启动和 frame flow | [architecture/flow.md](architecture/flow.md) | current |
| 当前模块边界、所有权和生命周期 | [architecture/overview.md](architecture/overview.md) | current |
| package、manifest 与 CMake 约定 | [architecture/package-first.md](architecture/package-first.md) | current |
| 构建与提交门禁 | [workflow/build.md](workflow/build.md)、[workflow/review.md](workflow/review.md) | current |
| GitHub Project、Epic、Slice 与 Done evidence | [planning/project-management.md](planning/project-management.md) | current |
| 目标基础框架 | [architecture/foundation-framework.md](architecture/foundation-framework.md) | proposal |
| 目标系统框架与迁移门禁 | [planning/system-architecture-roadmap.md](planning/system-architecture-roadmap.md) | proposal |
| 近期功能阶段顺序 | [planning/next-development-plan.md](planning/next-development-plan.md) | plan |

代码、CMake target 或 manifest 与文档冲突时，先确认真实行为，再修正文档或实现。proposal/plan
不得被引用为已落地能力。

## 按主题阅读

| 主题 | 文档 |
| --- | --- |
| 架构原则与 package 边界 | [architecture/architecture-principles.md](architecture/architecture-principles.md)、[architecture/package-first.md](architecture/package-first.md)、[Installable Package Manifest v2 ADR](architecture/adr-installable-package-manifest-v2.md)、[Project Package Manifest v1 ADR](architecture/adr-project-package-manifest-v1.md)、[Package Candidate / Lockfile v1 ADR](architecture/adr-package-candidate-lockfile-v1.md)、[Package Candidate Discovery v1 ADR](architecture/adr-package-candidate-discovery-v1.md)、[Package Resolver v1 ADR](architecture/adr-package-resolver-v1.md)、[Locked Graph Verification & Reuse v1 ADR](architecture/adr-package-lock-verification-v1.md)、[Host Profile v1 ADR](architecture/adr-host-profile-v1.md)、[Host Composition Plan v1 ADR](architecture/adr-host-composition-plan-v1.md)、[Source Build Plan v1 ADR](architecture/adr-source-build-plan-v1.md)、[Package Product & Artifact Evidence v1 ADR](architecture/adr-package-product-artifact-evidence-v1.md) |
| Runtime、线程与 frame loop | [architecture/frame-loop-threading.md](architecture/frame-loop-threading.md)、[architecture/flow.md](architecture/flow.md) |
| RenderGraph、RHI 与 renderer | [rendergraph/mvp.md](rendergraph/mvp.md)、[rendergraph/rhi-boundary.md](rendergraph/rhi-boundary.md)、[architecture/render-layer.md](architecture/render-layer.md) |
| 可编程渲染管线 | [rendergraph/programmable-pipeline.md](rendergraph/programmable-pipeline.md) |
| Editor 与扩展 | [architecture/editor.md](architecture/editor.md)、[architecture/editor-ui-scripting.md](architecture/editor-ui-scripting.md)、[architecture/managed-extension-model.md](architecture/managed-extension-model.md) |
| Editor UI 规范 | [architecture/editor-ui-style-v1.md](architecture/editor-ui-style-v1.md)、[architecture/editor-ui-visual-target.md](architecture/editor-ui-visual-target.md) |
| Studio 实现细节 | [apps/studio 架构入口](../apps/studio/docs/architecture/README.md) |
| 项目 build/cook/package/launch | [architecture/project-build-and-launch.md](architecture/project-build-and-launch.md) |
| Asset 与 runtime resource | [systems/asset-architecture.md](systems/asset-architecture.md)、[resource-runtime package 设计](../packages/resource-runtime/README.md) |
| Scene / World | [systems/scene-world.md](systems/scene-world.md) |
| Schema、反射与持久化 | [systems/reflection-serialization.md](systems/reflection-serialization.md) |
| Shader / Material | [systems/shader-material-authoring.md](systems/shader-material-authoring.md)、[specs/ashader-v2.md](specs/ashader-v2.md)、[specs/material-runtime-products-v2.md](specs/material-runtime-products-v2.md) |
| Scripting 与 graph authoring | [systems/scripting.md](systems/scripting.md)、[systems/graph-csharp-blueprint.md](systems/graph-csharp-blueprint.md) |
| Profiling | [systems/performance-profiling.md](systems/performance-profiling.md) |
| 技术栈与独立 package 构建 | [workflow/technical-stack.md](workflow/technical-stack.md)、[workflow/package-standalone-build.md](workflow/package-standalone-build.md) |
| 文档写作与发布 | [standards/documentation.md](standards/documentation.md)、[workflow/documentation-site.md](workflow/documentation-site.md) |
| 外部资料 | [research/sources.md](research/sources.md) |

## 推荐阅读顺序

1. [workflow/build.md](workflow/build.md)
2. [workflow/review.md](workflow/review.md)
3. [architecture/overview.md](architecture/overview.md)
4. [architecture/flow.md](architecture/flow.md)
5. [architecture/package-first.md](architecture/package-first.md)
6. [architecture/foundation-framework.md](architecture/foundation-framework.md)
7. [planning/system-architecture-roadmap.md](planning/system-architecture-roadmap.md)

专项开发再进入对应系统、规格或工作流文档。不要把专项设计当成第二套全局路线图。

## 维护规则

- 一个主题只保留一个当前事实源；其他文档通过链接引用，不复制正文。
- 当前事实先写，planned/proposal/future 内容必须单独标注。
- PR 状态、完成百分比、临时 blocker 和逐提交日志不进入长期文档。
- 已完成且不再提供合同价值的计划、审查快照和 agent 执行清单直接删除；历史由 Git 与 GitHub Issues 保留。
- 文档站从整个 `docs/` 单向同步，发布侧负责导航、翻译和公开筛选，不在仓库内维护平行正文树。
- Markdown 使用 UTF-8 without BOM。最低验证为
  `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1` 和 `git diff --check`。
