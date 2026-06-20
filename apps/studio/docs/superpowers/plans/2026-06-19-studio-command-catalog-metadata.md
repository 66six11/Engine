# Studio Command Catalog Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add metadata-rich command catalog support to Studio without changing command routing, shortcut routing, popup, or background task behavior.

**Architecture:** Keep `WorkbenchActionDescriptor` as the v1 command catalog record to avoid naming churn. Add metadata and registry validation first, then project the same metadata into the command palette and make the existing executor refuse disabled commands.

**Tech Stack:** Avalonia 12.0.4, .NET 10, CommunityToolkit.Mvvm, xUnit, existing Studio MVVM shell.

---

## Scope

Implement the approved spec from `apps/studio/docs/superpowers/specs/2026-06-19-studio-command-catalog-metadata-design.md`.

Do not implement command execution by id, shortcut routing, shortcut editing, popup/dialog/toast hosts, background task services, async commands, undo/redo, plugin commands, native bridge, scene, asset, renderer, or C++ changes.

## File Structure

Create:

- `apps/studio/Core/Models/WorkbenchActionScope.cs` - command availability scope metadata.

Modify:

- `apps/studio/Core/Models/WorkbenchActionDescriptor.cs` - add category, shortcut, scope, enabled state, disabled reason, and search text metadata.
- `apps/studio/Shell/Commands/WorkbenchActionRegistry.cs` - validate the new metadata.
- `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs` - refuse disabled descriptors before dispatching by kind.
- `apps/studio/Shell/ViewModels/CommandPaletteItemViewModel.cs` - expose command metadata to XAML.
- `apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs` - search category/search text and skip disabled command execution.
- `apps/studio/Shell/Views/CommandPaletteView.axaml` - show default shortcut and disabled reason in command rows.
- `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs` - register panel actions with category metadata.
- `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs` - cover metadata validation.
- `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs` - cover disabled command execution guard.
- `apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs` - cover metadata projection/search and disabled execution.
- `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs` - update stable action snapshots.

---

### Task 1: Core Command Metadata Contract

**Files:**
- Create: `apps/studio/Core/Models/WorkbenchActionScope.cs`
- Modify: `apps/studio/Core/Models/WorkbenchActionDescriptor.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchActionRegistry.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs`

- [ ] **Step 1: Replace registry tests with metadata coverage**

Replace `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs` with:

```csharp
using System;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchActionRegistryTests
{
    [Fact]
    public void Register_preserves_registration_order()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(CreatePanelAction("first", "First", "first-panel"));
        registry.Register(CreatePanelAction("second", "Second", "second-panel"));

        Assert.Equal(["first", "second"], registry.GetAll().Select(action => action.Id));
    }

    [Fact]
    public void Register_preserves_command_metadata()
    {
        var registry = new WorkbenchActionRegistry();
        var action = new WorkbenchActionDescriptor(
            "workbench.panel.console",
            "Console",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Console",
            TargetId: "console",
            IconKey: "studio.console",
            Category: "Window",
            DefaultShortcut: "Ctrl+Alt+C",
            Scope: WorkbenchActionScope.Global,
            SearchText: "log output diagnostics");

        registry.Register(action);

        var actual = Assert.Single(registry.GetAll());
        Assert.Equal(action, actual);
        Assert.True(actual.IsEnabled);
        Assert.Null(actual.DisabledReason);
    }

    [Fact]
    public void Register_accepts_disabled_action_with_reason()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(new WorkbenchActionDescriptor(
            "workbench.panel.disabled",
            "Disabled Panel",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Disabled",
            TargetId: "disabled",
            Category: "Window",
            IsEnabled: false,
            DisabledReason: "Disabled by test"));

        var action = Assert.Single(registry.GetAll());
        Assert.False(action.IsEnabled);
        Assert.Equal("Disabled by test", action.DisabledReason);
    }

    [Fact]
    public void Register_rejects_duplicate_action_ids()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(CreatePanelAction("duplicate", "First", "first-panel"));

        Assert.Throws<InvalidOperationException>(
            () => registry.Register(CreatePanelAction("duplicate", "Second", "second-panel")));
    }

    [Theory]
    [InlineData("", "Title", "Window/Panels/Panel", "Window")]
    [InlineData("id", "", "Window/Panels/Panel", "Window")]
    [InlineData("id", "Title", "", "Window")]
    [InlineData("id", "Title", "Window/Panels/Panel", "")]
    public void Register_rejects_empty_required_text(
        string id,
        string title,
        string menuPath,
        string category)
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentException>(
            () => registry.Register(new WorkbenchActionDescriptor(
                id,
                title,
                WorkbenchActionKind.OpenPanel,
                menuPath,
                TargetId: "panel",
                Category: category)));
    }

    [Fact]
    public void Register_rejects_open_panel_action_without_target_panel()
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentException>(
            () => registry.Register(new WorkbenchActionDescriptor(
                "broken",
                "Broken",
                WorkbenchActionKind.OpenPanel,
                "Window/Panels/Broken",
                Category: "Window")));
    }

    [Fact]
    public void Register_rejects_disabled_action_without_reason()
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentException>(
            () => registry.Register(new WorkbenchActionDescriptor(
                "workbench.panel.disabled",
                "Disabled Panel",
                WorkbenchActionKind.OpenPanel,
                "Window/Panels/Disabled",
                TargetId: "disabled",
                Category: "Window",
                IsEnabled: false)));
    }

    private static WorkbenchActionDescriptor CreatePanelAction(
        string id,
        string title,
        string panelId)
    {
        return new WorkbenchActionDescriptor(
            id,
            title,
            WorkbenchActionKind.OpenPanel,
            $"Window/Panels/{title}",
            TargetId: panelId,
            Category: "Window");
    }
}
```

- [ ] **Step 2: Run registry tests to verify they fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter WorkbenchActionRegistryTests
```

Expected: FAIL with missing `WorkbenchActionScope` and missing `WorkbenchActionDescriptor` metadata member errors.

- [ ] **Step 3: Add action scope enum**

Create `apps/studio/Core/Models/WorkbenchActionScope.cs`:

```csharp
namespace Editor.Core.Models;

public enum WorkbenchActionScope
{
    Global,
    FocusedPanel,
}
```

- [ ] **Step 4: Extend action descriptor metadata**

Replace `apps/studio/Core/Models/WorkbenchActionDescriptor.cs` with:

```csharp
namespace Editor.Core.Models;

public sealed record WorkbenchActionDescriptor(
    string Id,
    string Title,
    WorkbenchActionKind Kind,
    string MenuPath,
    string? TargetId = null,
    string? IconKey = null,
    string Category = "General",
    string? DefaultShortcut = null,
    WorkbenchActionScope Scope = WorkbenchActionScope.Global,
    bool IsEnabled = true,
    string? DisabledReason = null,
    string? SearchText = null);
```

- [ ] **Step 5: Add registry metadata validation**

Replace `apps/studio/Shell/Commands/WorkbenchActionRegistry.cs` with:

```csharp
using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

public sealed class WorkbenchActionRegistry : IWorkbenchActionRegistry
{
    private readonly Dictionary<string, WorkbenchActionDescriptor> descriptors_ = new(StringComparer.Ordinal);
    private readonly List<WorkbenchActionDescriptor> descriptorsInRegistrationOrder_ = [];

