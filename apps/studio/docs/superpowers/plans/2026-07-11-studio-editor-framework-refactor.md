# Studio Editor Framework Structural Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current single `Editor.csproj` compile-time boundary with the production Studio project graph, preserve current editor behavior, and make built-in features consume the same public Editor API that future project and Package extensions will use.

**Architecture:** Use branch-by-abstraction from the dependency bottom upward. First establish `Asharia.Editor` contracts and architecture gates, then move Code-first and UI-neutral state, add static module hosting, separate Avalonia and native adapters, migrate built-ins, and make `Asharia.Studio.App` the only executable composition root. Compatibility adapters exist only while both sides are covered by tests and are deleted before this wave completes.

**Tech Stack:** .NET 10, C# 14, Avalonia 12.0.4, CommunityToolkit.Mvvm 8.4.1, xUnit 2.9.3, MSBuild, CMake/Conan native runtime artifacts.

## Global Constraints

- Target Windows, Linux, and macOS from the project graph and path/RID contracts; do not embed Windows-only policy above `Asharia.Studio.EngineBridge` or `Asharia.Studio.Presentation.Avalonia`.
- Do not change the C++ editor native ABI, renderer command recording, Vulkan synchronization, or world ownership in this structural wave.
- `Asharia.Editor` must not reference Avalonia, Studio host projects, P/Invoke, filesystem/process implementations, OS handles, or Vulkan types.
- `Asharia.Studio.BuiltInExtensions` may reference only `Asharia.Editor` and `Asharia.Editor.Avalonia`; it must not reference Application, EngineBridge, Presentation, or App.
- Production projects must not use `InternalsVisibleTo` to bypass dependency direction. Test-only visibility is allowed only for the owning test assembly.
- Code-first is the default UI contract. Avalonia/XAML enters through `Asharia.Editor.Avalonia` content leases and never through global `Application.Current` mutation.
- Every migration commit must leave `Asharia.Studio.sln` buildable and the tests for the moved boundary green. Do not perform a repository-wide move followed by later repair commits.
- Keep the legacy `Editor.csproj` and `Editor.sln` runnable until Task 9 switches the executable root; delete them only after parity and native-copy tests pass through the new App project.
- Non-C/C++ files remain UTF-8 without BOM. Run the repository encoding gate before every Slice handoff.
- Each Task below is a reviewable Slice boundary. Create or reuse a GitHub Slice, set Project `Status`, `Priority`, and `Size`, and use `Refs #N` until that Slice's acceptance evidence is complete.

---

## Scope and Delivery Waves

This plan is Wave 1: compile-time boundaries, static built-in dogfooding, existing native viewport behavior, and executable composition. It intentionally stops before executing untrusted or user-authored assemblies.

Wave 2 receives separate specs and plans after Wave 1 lands:

1. `.asmdef`, `asharia.package.json.editor`, generated SDK projects, NuGet locks, Package locks, module source generation, and diagnostics.
2. Package generation hosts, collectible/pinned ALCs, dependency closure resolution, reload handover, pending-restart boot, and last-known-good recovery.
3. ProjectSession/Edit/Play/Preview domains, Game View, standalone Play presentation, capability Epoch recovery, and three-platform native viewport backends.

Wave 1 must leave real ports and stable identities for those waves. It must not add a fake reflection loader, process-wide service locator, or Windows-only abstraction and call it extensibility.

## Target File Map

```text
apps/studio/
  Directory.Build.props
  Asharia.Studio.sln
  src/
    Asharia.Editor/
    Asharia.Editor.Avalonia/
    Asharia.Editor.Analyzers/                 # created with module generation in Wave 2
    Asharia.Studio.Application/
    Asharia.Studio.EngineInterop/
    Asharia.Studio.EngineBridge/
    Asharia.Studio.Presentation.Avalonia/
    Asharia.Studio.BuiltInExtensions/
    Asharia.Studio.App/
  tests/
    Asharia.Editor.Tests/
    Asharia.Studio.Application.Tests/
    Asharia.Studio.EngineInterop.Tests/
    Asharia.Studio.EngineBridge.Tests/
    Asharia.Studio.Presentation.Avalonia.Tests/
    Asharia.Studio.ExtensionIntegration.Tests/
    Asharia.Studio.Architecture.Tests/
```

The analyzer project is not created in Wave 1. Analyzer rules, the project itself, and the module-index source generator belong to Wave 2 because they are coupled to the finalized `.asmdef` and generated-project schema; an empty project shell would not enforce a real boundary.

### Current-to-target ownership

| Current path | Wave 1 owner |
| --- | --- |
| `Core/CodeFirstUI/**` | `src/Asharia.Editor/UI/CodeFirst/**` |
| UI-neutral `Core/Abstractions/**` and `Core/Models/**` | `src/Asharia.Editor/**` |
| `Core/Services/**`, `Shell/Selection/**`, UI-neutral `Shell/Services/**` | `src/Asharia.Studio.Application/**` |
| `Shell/Composition/**`, registries and command routing | `src/Asharia.Studio.Application/**` |
| Raw native structs/entry points in `Core/Interop/**/Api/**` | `src/Asharia.Studio.EngineBridge/Abi/**` |
| Native adapters in `Core/Interop/**/Adapters/**` | `src/Asharia.Studio.EngineBridge/Adapters/**` |
| UI-neutral external-image/device descriptors | `src/Asharia.Studio.EngineInterop/**` |
| `Shell/CodeFirstUI/**`, `Shell/Docking/**`, `Shell/ViewModels/**`, `Shell/Views/**`, `UI/**` | `src/Asharia.Studio.Presentation.Avalonia/**` |
| `Features/**` | `src/Asharia.Studio.BuiltInExtensions/Features/**` |
| `Program.cs`, `App.axaml*`, final composition | `src/Asharia.Studio.App/**` |
| `Tests/Editor.Tests/**` | matching owner under `tests/**` |

---

### Task 1: Establish the target solution and enforce the project graph

**Slice size:** S

**Files:**

- Create: `apps/studio/Asharia.Studio.sln`
- Create: `apps/studio/src/Asharia.Editor/Asharia.Editor.csproj`
- Create: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj`
- Create: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- Modify: `apps/studio/Editor.csproj`
- Modify: `apps/studio/docs/architecture/studio-code-framework.md`

**Interfaces:**

- Consumes: the current `Editor.csproj` executable and existing `Editor.Tests` suite.
- Produces: a stable solution name, an empty public assembly boundary, legacy glob exclusions, and an architecture test that later Tasks extend.

- [x] **Step 1: Write the failing project-graph test**

Create `ProjectReferenceGraphTests.cs` with a repository-root finder and a test that loads `src/Asharia.Editor/Asharia.Editor.csproj`, rejects all ProjectReference/PackageReference items, and asserts `net10.0`, assembly/root namespace `Asharia.Editor`, and nullable enabled:

```csharp
[Fact]
public void Public_editor_project_is_a_dependency_free_net10_library()
{
    var project = LoadProject("src/Asharia.Editor/Asharia.Editor.csproj");
    Assert.Empty(project.Descendants("ProjectReference"));
    Assert.Empty(project.Descendants("PackageReference"));
}
```

`LoadProject` must use `XDocument.Load`, normalize separators, and fail with the absolute missing path. Do not shell out to MSBuild from a unit test.

- [x] **Step 2: Run the architecture test and verify it fails**

Run:

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release
```

Expected: FAIL because the target public project does not exist.

- [x] **Step 3: Add the first public project boundary**

Use this complete public project and do not add source files, PackageReference, ProjectReference, or shared `Directory.Build.props` in this Slice:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>Asharia.Editor</AssemblyName>
    <RootNamespace>Asharia.Editor</RootNamespace>
    <Nullable>enable</Nullable>
  </PropertyGroup>
