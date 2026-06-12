# Asharia Engine 文档索引

本目录是项目知识入口。根目录 `README.md` 面向快速上手；这里只保留稳定入口、维护规则和当前事实来源，历史进度放在 GitHub Issues / Project。

## 主要入口

| 主题 | 先读 |
| --- | --- |
| 构建、门禁和日常 workflow | [workflow/build.md](workflow/build.md)、[workflow/review.md](workflow/review.md)、[workflow/technical-stack.md](workflow/technical-stack.md) |
| 编码和文本规则 | [standards/coding.md](standards/coding.md)、[standards/encoding.md](standards/encoding.md)、[standards/naming.md](standards/naming.md) |
| 当前架构和真实数据流 | [architecture/overview.md](architecture/overview.md)、[architecture/flow.md](architecture/flow.md)、[architecture/package-first.md](architecture/package-first.md) |
| RenderGraph / RHI 边界 | [rendergraph/mvp.md](rendergraph/mvp.md)、[rendergraph/rhi-boundary.md](rendergraph/rhi-boundary.md)、[rendergraph/roadmap.md](rendergraph/roadmap.md) |
| Editor 架构和 UI | [architecture/editor.md](architecture/editor.md)、[architecture/editor-ui-style-v1.md](architecture/editor-ui-style-v1.md)、[architecture/editor-ui-visual-target.md](architecture/editor-ui-visual-target.md)、[planning/editor-development-plan.md](planning/editor-development-plan.md) |
| 系统设计 | [systems/reflection-serialization.md](systems/reflection-serialization.md)、[systems/asset-architecture.md](systems/asset-architecture.md)、[systems/shader-material-authoring.md](systems/shader-material-authoring.md)、[systems/scene-world.md](systems/scene-world.md)、[systems/scripting.md](systems/scripting.md)、[systems/performance-profiling.md](systems/performance-profiling.md) |
| 计划和项目管理 | [planning/next-development-plan.md](planning/next-development-plan.md)、[planning/project-management.md](planning/project-management.md) |
| 研究资料 | [research/sources.md](research/sources.md) |

## 阅读顺序

1. [workflow/build.md](workflow/build.md) - bootstrap、configure、build 和 preset。
2. [workflow/review.md](workflow/review.md) - 提交前门禁和 smoke 清单。
3. [standards/encoding.md](standards/encoding.md) - BOM / UTF-8 规则。
4. [architecture/overview.md](architecture/overview.md) - 模块边界、所有权和生命周期。
5. [architecture/flow.md](architecture/flow.md) - 当前包依赖、启动、frame loop 和 RenderGraph/RHI 数据流。
6. [rendergraph/rhi-boundary.md](rendergraph/rhi-boundary.md) - RenderGraph 与 Vulkan backend 的职责边界。
7. [architecture/editor.md](architecture/editor.md) - 当前 editor shell、panel、viewport 和验证入口。
8. [planning/next-development-plan.md](planning/next-development-plan.md) - 下一阶段主路线。

专项开发时再读对应目录下的细化文档，不把专项文档当成第二套路线路线图。

## 当前事实来源

- 真实架构和 frame flow 以 [architecture/flow.md](architecture/flow.md) 为准。
- package 边界以 [architecture/package-first.md](architecture/package-first.md) 和 CMake target 为准。
- 提交门禁以 [workflow/review.md](workflow/review.md) 为准。
- 全局阶段顺序以 [planning/next-development-plan.md](planning/next-development-plan.md) 为准。
- GitHub Project、Issue、PR、Epic/Slice、blocker 和 Done evidence 规则以 [planning/project-management.md](planning/project-management.md) 为准。

## 文档维护规则

- 本地文档保留当前合同、入口、门禁和设计依据；不要长期维护进度流水账。
- 已完成阶段、历史风险、跨 PR 清理项和状态同步写到 GitHub Issues / Project，优先同步到 #20 `[Epic] Workflow: roadmap, docs, and Project sync`。
- 新增 smoke、包依赖、frame loop、RenderGraph 语义或 Vulkan 生命周期变化时，同步更新事实来源文档和 [workflow/review.md](workflow/review.md)。
- Markdown 文件使用 UTF-8 without BOM；验证入口是 `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`。

## 历史资料

- `full-diagnosis-2026-05-05.md` 是一次性诊断快照，结论已合并到当前架构、review 和计划文档，不再维护。
- [architecture/architecture-review-2026-05-23.md](architecture/architecture-review-2026-05-23.md) 和 [planning/issues-and-solutions.md](planning/issues-and-solutions.md) 仅保留历史审查语境；当前执行顺序仍以 [planning/next-development-plan.md](planning/next-development-plan.md) 和 GitHub Project 为准。
