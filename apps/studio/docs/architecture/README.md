# Studio 架构文档索引

状态：Current

更新日期：2026-08-12

本目录是 `apps/studio` 的正式框架技术文档入口。目标读者是修改 Studio 框架、编辑器功能、native bridge、Viewport、Play Mode 或扩展系统的开发者。

## 权威性规则

Studio 文档分为三类，不能混用：

| 文档类型 | 回答的问题 | 权威性 |
| --- | --- | --- |
| Architecture | 稳定目标合同、所有权和依赖方向是什么 | 目标架构的权威来源 |
| ADR | 为什么选择这项不可逆或高成本决策 | 决策理由的权威来源 |
| Current guide | 当前源码实际上如何工作 | 当前实现说明；必须与源码和测试一致 |

当文档与源码不一致时：

1. “当前是否已实现”以源码、项目引用和测试为准。
2. “最终应该怎样设计”以本目录和 `docs/adr` 为准。
3. 发现偏差后应更新迁移状态，不得把未实现目标描述为 Current。

## 状态词

| 状态 | 含义 |
| --- | --- |
| Current | 已由当前源码执行，可作为实现事实 |
| Partial | 已有一部分生产路径，但合同或闭环不完整 |
| Target | 已批准的目标合同，仍处于迁移中 |
| Experimental | 只用于验证技术可行性，不是生产合同 |
| Superseded | 已被新文档替代，仅保留历史背景 |
| Historical | 记录过去的设计或实施过程 |

## 架构文档

- [Studio 前端硬切架构](studio-frontend-hard-cut.md)：当前权威目标；Document-first 分层、owner/lifetime、
  数据结构、设计模式、native 问题账本、删除范围和实施门禁。
- [Studio native boundary 审查](studio-native-boundary-audit.md)：当前 managed/native 风险、触发条件、
  hard-cut C ABI、数据布局与独立验证门禁。
- [Studio 开发态可观测性与诊断访问](studio-development-observability.md)：无 Avalonia Plus 前提下的本机只读
  状态/诊断/UI Probe、CLI/MCP adapter、权限、预算与分阶段接入门禁。
- [Studio 生产工作台体验规范](studio-workbench-experience.md)：默认信息架构、selection/focus/state、反馈层级和首批前端实现切片。
- [Studio 生命周期](studio-lifecycle.md)：应用、Project、Engine、Window、Panel、任务和关闭顺序。
- [编辑世界与 Play Mode](editor-worlds-and-play-mode.md)：Edit/Play/Preview World、事务和三种 Play presentation。
- [Viewport 渲染架构](viewport-rendering.md)：多 Viewport、调度、跨平台 GPU 共享和 frame lease。

以下文档已由 ADR-0007 取代，只保留历史设计和代码审查证据：

- [旧 Studio 架构总览](studio-overview.md)
- [旧 Studio 前端框架](studio-frontend-framework.md)
- [旧 Studio 代码框架设计](studio-code-framework.md)
- [旧 Studio 统一扩展模型](studio-extension-model.md)
- [旧 Editor 扩展开发模型](editor-extension-authoring.md)
- [旧 Editor 扩展构建、装载与重载](editor-extension-build-and-reload.md)
- [旧 Avalonia/XAML Editor 扩展规范](editor-extension-avalonia.md)

## ADR

- [ADR-0001：采用同进程模块化 EngineHost](../adr/0001-in-process-engine-host.md)
- [ADR-0002：采用跨平台共享图像的嵌入式 Viewport](../adr/0002-cross-platform-viewport-presentation.md)
- [ADR-0003：用六个项目建立编译期边界](../adr/0003-studio-project-boundaries.md)（Superseded）
- [ADR-0004：采用统一 Editor Extension Framework](../adr/0004-unified-editor-extension-framework.md)（Superseded）
- [ADR-0005：采用隔离构建、generation reload 与 last-known-good](../adr/0005-managed-editor-module-build-and-reload.md)（Superseded for Studio v1）
- [ADR-0006：面板交互 Resize 采用连续呈现与最新尺寸收敛](../adr/0006-viewport-interactive-resize.md)
- [ADR-0007：Studio 前端采用 Document-first 硬切重构](../adr/0007-studio-frontend-hard-cut.md)
- [ADR-0011：Studio shared viewport 由 native 进程级 RenderThread 拥有](../adr/0011-native-shared-viewport-render-thread.md)
- [ADR-0012：Viewport 显式保持水平或垂直 FOV](../adr/0012-viewport-field-of-view-axis.md)
- [ADR-0013：Studio 采用 authoritative document Transform Undo/Redo 与逻辑保存点](../adr/0013-authoritative-document-transform-undo-redo.md)

## Current guides

- 当前R0没有独立的UI平台/控件/Dock current guide；实现事实以
  [Studio前端硬切架构](studio-frontend-hard-cut.md)的current owner cards、production source与自动化门禁为准。
- 通用编码、构建、编码格式与合入门禁以仓库`AGENTS.md`、
  [`architecture-health.md`](../../../../docs/workflow/architecture-health.md)和
  [`review.md`](../../../../docs/workflow/review.md)为准。

## Historical guides

- [项目规范](../项目规范.md)、[编辑器 UI 平台规范](../编辑器UI平台规范.md)、
  [颜色 Token 指南](../颜色Token指南.md)、[控件开发指南](../控件开发指南.md)和
  [编辑器 UI 能力目录](../编辑器UI组件.md)：旧UI平台/目录/能力规划记录；不是当前实现或接入顺序。
- [Code-first UI 设计](../Code-first%20UI设计.md)：已删除方案的审计记录。
- [Dock 系统指南](../Dock系统指南.md)与[Dock 手工回归清单](../Dock手工回归清单.md)：已删除Dock实现的历史行为与手工清单。

已被当前 Architecture/ADR 吸收的 v0 总纲、日期化 spec 和 agent execution plan 不在活动文档树保留；
需要审计历史时使用 Git、GitHub Issues 和 PR。

## 维护要求

以下变化必须同步更新对应文档和 ADR：

- 项目引用或模块边界变化；
- Engine/World/Viewport 所有权变化；
- frame lease、GPU handle 或同步语义变化；
- Play Mode world copy/load、selection remap 或应用变更规则变化；
- Feature、Action、Document、Selection、transaction 或 panel 生命周期变化；
- public Editor SDK 被重新提出或出现第二个真实外部 consumer；
- extension build/ALC/generation/last-known-good 被重新提出；
- 启动、关闭、device lost 或 standalone process 流程变化。
- 开发态 observation capability、协议、权限、预算、artifact 或 Release closure 变化。