</Project>
```

- [x] **Step 4: Write and run the legacy glob exclusion test**

Add `Legacy_editor_project_excludes_the_target_source_and_test_trees`, load `Editor.csproj`, normalize backslashes to slashes, and assert removal patterns `src/**/*.cs`, existing `Tests/**/*.cs`, and `src/**/*.axaml`. Migration keeps the existing uppercase test root until the legacy tree can be renamed atomically for case-sensitive and case-insensitive filesystems.

Run:

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --filter FullyQualifiedName~Legacy_editor_project_excludes
```

Expected: FAIL because `Editor.csproj` only excludes the legacy uppercase `Tests` tree.

- [x] **Step 5: Keep legacy default globbing away from the new tree**

Add to `Editor.csproj`:

```xml
<ItemGroup>
  <Compile Remove="src\**\*.cs" />
  <AvaloniaResource Remove="src\**\*.axaml" />
</ItemGroup>
```

This prevents the compatibility executable from compiling new project sources twice.

- [x] **Step 6: Write and run the target Solution membership test**

Add `Target_solution_contains_only_the_declared_boundary_projects`. Parse only `.csproj` Project lines and assert the exact set: `Editor.csproj`, `Tests/Editor.Tests/Editor.Tests.csproj`, `src/Asharia.Editor/Asharia.Editor.csproj`, and `Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj`.

Run:

```powershell
dotnet test apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj -c Release --filter FullyQualifiedName~Target_solution_contains
```

Expected: FAIL because `Asharia.Studio.sln` does not exist.

- [x] **Step 7: Create and populate the target solution**

Run:

```powershell
dotnet new sln --name Asharia.Studio --format sln --output apps/studio
dotnet sln apps/studio/Asharia.Studio.sln add apps/studio/Editor.csproj --in-root
dotnet sln apps/studio/Asharia.Studio.sln add apps/studio/Tests/Editor.Tests/Editor.Tests.csproj --in-root
dotnet sln apps/studio/Asharia.Studio.sln add apps/studio/src/Asharia.Editor/Asharia.Editor.csproj --in-root
dotnet sln apps/studio/Asharia.Studio.sln add apps/studio/Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj --in-root
```

- [x] **Step 8: Run both solutions**

Run:

```powershell
dotnet test apps/studio/Editor.sln -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
```

Expected: the existing 657 tests pass; the new architecture test passes.

- [x] **Step 9: Commit**

```powershell
git add apps/studio/Asharia.Studio.sln apps/studio/src/Asharia.Editor apps/studio/Tests/Asharia.Studio.Architecture.Tests apps/studio/Editor.csproj apps/studio/docs/architecture/studio-code-framework.md apps/studio/docs/superpowers/plans/2026-07-11-studio-editor-framework-refactor.md
git commit -m "build(studio): establish target solution graph"
```

---

### Task 2: Implement the minimal public module identity and lifetime contract

**Slice size:** M

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Extensions/PackageName.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorAssemblyName.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/ModuleLocalId.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/ScopeInstanceId.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorIdentityValidation.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleIdentity.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleAttribute.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModule.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleActivation.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleBuilder.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleContext.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleMetadata.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModulePolicies.cs`
- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorCapabilitySnapshot.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Extensions/EditorModuleIdentityTests.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Extensions/EditorModuleMetadataTests.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Extensions/EditorModuleActivationTests.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Extensions/EditorModuleContextTests.cs`
- Modify: `apps/studio/Tests/Asharia.Studio.Architecture.Tests/ProjectReferenceGraphTests.cs`
- Modify: `apps/studio/Asharia.Studio.sln`
- Modify: `apps/studio/docs/architecture/studio-code-framework.md`

**Interfaces:**

- Consumes: no Studio implementation project.
- Produces: `EditorModule`, stable definition/instance identities, scope and activation policies, read-only capability Epoch snapshots, and no-op recoverable activation. Contribution and dependency declarations remain out of scope.

- [x] **Step 1: Write identity validation tests and verify RED**

Cover these exact cases:

```csharp
[Theory]
[InlineData("terrain.editor", true)]
[InlineData("Terrain.Editor", false)]
[InlineData("terrain editor", false)]
[InlineData("terrain", false)]
public void Module_local_id_uses_lowercase_namespaced_ids(string value, bool valid)
{
    Assert.Equal(valid, ModuleLocalId.TryCreate(value, out _));
}
```

Also prove that Package names use lowercase canonical namespace syntax, assembly names reject path/qualified-name characters, default values are invalid, Project scope IDs use lowercase canonical UUIDs, and two scope IDs produce distinct `EditorModuleInstanceId` values.

- [x] **Step 2: Implement and verify stable identity value types**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --filter FullyQualifiedName~EditorModuleIdentityTests
```

The first run fails at compile time because the public identity namespace does not exist. Implement the following composite identities only after that RED result:

Use readonly record structs with validated `Create`/`TryCreate`, deterministic invalid defaults, and empty `ToString()` for `PackageName`, `EditorAssemblyName`, `ModuleLocalId`, and `ScopeInstanceId`. Do not freeze `PackageGenerationId` or a separate `ProjectSessionId` format in this Slice. The composite identity is:

```csharp
public readonly record struct EditorAssemblyId(PackageName Package, EditorAssemblyName Assembly);

public readonly record struct EditorModuleDefinitionId(
    EditorAssemblyId Assembly,
    ModuleLocalId Module,
    EditorModuleScopeKind Scope);

public readonly record struct EditorModuleInstanceId(
    EditorModuleDefinitionId Definition,
    ScopeInstanceId ScopeInstance);
```

Do not accept `Type`, `Assembly`, a CLR full name, or an ambient current Project in these identities.

- [x] **Step 3: Write metadata/policy tests, verify RED, then implement**

```csharp
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class EditorModuleAttribute(string id) : Attribute
{
    public string Id { get; } = id;
    public EditorModuleScopeKind Scope { get; init; } = EditorModuleScopeKind.Project;
    public EditorModuleActivationPolicy Activation { get; init; } = EditorModuleActivationPolicy.OnScopeReady;
    public EditorModuleHandoverPolicy Handover { get; init; } = EditorModuleHandoverPolicy.Coexist;
}
```

The enums contain exactly `Application/Project`, `OnScopeReady/OnDemand`, and `Coexist/QuiesceThenActivate/RestartRequired`.

`EditorModuleMetadata` stores the stable definition ID, entry type name for diagnostics only, activation policy, and handover policy. `CanReplace` uses `EditorModuleDefinitionId`, never `EntryTypeName`; triggers remain with future contribution/dependency declarations.

- [x] **Step 4: Write lifecycle/context tests and verify RED**

Tests require the shared Empty activation, synchronous/idempotent quiesce-resume-dispose, invalid stop-reason rejection, immutable copied capability snapshots, positive contract-major capability IDs, non-negative Epoch, and resume reason/scope/Epoch preservation. The first run fails because `EditorModule`, builder, contexts, and activation types do not exist.

- [x] **Step 5: Implement the minimal activation contract**

```csharp
public abstract class EditorModule
{
    public abstract void Configure(EditorModuleBuilder editor);

    public virtual ValueTask<IEditorModuleActivation> ActivateAsync(
        EditorModuleContext context,
        CancellationToken cancellationToken) =>
        new(EditorModuleActivation.Empty);
}

public interface IEditorModuleActivation : IAsyncDisposable
{
    ValueTask<EditorModuleQuiesceResult> QuiesceAsync(
        EditorModuleStopReason reason,
        CancellationToken cancellationToken);

