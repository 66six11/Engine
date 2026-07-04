# Studio Frame Debugger Snapshot v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Studio-managed read-only Frame Debugger snapshot v0 without native ABI or renderer interop.

**Architecture:** Core owns immutable Frame Debug snapshot records and an `IFrameDebuggerSnapshotProvider` contract. `Features/FrameDebugger` owns a Code-first panel that reads provider snapshots and publishes diagnostics for unconnected capture/resume actions. Workbench registers a fixture-backed provider and panel so the UI can be validated before native adapter work.

**Tech Stack:** .NET 10, C#, xUnit, Avalonia 12.0.4, existing Studio Code-first UI framework, existing Workbench feature registration.

---

## Files

- Create: `Core/Models/FrameDebug/FrameDebuggerState.cs`
- Create: `Core/Models/FrameDebug/FrameDebugCaptureSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugPassSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugCommandSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugResourceSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugAccessEdgeSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugDependencyEdgeSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugTransitionSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugExecutionEventSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebugPreviewSnapshot.cs`
- Create: `Core/Models/FrameDebug/FrameDebuggerSnapshot.cs`
- Create: `Core/Abstractions/IFrameDebuggerSnapshotProvider.cs`
- Create: `Core/Services/InMemoryFrameDebuggerSnapshotProvider.cs`
- Create: `Features/FrameDebugger/FrameDebuggerPanel.cs`
- Modify: `Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `UI/Icons/EditorIconKey.cs`
- Test: `Tests/Editor.Tests/Core/FrameDebuggerSnapshotProviderTests.cs`
- Test: `Tests/Editor.Tests/Features/FrameDebugger/FrameDebuggerPanelTests.cs`
- Modify: `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- Modify docs: `docs/Dock系统指南.md`, `docs/Studio代码分类.md`

## Task 1: Frame Debug Core Models

**Files:**
- Create all files under `Core/Models/FrameDebug/`
- Test: `Tests/Editor.Tests/Core/FrameDebuggerSnapshotProviderTests.cs`

- [ ] **Step 1: Write failing model tests**

Add tests that construct a populated `FrameDebuggerSnapshot`, assert read-only copied collections, assert blank required ids throw, and assert `FrameDebuggerSnapshot.Unavailable` contains `State = FrameDebuggerState.Unavailable`.

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FrameDebuggerSnapshotProviderTests"
```

Expected: FAIL because the `FrameDebug` models do not exist.

- [ ] **Step 2: Implement minimal model records**

Add immutable records with constructor validation for ids and display fields. Use `Array.AsReadOnly(input is null ? Array.Empty<T>() : [.. input])` for lists, matching scene snapshot style.

Required state enum:

```csharp
public enum FrameDebuggerState
{
    Unavailable,
    Running,
    CaptureRequested,
    CapturingFrame,
    PausedFrameDebug,
    ResumeRequested,
    Faulted,
}
```

Required static empty snapshot:

```csharp
public static FrameDebuggerSnapshot Unavailable { get; } = new(
    1,
    FrameDebuggerState.Unavailable,
    null,
    message: "Frame Debugger snapshot is unavailable.");
```

- [ ] **Step 3: Verify green**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FrameDebuggerSnapshotProviderTests"
```

Expected: PASS for model tests.

## Task 2: Frame Debug Snapshot Provider

**Files:**
- Create: `Core/Abstractions/IFrameDebuggerSnapshotProvider.cs`
- Create: `Core/Services/InMemoryFrameDebuggerSnapshotProvider.cs`
- Test: `Tests/Editor.Tests/Core/FrameDebuggerSnapshotProviderTests.cs`

- [ ] **Step 1: Write failing provider tests**

Add tests for:

- provider exposes current snapshot
- `TryGetPass` and `TryGetExecutionEvent` return expected records
- `ReplaceSnapshot` raises `SnapshotChanged` exactly once
- lookup dictionaries rebuild after replacement
- duplicate pass ids and duplicate event ids throw `InvalidOperationException`
- blank lookup ids return false

Run the same filtered test command. Expected: FAIL because provider types do not exist.

- [ ] **Step 2: Implement provider**

