# ADR-0004：采用统一 Editor Extension Framework

状态：Accepted

日期：2026-07-11

构建、加载、generation replacement 和 last-known-good 的独立决策见 [ADR-0005](0005-managed-editor-module-build-and-reload.md)。

取代：[ADR-0003：用六个项目建立 Studio 编译期边界](0003-studio-project-boundaries.md) 中的目标项目清单；ADR-0003 关于使用编译期边界而不是单项目目录约定的判断仍然成立。

## Context

Studio 已有 built-in `IEditorExtensionModule`、typed contribution registry 和 Code-first UI 垂直切片，但原设计仍把 built-in Feature、未来 project extension 和 packaged plugin 当成不同成熟阶段或不同 API 风险面处理。

游戏引擎编辑器需要同时满足：

- 用户可在游戏工程根 `Editor/` 中直接开发编辑器工具；
- 同一个 Package 可以包含 Runtime 与 Editor 部分；
- 内置功能与第三方工具共享 Panel、Inspector、Command、Viewport Tool 和 UI authoring；
- 复杂 UI 可以使用 Avalonia/XAML；
- Studio Shell、Dock、Window 和 native rendering ownership 不被扩展接管；
- Windows、Linux、macOS 使用同一 managed build/load 模型。

如果继续把“内部 Feature API”和“外部插件 API”分开，公共 API 不会被内置功能持续验证，两个模型会在能力和行为上漂移。反过来，直接把全部 Studio internal type 与 Avalonia Shell 暴露给插件，又会失去版本、所有权和故障隔离边界。

## Decision

采用一个统一的 Editor Extension Framework：

1. built-in、project `Editor/`、Package `Editor/` 和 installed plugin 都实现 `Asharia.Editor.EditorModule`；
2. Source kind 只影响发现、启用、缓存和 reload policy，不产生不同 authoring API；
3. Module scope 与 source 正交，显式分为 Application/Project；BuiltIn module 不自动获得 application-global lifetime；
4. Shell、Dock、platform Window、EngineHost 和 EngineBridge 是 host infrastructure，不属于普通 extension；
5. built-in Feature 放入 `Asharia.Studio.BuiltInExtensions`，该项目只引用公共 `Asharia.Editor` 和可选 `Asharia.Editor.Avalonia`；
6. Code-first 是默认 UI-neutral authoring；复杂 UI 可使用同一框架的可选 Avalonia/XAML bridge；
7. Avalonia extension 只能提供 host-owned panel/window content，不能创建 Studio top-level Window、修改 Dock 或持有 native rendering resource；
8. 项目根 `Editor/` 提供隐式 Editor assembly；可选 `*.asmdef` 定义显式 assembly；Package 复用仓库统一的 `asharia.package.json` identity，并增加可选 `editor` section；
9. Studio 将 `.asmdef` 转换为缓存 SDK project 并通过跨平台 `dotnet build` 构建；
10. Project/Package/Installed dynamic extension 按 policy 使用 dependency-aware collectible host（managed-reload eligible）或隔离、non-collectible pinned host（Tier-0/native/external-build restart-required）；App 直接引用的 BuiltIn assembly 只在 default ALC 加载一次，三者使用同一 EditorModule lifecycle；
11. 进程内 extension 明确是受信任代码；ALC 不是 security sandbox。

目标 production project 为：

```text
Asharia.Editor
Asharia.Editor.Avalonia
Asharia.Studio.Application
Asharia.Studio.EngineInterop
Asharia.Studio.EngineBridge
Asharia.Studio.Presentation.Avalonia
Asharia.Studio.BuiltInExtensions
Asharia.Studio.App
```

`Asharia.Editor` 是稳定公共 contract 与 Code-first API；`Asharia.Editor.Avalonia` 是版本周期更严格的可选 UI bridge。其余项目是 Studio host implementation。

## Alternatives

### Separate internal and external extension APIs

Rejected。会形成双重 Panel/Command/lifecycle 行为；外部 API 缺少内置功能 dogfooding，内部 Feature 会持续依赖不可发布的 service。

### Expose raw Studio/Avalonia internals as the only API

Rejected。开发快，但插件可直接修改 Dock/Window/global style/native lifetime，且公共兼容范围等同整个应用实现。

### Code-first only

Rejected。Inspector 和小工具适合 Code-first，但 Graph、Timeline、Asset Browser、Profiler 等复杂 UI 需要成熟的 templating、binding、virtualization 和 custom control 能力。

### Avalonia/XAML only

Rejected。会使简单工具样板过多，并迫使全部 extension 绑定更严格的 Avalonia version band。

### Use arbitrary `.csproj` as the default plugin definition

Rejected。MSBuild 项目是构建机制，不应同时隐式承担 extension identity、module lifecycle 和 contribution schema；任意 target 还会降低可重复性。高级受信任 Package 可以显式选择 external build project。

### Continue with six internal Studio projects only

Rejected as complete target。它可以隔离 Application/Bridge/Presentation，却没有公共 Editor API 和 built-in dogfooding boundary，不能支撑项目 `Editor/` 与 Package extension。

## Consequences

Positive：

- 一套 API 覆盖内置、项目和分发扩展；
- public contract 由 built-in Feature 持续验证；
- Code-first 与 Avalonia 共享 contribution、command、state 和 lifecycle；
- Editor code 与 Player/Runtime build 有明确 assembly boundary；
- build/load/reload 可在 Windows、Linux、macOS 复用；
- Shell/native ownership 不因允许复杂 XAML 而泄漏。

Negative：

- 从当前单项目提取公共 API 和 built-in assembly 的迁移成本较高；
- `Asharia.Editor.Avalonia` 不能承诺与 UI-neutral API 相同的兼容周期；
- collectible ALC 卸载是 cooperative，复杂 XAML/native extension 仍可能要求重启；
- `.asmdef` converter、Package resolver、build cache、diagnostics 和 integration fixtures 都需要正式实现；
- in-process trusted extension 不能提供恶意代码隔离。

## Follow-ups

- 提取 `Asharia.Editor` 最小 contract，不同时重写行为；
- 将现有 Code-first Core contract 移入公共 Editor assembly；
- 建立 BuiltInExtensions 依赖门禁并逐个迁移 Feature module；
- 设计并版本化 `.asmdef`、Package schema 和 generated project；
- 实现 Code-first 与 Avalonia UI backend host；
- 实现 project `Editor/` discovery、build diagnostics 和 last-known-good；
- 最后增加 ALC reload，先通过 negative leak fixture 再默认启用。

## Validation

- BuiltInExtensions 的 project reference 只包含 Editor/Editor.Avalonia；
- public Editor API 不引用 Studio host implementation；
- 三种来源的 fixture module 通过同一 contract 注册并产生一致行为；
- Avalonia content 不能取得 Dock/Window/native surface ownership；
- `.asmdef` dependency/RID/cycle 和 Package compatibility 可验证；
- build/reload failure 不破坏 last-known-good workspace；
- ALC leak、state migration 和 restart-required 有可重复测试；
- Windows、Linux、macOS 使用相同 schema 与 `dotnet build` pipeline。
