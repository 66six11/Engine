# Studio Extension Lifecycle Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Slice 1 of the Studio extension lifecycle design: a source-agnostic host that declares built-in module contributions once and commits panel/action registries through one composition path.

**Architecture:** Add a small `EditorExtensionHost` in `Shell/Composition` backed by Core contracts for extension id, contribution builder, extension module, and activation context. `WorkbenchFeatureModule` moves from separate `RegisterPanels` / `RegisterActions` calls to one `Declare()` call, while Panel opening, command execution, provider lifecycle, plugin loading, ALC, script VM, and C++ bridge remain out of scope.

**Tech Stack:** .NET 10, C# records/interfaces, Avalonia app composition, xUnit, existing `PanelRegistry`, `WorkbenchActionRegistry`, `WorkbenchFeatureModule`, `MainWindowViewModel`.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-extension-lifecycle-v0-design.md`

This plan implements only Slice 1: Host And Composition Root.

## File Structure

Create:

- `Core/Models/EditorExtensionId.cs` - stable validated id value for trusted editor extension modules.
- `Core/Abstractions/IEditorContributionBuilder.cs` - data-only declaration sink for panel/action contributions.
- `Core/Abstractions/IEditorExtensionActivationContext.cs` - typed activation context marker; no service locator.
- `Core/Abstractions/IEditorExtensionModule.cs` - source-agnostic module contract with `Declare()` and optional activation.
- `Shell/Composition/EditorContributionBuilder.cs` - temporary builder used during declaration.
- `Shell/Composition/EditorDeclaredContributions.cs` - immutable contribution batch owned by one extension id.
- `Shell/Composition/EditorExtensionActivationContext.cs` - current empty activation context singleton.
- `Shell/Composition/EditorExtensionComposition.cs` - returned panel/action registries from the host.
- `Shell/Composition/EditorExtensionHost.cs` - declares modules, validates duplicate ids before commit, commits registries, activates and disposes leases in reverse order.
- `Shell/Composition/StudioCompositionRoot.cs` - creates selection service, extension composition, saved layout, and root `MainWindowViewModel`.
- `Tests/Editor.Tests/Shell/Composition/EditorExtensionHostTests.cs` - focused host tests.
- `Tests/Editor.Tests/Shell/Composition/StudioCompositionRootTests.cs` - composition root smoke tests.

Modify:

- `Core/Abstractions/IEditorFeatureModule.cs` - make it a marker interface over `IEditorExtensionModule`.
- `Features/Workbench/WorkbenchFeatureModule.cs` - replace `RegisterPanels` / `RegisterActions` with `Declare()`.
- `Shell/Composition/EditorFeatureCatalog.cs` - return built-in extension modules once per composition.
- `Shell/ViewModels/MainWindowViewModel.cs` - replace two default registry factory calls with one default composition path; keep test helpers routed through the same host.
- `App.axaml.cs` - use `StudioCompositionRoot`.
- `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs` - test declared workbench contributions through the host composition.
- `Tests/Editor.Tests/Shell/Composition/EditorFeatureCatalogTests.cs` - test default composition rather than manual dual registration.
- `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs` - update helper construction to use `CreateDefaultComposition()`.
- `Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs` - update helper construction to use `CreateDefaultComposition()`.
- `docs/Dock系统指南.md` - record the new current fact: built-in Feature modules are composed once through `EditorExtensionHost v0`; no external plugin loading.
- `docs/编辑器UI平台规范.md` - mark `EditorExtensionHost v0` as Current for built-in panel/action composition only; keep provider/plugin/native lifecycle Deferred.

## Task 1: Core Extension Contracts

**Files:**
- Create: `Core/Models/EditorExtensionId.cs`
- Create: `Core/Abstractions/IEditorContributionBuilder.cs`
- Create: `Core/Abstractions/IEditorExtensionActivationContext.cs`
- Create: `Core/Abstractions/IEditorExtensionModule.cs`
- Modify: `Core/Abstractions/IEditorFeatureModule.cs`

- [ ] **Step 1: Write failing contract compile tests**

Create `Tests/Editor.Tests/Shell/Composition/EditorExtensionHostTests.cs` with the following initial tests. These compile-fail until the Core contracts and host exist.