Mirror `InMemorySceneSnapshotProvider`: keep `current_`, pass dictionary, event dictionary, `ReplaceSnapshot`, and lookup helpers. Build indexes before replacing current snapshot so failed replacement leaves provider state unchanged.

- [ ] **Step 3: Verify green**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FrameDebuggerSnapshotProviderTests"
```

Expected: PASS.

## Task 3: Code-first Frame Debugger Panel

**Files:**
- Create: `Features/FrameDebugger/FrameDebuggerPanel.cs`
- Test: `Tests/Editor.Tests/Features/FrameDebugger/FrameDebuggerPanelTests.cs`

- [ ] **Step 1: Write failing panel tests**

Add tests using `CodeFirstPanelHostViewModel` that verify:

- unavailable snapshot renders "Frame Debugger" and unavailable message
- fixture snapshot renders pass list and selected pass details
- selecting a pass list item updates selected pass details
- replacing provider snapshot clears stale selected pass
- clicking capture/resume publishes diagnostics when no native adapter is connected

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FrameDebuggerPanelTests"
```

Expected: FAIL because `FrameDebuggerPanel` does not exist.

- [ ] **Step 2: Implement panel**

The panel should:

- subscribe to provider `SnapshotChanged` in `OnCreate`
- unsubscribe in `OnDestroy`
- call `RequestRepaintOnFrame` indirectly only through host rebuild by using a callback from the provider change event if needed
- read provider snapshot in `OnGui`
- render toolbar buttons, state, frame, epoch, pass list, selected pass properties, command/event/resource/dependency/transition foldouts
- publish diagnostics for capture/resume clicks with source `frame-debugger` and channel `Debug`

If the host does not expose a public rebuild callback to the panel, keep v0 simpler: tests can replace snapshots before creating/rebuilding the host, and a follow-up can add provider-driven rebuild. Do not broaden Code-first host architecture in this task unless a failing test requires it.

- [ ] **Step 3: Verify green**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FrameDebuggerPanelTests"
```

Expected: PASS.

## Task 4: Workbench Registration and Architecture Tests

**Files:**
- Modify: `Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `UI/Icons/EditorIconKey.cs`
- Modify: `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- Modify docs: `docs/Dock系统指南.md`, `docs/Studio代码分类.md`

- [ ] **Step 1: Write failing Workbench tests**

Update stable descriptor and action tests to include a `frame-debugger` panel:

```text
id: frame-debugger
title: Frame Debugger
kind: Tool
default area: Right
menu path: Window/Panels/Frame Debugger
cache policy: KeepAlive
icon: studio.frame-debugger
tag: DEBUG
title detail: read-only snapshot
status text: snapshot
```

Add a test that creates the panel content and asserts it is a `CodeFirstPanelHostViewModel`.

- [ ] **Step 2: Implement Workbench registration**

Add `EditorIconKey.PanelFrameDebugger`, create a shared `InMemoryFrameDebuggerSnapshotProvider` fixture in `WorkbenchFeatureModule`, and register the panel with `new CodeFirstPanelHostViewModel(new FrameDebuggerPanel(frameDebuggerProvider_, diagnostics_))`.

- [ ] **Step 3: Update architecture tests and docs**

Update architecture classification to accept `FrameDebug` Core models/provider contracts. Update docs to record that Frame Debugger v0 is read-only, fixture-backed, and does not connect native ABI.

- [ ] **Step 4: Verify focused tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FrameDebugger|WorkbenchFeatureModule|StudioLayering"
```

Expected: PASS.

## Task 5: Full Validation

**Files:** no new files unless validation identifies a bug.

- [ ] **Step 1: Run full managed tests**

```powershell
dotnet test Editor.sln
```

Expected: all tests pass.

- [ ] **Step 2: Run encoding and whitespace gates**

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected: 0 encoding violations and no whitespace errors.

- [ ] **Step 3: Commit implementation**

```powershell
git status --short
git add Core\Abstractions Core\Models\FrameDebug Core\Services Features\FrameDebugger Features\Workbench UI\Icons Tests docs
git commit -m "feat: add studio frame debugger snapshots"
```

Expected: commit succeeds on `codex/studio-frame-debugger-snapshot-design`.