    public void Register(WorkbenchActionDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        if (string.IsNullOrWhiteSpace(descriptor.Id))
        {
            throw new ArgumentException("Workbench action id must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.Title))
        {
            throw new ArgumentException("Workbench action title must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.MenuPath))
        {
            throw new ArgumentException("Workbench action menu path must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.Category))
        {
            throw new ArgumentException("Workbench action category must not be empty.", nameof(descriptor));
        }

        if (!descriptor.IsEnabled && string.IsNullOrWhiteSpace(descriptor.DisabledReason))
        {
            throw new ArgumentException("Disabled workbench actions must specify a disabled reason.", nameof(descriptor));
        }

        if (descriptor.Kind == WorkbenchActionKind.OpenPanel
            && string.IsNullOrWhiteSpace(descriptor.TargetId))
        {
            throw new ArgumentException("OpenPanel workbench actions must specify a target panel id.", nameof(descriptor));
        }

        if (!descriptors_.TryAdd(descriptor.Id, descriptor))
        {
            throw new InvalidOperationException($"Workbench action id '{descriptor.Id}' is already registered.");
        }

        descriptorsInRegistrationOrder_.Add(descriptor);
    }

    public IReadOnlyList<WorkbenchActionDescriptor> GetAll()
    {
        return descriptorsInRegistrationOrder_.ToArray();
    }
}
```

- [ ] **Step 6: Run registry tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter WorkbenchActionRegistryTests
```

Expected: PASS.

- [ ] **Step 7: Commit core metadata contract**

Run:

```powershell
git add apps/studio/Core/Models/WorkbenchActionScope.cs `
        apps/studio/Core/Models/WorkbenchActionDescriptor.cs `
        apps/studio/Shell/Commands/WorkbenchActionRegistry.cs `
        apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionRegistryTests.cs
git commit -m "feat(studio): add command catalog metadata"
```

Expected: commit succeeds.

---

### Task 2: Command Palette Metadata Projection

**Files:**
- Modify: `apps/studio/Shell/ViewModels/CommandPaletteItemViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs`
- Modify: `apps/studio/Shell/Views/CommandPaletteView.axaml`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs`

- [ ] **Step 1: Replace command palette tests with metadata behavior**

Replace `apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs` with:

```csharp
using System;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

public sealed class CommandPaletteViewModelTests
{
    [Fact]
    public void Open_resets_query_and_selects_first_action()
    {
        var viewModel = CreatePalette();
        viewModel.Query = "console";

        viewModel.OpenCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
        Assert.Equal(string.Empty, viewModel.Query);
        Assert.Equal(["Scene View", "Console", "Disabled"], viewModel.FilteredItems.Select(item => item.Title));
        Assert.Equal("Scene View", viewModel.SelectedItem?.Title);
    }

    [Fact]
    public void Item_exposes_command_metadata()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        var item = viewModel.FilteredItems.Single(item => item.Id == "workbench.panel.console");

        Assert.Equal("Window", item.Category);
        Assert.Equal("Ctrl+Alt+C", item.DefaultShortcut);
        Assert.True(item.HasDefaultShortcut);
        Assert.True(item.IsEnabled);
        Assert.False(item.HasDisabledReason);
        Assert.Equal(1.0, item.RowOpacity);
    }

    [Fact]
    public void Disabled_item_exposes_reason_and_dimmed_opacity()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        var item = viewModel.FilteredItems.Single(item => item.Id == "workbench.panel.disabled");

        Assert.False(item.IsEnabled);
        Assert.Equal("Disabled by test", item.DisabledReason);
        Assert.True(item.HasDisabledReason);
        Assert.Equal(0.55, item.RowOpacity);
    }

    [Fact]
    public void Query_filters_by_title_menu_path_id_category_or_search_text()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "window/panels/console";

        var item = Assert.Single(viewModel.FilteredItems);
        Assert.Equal("Console", item.Title);

        viewModel.Query = "workbench.panel.scene";

        item = Assert.Single(viewModel.FilteredItems);
        Assert.Equal("Scene View", item.Title);

        viewModel.Query = "diagnostics";

        item = Assert.Single(viewModel.FilteredItems);
        Assert.Equal("Console", item.Title);

        viewModel.Query = "window";

        Assert.Equal(["Scene View", "Console", "Disabled"], viewModel.FilteredItems.Select(command => command.Title));
    }

    [Fact]
    public void Query_reports_no_matches()
    {
        var viewModel = CreatePalette();
        viewModel.OpenCommand.Execute(null);

        viewModel.Query = "missing";

        Assert.Empty(viewModel.FilteredItems);
        Assert.True(viewModel.HasNoMatches);
        Assert.Null(viewModel.SelectedItem);
    }

    [Fact]
    public void ExecuteSelected_runs_action_and_closes_on_success()
    {
        string? executedActionId = null;
        var viewModel = CreatePalette(action =>
        {
            executedActionId = action.Id;
            return true;
        });
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "console";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Equal("workbench.panel.console", executedActionId);
        Assert.False(viewModel.IsOpen);
    }

    [Fact]
    public void ExecuteSelected_keeps_palette_open_when_action_fails()
    {
        var viewModel = CreatePalette(_ => false);
        viewModel.OpenCommand.Execute(null);

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.True(viewModel.IsOpen);
    }

    [Fact]
    public void ExecuteSelected_ignores_disabled_action()
    {
        string? executedActionId = null;
        var viewModel = CreatePalette(action =>
        {
            executedActionId = action.Id;
            return true;
        });
        viewModel.OpenCommand.Execute(null);
        viewModel.Query = "disabled";

        viewModel.ExecuteSelectedCommand.Execute(null);

        Assert.Null(executedActionId);
        Assert.True(viewModel.IsOpen);
    }

    private static CommandPaletteViewModel CreatePalette(
        Func<WorkbenchActionDescriptor, bool>? execute = null)
    {
        return new CommandPaletteViewModel(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    Category: "Window"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    Category: "Window",
                    DefaultShortcut: "Ctrl+Alt+C",
                    SearchText: "log output diagnostics"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.disabled",
                    "Disabled",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Disabled",
                    TargetId: "disabled",
                    Category: "Window",
                    IsEnabled: false,
                    DisabledReason: "Disabled by test"),
            ],
            execute ?? (_ => true));
    }
}
```

- [ ] **Step 2: Run command palette tests to verify they fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter CommandPaletteViewModelTests
```

Expected: FAIL because `CommandPaletteItemViewModel` does not expose metadata and `CommandPaletteViewModel` still executes disabled commands.

- [ ] **Step 3: Replace command palette item view model**

Replace `apps/studio/Shell/ViewModels/CommandPaletteItemViewModel.cs` with:

```csharp
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteItemViewModel
{
    internal CommandPaletteItemViewModel(WorkbenchActionDescriptor action)
    {
        Action = action;
        Id = action.Id;
        Title = action.Title;
        Detail = action.MenuPath;
        Category = action.Category;
        IconKey = action.IconKey;
        DefaultShortcut = action.DefaultShortcut ?? string.Empty;
        IsEnabled = action.IsEnabled;
        DisabledReason = action.DisabledReason ?? string.Empty;
        SearchText = action.SearchText ?? string.Empty;
        RowOpacity = action.IsEnabled ? 1.0 : 0.55;
    }

    internal WorkbenchActionDescriptor Action { get; }

    public string Id { get; }

    public string Title { get; }

    public string Detail { get; }

    public string Category { get; }

    public string? IconKey { get; }

    public string DefaultShortcut { get; }

    public bool HasDefaultShortcut => !string.IsNullOrWhiteSpace(DefaultShortcut);

    public bool IsEnabled { get; }

    public string DisabledReason { get; }

    public bool HasDisabledReason => !string.IsNullOrWhiteSpace(DisabledReason);

    public string SearchText { get; }

    public double RowOpacity { get; }
}
```

- [ ] **Step 4: Replace command palette view model**

Replace `apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs` with:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteViewModel : ViewModelBase
{
    private readonly IReadOnlyList<CommandPaletteItemViewModel> allItems_;
    private readonly Func<WorkbenchActionDescriptor, bool> executeAction_;
    private bool isOpen_;
    private string query_ = string.Empty;
    private IReadOnlyList<CommandPaletteItemViewModel> filteredItems_;
    private CommandPaletteItemViewModel? selectedItem_;
    private bool hasNoMatches_;

    public CommandPaletteViewModel(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        Func<WorkbenchActionDescriptor, bool> executeAction)
    {
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentNullException.ThrowIfNull(executeAction);

        executeAction_ = executeAction;
        allItems_ = actions.Select(action => new CommandPaletteItemViewModel(action)).ToArray();
        filteredItems_ = allItems_;
        selectedItem_ = filteredItems_.FirstOrDefault();
        hasNoMatches_ = filteredItems_.Count == 0;

        OpenCommand = new RelayCommand(Open);
        CloseCommand = new RelayCommand(Close);
        ExecuteSelectedCommand = new RelayCommand(ExecuteSelected);
    }

    public bool IsOpen
    {
        get => isOpen_;
        private set => SetProperty(ref isOpen_, value);
    }

    public string? Query
    {
        get => query_;
        set
        {
            if (SetProperty(ref query_, value ?? string.Empty))
            {
                RefreshFilteredItems();
            }
        }
    }

    public IReadOnlyList<CommandPaletteItemViewModel> FilteredItems
    {
        get => filteredItems_;
        private set => SetProperty(ref filteredItems_, value);
    }

    public CommandPaletteItemViewModel? SelectedItem
    {
        get => selectedItem_;
        set => SetProperty(ref selectedItem_, value);
    }

    public bool HasNoMatches
    {
        get => hasNoMatches_;
        private set => SetProperty(ref hasNoMatches_, value);
    }

    public IRelayCommand OpenCommand { get; }

    public IRelayCommand CloseCommand { get; }

    public IRelayCommand ExecuteSelectedCommand { get; }

    internal void Open()
    {
        Query = string.Empty;
        RefreshFilteredItems();
        SelectedItem = FilteredItems.FirstOrDefault();
        IsOpen = true;
    }

    internal void Close()
    {
        IsOpen = false;
    }

    private void ExecuteSelected()
    {
        if (SelectedItem is null || !SelectedItem.IsEnabled)
        {
            return;
        }

        if (executeAction_(SelectedItem.Action))
        {
            Close();
        }
    }

    private void RefreshFilteredItems()
    {
        var query = query_.Trim();
        var filtered = string.IsNullOrEmpty(query)
            ? allItems_
            : allItems_.Where(item => MatchesQuery(item, query)).ToArray();

        FilteredItems = filtered;
        HasNoMatches = FilteredItems.Count == 0;
        if (SelectedItem is null || !FilteredItems.Contains(SelectedItem))
        {
            SelectedItem = FilteredItems.FirstOrDefault();
        }
    }

    private static bool MatchesQuery(CommandPaletteItemViewModel item, string query)
    {
        return item.Title.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Detail.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Id.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Category.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.SearchText.Contains(query, StringComparison.OrdinalIgnoreCase);
    }
}
```

- [ ] **Step 5: Update command palette row template**

In `apps/studio/Shell/Views/CommandPaletteView.axaml`, replace the current `ListBox.ItemTemplate` block with:

```xml
                        <ListBox.ItemTemplate>
                            <DataTemplate x:DataType="vm:CommandPaletteItemViewModel">
                                <Grid ColumnDefinitions="Auto,*,Auto"
                                      ColumnSpacing="8"
                                      Margin="8,3"
                                      Opacity="{Binding RowOpacity}">
                                    <icons:EditorIconView Grid.Column="0"
                                                          Classes="command-palette-icon"
                                                          IconKey="{Binding IconKey}"
                                                          IconSize="14"
                                                          StrokeWidth="2" />
                                    <StackPanel Grid.Column="1"
                                                Spacing="1">
                                        <TextBlock Classes="command-palette-title"
                                                   Text="{Binding Title}" />
                                        <TextBlock Classes="command-palette-detail"
                                                   Text="{Binding Detail}" />
                                        <TextBlock Classes="command-palette-detail"
                                                   Text="{Binding DisabledReason}"
                                                   IsVisible="{Binding HasDisabledReason}" />
                                    </StackPanel>
                                    <TextBlock Grid.Column="2"
                                               Classes="command-palette-detail"
                                               Text="{Binding DefaultShortcut}"
                                               IsVisible="{Binding HasDefaultShortcut}"
                                               VerticalAlignment="Center" />
                                </Grid>
                            </DataTemplate>
                        </ListBox.ItemTemplate>
```

- [ ] **Step 6: Run command palette tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter CommandPaletteViewModelTests
```

Expected: PASS.

- [ ] **Step 7: Commit command palette metadata projection**

Run:

```powershell
git add apps/studio/Shell/ViewModels/CommandPaletteItemViewModel.cs `
        apps/studio/Shell/ViewModels/CommandPaletteViewModel.cs `
        apps/studio/Shell/Views/CommandPaletteView.axaml `
        apps/studio/Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs
git commit -m "feat(studio): show command metadata in palette"
```

Expected: commit succeeds.

---

### Task 3: Workbench Wiring And Executor Guard

**Files:**
- Modify: `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs`

- [ ] **Step 1: Update Workbench action snapshot tests**

In `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`, replace the expected `RegisterActions_registers_stable_workbench_panel_actions` array with:

```csharp
        Assert.Equal(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    IconKey: EditorIconKey.PanelSceneView,
                    Category: "Window",
                    SearchText: "viewport document"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.hierarchy",
                    "Hierarchy",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Hierarchy",
                    TargetId: "hierarchy",
                    IconKey: EditorIconKey.PanelHierarchy,
                    Category: "Window",
                    SearchText: "scene tree outliner"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.inspector",
                    "Inspector",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Inspector",
                    TargetId: "inspector",
                    IconKey: EditorIconKey.PanelInspector,
                    Category: "Window",
                    SearchText: "properties selection"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    IconKey: EditorIconKey.PanelConsole,
                    Category: "Window",
                    SearchText: "log output diagnostics"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.problems",
                    "Problems",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Problems",
                    TargetId: "problems",
                    IconKey: EditorIconKey.PanelProblems,
                    Category: "Window",
                    SearchText: "validation diagnostics"),
            ],
            registry.GetAll().ToArray());
```

- [ ] **Step 2: Add disabled executor test**

In `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs`, add this test after `Execute_open_panel_action_uses_panel_command_route`:

```csharp
    [Fact]
    public void Execute_disabled_action_returns_false_without_opening_panel()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs.Single(tab => tab.Id == "panel");
        var executor = new WorkbenchActionExecutor(new PanelCommandService(workspace));
        Assert.True(workspace.CloseTab(tab));
        var action = new WorkbenchActionDescriptor(
            "workbench.panel.disabled",
            "Disabled",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Disabled",
            TargetId: "panel",
            Category: "Window",
            IsEnabled: false,
            DisabledReason: "Disabled by test");

        Assert.False(executor.Execute(action));

        Assert.False(workspace.ContainsPanel("panel"));
    }
```

- [ ] **Step 3: Run affected tests to verify they fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~WorkbenchActionExecutorTests"
```

Expected: FAIL because `WorkbenchFeatureModule` does not provide metadata and `WorkbenchActionExecutor` still executes disabled actions.

- [ ] **Step 4: Update WorkbenchFeatureModule action registration**

In `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`, replace the `actions.Register(new WorkbenchActionDescriptor(...))` call inside `RegisterActions` with:

```csharp
            actions.Register(new WorkbenchActionDescriptor(
                $"workbench.panel.{descriptor.Id}",
                descriptor.Title,
                WorkbenchActionKind.OpenPanel,
                descriptor.MenuPath,
                TargetId: descriptor.Id,
                IconKey: descriptor.IconKey,
                Category: "Window",
                SearchText: CommandSearchTextForPanel(descriptor.Id)));
```

Then add this private helper before `CreateDefaultSceneSnapshotProvider()`:

```csharp
    private static string? CommandSearchTextForPanel(string panelId)
    {
        return panelId switch
        {
            "scene-view" => "viewport document",
            "hierarchy" => "scene tree outliner",
            "inspector" => "properties selection",
            "console" => "log output diagnostics",
            "problems" => "validation diagnostics",
            _ => null,
        };
    }
```

- [ ] **Step 5: Add executor disabled guard**

Replace `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs` with:

```csharp
using System;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

internal interface IWorkbenchActionExecutor
{
    bool Execute(WorkbenchActionDescriptor action);
}

internal sealed class WorkbenchActionExecutor : IWorkbenchActionExecutor
{
    private readonly PanelCommandService panelCommandService_;

    public WorkbenchActionExecutor(PanelCommandService panelCommandService)
    {
        ArgumentNullException.ThrowIfNull(panelCommandService);

        panelCommandService_ = panelCommandService;
    }

    public bool Execute(WorkbenchActionDescriptor action)
    {
        ArgumentNullException.ThrowIfNull(action);

        if (!action.IsEnabled)
        {
            return false;
        }

        return action.Kind switch
        {
            WorkbenchActionKind.OpenPanel => panelCommandService_.OpenOrFocusPanel(action.TargetId),
            _ => false,
        };
    }
}
```

- [ ] **Step 6: Run affected tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~WorkbenchActionExecutorTests"
```

Expected: PASS.

- [ ] **Step 7: Commit workbench wiring and executor guard**

Run:

```powershell
git add apps/studio/Features/Workbench/WorkbenchFeatureModule.cs `
        apps/studio/Shell/Commands/WorkbenchActionExecutor.cs `
        apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs `
        apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs
git commit -m "feat(studio): wire workbench command metadata"
```

Expected: commit succeeds.

---

### Task 4: Full Verification

**Files:**
- Inspect all changed files from Tasks 1-3.

- [ ] **Step 1: Run full Studio release tests**

Run:

```powershell
dotnet test Editor.sln -c Release
```

Expected: PASS.

- [ ] **Step 2: Run Debug tests if no live Studio process locks Debug output**

Run:

```powershell
dotnet test Editor.sln
```

Expected: PASS if no running Studio process is locking `apps/studio/bin/Debug/net10.0/Editor.dll`. If it fails only with file-lock errors, record the lock and keep the Release test as the completed verification.

- [ ] **Step 3: Run repository text and whitespace gates**

Run from `D:\TechArt\VkEngine-studio-frontend`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: both commands pass.

- [ ] **Step 4: Inspect changed scope**

Run:

```powershell
git status --short
git diff --stat HEAD
```

Expected: only files listed in this plan changed. No generated output, unrelated formatting, scene, native, asset, renderer, or C++ files are included.

- [ ] **Step 5: Commit verification cleanup only if needed**

If Task 4 required a small fix, commit it:

```powershell
git add apps/studio
git commit -m "test(studio): verify command catalog metadata"
```

Expected: commit succeeds. Skip this step when Tasks 1-3 already leave a clean verified state.

## Plan Self-Review

Spec coverage:

- Metadata fields are covered by Task 1.
- Registry validation is covered by Task 1.
- Disabled command execution behavior is covered by Tasks 2 and 3.
- Command palette search and metadata projection are covered by Task 2.
- Workbench panel action metadata is covered by Task 3.
- Shortcut routing, dialogs, notifications, background tasks, plugins, scene, asset, renderer, native ABI, and C++ work are explicitly excluded.

Placeholder scan:

- The plan contains no `TBD`, `TODO`, or deferred implementation placeholder inside a task.
- Follow-up behavior is excluded from this slice or named in the already approved design spec.

Type consistency:

- `WorkbenchActionScope` is introduced in Task 1 before `WorkbenchActionDescriptor` uses it.
- `CommandPaletteItemViewModel` exposes `Category`, `DefaultShortcut`, `HasDefaultShortcut`, `IsEnabled`, `DisabledReason`, `HasDisabledReason`, `SearchText`, and `RowOpacity`; tests and XAML use the same names.
- `WorkbenchActionExecutor.Execute()` keeps the existing `bool` result contract until the command-router slice.
