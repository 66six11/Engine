# Studio 架构文档索引

状态：Current

更新日期：2026-07-13

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

- [Studio 架构总览](studio-overview.md)：目标分层、依赖方向、所有权和迁移边界。
- [Studio 代码框架设计](studio-code-framework.md)：solution/project、目录、命名空间、关键接口、测试和迁移映射。
- [Editor 扩展开发模型](editor-extension-authoring.md)：项目 `Editor/`、`.asmdef`、Package、Code-first/Avalonia、构建和重载。
- [Editor 扩展构建、装载与重载](editor-extension-build-and-reload.md)：generated project、Package lock、ALC、generation 与 last-known-good。
- [Avalonia/XAML Editor 扩展规范](editor-extension-avalonia.md)：content lease、资源/样式、Host ownership 和 reload tier。
- [Studio 生命周期](studio-lifecycle.md)：应用、Project、Engine、Window、Panel、任务和关闭顺序。
- [编辑世界与 Play Mode](editor-worlds-and-play-mode.md)：Edit/Play/Preview World、事务和三种 Play presentation。
- [Viewport 渲染架构](viewport-rendering.md)：多 Viewport、调度、跨平台 GPU 共享和 frame lease。
- [Studio 统一扩展模型](studio-extension-model.md)：built-in/project/Package/plugin 的统一 module、contribution、生命周期和隔离。

## ADR

- [ADR-0001：采用同进程模块化 EngineHost](../adr/0001-in-process-engine-host.md)
- [ADR-0002：采用跨平台共享图像的嵌入式 Viewport](../adr/0002-cross-platform-viewport-presentation.md)
- [ADR-0003：用六个项目建立编译期边界](../adr/0003-studio-project-boundaries.md)（Superseded）
- [ADR-0004：采用统一 Editor Extension Framework](../adr/0004-unified-editor-extension-framework.md)
- [ADR-0005：采用隔离构建、generation reload 与 last-known-good](../adr/0005-managed-editor-module-build-and-reload.md)

## Current guides

- [项目规范](../项目规范.md)：通用编码、MVVM、性能和合入规则。
- [编辑器 UI 平台规范](../编辑器UI平台规范.md)：Partial；只约束仍成立的 Dialog、Command、Shortcut、
  Background Task、样式和 Shell 行为。
- [Code-first UI 设计](../Code-first%20UI设计.md)：Code-first authoring 与 Host ownership。
- [Dock 系统指南](../Dock系统指南.md)：当前 Dock 结构和交互事实。
- [Dock 手工回归清单](../Dock手工回归清单.md)：UI-sensitive 合并前检查。

已被当前 Architecture/ADR 吸收的 v0 总纲、日期化 spec 和 agent execution plan 不在活动文档树保留；
需要审计历史时使用 Git、GitHub Issues 和 PR。

## 维护要求

以下变化必须同步更新对应文档和 ADR：

- 项目引用或模块边界变化；
- Engine/World/Viewport 所有权变化；
- frame lease、GPU handle 或同步语义变化；
- Play Mode world copy/load、selection remap 或应用变更规则变化；
- Feature contribution、plugin、provider 或 panel 生命周期变化；
- `Asharia.Editor` public API、module scope 或 compatibility policy 变化；
- `.asmdef`、`asharia.package.json.editor`、Package lock 或 generated project 变化；
- extension build/ALC/generation/last-known-good 或 Avalonia reload tier 变化；
- 启动、关闭、device lost 或 standalone process 流程变化。
