# Studio Contribution Ownership Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Slice 2 of the Studio extension lifecycle design: typed panel/action registries record contribution owners and return removal leases that the extension host owns.

**Architecture:** Keep `PanelDescriptor` and `WorkbenchActionDescriptor` data-only for existing consumers. Add owner-aware registration paths on the concrete typed registries, let `EditorExtensionHost` use those paths, and keep panel opening, command execution, provider lifecycle, external plugin loading, ALC, script VM, and native bridge out of scope.

**Tech Stack:** .NET 10, C#, xUnit, existing `PanelRegistry`, `WorkbenchActionRegistry`, `EditorExtensionHost`.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-extension-lifecycle-v0-design.md`
- Slice 2: Contribution Ownership.

## External Reference Notes

- VS Code keeps contribution declarations in extension manifests and command implementation behind activation.
- IntelliJ extension points separate descriptor registration from runtime services and recommend stateless extension implementations.
- Unity editor windows separate GUI creation, per-window updates, and cleanup callbacks.

## Scope

In:

- Registry entries record `OwnerExtensionId`.
- Owner-aware registration returns a removal handle.
- Duplicate diagnostics include contribution id, existing owner, and new owner.
- `EditorExtensionHost` keeps registration handles and disposes them on host disposal or activation rollback.
- `StudioCompositionSession` keeps the host alive for the desktop app and disposes it on application exit.

Out:

- Runtime enable/disable UI.
- Panel instance lifecycle.
- Provider contribution lifecycle.
- External plugin loading, hot reload, script VM, C++ ABI, native bridge.

## Task 1: Registry Ownership Contracts

**Files:**
- Modify: `Shell/Docking/PanelRegistry.cs`
- Modify: `Shell/Commands/WorkbenchActionRegistry.cs`
- Create: `Tests/Editor.Tests/Shell/Docking/PanelRegistryTests.cs`
- Modify: `Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs`

- [x] **Step 1: Write failing tests**

Add tests proving owned registration records owner ids, duplicate diagnostics name both owners, removal handles unregister the exact entry, and stale handles do not remove newer registrations.

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~PanelRegistryTests|FullyQualifiedName~WorkbenchActionRegistryTests"
```

Expected: compile failure because `RegisterOwned()` and `GetOwnerId()` do not exist.

- [x] **Step 3: Implement registry entries and handles**

Add internal owner-aware methods to concrete registries:

```csharp
internal IDisposable RegisterOwned(PanelDescriptor descriptor, EditorExtensionId ownerId);
internal EditorExtensionId GetOwnerId(string id);
```

and the equivalent for `WorkbenchActionDescriptor`. Preserve existing `Register()` as compatibility for current tests and direct setup helpers.

- [x] **Step 4: Verify GREEN**

Run the same focused test command. Expected: pass.

## Task 2: Host-Owned Registration Leases

**Files:**
- Modify: `Shell/Composition/EditorExtensionHost.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/EditorExtensionHostTests.cs`

- [x] **Step 1: Write failing host tests**

Add tests proving host-composed registries expose owner ids, host disposal removes panel/action contributions, and activation failure removes committed contributions after rolling back activation leases.

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests"
```

Expected: failure because `EditorExtensionHost` still uses non-owned `Register()` and does not keep registration leases.

- [x] **Step 3: Implement host lease ownership**

Have `EditorExtensionHost.Compose()` call `RegisterOwned()` on both typed registries, store returned handles, and dispose them in reverse order after activation leases. On activation failure, dispose started activation leases and then remove contribution registrations.

- [x] **Step 4: Verify GREEN**

Run the same host test command. Expected: pass.

## Task 3: Documentation And Verification

**Files:**
- Modify: `App.axaml.cs`
- Create: `Shell/Composition/StudioCompositionSession.cs`
- Modify: `Shell/Composition/StudioCompositionRoot.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/StudioCompositionRootTests.cs`
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`

- [x] **Step 1: Record Slice 2 boundary**

Document that typed registries now track owner ids and host-owned removal leases for built-in panel/action contributions only.

- [x] **Step 1b: Wire production root ownership**

Add `StudioCompositionSession` so the app root retains the host and releases host-owned contribution leases on application exit. Keep `CreateDefaultComposition()` available for tests and compatibility helpers.

- [x] **Step 2: Run focused and full verification**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~PanelRegistryTests|FullyQualifiedName~WorkbenchActionRegistryTests|FullyQualifiedName~EditorExtensionHostTests|FullyQualifiedName~StudioCompositionRootTests|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~EditorFeatureCatalogTests"
dotnet test Editor.sln -c Release
```

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

From `apps/studio`:

```powershell
git diff --check
rg -n "AssemblyLoadContext|NativeEditorBridge|C\+\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly" Core Shell Features Tests
```

Expected: tests pass; encoding has 0 issues; diff check is clean; non-goal search has no implementation matches.

- [x] **Step 3: Commit**

```powershell
git add App.axaml.cs Shell\Docking\PanelRegistry.cs Shell\Commands\WorkbenchActionRegistry.cs Shell\Composition\EditorExtensionHost.cs Shell\Composition\StudioCompositionRoot.cs Shell\Composition\StudioCompositionSession.cs Tests\Editor.Tests\Shell\Docking\PanelRegistryTests.cs Tests\Editor.Tests\Shell\Commands\WorkbenchActionRegistryTests.cs Tests\Editor.Tests\Shell\Composition\EditorExtensionHostTests.cs Tests\Editor.Tests\Shell\Composition\StudioCompositionRootTests.cs docs\Dock系统指南.md docs\编辑器UI平台规范.md docs\superpowers\plans\2026-06-23-studio-contribution-ownership.md
git commit -m "feat: track studio contribution ownership"
```