    ValueTask ResumeAsync(
        EditorModuleResumeContext context,
        CancellationToken cancellationToken);
}
```

`EditorModuleActivation.Empty` must be a process-wide immutable no-op object whose quiesce result is `Ready` and whose resume/dispose methods complete synchronously.

- [x] **Step 6: Implement minimal builder/context and capability snapshots**

`EditorModuleBuilder` exposes only immutable `EditorModuleDefinitionContext`. `EditorModuleContext` exposes instance identity and a defensive read-only copy of capability snapshots. `EditorModuleResumeContext` exposes `ReloadRollback | CapabilityRecovered`, scope ID, and a defensive read-only copy of capability ID/Epoch/state snapshots. No dependency/provide/require API is added.

- [x] **Step 7: Add the test project to the Solution with RED/GREEN membership proof**

Update the exact Solution membership test first and observe it fail because `Asharia.Editor.Tests` is absent; then add `Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj` to `Asharia.Studio.sln` with `--in-root`.

- [x] **Step 8: Prove the public assembly is UI/native neutral**

Extend `ProjectReferenceGraphTests` and scan `Asharia.Editor` source text for forbidden tokens:

```csharp
Assert.DoesNotContain("Avalonia", source, StringComparison.Ordinal);
Assert.DoesNotContain("LibraryImport", source, StringComparison.Ordinal);
Assert.DoesNotContain("DllImport", source, StringComparison.Ordinal);
Assert.DoesNotContain("Editor.Shell", source, StringComparison.Ordinal);
```

- [x] **Step 9: Run and commit**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
git add apps/studio/src/Asharia.Editor apps/studio/Tests/Asharia.Editor.Tests apps/studio/Tests/Asharia.Studio.Architecture.Tests apps/studio/Asharia.Studio.sln apps/studio/docs
git commit -m "feat(studio): add public editor module contract"
```

---

### Task 2b: Add immutable module dependency and capability declarations

**Slice size:** M

**Files:**