```csharp
using System;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorExtensionHostTests
{
    [Fact]
    public void Compose_declares_each_module_once_and_registers_panels_and_actions_together()
    {
        var module = new TestExtensionModule(
            "test.extension",
            builder =>
            {
                builder.AddPanel(CreatePanel("panel"));
                builder.AddAction(CreateAction("command", "panel"));
            });
        var host = new EditorExtensionHost([module]);

        var composition = host.Compose();

        Assert.Equal(1, module.DeclareCount);
        Assert.Equal(["panel"], composition.PanelRegistry.GetAll().Select(panel => panel.Id));
        Assert.Equal(["command"], composition.ActionRegistry.GetAll().Select(action => action.Id));
    }

    private static PanelDescriptor CreatePanel(string id)
    {
        return new PanelDescriptor(
            id,
            "Panel",
            PanelKind.Tool,
            DockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => new object());
    }

    private static WorkbenchActionDescriptor CreateAction(string id, string targetPanelId)
    {
        return new WorkbenchActionDescriptor(
            id,
            "Panel",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Panel",
            TargetId: targetPanelId,
            Category: "Window");
    }

    private sealed class TestExtensionModule(
        string id,
        Action<IEditorContributionBuilder> declare) : IEditorExtensionModule
    {
        public EditorExtensionId Id { get; } = new(id);

        public int DeclareCount { get; private set; }

        public void Declare(IEditorContributionBuilder builder)
        {
            DeclareCount++;
            declare(builder);
        }
    }
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests"
```

Expected: FAIL at compile time because `EditorExtensionHost`, `IEditorExtensionModule`, `IEditorContributionBuilder`, `IEditorExtensionActivationContext`, and `EditorExtensionId` do not exist.

- [ ] **Step 3: Add Core contracts**

Create `Core/Models/EditorExtensionId.cs`:

```csharp
using System;

namespace Editor.Core.Models;

public readonly record struct EditorExtensionId
{
    public EditorExtensionId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Editor extension id must not be empty.", nameof(value));
        }

        Value = value;
    }

    public string Value { get; }

    public override string ToString()
    {
        return Value;
    }
}
```

Create `Core/Abstractions/IEditorContributionBuilder.cs`:

```csharp
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorContributionBuilder
{
    void AddPanel(PanelDescriptor descriptor);

    void AddAction(WorkbenchActionDescriptor descriptor);
}
```

Create `Core/Abstractions/IEditorExtensionActivationContext.cs`:

```csharp
namespace Editor.Core.Abstractions;

public interface IEditorExtensionActivationContext
{
}
```

Create `Core/Abstractions/IEditorExtensionModule.cs`:

```csharp
using System;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorExtensionModule
{
    EditorExtensionId Id { get; }

    void Declare(IEditorContributionBuilder builder);

    ValueTask<IAsyncDisposable?> ActivateAsync(
        IEditorExtensionActivationContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult<IAsyncDisposable?>(null);
    }
}
```

Replace `Core/Abstractions/IEditorFeatureModule.cs` with:

```csharp
namespace Editor.Core.Abstractions;

public interface IEditorFeatureModule : IEditorExtensionModule
{
}
```

- [ ] **Step 4: Run the focused test and verify remaining failures**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests"
```

Expected: FAIL because `EditorExtensionHost` and composition implementation still do not exist.

- [ ] **Step 5: Continue without committing**

Do not commit after this task. Changing `IEditorFeatureModule` to inherit `IEditorExtensionModule` intentionally requires Task 3 to migrate `WorkbenchFeatureModule` before the full project can compile.

## Task 2: EditorExtensionHost Composition

**Files:**
- Create: `Shell/Composition/EditorContributionBuilder.cs`
- Create: `Shell/Composition/EditorDeclaredContributions.cs`
- Create: `Shell/Composition/EditorExtensionActivationContext.cs`
- Create: `Shell/Composition/EditorExtensionComposition.cs`
- Create: `Shell/Composition/EditorExtensionHost.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/EditorExtensionHostTests.cs`

- [ ] **Step 1: Add failing host behavior tests**

Append these tests to `Tests/Editor.Tests/Shell/Composition/EditorExtensionHostTests.cs`:

```csharp
[Fact]
public void Compose_rejects_duplicate_panel_ids_before_returning_composition()
{
    var host = new EditorExtensionHost(
        [
            new TestExtensionModule("first.extension", builder => builder.AddPanel(CreatePanel("duplicate"))),
            new TestExtensionModule("second.extension", builder => builder.AddPanel(CreatePanel("duplicate"))),
        ]);

    var exception = Assert.Throws<InvalidOperationException>(() => host.Compose());

    Assert.Contains("Panel id 'duplicate'", exception.Message);
    Assert.Contains("first.extension", exception.Message);
    Assert.Contains("second.extension", exception.Message);
}

