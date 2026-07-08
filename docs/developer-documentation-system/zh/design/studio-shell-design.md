# 详细设计：Avalonia Studio Shell 与 Native Interop

## 背景

`apps/studio` 是 Avalonia/.NET editor shell。它提供 workbench、panels、commands、diagnostics、selection、CodeFirst UI、native viewport bridge 和 frame debugger bridge。它通过 C# abstractions 与 native runtime 交互，不直接拥有 Vulkan backend。

## 目标

- 用 Core abstractions 定义 editor services 和 contribution contracts。
- 用 Shell/Features 组织 Avalonia UI panels 和 workflows。
- 用 CodeFirst UI model 描述可验证的 UI tree。
- 用 native interop adapters 访问 C++ editor-native viewport/frame debugger ABI。
- 用 tests 覆盖 panel lifecycle、transactions、viewport native bridge 和 frame debugger models。

## 非目标

- 不在 C# shell 中创建 Vulkan device。
- 不把 C++ editor panel state 复制成长期 truth。
- 不让 Avalonia ViewModel 直接持有 native handles lifetime。
- 不在 Studio 中执行 asset importer internals。

## 当前约束

- Studio 使用 `apps/studio/Editor.sln`。
- Studio target 是 `net10.0`，使用 Avalonia、CommunityToolkit.Mvvm 和 Lucide.Avalonia。
- Core abstractions 位于 `apps/studio/Core/Abstractions/`。
- Native interop 位于 `apps/studio/Core/Interop/`。
- Tests 位于 `apps/studio/Tests/`。
- Native viewport ABI 必须检查 abi version、struct size、device identity 和 handle type。

## 总体方案

Studio shell 用 dependency-injected services 表达 editor 能力。`EditorExtensionHost` v0 只组合受信任的内置 modules。Module 当前通过 `IEditorContributionBuilder` 声明 panel、action 和 scene-provider contributions；diagnostic-source 与 lifecycle contribution point 尚未作为 contribution declaration 实现。Panel lifecycle 由 activation lease 和 panel content sink interfaces 处理。

Native viewport bridge 通过 C ABI 请求 compatibility，然后 acquire present packet。C# 侧只持有可释放 packet/handle 的托管 wrapper，释放必须回到 native release function。Studio Frame Debugger v0 默认是 fixture-backed 的只读面板；native frame-debugger bridge 和 JSON projection 已作为可注入服务存在，但 Workbench 默认 provider 尚未接入它们。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `apps/studio/Core/Abstractions/` | editor services、modules、transactions、panel lifecycle interfaces |
| `apps/studio/Core/Models/` | diagnostics、panels、frame debug、scene、selection、viewport models |
| `apps/studio/Core/CodeFirstUI/` | code-first UI tree authoring/building/validation |
| `apps/studio/Core/Interop/Viewports/` | viewport native API/adapters/present drain |
| `apps/studio/Core/Interop/FrameDebugger/` | frame debugger native API/adapters |
| `apps/studio/Shell/` | Avalonia shell composition |
| `apps/studio/Features/` | feature modules and panels |
| `apps/studio/Tests/` | unit/integration tests for shell contracts |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `EditorPanelContributionDescriptor` | panel id、title、dock/content model | panel contribution contract |
| `EditorEditCommandDescriptor` | command id、merge policy、validation | transaction command contract |
| `GuiTreeSnapshot` | node tree、payloads、validation state | CodeFirst UI frame model |
| `ViewportNativeCompatibilityRequest/Result` | ABI header、handle types、device identity、status | native compatibility contract |
| `FrameDebuggerSnapshot` | passes、resources、transitions、events | managed frame debug model |

## API 设计

- Feature modules implement registration interfaces，并通过 abstractions 暴露 panel/action/scene-provider capabilities。
- Transaction service 用于 persistent mutations 且已有测试；默认 Workbench feature panels 尚未广泛接入它。
- Native adapters wrap C ABI calls and convert status into managed result objects.
- CodeFirst UI validator rejects malformed node trees before rendering.

## 关键流程

### 正常流程

1. Shell starts services and modules.
2. Feature modules register panel/action contributions.
3. Workbench creates panels from descriptors.
4. Panels read snapshots from services.
5. User edit flows 在显式接入时可以使用 transactions。
6. Viewport panel queries native compatibility and acquires present packets.
7. Frame debugger panel displays snapshot model.

### 失败流程

- contribution validation failure：module registration reports diagnostics。
- invalid edit command：transaction service rejects before mutation。
- UI tree invalid：CodeFirst validator reports errors。
- native ABI incompatible：adapter reports unsupported ABI/device/handle status。
- native packet release failure path：viewport packet release 调用 native release API，然后放弃 managed ownership；当前不记录 diagnostic。

### 边界流程

- C# shell owns UI state and service orchestration。
- C++ native runtime owns Vulkan resources and exported packet lifetime。
- Native handles must not outlive their packet/release contract。

## 生命周期

Services live for shell lifetime. Panels are created/destroyed by workbench lifecycle. CodeFirst UI snapshots are frame/update values. Native packets are acquired and released explicitly. Background tasks publish snapshots and diagnostics instead of blocking UI thread.

## 错误处理

Managed services should return validation results or diagnostics instead of throwing through UI event handlers. Native interop maps status enums to diagnostics with ABI/device details. UI dispatcher boundaries must marshal UI updates to Avalonia thread.

## 测试方案

```powershell
dotnet build apps\studio\Editor.csproj -c Release
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
dotnet test apps\studio\Editor.sln -c Release
```

Windows 上先构建提供 `editor_native.dll` 和 `slang.dll` 的 native preset，或传入 `/p:StudioNativeBuildPreset=<preset>`。

## 风险

- ViewModel 直接 owning native handles 会 leak 或 use-after-release。缓解：adapter-owned release lifecycle。
- Service locator 过宽会让 feature modules 难测。缓解：capability-specific abstractions。
- UI tree model 和 rendered UI drift。缓解：CodeFirst validation 和 focused tests。
