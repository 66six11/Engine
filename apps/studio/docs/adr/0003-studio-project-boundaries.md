# ADR-0003：用六个项目建立 Studio 编译期边界

状态：Superseded by [ADR-0004](0004-unified-editor-extension-framework.md)

日期：2026-07-11

## Context

> 本 ADR 保留“使用编译期 project reference 强制边界”的理由。六项目目标清单已被 ADR-0004 的公共 Editor Framework 与 built-in dogfooding 边界取代。

当前 Studio 是单一 `Editor.csproj`。Core/Shell/UI/Features 边界主要由目录、命名空间、文档和源码字符串测试维护。当前 `Core` 已同时包含 UI-neutral contract、service、P/Invoke、native adapter 和 Avalonia vocabulary，说明目录规则不足以阻止反向依赖。

候选方案：

- 保持单项目，只增强 architecture tests；
- 每个 Feature 都立即拆成独立 assembly；
- 先按稳定技术边界拆成少量项目，Feature 继续垂直组织。

## Decision

采用第三种方案，目标项目为：

```text
Asharia.Studio.Contracts
Asharia.Studio.Application
Asharia.Studio.EngineInterop
Asharia.Studio.EngineBridge
Asharia.Studio.Presentation.Avalonia
Asharia.Studio.App
```

`EngineInterop` 是必要的窄边界。没有它，`ViewportFrameLease` 和 platform GPU descriptors 会污染 general Contracts/Application，或让 Presentation 依赖具体 native bridge implementation。

Feature 暂不逐个拆 assembly；等独立发布、加载、编译隔离或真实插件需求出现后再决定。

## Alternatives

### Single project plus tests

Rejected as target。测试可以检测一部分路径，但不能像 project reference 一样阻止编译期逆向依赖；源码字符串测试也容易固化错误目录。

### Assembly per feature immediately

Rejected for now。会快速增加项目数量、composition 和 shared UI contract 成本，且当前没有外部发布/加载需求证明收益。

## Consequences

Positive：

- 编译器强制依赖方向；
- Application 可在无 Avalonia/native runtime 环境测试；
- EngineBridge 与 Presentation 通过窄 interop protocol 接触；
- Native AOT、trimming 和 plugin 边界更清晰。

Negative：

- 首次迁移会暴露隐藏 upward dependency；
- test project/reference 和 internal visibility 需要调整；
- 一段时间内需要 compatibility adapter；
- build/publish/native copy 配置必须重新归位。

## Follow-ups

- 先移动 pure contract，不进行行为重写。
- 建立 project-reference architecture tests。
- 将 `Core/Interop` 移到 EngineInterop/EngineBridge。
- 将 App/Shell/Avalonia presentation 从 Application contract 中剥离。
- 每个迁移阶段保持 solution 可构建和测试通过。

## Validation

- Contracts 的 project references 为空或仅允许基础 BCL。
- Application 只引用 Contracts。
- EngineInterop 只引用 Contracts。
- EngineBridge 引用 Application/Contracts/EngineInterop，不引用 Avalonia。
- Presentation 引用 Application/Contracts/EngineInterop，不引用 EngineBridge implementation。
- App 是 composition root，可引用所有 required adapters。