[Fact]
public void Compose_rejects_duplicate_action_ids_before_returning_composition()
{
    var host = new EditorExtensionHost(
        [
            new TestExtensionModule("first.extension", builder => builder.AddAction(CreateAction("duplicate", "panel"))),
            new TestExtensionModule("second.extension", builder => builder.AddAction(CreateAction("duplicate", "panel"))),
        ]);

    var exception = Assert.Throws<InvalidOperationException>(() => host.Compose());

    Assert.Contains("Workbench action id 'duplicate'", exception.Message);
    Assert.Contains("first.extension", exception.Message);
    Assert.Contains("second.extension", exception.Message);
}

[Fact]
public async Task ActivateAsync_disposes_started_leases_in_reverse_order_when_later_module_fails()
{
    var disposalOrder = new List<string>();
    var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "first.extension",
                _ => { },
                _ => ValueTask.FromResult<IAsyncDisposable?>(new RecordingLease("first", disposalOrder))),
            new TestExtensionModule(
                "second.extension",
                _ => { },
                _ => ValueTask.FromResult<IAsyncDisposable?>(new RecordingLease("second", disposalOrder))),
            new TestExtensionModule(
                "failing.extension",
                _ => { },
                _ => throw new InvalidOperationException("activation failed")),
        ]);

    var exception = await Assert.ThrowsAsync<InvalidOperationException>(
        async () => await host.ActivateAsync());

    Assert.Equal("activation failed", exception.Message);
    Assert.Equal(["second", "first"], disposalOrder);
}
```

Update the test helper in the same file:

```csharp
private sealed class TestExtensionModule(
    string id,
    Action<IEditorContributionBuilder> declare,
    Func<CancellationToken, ValueTask<IAsyncDisposable?>>? activate = null) : IEditorExtensionModule
{
    public EditorExtensionId Id { get; } = new(id);

    public int DeclareCount { get; private set; }

    public void Declare(IEditorContributionBuilder builder)
    {
        DeclareCount++;
        declare(builder);
    }

    public ValueTask<IAsyncDisposable?> ActivateAsync(
        IEditorExtensionActivationContext context,
        CancellationToken cancellationToken)
    {
        return activate?.Invoke(cancellationToken) ?? ValueTask.FromResult<IAsyncDisposable?>(null);
    }
}

private sealed class RecordingLease(string id, List<string> disposalOrder) : IAsyncDisposable
{
    public ValueTask DisposeAsync()
    {
        disposalOrder.Add(id);
        return ValueTask.CompletedTask;
    }
}
```

Ensure the file has these usings:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
```

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests"
```

Expected: FAIL because the host implementation is missing duplicate validation and activation lease handling.

- [ ] **Step 3: Add Shell composition implementation**

Create `Shell/Composition/EditorDeclaredContributions.cs`:

```csharp
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Shell.Composition;

internal sealed record EditorDeclaredContributions(
    EditorExtensionId OwnerId,
    IReadOnlyList<PanelDescriptor> Panels,
    IReadOnlyList<WorkbenchActionDescriptor> Actions);
```

Create `Shell/Composition/EditorContributionBuilder.cs`:

```csharp
using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Composition;

internal sealed class EditorContributionBuilder : IEditorContributionBuilder
{
    private readonly List<PanelDescriptor> panels_ = [];
    private readonly List<WorkbenchActionDescriptor> actions_ = [];

    public void AddPanel(PanelDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        panels_.Add(descriptor);
    }

    public void AddAction(WorkbenchActionDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        actions_.Add(descriptor);
    }

    public EditorDeclaredContributions Build(EditorExtensionId ownerId)
    {
        return new EditorDeclaredContributions(
            ownerId,
            panels_.ToArray(),
            actions_.ToArray());
    }
}
```

Create `Shell/Composition/EditorExtensionActivationContext.cs`:

```csharp
using Editor.Core.Abstractions;

namespace Editor.Shell.Composition;

internal sealed class EditorExtensionActivationContext : IEditorExtensionActivationContext
{
    public static EditorExtensionActivationContext Instance { get; } = new();

