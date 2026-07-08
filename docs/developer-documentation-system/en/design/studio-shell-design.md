# Detailed Design: Avalonia Studio Shell And Native Interop

## Background

`apps/studio` is the Avalonia/.NET editor shell. It provides workbench, panels, commands, diagnostics, selection, CodeFirst UI, native viewport bridge, and frame debugger bridge. It interacts with native runtime through C# abstractions and does not directly own the Vulkan backend.

## Goals

- Use Core abstractions to define editor services and contribution contracts.
- Use Shell/Features to organize Avalonia UI panels and workflows.
- Use CodeFirst UI model to describe a validatable UI tree.
- Use native interop adapters to access C++ editor-native viewport/frame debugger ABI.
- Use tests to cover panel lifecycle, transactions, viewport native bridge, and frame debugger models.

## Non-Goals

- Do not create Vulkan devices in the C# shell.
- Do not copy C++ editor panel state into long-lived truth.
- Do not let Avalonia ViewModels directly own native handle lifetime.
- Do not execute asset importer internals in Studio.

## Current Constraints

- Studio uses `apps/studio/Editor.sln`.
- Studio targets `net10.0` and uses Avalonia, CommunityToolkit.Mvvm, and Lucide.Avalonia.
- Core abstractions live under `apps/studio/Core/Abstractions/`.
- Native interop lives under `apps/studio/Core/Interop/`.
- Tests live under `apps/studio/Tests/`.
- Native viewport ABI must check ABI version, struct size, device identity, and handle type.

## Overall Design

Studio shell expresses editor capabilities through dependency-injected services. `EditorExtensionHost` v0 composes trusted built-in modules. Modules currently declare panel, action, and scene-provider contributions through `IEditorContributionBuilder`; diagnostic-source and lifecycle contribution points are not implemented as contribution declarations. Panel lifecycle is handled by activation leases and panel content sink interfaces.

The native viewport bridge calls the C ABI for compatibility and then acquires present packets. The C# side only owns managed wrappers for releasable packet/handle data, and release must call the matching native release function. Studio Frame Debugger v0 is read-only and fixture-backed by default; native frame-debugger bridge and JSON projection exist as injectable services, but Workbench does not use them as the default provider yet.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `apps/studio/Core/Abstractions/` | editor services, modules, transactions, panel lifecycle interfaces |
| `apps/studio/Core/Models/` | diagnostics, panels, frame debug, scene, selection, viewport models |
| `apps/studio/Core/CodeFirstUI/` | code-first UI tree authoring/building/validation |
| `apps/studio/Core/Interop/Viewports/` | viewport native API/adapters/present drain |
| `apps/studio/Core/Interop/FrameDebugger/` | frame debugger native API/adapters |
| `apps/studio/Shell/` | Avalonia shell composition |
| `apps/studio/Features/` | feature modules and panels |
| `apps/studio/Tests/` | unit/integration tests for shell contracts |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `EditorPanelContributionDescriptor` | panel id, title, dock/content model | panel contribution contract |
| `EditorEditCommandDescriptor` | command id, merge policy, validation | transaction command contract |
| `GuiTreeSnapshot` | node tree, payloads, validation state | CodeFirst UI frame model |
| `ViewportNativeCompatibilityRequest/Result` | ABI header, handle types, device identity, status | native compatibility contract |
| `FrameDebuggerSnapshot` | passes, resources, transitions, events | managed frame debug model |

## API Design

- Feature modules implement registration interfaces and expose panel/action/scene-provider capabilities through abstractions.
- Transaction service exists for persistent mutations and is tested; default Workbench feature panels are not broadly wired to it yet.
- Native adapters wrap C ABI calls and convert status into managed result objects.
- CodeFirst UI validator rejects malformed node trees before rendering.

## Key Flows

### Normal Flow

1. Shell starts services and modules.
2. Feature modules register panel/action contributions.
3. Workbench creates panels from descriptors.
4. Panels read snapshots from services.
5. User edit flows may use transactions where explicitly wired.
6. Viewport panel queries native compatibility and acquires present packets.
7. Frame debugger panel displays snapshot model.

### Failure Flow

- Contribution validation failure: module registration reports diagnostics.
- Invalid edit command: transaction service rejects before mutation.
- Invalid UI tree: CodeFirst validator reports errors.
- Native ABI incompatible: adapter reports unsupported ABI/device/handle status.
- Native packet release failure path: viewport packet release calls the native release API and then drops managed ownership; it does not currently log a diagnostic.

### Boundary Flow

- C# shell owns UI state and service orchestration.
- C++ native runtime owns Vulkan resources and exported packet lifetime.
- Native handles must not outlive their packet/release contract.

## Lifetime

Services live for shell lifetime. Panels are created/destroyed by workbench lifecycle. CodeFirst UI snapshots are frame/update values. Native packets are acquired and explicitly released. Background tasks publish snapshots and diagnostics instead of blocking the UI thread.

## Error Handling

Managed services should return validation results or diagnostics instead of throwing through UI event handlers. Native interop maps status enums to diagnostics with ABI/device details. UI dispatcher boundaries must marshal UI updates to the Avalonia thread.

## Test Plan

```powershell
dotnet build apps\studio\Editor.csproj -c Release
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
dotnet test apps\studio\Editor.sln -c Release
```

On Windows, build the native preset that provides `editor_native.dll` and `slang.dll` first, or pass `/p:StudioNativeBuildPreset=<preset>`.

## Risks

- ViewModels directly owning native handles can leak or use after release. Mitigation: adapter-owned release lifecycle.
- A broad service locator makes feature modules hard to test. Mitigation: capability-specific abstractions.
- UI tree model and rendered UI can drift. Mitigation: CodeFirst validation and focused tests.