- Create: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleDeclaration.cs`
- Create: `apps/studio/Tests/Asharia.Editor.Tests/Extensions/EditorModuleDeclarationTests.cs`
- Modify: `apps/studio/src/Asharia.Editor/Extensions/EditorModuleBuilder.cs`
- Modify: `apps/studio/docs/architecture/studio-code-framework.md`

**Interfaces:**

- Consumes: identity, scope, builder, and capability ID contracts from Task 2.
- Produces: ordered required/optional module dependencies, required/optional capability dependencies, provided capabilities, and an immutable `EditorModuleDeclaration` snapshot.
- Defers: contribution descriptors, Host graph resolution, capability provider selection, activation topology, Package/assembly resolution, and scope-instance binding.

- [x] **Step 1: Write declaration contract tests and verify RED**

The first targeted run fails at compile time because `EditorModuleBuilder` has no dependency/capability builders or `Build()` method. Tests cover ordered output, repeated-build identity, read-only collections, freeze, duplicates, required/optional conflicts, self-dependency, invalid identities, and scope direction.

- [x] **Step 2: Implement isolated declaration builders and immutable freeze**

`EditorModuleBuilder.Dependencies` declares required/optional module and capability edges. `EditorModuleBuilder.Capabilities` declares provided capability IDs. `Build()` copies all ordered declarations into read-only collections, caches the declaration, and makes every later mutation fail explicitly.

- [x] **Step 3: Enforce declaration-time structural rules**

Reject duplicate module/capability declarations, required/optional overlap, module self-dependency, invalid default identities, and Application→Project module edges. Project→Application and same-scope edges remain valid. Cross-Package dependency legality, provider ambiguity, cycles, and runtime capability availability remain Host responsibilities.

- [x] **Step 4: Run full gates, commit, and open the Slice PR**

```powershell
dotnet build apps/studio/src/Asharia.Editor/Asharia.Editor.csproj -c Release --no-restore -warnaserror
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --no-restore
dotnet test apps/studio/Editor.sln -c Release --no-restore
dotnet test apps/studio/Asharia.Studio.sln -c Release --no-restore
dotnet format apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj --verify-no-changes --no-restore
```

---

### Task 3: Move Code-first UI into `Asharia.Editor` — Complete

**Slice size:** M

**Files:**

- Move: `apps/studio/Core/CodeFirstUI/Abstractions/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/Abstractions/**`
- Move: `apps/studio/Core/CodeFirstUI/Authoring/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/Authoring/**`
- Move: `apps/studio/Core/CodeFirstUI/Building/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/Building/**`
- Move: `apps/studio/Core/CodeFirstUI/Events/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/Events/**`
- Move: `apps/studio/Core/CodeFirstUI/Models/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/Models/**`
- Move: `apps/studio/Core/CodeFirstUI/State/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/State/**`
- Move: `apps/studio/Core/CodeFirstUI/Validation/**` to `apps/studio/src/Asharia.Editor/UI/CodeFirst/Validation/**`
- Move: `apps/studio/Tests/Editor.Tests/Core/CodeFirstUI/**` to `apps/studio/Tests/Asharia.Editor.Tests/UI/CodeFirst/**`
- Modify: `apps/studio/Editor.csproj`
- Modify: `apps/studio/Features/FrameDebugger/FrameDebuggerPanel.cs`
- Modify: `apps/studio/Features/UiStyle/UiStylePanel.cs`
- Modify: `apps/studio/Shell/CodeFirstUI/**`
- Modify: all Code-first `using` directives under `apps/studio/Tests/Editor.Tests/**`

**Interfaces:**

- Consumes: `Asharia.Editor` from Task 2 plus the minimal UI-neutral prerequisite closure promoted before the move: `Asharia.Editor.Diagnostics`, `Asharia.Editor.Commands`, and `Asharia.Editor.Panels`.
- Produces: `Asharia.Editor.UI.CodeFirst` with no Avalonia reference; the compatibility executable consumes it through a ProjectReference.

**Implemented result:** The prerequisite contracts were promoted first, the Code-first kernel and authoring surface were then moved into `Asharia.Editor`, and `ICodeFirstEditorPanelHost` now provides the explicit cross-assembly lifecycle boundary. Legacy Avalonia, Dock, Shell host, and dispatcher implementations remain in `Editor`; contribution descriptors and Host/resolver behavior remain deferred.

- [x] **Step 1: Add a failing assembly-boundary test**

```csharp
[Fact]
public void Code_first_authoring_types_are_owned_by_editor_api()
{
    Assert.Equal("Asharia.Editor", typeof(EditorGui).Assembly.GetName().Name);
    Assert.Equal("Asharia.Editor", typeof(GuiTreeSnapshot).Assembly.GetName().Name);
}
```

- [x] **Step 2: Run the focused test and verify it fails**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release --filter FullyQualifiedName~CodeFirst
```

Expected: FAIL because the types are still compiled into `Editor`.

- [x] **Step 3: Move files and normalize namespaces**

Apply this exact namespace mapping:

```text
Editor.Core.CodeFirstUI.* -> Asharia.Editor.UI.CodeFirst.*
```

Do not introduce compatibility type-forwarders. Update all callers in the same commit so there is one source of truth.

- [x] **Step 4: Reference the public project from the compatibility executable**

Add:

```xml
<ProjectReference Include="src\Asharia.Editor\Asharia.Editor.csproj" />
```

The legacy project already excludes `src/**`, so types compile once.

- [x] **Step 5: Run Code-first and full Studio tests**

```powershell
dotnet test apps/studio/Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj -c Release
dotnet test apps/studio/Editor.sln -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
```

- [x] **Step 6: Commit**

```powershell
git add apps/studio/Core/CodeFirstUI apps/studio/src/Asharia.Editor apps/studio/Tests/Asharia.Editor.Tests apps/studio/Editor.csproj apps/studio/Features apps/studio/Shell/CodeFirstUI apps/studio/Tests/Editor.Tests
git commit -m "refactor(studio): extract code first editor api"
```

---

### Task 4a: Add declaration-only Panel contribution contract — Complete

**Slice size:** M

**Implemented result:** `Asharia.Editor` now owns stable contribution/backend/factory-local identities, immutable UI-neutral `EditorPanelDescriptor`, `EditorModuleBuilder.Panels`, module-local duplicate validation, and an ordered defensive snapshot frozen by `Build()`. Panel scope comes only from the module definition. The declaration stores `EditorFactoryLocalId`; generation ownership and runtime factory binding remain a future Host Slice.

**Evidence:** Public contract tests cover identity syntax, descriptor validation, enum stability, ordering, immutability, duplicate rejection, shared freeze behavior, and scope ownership. Architecture tests reject CLR factories, implementation/native/UI vocabulary, generation handles, and duplicate scope fields from the public contract. Both legacy and target Solutions remain green.

**Explicit non-goals:** No Panel registry, factory registration/binding, Dock integration, Host resolver, runtime content creation, `PackageGenerationId`, or `GenerationScopedFactoryHandle` was added. Legacy `PanelDescriptor(Func<object>)` remains compatibility-only in `Editor`.

See [the approved design](../specs/2026-07-11-studio-declaration-only-panel-contribution-design.md) and [the completed implementation plan](2026-07-11-studio-declaration-only-panel-contribution.md).

---

### Task 4b: Add public Dialog data contract and migrate the compatibility Host — Complete

**Slice size:** M

**Implemented result:** `Asharia.Editor.Dialogs` owns seven validated UI-neutral public types. Action identity, role, default, destructive styling and completion remain orthogonal; request construction freezes a defensive read-only action snapshot. The compatibility Dialog Host and About path consume the public contract, while Presentation retains overlay, focus, action projection and single-active-modal policy. The legacy `Editor.Core.Models.Dialogs` family was deleted without wrappers or duplicate DTOs.

**Evidence:** Public contract tests cover stable enums and IDs, descriptor/request/result invariants and collection immutability. Compatibility tests cover About, projection, exact action result, system-dismiss policy, second-request rejection, and generation-scoped single completion: retained or repeated terminal signals from an earlier request cannot complete a later request. Architecture gates enforce the seven-type public surface, dependency/UI/native neutrality (including no `CancellationToken` in Dialog source or authored constructor/static-method inputs), legacy project consumption and deletion of the legacy source family. Both legacy and target Solutions remain green.

**Explicit non-goals:** No dialog service, static Host/service resolution, owner-window routing, custom content, platform ordering, localization, file picker, progress, notification or modal queue was added. User system-dismiss remains distinct from future operation cancellation.

See [the implemented design](../specs/2026-07-11-studio-public-dialog-contract-design.md) and [the completed implementation plan](2026-07-11-studio-public-dialog-contract.md).

---

### Task 4: Extract UI-neutral Editor services and immutable state — Complete

**Slice size:** L

**Implemented result (#239):** Background tasks, diagnostic record/service, Frame Debug snapshots/provider, editing commands, lifecycle events, selection, transactions, scene/world snapshots, backend-neutral Viewport identity/clock/render/scheduler/state, status messages, and Panel lifecycle/frame sink contracts compile from `Asharia.Editor`. Legacy implementations and Presentation consumers use the public contracts. Native Frame Debug payload/bridge, Viewport composition capability/native present transport, `EditorDiagnosticSourceDescriptor`, `SceneProviderDescriptor(Func<ISceneSnapshotProvider>)`, provider registration/status, fixture provider implementation, `PanelDescriptor(Func<object>)`, and `WorkbenchActionDescriptor` remain compatibility-only because their dependencies, transport vocabulary, runtime factories, or Host semantics are not stable public API.

**Evidence:** Public assembly-ownership tests cover every promoted family and reject UI/native/Studio implementation references. Pure contract tests moved with their owners. Both target and legacy Solutions remain green after each family migration.

**Files:**

- Move: `Core/Models/BackgroundTasks/**` to `src/Asharia.Editor/Tasks/**`
- Move: `Core/Models/Diagnostics/**` to `src/Asharia.Editor/Diagnostics/**`
- Move: `Core/Models/Editing/**` to `src/Asharia.Editor/Editing/**`
- Move: `Core/Models/FrameDebug/**` to `src/Asharia.Editor/Diagnostics/FrameDebug/**`
- Move: `Core/Models/Lifecycle/**` to `src/Asharia.Editor/Lifecycle/**`
- Move: UI-neutral files from `Core/Models/Panels/**` to `src/Asharia.Editor/Panels/**`
- Move: `Core/Models/Scene/**` to `src/Asharia.Editor/Worlds/Snapshots/**`
- Move: `Core/Models/Selection/**` to `src/Asharia.Editor/Selection/**`
- Move: `Core/Models/Transactions/**` to `src/Asharia.Editor/Transactions/**`
- Move: UI-neutral files from `Core/Models/Viewports/**` to `src/Asharia.Editor/Viewports/**`
- Move: UI-neutral files from `Core/Models/Workbench/**` to `src/Asharia.Editor/Commands/**`
- Move: matching extension-facing interfaces from `Core/Abstractions/**` to the owning public folders
- Create: `apps/studio/tests/Asharia.Editor.Tests/PublicApi/PublicContractTests.cs`
- Modify: all current consumers and tests of moved namespaces

**Interfaces:**

- Consumes: public module, Code-first, and declaration-only Panel contracts.
- Produces: the remaining stable immutable requests/snapshots/services plus UI-neutral contribution descriptors. It does not export the legacy `PanelDescriptor(Func<object>)`, delegate-based `SceneProviderDescriptor`, or `WorkbenchActionDescriptor` as permanent API. Generation-scoped factory binding remains a separate Host concern.

- [x] **Step 1: Write the public contract test**

Enumerate exported public types and reject implementation vocabulary:

```csharp
var exported = typeof(EditorModule).Assembly.GetExportedTypes();
Assert.DoesNotContain(exported, type => type.FullName!.Contains("Avalonia", StringComparison.Ordinal));
Assert.DoesNotContain(exported, type => type.FullName!.Contains("NativeLibrary", StringComparison.Ordinal));
Assert.DoesNotContain(exported, type => type.GetProperties().Any(property => property.PropertyType == typeof(object)));
```

Add explicit assembly-ownership assertions for selection, transaction, diagnostics, viewport snapshot, and frame-debug snapshot types.

- [x] **Step 2: Move one model family at a time**

For each family: move source, rename `Editor.Core.*` to `Asharia.Editor.*`, update its focused tests, run those tests, then continue. The required order is IDs/enums, immutable records, interfaces, then dependent descriptors.

- [x] **Step 3: Keep legacy descriptors private to the compatibility executable**

`PanelDescriptor`, `SceneProviderDescriptor`, `WorkbenchActionDescriptor`, `IEditorExtensionModule`, `IEditorFeatureModule`, `IEditorContributionBuilder`, `IPanelRegistry`, and `IWorkbenchActionRegistry` remain compiled only by the legacy executable and are excluded from `Asharia.Editor`; public-source architecture gates prevent new public dependencies. Physical relocation, `[Obsolete]`, and internalization remain Task 8 compatibility-host cleanup because current legacy consumers and behavior tests still compile directly against them.

- [x] **Step 4: Update architecture tests**

Replace directory-presence assertions in the 1,000-line legacy `StudioLayeringTests` with assembly ownership and ProjectReference assertions. Preserve behavior tests; delete only assertions that encode obsolete physical paths.

- [x] **Step 5: Run and commit**

```powershell
dotnet test apps/studio/Asharia.Studio.sln -c Release
dotnet test apps/studio/Editor.sln -c Release
git add apps/studio/Core apps/studio/Shell/Compatibility apps/studio/src/Asharia.Editor apps/studio/tests apps/studio/Tests
git commit -m "refactor(studio): extract editor service contracts"
```

---

### Task 5: Create the Application kernel and static module host — In progress

**Slice size:** L

**Current checkpoint (#243 plus follow-up slices):** `Asharia.Studio.Application` and its test project exist in the target Solution and reference only `Asharia.Editor`. `StaticPackageGenerationHost.Create` validates duplicate registrations before invoking factories, creates each definition object once, calls `Configure` once, freezes its declaration, and publishes a read-only definition map only after the complete static set succeeds. `EditorScopeTransaction.Prepare` builds an invisible immutable candidate, validates scope/required dependency/mixed module-capability cycle/Panel contribution/capability-provider structure against its captured registry snapshot, and `Commit` performs one stale-checked registry swap. `EditorModuleHost` single-flights each scope instance while allowing different Project scopes to activate concurrently, records `Active`/`WaitingForCapability`/`Faulted`/`Blocked` outcomes, isolates required dependent failure chains, and disposes activation leases dependents-first with reverse-registration tie breaking. The legacy executable now references Application through one `LegacyEditorModuleCompatibilityAdapter`; it projects legacy built-ins into ordered Application definitions while temporarily retaining legacy Panel/Action/Provider registration. `EditorSelectionService`, `EditorBackgroundTaskService`, `EditorLifecycleEventService`, and their focused behavior tests have moved from legacy Shell ownership to Application while their public contracts remain in `Asharia.Editor`; the remaining UI-neutral service moves are still pending.

**Files:**

- Create: `apps/studio/src/Asharia.Studio.Application/Asharia.Studio.Application.csproj`
- Create: `apps/studio/src/Asharia.Studio.Application/Extensions/StaticPackageGenerationHost.cs`
- Create: `apps/studio/src/Asharia.Studio.Application/Extensions/EditorModuleDefinition.cs`
- Create: `apps/studio/src/Asharia.Studio.Application/Extensions/EditorModuleInstance.cs`
- Create: `apps/studio/src/Asharia.Studio.Application/Extensions/EditorModuleRegistry.cs`
- Create: `apps/studio/src/Asharia.Studio.Application/Extensions/EditorScopePartition.cs`
- Create: `apps/studio/src/Asharia.Studio.Application/Extensions/EditorScopeTransaction.cs`
- Move: `Shell/Composition/EditorExtensionHost.cs` behavior into `Application/Extensions/EditorModuleHost.cs`
- Create: `apps/studio/Shell/Compatibility/LegacyEditorModuleCompatibilityAdapter.cs`
- Move: `Shell/Composition/EditorProviderHost.cs` into `Application/Providers/EditorProviderHost.cs`
- Move: `Shell/Commands/**` into `Application/Commands/**`
- Move: `Shell/Selection/EditorSelectionService.cs` into `Application/Selection/EditorSelectionService.cs`
- Move: UI-neutral `Shell/Services/**` and `Core/Services/**` into matching Application folders
- Create: `apps/studio/tests/Asharia.Studio.Application.Tests/Asharia.Studio.Application.Tests.csproj`
- Create: `apps/studio/tests/Asharia.Studio.Application.Tests/Extensions/StaticPackageGenerationHostTests.cs`
- Create: `apps/studio/tests/Asharia.Studio.Application.Tests/Extensions/EditorScopeTransactionTests.cs`
- Create: `apps/studio/tests/Asharia.Studio.Application.Tests/Extensions/EditorModuleHostTests.cs`

**Interfaces:**

- Consumes: only `Asharia.Editor`.
- Produces: definition/configure-once semantics, Application/Project instance identities, staged registry partitions, activation leases, reverse-order disposal, and a static built-in generation host. It does not load files or create ALCs.

- [x] **Step 1: Write host state-machine tests**

Add fake modules proving:

1. one definition object is configured exactly once;
2. the same Project definition activates once for each distinct `ProjectSessionId`;
3. two Project activations may overlap without shared mutable state;
4. duplicate contribution IDs reject the invisible candidate partition;
5. a required capability in `Unavailable` publishes `WaitingForCapability`, not a Project-open failure;
6. disposal is dependents-first and reverse registration order;
7. activation failure faults only the instance and required dependent chain.

- [x] **Step 2: Create the Application project**

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <AssemblyName>Asharia.Studio.Application</AssemblyName>
    <RootNamespace>Asharia.Studio.Application</RootNamespace>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\Asharia.Editor\Asharia.Editor.csproj" />
  </ItemGroup>
</Project>
```

- [x] **Step 3: Implement configure-once static definitions**

`StaticPackageGenerationHost` receives explicit generated-style registrations, not reflection scanning:

```csharp
public sealed record StaticEditorModuleRegistration(
    EditorModuleDefinitionId DefinitionId,
    Func<EditorModule> CreateDefinition,
    EditorModuleMetadata Metadata);
```

It creates each definition once, calls `Configure` once into a staging builder, freezes descriptors/dependencies, and retains the default-ALC generation until process exit.

- [x] **Step 4: Implement atomic scope partitions**

`EditorScopeTransaction.Prepare` validates identity, contribution, role, scope direction, required dependency, and capability-provider graphs without mutating visible registries. `Commit` swaps one immutable partition reference. A structural error returns diagnostics and leaves the old partition untouched.

- [x] **Step 5: Port current host behavior behind an adapter**

Create one compatibility adapter in the legacy executable that converts legacy built-in modules to `StaticEditorModuleRegistration`. It is the only caller of legacy `IEditorExtensionModule` after this Task. Mark the adapter for deletion in Task 8 and add an architecture test that prevents any new caller.

- [ ] **Step 6: Move services and preserve focused tests**

Move each existing test with its implementation owner. Replace namespace assertions with behavior/assembly ownership. Keep in-memory scene/frame-debug fixtures in test projects; production defaults must not create fixture worlds in constructors.

Service checkpoints: `EditorSelectionService` compiles from `Asharia.Studio.Application.Selection`, `EditorBackgroundTaskService` from `Asharia.Studio.Application.Tasks`, and `EditorLifecycleEventService` from `Asharia.Studio.Application.Lifecycle`. Their focused behavior tests compile from `Asharia.Studio.Application.Tests`; legacy consumers reference the Application implementations, and architecture gates enforce Application assembly ownership without relying on legacy physical directories.

- [ ] **Step 7: Run and commit**

```powershell
dotnet test apps/studio/tests/Asharia.Studio.Application.Tests/Asharia.Studio.Application.Tests.csproj -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
git add apps/studio/src/Asharia.Studio.Application apps/studio/tests/Asharia.Studio.Application.Tests apps/studio/Shell apps/studio/Core apps/studio/Tests apps/studio/Asharia.Studio.sln
git commit -m "refactor(studio): extract application module host"
```

---

### Task 6: Separate Avalonia authoring contracts from Studio presentation

**Slice size:** L

**Files:**

- Create: `apps/studio/src/Asharia.Editor.Avalonia/Asharia.Editor.Avalonia.csproj`
- Create: `apps/studio/src/Asharia.Editor.Avalonia/Panels/IAvaloniaContentLease.cs`
- Create: `apps/studio/src/Asharia.Editor.Avalonia/Panels/AvaloniaPanelBuilderExtensions.cs`
- Create: `apps/studio/src/Asharia.Editor.Avalonia/Theming/EditorThemeResourceKeys.cs`
- Create: `apps/studio/src/Asharia.Studio.Presentation.Avalonia/Asharia.Studio.Presentation.Avalonia.csproj`
- Move: `Shell/CodeFirstUI/**` to `Presentation.Avalonia/ExtensionUiBackends/CodeFirst/**`
- Move: `Shell/Docking/**` to `Presentation.Avalonia/Docking/**`
- Move: `Shell/ViewModels/**` to `Presentation.Avalonia/ViewModels/**`
- Move: `Shell/Views/**` to `Presentation.Avalonia/Views/**`
- Move: `UI/**` to `Presentation.Avalonia/UI/**`
- Move: `Shell/Services/AvaloniaEditorUiDispatcher.cs` to `Presentation.Avalonia/Threading/AvaloniaEditorUiDispatcher.cs`
- Create: `apps/studio/tests/Asharia.Studio.Presentation.Avalonia.Tests/Asharia.Studio.Presentation.Avalonia.Tests.csproj`
- Create: `apps/studio/tests/Asharia.Studio.Presentation.Avalonia.Tests/Extensions/AvaloniaContentLeaseHostTests.cs`

**Interfaces:**

- Consumes: Editor.Avalonia references Editor; Presentation references Editor, Editor.Avalonia, and Application.
- Produces: reversible attach/activate/deactivate/detach content leases, Code-first/Avalonia backend registries, Dock/Window ownership, and no Feature-specific code.

- [ ] **Step 1: Write lease lifecycle tests**

Prove the exact order `Attach -> Activate -> Deactivate -> Detach -> Dispose`, idempotent terminal disposal, KeepAlive reversible detach, Recreate terminal detach, and host `finally` cleanup when a callback throws.

- [ ] **Step 2: Implement the public lease contract**

```csharp
public interface IAvaloniaContentLease : IAsyncDisposable
{
    Control Content { get; }
    ValueTask AttachAsync(AvaloniaContentHostContext context, CancellationToken cancellationToken);
    ValueTask ActivateAsync(CancellationToken cancellationToken);
    ValueTask DeactivateAsync(CancellationToken cancellationToken);
    ValueTask DetachAsync(AvaloniaContentDetachReason reason, CancellationToken cancellationToken);
}
```

Do not expose Dock nodes, Window, TopLevel, compositor, or native presentation objects in `AvaloniaContentHostContext`.

- [ ] **Step 3: Create the two project files**

`Asharia.Editor.Avalonia` references Avalonia 12.0.4 and `Asharia.Editor`. Presentation references Avalonia packages, CommunityToolkit.Mvvm, Lucide.Avalonia, Editor, Editor.Avalonia, and Application. Neither project references EngineBridge in this Task.

- [ ] **Step 4: Move presentation files without Feature leakage**

After moving, run:

```powershell
rg -n "Asharia\.Studio\.BuiltInExtensions|Editor\.Features" apps/studio/src/Asharia.Studio.Presentation.Avalonia
```

Expected: no matches. Replace `ViewLocator` Feature type switches with backend/factory registration keyed by `GenerationScopedFactoryHandle`.

- [ ] **Step 5: Move tests by owner and run Avalonia headless tests**

All Dock, Window, Shell VM, Code-first adapter, controls, styles, and icon tests move to Presentation tests. Preserve `AvaloniaTestApplication` setup inside that test assembly.

- [ ] **Step 6: Run and commit**

```powershell
dotnet test apps/studio/tests/Asharia.Studio.Presentation.Avalonia.Tests/Asharia.Studio.Presentation.Avalonia.Tests.csproj -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
git add apps/studio/src/Asharia.Editor.Avalonia apps/studio/src/Asharia.Studio.Presentation.Avalonia apps/studio/tests apps/studio/Shell apps/studio/UI apps/studio/Asharia.Studio.sln
git commit -m "refactor(studio): isolate avalonia presentation boundary"
```

---

### Task 7: Separate native ABI, engine adapters, and Avalonia presentation

**Slice size:** L

**Files:**

- Create: `apps/studio/src/Asharia.Studio.EngineInterop/Asharia.Studio.EngineInterop.csproj`
- Create: `apps/studio/src/Asharia.Studio.EngineInterop/Viewports/ExternalImageDescriptor.cs`
- Create: `apps/studio/src/Asharia.Studio.EngineInterop/Viewports/ExternalSemaphoreDescriptor.cs`
- Create: `apps/studio/src/Asharia.Studio.EngineInterop/Viewports/IViewportFrameLease.cs`
- Create: `apps/studio/src/Asharia.Studio.EngineBridge/Asharia.Studio.EngineBridge.csproj`
- Move: `Core/Interop/Viewports/Api/**` to `EngineBridge/Abi/Viewports/**`
- Move: `Core/Interop/Viewports/Adapters/**` to `EngineBridge/Adapters/Viewports/**`
- Move: `Core/Interop/FrameDebugger/Api/**` to `EngineBridge/Abi/FrameDebugger/**`
- Move: `Core/Interop/FrameDebugger/Adapters/**` to `EngineBridge/Adapters/FrameDebugger/**`
- Move: `Features/SceneView/Interop/SceneViewCompositionCapabilityReader.cs` to `Presentation.Avalonia/Viewports/AvaloniaCompositionCapabilityReader.cs`
- Move: `Features/SceneView/Interop/SceneViewCompositionPresenter.cs` to `Presentation.Avalonia/Viewports/AvaloniaViewportPresenter.cs`
- Create: `apps/studio/tests/Asharia.Studio.EngineInterop.Tests/Asharia.Studio.EngineInterop.Tests.csproj`
- Create: `apps/studio/tests/Asharia.Studio.EngineBridge.Tests/Asharia.Studio.EngineBridge.Tests.csproj`
- Move: current native bridge tests to EngineBridge tests
- Modify: `apps/studio/src/Asharia.Studio.Presentation.Avalonia/Asharia.Studio.Presentation.Avalonia.csproj`

**Interfaces:**

- Consumes: EngineInterop references Editor; EngineBridge references Editor, Application, and EngineInterop; Presentation references EngineInterop but not EngineBridge implementation.
- Produces: UI-neutral external resource descriptors and frame leases, raw ABI isolated below EngineBridge, and Avalonia-only import-property mapping in Presentation.

- [ ] **Step 1: Write the boundary regression test**

```csharp
[Fact]
public void Native_present_packet_does_not_reference_avalonia()
{
    var references = typeof(ViewportNativePresentPacket).Assembly.GetReferencedAssemblies();
    Assert.DoesNotContain(references, item => item.Name!.StartsWith("Avalonia", StringComparison.Ordinal));
}
```

Also assert Presentation has no `LibraryImport`/`DllImport` tokens and no reference to EngineBridge.

- [ ] **Step 2: Define UI-neutral frame descriptors**

```csharp
public sealed record ExternalImageDescriptor(
    ExternalImageHandleType HandleType,
    nint Handle,
    int Width,
    int Height,
    ExternalImageFormat Format,
    ulong MemoryOffset,
    ulong MemorySize,
    bool TopLeftOrigin);

public interface IViewportFrameLease : IAsyncDisposable
{
    ViewportFrameSnapshot Snapshot { get; }
    ExternalImageDescriptor Image { get; }
    ExternalSemaphoreDescriptor WaitSemaphore { get; }
    ExternalSemaphoreDescriptor SignalSemaphore { get; }
}
```

The contract uses engine enums such as `VulkanOpaqueNt`, `VulkanOpaqueFd`, and `MetalSharedTexture` without exposing Vulkan/Avalonia SDK types.

- [ ] **Step 3: Remove Avalonia mapping from `ViewportNativePresentPacket`**

Delete `CreateAvaloniaImageProperties`. EngineBridge maps the packet to `IViewportFrameLease`; Presentation maps `ExternalImageDescriptor` to `PlatformGraphicsExternalImageProperties`.

- [ ] **Step 4: Move global native shutdown behind an owned Engine session**

Replace calls from `App.axaml.cs` and `MainWindow.axaml.cs` to static `ViewportNativeLibraryApi`/`ViewportNativePresentDrain` with an injected `IEngineSession`/`IViewportPresentationDrain` owned by the Application session. Closing waits for the drain; only the EngineBridge session invokes native shutdown.

- [ ] **Step 5: Add platform-path tests**

Test these names without requiring the current host OS:

```text
win-x64   -> editor_native.dll, slang.dll
linux-x64 -> libeditor_native.so, libslang.so
osx-arm64 -> libeditor_native.dylib, libslang.dylib
```

Do not claim the Linux/macOS native binaries exist until their CMake smoke Slice provides them.

- [ ] **Step 6: Run and commit**

```powershell
dotnet test apps/studio/tests/Asharia.Studio.EngineInterop.Tests/Asharia.Studio.EngineInterop.Tests.csproj -c Release
dotnet test apps/studio/tests/Asharia.Studio.EngineBridge.Tests/Asharia.Studio.EngineBridge.Tests.csproj -c Release
dotnet test apps/studio/tests/Asharia.Studio.Presentation.Avalonia.Tests/Asharia.Studio.Presentation.Avalonia.Tests.csproj -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
git add apps/studio/src/Asharia.Studio.EngineInterop apps/studio/src/Asharia.Studio.EngineBridge apps/studio/src/Asharia.Studio.Presentation.Avalonia apps/studio/tests apps/studio/Core apps/studio/Features/SceneView apps/studio/App.axaml.cs apps/studio/Shell
git commit -m "refactor(studio): isolate engine bridge and viewport interop"
```

---

### Task 8: Move built-in features onto the public module API

**Slice size:** L

**Files:**

- Create: `apps/studio/src/Asharia.Studio.BuiltInExtensions/Asharia.Studio.BuiltInExtensions.csproj`
- Create: `apps/studio/src/Asharia.Studio.BuiltInExtensions/BuiltInEditorModuleCatalog.cs`
- Create: `apps/studio/src/Asharia.Studio.BuiltInExtensions/Features/Workbench/WorkbenchApplicationModule.cs`
- Create: `apps/studio/src/Asharia.Studio.BuiltInExtensions/Features/SceneEditing/SceneEditingProjectModule.cs`
- Create: `apps/studio/src/Asharia.Studio.BuiltInExtensions/Features/Diagnostics/DiagnosticsProjectModule.cs`
- Create: `apps/studio/src/Asharia.Studio.BuiltInExtensions/Features/UiStyle/UiStyleProjectModule.cs`
- Move: `Features/Console/**`, `Features/FrameDebugger/**`, `Features/Hierarchy/**`, `Features/Inspector/**`, `Features/Problems/**`, `Features/SceneView/**`, and `Features/UiStyle/**` into matching BuiltInExtensions folders
- Delete: `Features/Workbench/WorkbenchFeatureModule.cs`
- Delete: `Shell/Composition/EditorFeatureCatalog.cs`
- Delete: `Shell/Composition/BuiltInContributionDescriptorAdapter.cs`
- Delete: `Shell/Compatibility/LegacyExtensions/**`
- Create: `apps/studio/tests/Asharia.Studio.ExtensionIntegration.Tests/Asharia.Studio.ExtensionIntegration.Tests.csproj`
- Create: `apps/studio/tests/Asharia.Studio.ExtensionIntegration.Tests/BuiltIns/BuiltInModuleContractTests.cs`
- Move: all Feature tests to ExtensionIntegration tests

**Interfaces:**

- Consumes: only Editor and Editor.Avalonia public APIs.
- Produces: explicit static registrations and independent Application/Project modules; no aggregate Workbench module and no dependency on host implementations.

- [ ] **Step 1: Add the hard ProjectReference gate**

```csharp
[Fact]
public void Built_ins_reference_only_public_editor_projects()
{
    var references = ProjectReferences("src/Asharia.Studio.BuiltInExtensions/Asharia.Studio.BuiltInExtensions.csproj");
    Assert.Equal(
        ["Asharia.Editor", "Asharia.Editor.Avalonia"],
        references.Order(StringComparer.Ordinal).ToArray());
}
```

- [ ] **Step 2: Create independent module entries**

Use this exact identity and responsibility table:

| Type | Stable ID | Scope | Activation | Contributions |
| --- | --- | --- | --- | --- |
| `WorkbenchApplicationModule` | `studio.workbench` | Application | OnScopeReady | Command Palette and About commands |
| `SceneEditingProjectModule` | `studio.scene-editing` | Project | OnScopeReady | Scene View, Hierarchy, Inspector and active-scene provider |
| `DiagnosticsProjectModule` | `studio.diagnostics` | Project | OnScopeReady | Console, Problems and Frame Debugger |
| `UiStyleProjectModule` | `studio.ui-style` | Project | OnDemand | UI Style panel with trigger `OnPanel:ui-style` |

Each class derives from `EditorModule`, implements `Configure(EditorModuleBuilder)`, and declares only the contributions in this table. Factories receive a scope/panel context and resolve public services there. Definition fields may contain only immutable configuration.

- [ ] **Step 3: Replace aggregate constructor injection**

Delete `WorkbenchFeatureModule` constructors that create diagnostics, scene fixtures, frame-debug fixtures, or Avalonia dispatchers. Application composition provides real service implementations; tests register explicit fakes.

- [ ] **Step 4: Register Code-first and Avalonia panels through public builders**

Frame Debugger and UI Style use Code-first builder extensions. Scene View, Hierarchy, Inspector, Console, and Problems use `Asharia.Editor.Avalonia` content factories. No built-in module constructs a Dock, Window, `CodeFirstPanelHostViewModel`, or `AvaloniaEditorUiDispatcher`.

- [ ] **Step 5: Delete the legacy host surface**

After all existing Feature tests pass through `StaticPackageGenerationHost`, delete the compatibility adapter and legacy module/descriptors. Add an `rg` gate:

```powershell
rg -n "IEditorFeatureModule|IEditorExtensionModule|WorkbenchFeatureModule|BuiltInContributionDescriptorAdapter|Func<object>" apps/studio/src apps/studio/tests
```

Expected: no matches.

- [ ] **Step 6: Run and commit**

```powershell
dotnet test apps/studio/tests/Asharia.Studio.ExtensionIntegration.Tests/Asharia.Studio.ExtensionIntegration.Tests.csproj -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
git add apps/studio/src/Asharia.Studio.BuiltInExtensions apps/studio/tests apps/studio/Features apps/studio/Shell apps/studio/Asharia.Studio.sln
git commit -m "refactor(studio): dogfood public editor modules"
```

---

### Task 9: Make `Asharia.Studio.App` the only composition root

**Slice size:** M

**Files:**

- Create: `apps/studio/src/Asharia.Studio.App/Asharia.Studio.App.csproj`
- Move: `Program.cs` to `src/Asharia.Studio.App/Program.cs`
- Move: `App.axaml` to `src/Asharia.Studio.App/App.axaml`
- Move: `App.axaml.cs` to `src/Asharia.Studio.App/App.axaml.cs`
- Move: `app.manifest` to `src/Asharia.Studio.App/app.manifest`
- Move: `Assets/**` to `src/Asharia.Studio.App/Assets/**`
- Create: `apps/studio/src/Asharia.Studio.App/Composition/StudioApplicationFactory.cs`
- Create: `apps/studio/src/Asharia.Studio.App/Composition/StudioApplicationSession.cs`
- Move: current `ViewLocator.cs` behavior into Presentation backend registration; delete the Feature type switch
- Create: `apps/studio/tests/Asharia.Studio.ExtensionIntegration.Tests/App/StudioApplicationFactoryTests.cs`
- Modify: `apps/studio/Asharia.Studio.sln`
- Delete: `apps/studio/Editor.csproj`
- Delete: `apps/studio/Editor.sln`
- Delete: empty legacy `Core/`, `Shell/`, `UI/`, `Features/`, and `Tests/Editor.Tests/` paths after `git status` proves all tracked files were moved

**Interfaces:**

- Consumes: App references Application, EngineBridge, Presentation.Avalonia, and BuiltInExtensions.
- Produces: one explicit process composition root and one owned application session whose async disposal enforces presentation-before-engine shutdown.

- [ ] **Step 1: Write composition tests before creating App**

Prove that factory construction registers the static built-in catalog exactly once, creates Application scope before Project scope, and disposes in this order:

```text
Project panels/modules -> Project providers -> native-safe barrier -> Engine session
-> Application modules -> Presentation windows -> process services
```

- [ ] **Step 2: Create the executable project**

The project references exactly Application, EngineBridge, Presentation.Avalonia, and BuiltInExtensions. Move all Avalonia package references out of App except `Avalonia.Desktop`, `Avalonia.Fonts.Inter`, and diagnostics needed by `Program` startup.

- [ ] **Step 3: Implement explicit composition**

`StudioApplicationFactory.CreateAsync` constructs implementations and supplies them through public service/capability registries. It must not expose a static service provider or `CurrentProject`. Built-in registration comes from `BuiltInEditorModuleCatalog.Registrations` and is attached through `StaticPackageGenerationHost`.

- [ ] **Step 4: Move native runtime copy policy to App**

Retain the existing Windows behavior. Select library names by explicit RID and emit an actionable build error for missing Linux/macOS artifacts instead of silently copying Windows DLLs. Update `EditorNativeRuntimeCopyTests` into App integration tests with parameterized RID path-policy coverage and a host-RID file existence check.

- [ ] **Step 5: Switch the solution and remove legacy projects**

Run the new App once before deletion:

```powershell
dotnet build apps/studio/src/Asharia.Studio.App/Asharia.Studio.App.csproj -c Release
dotnet test apps/studio/Asharia.Studio.sln -c Release
```

Then remove legacy solution/project entries and files. Do not leave forwarding MSBuild projects that can diverge.

- [ ] **Step 6: Run and commit**

```powershell
dotnet test apps/studio/Asharia.Studio.sln -c Release
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
git diff --check
git add apps/studio
git commit -m "refactor(studio): switch to explicit app composition"
```

---

### Task 10: Finish test ownership, API baselines, and three-platform managed gates

**Slice size:** M

**Files:**

- Modify: every `apps/studio/tests/*.Tests/*.csproj`
- Create: `apps/studio/tests/Asharia.Editor.Tests/PublicApi/Asharia.Editor.PublicAPI.Shipped.txt`
- Create: `apps/studio/tests/Asharia.Editor.Tests/PublicApi/Asharia.Editor.PublicAPI.Unshipped.txt`
- Create: `apps/studio/tests/Asharia.Studio.Architecture.Tests/ForbiddenReferenceTests.cs`
- Create: `apps/studio/tests/Asharia.Studio.Architecture.Tests/NamespaceOwnershipTests.cs`
- Create: `apps/studio/tests/Asharia.Studio.ExtensionIntegration.Tests/Lifecycle/ProjectCloseOrderingTests.cs`
- Create: `apps/studio/tests/Asharia.Studio.ExtensionIntegration.Tests/Lifecycle/EngineUnavailableTests.cs`
- Create: `.github/workflows/studio-managed.yml`
- Modify: `apps/studio/docs/architecture/studio-code-framework.md`
- Modify: `apps/studio/docs/architecture/README.md`
- Modify: `apps/studio/docs/workflow` documentation if the repository has a Studio-specific workflow page; otherwise update `docs/workflow/review.md`

**Interfaces:**

- Consumes: the complete Wave 1 project graph.
- Produces: public API review, forbidden-reference enforcement, lifecycle integration evidence, and managed build/test coverage on Windows, Ubuntu, and macOS.

- [ ] **Step 1: Add public API analyzers to `Asharia.Editor`**

Reference `Microsoft.CodeAnalysis.PublicApiAnalyzers` as a private analyzer and commit shipped/unshipped baselines. Treat a public API diff as a review event; do not auto-regenerate the baseline in CI.

- [ ] **Step 2: Encode the complete ProjectReference matrix**

Assert the exact graph from `studio-code-framework.md`, including the negative edges. Also reject production `InternalsVisibleTo`, direct P/Invoke outside EngineBridge, Avalonia outside Editor.Avalonia/Presentation/BuiltIn/App startup, and BuiltIn references outside the two public Editor projects.

- [ ] **Step 3: Add lifecycle integration gates**

Use deterministic fakes to prove Project close does not stop Engine before panel/module/provider leases reach zero, a failed native-safe barrier leaves the Project quarantined with Engine alive, Engine unavailable publishes Waiting/Blocked placeholders, and Application close stops when a Project cannot cross the barrier.

- [ ] **Step 4: Add the managed CI matrix**

Use:

```yaml
strategy:
  matrix:
    os: [windows-latest, ubuntu-latest, macos-latest]
steps:
  - uses: actions/checkout@v4
  - uses: actions/setup-dotnet@v4
    with:
      dotnet-version: '10.0.x'
  - run: dotnet test apps/studio/Asharia.Studio.sln -c Release
```

Tests that require native binaries must be separated and skipped only with an explicit reason on hosts where the native build Slice is not yet available. UI-neutral projects and public API tests must never be skipped.

- [ ] **Step 5: Run all Wave 1 gates**

```powershell
dotnet test apps/studio/Asharia.Studio.sln -c Release
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Record Linux/macOS managed CI URLs. Record native backend gaps as follow-up Slice links, not as a claim that the backend passed.

- [ ] **Step 6: Commit**

```powershell
git add apps/studio/tests apps/studio/src/Asharia.Editor .github/workflows/studio-managed.yml apps/studio/docs docs/workflow
git commit -m "test(studio): enforce editor framework boundaries"
```

---

## Wave 1 Completion Gate

Wave 1 is complete only when all statements are true:

- `Editor.csproj`, `Editor.sln`, `IEditorFeatureModule`, `IEditorExtensionModule`, `WorkbenchFeatureModule`, and `PanelDescriptor(Func<object>)` no longer exist.
- `Asharia.Editor` and Code-first contracts have zero Avalonia/native/Studio implementation references.
- Built-in modules compile with only Editor and Editor.Avalonia references.
- App is the only executable and the only place that composes Application, Bridge, Presentation, and Built-ins.
- Existing user-visible panels, commands, Dock persistence, diagnostics, frame-debug snapshots, Scene View presentation, and latest status feedback retain test coverage.
- Engine/native shutdown is owned by an application/project session and not by static calls from a Window or View.
- The managed solution passes on Windows, Linux, and macOS; native smoke results are reported per actual backend availability.
- Architecture docs describe the implemented state and keep Wave 2/3 contracts explicitly labeled as planned.

## Required Follow-up Slices

Create these only after Wave 1 lands, because their exact file paths and tests depend on the final project graph:

1. `[Slice] Studio: generate project Editor assemblies from Asharia asmdef`
   - Inputs: root `Editor/`, Asharia `*.asmdef`, `asharia.package.json.editor`.
   - Exit: deterministic SDK project generation, project-scoped NuGet locks, module index, build diagnostics; no runtime load.
2. `[Slice] Studio: resolve Package generations and catalog locks`
   - Exit: SemVer comparator-set resolver, artifact selection, immutable PackageGenerationId, exact closure reuse, prepared/current/previous catalog recovery.
3. `[Slice] Studio: load static and pinned Package generations`
   - Exit: exact shared/cross-Package/private dependency resolution, Pinned host, PendingRestart boot attempt, no collectible unload claim.
4. `[Slice] Studio: enable collectible managed extension reload`
   - Exit: lease tracking, QTA propagation, rollback/resume diagnostics, unload probes, LKG and cache retention.
5. `[Slice] Studio: introduce ProjectSession and Play Mode domains`
   - Exit: Project scope partition transaction, Edit/Play/Preview worlds, in-Studio Game View and standalone presentation policy.
6. `[Slice] Studio/Renderer: validate viewport presentation on Windows Linux macOS`
   - Exit: real per-platform handle/sync backend, device-lost Epoch recovery, native-safe barrier smoke, copy/readback fallback evidence.

Each follow-up needs its own design check and implementation plan. Do not copy Wave 1 assumptions into loader, GPU synchronization, or serialized project schemas without revalidating the now-current code.

## Self-review Checklist

- Every target runtime project has an owning Task and an exact dependency list.
- Every current top-level source area has a target owner in the file map.
- The plan removes, rather than preserves, the current aggregate Workbench module and legacy `Func<object>` panel ABI.
- Native packet/Avalonia coupling and static shutdown ownership have explicit migration Tasks.
- Windows/Linux/macOS support is enforced at managed boundaries without falsely claiming unfinished native backends.
- Dynamic build/load/reload and Play Mode are split into follow-up Slices instead of being hidden inside structural moves.
- No Task requires a later Task to repair a knowingly broken solution.