    private EditorExtensionActivationContext()
    {
    }
}
```

Create `Shell/Composition/EditorExtensionComposition.cs`:

```csharp
using Editor.Core.Abstractions;

namespace Editor.Shell.Composition;

internal sealed record EditorExtensionComposition(
    IPanelRegistry PanelRegistry,
    IWorkbenchActionRegistry ActionRegistry);
```

Create `Shell/Composition/EditorExtensionHost.cs`:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Editor.Shell.Docking;

namespace Editor.Shell.Composition;

internal sealed class EditorExtensionHost(IEnumerable<IEditorExtensionModule> modules) : IAsyncDisposable
{
    private readonly IReadOnlyList<IEditorExtensionModule> modules_ =
        modules?.ToArray() ?? throw new ArgumentNullException(nameof(modules));
    private readonly List<IAsyncDisposable> activationLeases_ = [];

    public EditorExtensionComposition Compose()
    {
        ValidateUniqueExtensionIds();
        var declarations = DeclareModules();
        ValidateUniquePanelIds(declarations);
        ValidateUniqueActionIds(declarations);

        var panelRegistry = new PanelRegistry();
        var actionRegistry = new WorkbenchActionRegistry();

        foreach (var declaration in declarations)
        {
            foreach (var panel in declaration.Panels)
            {
                panelRegistry.Register(panel);
            }

            foreach (var action in declaration.Actions)
            {
                actionRegistry.Register(action);
            }
        }

        return new EditorExtensionComposition(panelRegistry, actionRegistry);
    }

    public async ValueTask ActivateAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            foreach (var module in modules_)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var lease = await module.ActivateAsync(
                    EditorExtensionActivationContext.Instance,
                    cancellationToken);
                if (lease is not null)
                {
                    activationLeases_.Add(lease);
                }
            }
        }
        catch
        {
            await DisposeAsync();
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        for (var index = activationLeases_.Count - 1; index >= 0; index--)
        {
            await activationLeases_[index].DisposeAsync();
        }

        activationLeases_.Clear();
    }

    private IReadOnlyList<EditorDeclaredContributions> DeclareModules()
    {
        var declarations = new List<EditorDeclaredContributions>();
        foreach (var module in modules_)
        {
            var builder = new EditorContributionBuilder();
            module.Declare(builder);
            declarations.Add(builder.Build(module.Id));
        }

        return declarations;
    }

    private void ValidateUniqueExtensionIds()
    {
        var ownersById = new HashSet<string>(StringComparer.Ordinal);
        foreach (var module in modules_)
        {
            if (!ownersById.Add(module.Id.Value))
            {
                throw new InvalidOperationException(
                    $"Editor extension id '{module.Id.Value}' is already registered.");
            }
        }
    }

    private static void ValidateUniquePanelIds(IReadOnlyList<EditorDeclaredContributions> declarations)
    {
        var ownersByPanelId = new Dictionary<string, EditorExtensionId>(StringComparer.Ordinal);
        foreach (var declaration in declarations)
        {
            foreach (var panel in declaration.Panels)
            {
                if (ownersByPanelId.TryGetValue(panel.Id, out var existingOwner))
                {
                    throw new InvalidOperationException(
                        $"Panel id '{panel.Id}' is contributed by both '{existingOwner}' and '{declaration.OwnerId}'.");
                }

                ownersByPanelId.Add(panel.Id, declaration.OwnerId);
            }
        }
    }

    private static void ValidateUniqueActionIds(IReadOnlyList<EditorDeclaredContributions> declarations)
    {
        var ownersByActionId = new Dictionary<string, EditorExtensionId>(StringComparer.Ordinal);
        foreach (var declaration in declarations)
        {
            foreach (var action in declaration.Actions)
            {
                if (ownersByActionId.TryGetValue(action.Id, out var existingOwner))
                {
                    throw new InvalidOperationException(
                        $"Workbench action id '{action.Id}' is contributed by both '{existingOwner}' and '{declaration.OwnerId}'.");
                }

                ownersByActionId.Add(action.Id, declaration.OwnerId);
            }
        }
    }
}
```

- [ ] **Step 4: Run focused tests and verify pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests"
```

Expected: PASS.

- [ ] **Step 5: Continue without committing**

Do not commit after this task. The project still needs Task 3 to migrate the built-in `WorkbenchFeatureModule` from the old registration methods to `Declare()`.

## Task 3: WorkbenchFeatureModule Declaration Migration

**Files:**
- Modify: `Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/EditorFeatureCatalogTests.cs`
- Modify: `Shell/Composition/EditorFeatureCatalog.cs`

- [ ] **Step 1: Update Workbench tests to consume host composition**

In `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`, replace direct `RegisterPanels()` and `RegisterActions()` calls with a helper that composes a host from one `WorkbenchFeatureModule`:

```csharp
private static EditorExtensionComposition ComposeWorkbench(IEditorSelectionService? selectionService = null)
{
    selectionService ??= new EditorSelectionService();
    var host = new EditorExtensionHost([new WorkbenchFeatureModule(selectionService)]);
    return host.Compose();
}
```

Update the first test setup:

```csharp
var descriptors = ComposeWorkbench().PanelRegistry.GetAll().ToArray();
```

Update the stable action test setup:

```csharp
var registry = ComposeWorkbench().ActionRegistry;
```

Update the shared selection/provider test setup:

```csharp
var selectionService = new EditorSelectionService();
var registry = ComposeWorkbench(selectionService).PanelRegistry;
```

Ensure the file has these usings:

```csharp
using Editor.Core.Abstractions;
using Editor.Shell.Composition;
```

- [ ] **Step 2: Update feature catalog tests to use host composition**

Replace `Tests/Editor.Tests/Shell/Composition/EditorFeatureCatalogTests.cs` with:

```csharp
using System.Linq;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorFeatureCatalogTests
{
    [Fact]
    public void CreateDefaultModules_registers_default_workbench_panels_through_extension_host()
    {
        var composition = ComposeDefault();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            composition.PanelRegistry.GetAll().Select(descriptor => descriptor.Id));
    }

    [Fact]
    public void CreateDefaultModules_registers_default_workbench_actions_through_extension_host()
    {
        var composition = ComposeDefault();

        Assert.Equal(
            [
                "workbench.commandPalette.open",
                "workbench.about.open",
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
                "workbench.panel.inspector",
                "workbench.panel.console",
                "workbench.panel.problems",
            ],
            composition.ActionRegistry.GetAll().Select(action => action.Id));
    }

    private static EditorExtensionComposition ComposeDefault()
    {
        return new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules()).Compose();
    }
}
```

- [ ] **Step 3: Run focused tests and verify fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~EditorFeatureCatalogTests"
```

Expected: FAIL because `WorkbenchFeatureModule` still exposes `RegisterPanels()` / `RegisterActions()` and does not implement `Declare()`.

- [ ] **Step 4: Migrate WorkbenchFeatureModule**

Modify `Features/Workbench/WorkbenchFeatureModule.cs`:

1. Add an `Id` property:

```csharp
public EditorExtensionId Id { get; } = new("studio.workbench");
```

2. Replace `RegisterPanels()` and `RegisterActions()` with:

```csharp
public void Declare(IEditorContributionBuilder builder)
{
    ArgumentNullException.ThrowIfNull(builder);

    var panelDescriptors = CreatePanelDescriptors();
    foreach (var descriptor in panelDescriptors)
    {
        builder.AddPanel(descriptor);
    }

    builder.AddAction(new WorkbenchActionDescriptor(
        "workbench.commandPalette.open",
        "Command Palette",
        WorkbenchActionKind.OpenCommandPalette,
        "Tools/Command Palette",
        IconKey: EditorIconKey.UiSearch,
        Category: "Tools",
        DefaultShortcut: "Ctrl+Shift+P",
        SearchText: "command palette launcher"));
    builder.AddAction(new WorkbenchActionDescriptor(
        "workbench.about.open",
        "About",
        WorkbenchActionKind.OpenAboutDialog,
        "Help/About",
        Category: "Help",
        SearchText: "about studio version information"));

    foreach (var descriptor in panelDescriptors)
    {
        builder.AddAction(new WorkbenchActionDescriptor(
            $"workbench.panel.{descriptor.Id}",
            descriptor.Title,
            WorkbenchActionKind.OpenPanel,
            descriptor.MenuPath,
            TargetId: descriptor.Id,
            IconKey: descriptor.IconKey,
            Category: "Window",
            SearchText: CommandSearchTextForPanel(descriptor.Id)));
    }
}
```

3. Ensure the file has `using System;` because `ArgumentNullException` is now used.

Keep `Shell/Composition/EditorFeatureCatalog.cs` returning built-in feature modules with this exact signature:

```csharp
public static IReadOnlyList<IEditorFeatureModule> CreateDefaultModules(
    IEditorSelectionService? selectionService = null)
```

This return type is valid because `IEditorFeatureModule` now inherits `IEditorExtensionModule`.

- [ ] **Step 5: Run focused tests and verify pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~EditorFeatureCatalogTests|FullyQualifiedName~EditorExtensionHostTests"
```

Expected: PASS.

- [ ] **Step 6: Commit contracts, host, and Workbench migration**

```powershell
git add Core\Models\EditorExtensionId.cs `
        Core\Abstractions\IEditorContributionBuilder.cs `
        Core\Abstractions\IEditorExtensionActivationContext.cs `
        Core\Abstractions\IEditorExtensionModule.cs `
        Core\Abstractions\IEditorFeatureModule.cs `
        Shell\Composition\EditorContributionBuilder.cs `
        Shell\Composition\EditorDeclaredContributions.cs `
        Shell\Composition\EditorExtensionActivationContext.cs `
        Shell\Composition\EditorExtensionComposition.cs `
        Shell\Composition\EditorExtensionHost.cs `
        Features\Workbench\WorkbenchFeatureModule.cs `
        Shell\Composition\EditorFeatureCatalog.cs `
        Tests\Editor.Tests\Shell\Composition\EditorExtensionHostTests.cs `
        Tests\Editor.Tests\Features\Workbench\WorkbenchFeatureModuleTests.cs `
        Tests\Editor.Tests\Shell\Composition\EditorFeatureCatalogTests.cs
git commit -m "feat: declare workbench contributions once"
```

## Task 4: Studio Composition Root

**Files:**
- Create: `Shell/Composition/StudioCompositionRoot.cs`
- Modify: `App.axaml.cs`
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/StudioCompositionRootTests.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
- Modify: `Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs`

- [ ] **Step 1: Add failing composition root tests**

Create `Tests/Editor.Tests/Shell/Composition/StudioCompositionRootTests.cs`:

```csharp
using System.Linq;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioCompositionRootTests
{
    [Fact]
    public void CreateDefaultComposition_declares_modules_once_for_panel_and_action_registries()
    {
        var composition = StudioCompositionRoot.CreateDefaultComposition();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            composition.PanelRegistry.GetAll().Select(panel => panel.Id));
        Assert.Equal(
            [
                "workbench.commandPalette.open",
                "workbench.about.open",
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
                "workbench.panel.inspector",
                "workbench.panel.console",
                "workbench.panel.problems",
            ],
            composition.ActionRegistry.GetAll().Select(action => action.Id));
    }

    [Fact]
    public void CreateMainWindowViewModel_uses_shared_default_composition()
    {
        var viewModel = new StudioCompositionRoot().CreateMainWindowViewModel(savedLayout: null);

        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy").Content);
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            viewModel.DockWorkspace.RightWindow.Tabs.Single(tab => tab.Id == "inspector").Content);

        var cube = hierarchy.Nodes.Single(node => node.Id == "scene:main/cube");
        hierarchy.SelectedNode = cube;

        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
    }
}
```

- [ ] **Step 2: Run focused tests and verify fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~StudioCompositionRootTests"
```

Expected: FAIL because `StudioCompositionRoot` does not exist.

- [ ] **Step 3: Add StudioCompositionRoot**

Create `Shell/Composition/StudioCompositionRoot.cs`:

```csharp
using Editor.Core.Abstractions;
using Editor.Shell.Docking;
using Editor.Shell.Selection;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionRoot
{
    public MainWindowViewModel CreateMainWindowViewModel()
    {
        return CreateMainWindowViewModel(EditorDockLayoutStore.TryLoad());
    }

    internal MainWindowViewModel CreateMainWindowViewModel(EditorDockLayoutSnapshot? savedLayout)
    {
        var selectionService = new EditorSelectionService();
        var composition = CreateDefaultComposition(selectionService);
        return new MainWindowViewModel(
            composition.PanelRegistry,
            composition.ActionRegistry,
            savedLayout,
            selectionService);
    }

    public static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null)
    {
        selectionService ??= new EditorSelectionService();
        var host = new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules(selectionService));
        return host.Compose();
    }
}
```

- [ ] **Step 4: Route MainWindowViewModel default construction through one composition**

Modify `Shell/ViewModels/MainWindowViewModel.cs`:

1. Replace the public default constructor and private selection constructor with this single composition path. The argument record keeps one `EditorSelectionService` shared by the composition and the root view model:

```csharp
public MainWindowViewModel()
    : this(EditorDockLayoutStore.TryLoad())
{
}

private MainWindowViewModel(EditorDockLayoutSnapshot? savedLayout)
    : this(CreateDefaultViewModelArguments(savedLayout))
{
}

private MainWindowViewModel(MainWindowViewModelArguments arguments)
    : this(
        arguments.Composition.PanelRegistry,
        arguments.Composition.ActionRegistry,
        arguments.SavedLayout,
        arguments.SelectionService)
{
}

private static MainWindowViewModelArguments CreateDefaultViewModelArguments(
    EditorDockLayoutSnapshot? savedLayout)
{
    var selectionService = new EditorSelectionService();
    return new MainWindowViewModelArguments(
        StudioCompositionRoot.CreateDefaultComposition(selectionService),
        savedLayout,
        selectionService);
}

private sealed record MainWindowViewModelArguments(
    EditorExtensionComposition Composition,
    EditorDockLayoutSnapshot? SavedLayout,
    IEditorSelectionService SelectionService);
```

2. Add this helper near the existing static registry helpers:

```csharp
internal static EditorExtensionComposition CreateDefaultComposition(
    IEditorSelectionService? selectionService = null)
{
    return StudioCompositionRoot.CreateDefaultComposition(selectionService);
}
```

3. Replace static helper bodies so old tests and view hook tests still use the same host:

```csharp
internal static IPanelRegistry CreatePanelRegistry(IEditorSelectionService? selectionService = null)
{
    return CreateDefaultComposition(selectionService).PanelRegistry;
}

internal static IWorkbenchActionRegistry CreateWorkbenchActionRegistry(IEditorSelectionService? selectionService = null)
{
    return CreateDefaultComposition(selectionService).ActionRegistry;
}
```

Implementation note: the helper methods can still create separate compositions if called separately by tests. The application path must use `StudioCompositionRoot` or `CreateDefaultViewModelArguments()` so it composes once.

- [ ] **Step 5: Route App through StudioCompositionRoot**

Modify `App.axaml.cs`:

```csharp
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Editor.Shell.Composition;
using Editor.Shell.Views;

namespace Editor;

// ReSharper disable once PartialTypeWithSinglePart
public partial class App : Application
{
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = new MainWindow
            {
                DataContext = new StudioCompositionRoot().CreateMainWindowViewModel(),
            };
        }

        base.OnFrameworkInitializationCompleted();
    }
}
```

- [ ] **Step 6: Update tests that create separate default registries**

In `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`, add:

```csharp
private static EditorExtensionComposition CreateDefaultComposition(
    IEditorSelectionService? selectionService = null)
{
    return StudioCompositionRoot.CreateDefaultComposition(selectionService);
}
```

Update the `Default_panel_content_shares_main_window_selection_service` test setup:

```csharp
var selectionService = new EditorSelectionService();
var composition = CreateDefaultComposition(selectionService);
var viewModel = new MainWindowViewModel(
    composition.PanelRegistry,
    composition.ActionRegistry,
    savedLayout: null,
    selectionService);
```

Update the `Restored_floating_window_requests_share_lifecycle_event_service` setup:

```csharp
var composition = CreateDefaultComposition();
var viewModel = new MainWindowViewModel(
    composition.PanelRegistry,
    composition.ActionRegistry,
    snapshot,
    lifecycleEvents: lifecycleEvents);
```

Update the private `CreateMainWindowViewModel()` helper:

```csharp
var composition = CreateDefaultComposition();

return new MainWindowViewModel(
    composition.PanelRegistry,
    composition.ActionRegistry,
    savedLayout: null,
    backgroundTasks: backgroundTasks,
    uiDispatcher: uiDispatcher,
    lifecycleEvents: lifecycleEvents);
```

In `Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs`, replace paired calls to `CreatePanelRegistry()` and `CreateWorkbenchActionRegistry()` with:

```csharp
var composition = MainWindowViewModel.CreateDefaultComposition();
var viewModel = new MainWindowViewModel(
    composition.PanelRegistry,
    composition.ActionRegistry,
    savedLayout: null,
    lifecycleEvents: lifecycleEvents);
```

For the floating workspace test:

```csharp
var composition = MainWindowViewModel.CreateDefaultComposition();
var workspace = new EditorDockWorkspaceViewModel(
    composition.PanelRegistry,
    lifecycleEvents);
```

- [ ] **Step 7: Run focused composition and view model tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~StudioCompositionRootTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorLifecycleViewHookTests"
```

Expected: PASS.

- [ ] **Step 8: Commit composition root**

```powershell
git add App.axaml.cs `
        Shell\Composition\StudioCompositionRoot.cs `
        Shell\ViewModels\MainWindowViewModel.cs `
        Tests\Editor.Tests\Shell\Composition\StudioCompositionRootTests.cs `
        Tests\Editor.Tests\Shell\ViewModels\MainWindowViewModelTests.cs `
        Tests\Editor.Tests\Shell\Views\EditorLifecycleViewHookTests.cs
git commit -m "feat: compose studio extensions once"
```

## Task 5: Documentation And Validation

**Files:**
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`

- [ ] **Step 1: Update Dock current facts**

In `docs/Dock系统指南.md`, add this new current fact after the existing feature/panel/action registration facts, using the next available number in that numbered current-facts block:

```text
45. Editor extension host v0 composes trusted built-in Feature modules once per application composition. Built-in modules declare panel/action contributions through a temporary builder, `EditorExtensionHost` validates duplicate ids before committing to typed registries, and Shell consumers still use `PanelRegistry` / `WorkbenchActionRegistry`. This does not add external plugin loading, ALC reload, provider lifecycle, native bridge, or script VM.
```

- [ ] **Step 2: Update UI platform current facts**

In `docs/编辑器UI平台规范.md`, add a current row under the existing UI platform contracts table:

```markdown
| 内置扩展组合 v0 | `EditorExtensionHost`, `EditorContributionBuilder`, `StudioCompositionRoot` | Current / built-in panel-action composition only |
```

In the Deferred table, keep `Feature/provider/plugin lifecycle`, `Managed plugin hot reload`, and `Native C ABI` Deferred. Add a clarifying sentence under the table:

```text
`EditorExtensionHost v0` 只统一内置 panel/action contribution 的声明和注册所有权，不代表 provider lifecycle、外部 plugin lifecycle、hot reload 或 native bridge 已实现。
```

- [ ] **Step 3: Run focused test suite**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests|FullyQualifiedName~StudioCompositionRootTests|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~EditorFeatureCatalogTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorLifecycleViewHookTests"
```

Expected: PASS.

- [ ] **Step 4: Run full Studio tests**

Run:

```powershell
dotnet test Editor.sln -c Release
```

Expected: PASS.

- [ ] **Step 5: Run repository text and whitespace gates**

From repository root `D:\TechArt\VkEngine-studio-frontend`, run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

Expected:

```text
Missing required UTF-8 BOM: 0
Unexpected UTF-8 BOM: 0
Invalid UTF-8: 0
```

From `apps/studio`, run:

```powershell
git diff --check
```

Expected: no output and exit code 0.

- [ ] **Step 6: Commit docs and validation updates**

```powershell
git add docs\Dock系统指南.md docs\编辑器UI平台规范.md
git commit -m "docs: record editor extension host boundary"
```

## Final Review

- [ ] **Step 1: Confirm only editor-framework files changed**

Run:

```powershell
git diff --name-only origin/main..HEAD
```

Expected changed paths are under `apps/studio/Core`, `apps/studio/Shell`, `apps/studio/Features/Workbench`, `apps/studio/Tests`, and `apps/studio/docs`. No C++ package, native bridge, renderer, Vulkan, script VM, or external plugin loader files should appear.

- [ ] **Step 2: Confirm no old direct feature registration call sites remain outside tests**

Run:

```powershell
rg -n "RegisterPanels|RegisterActions|CreateDefaultModules\\(" Core Shell Features App.axaml.cs Tests
```

Expected:

- No production `RegisterPanels` or `RegisterActions` call sites.
- `CreateDefaultModules()` is called by `StudioCompositionRoot` and composition tests only.
- Tests may mention old method names only in deleted diff context; final source should not.

- [ ] **Step 3: Confirm non-goals remain absent**

Run:

```powershell
rg -n "AssemblyLoadContext|NativeEditorBridge|C\\+\\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly" Core Shell Features Tests
```

Expected: no production implementation references introduced by this slice.

- [ ] **Step 4: Final commit check**

Run:

```powershell
git status --short --branch
```

Expected: clean worktree on the feature branch after commits.
