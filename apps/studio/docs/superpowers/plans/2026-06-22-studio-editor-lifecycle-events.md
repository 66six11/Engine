# Studio Editor Lifecycle Events Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first Studio editor lifecycle event surface for Shell/Core window lifecycle diagnostics without connecting native runtime, providers, Play Session, or plugin reload.

**Architecture:** Core owns UI-neutral lifecycle event models and the service contract. Shell owns the in-memory implementation and translates existing Avalonia main-window and floating-window hooks into lifecycle snapshots. Lifecycle events remain a bounded recent event stream, not a persisted log, provider bus, runtime bridge, or plugin lifecycle manager.

**Tech Stack:** .NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit, existing Studio Core/Shell/Features/UI layout, `docs/superpowers/specs/2026-06-22-studio-editor-lifecycle-events-design.md`.

---

## File Structure

- Create `apps/studio/Core/Models/EditorLifecycleEventKind.cs` - stable enum for editor-framework lifecycle event kinds.
- Create `apps/studio/Core/Models/EditorLifecycleEventSnapshot.cs` - immutable UI-neutral lifecycle event snapshot.
- Create `apps/studio/Core/Abstractions/IEditorLifecycleEventService.cs` - publish/recent-list service contract.
- Create `apps/studio/Shell/Services/EditorLifecycleEventService.cs` - in-memory bounded recent-list implementation.
- Create `apps/studio/Tests/Editor.Tests/Shell/Services/EditorLifecycleEventServiceTests.cs` - pure service tests.
- Modify `apps/studio/Shell/ViewModels/MainWindowViewModel.cs` - create and expose the lifecycle service.
- Modify `apps/studio/Shell/ViewModels/EditorDockWorkspaceViewModel.cs` - carry the lifecycle service into dynamically created floating workspaces.
- Modify `apps/studio/Shell/ViewModels/EditorDockFloatingWindowViewModel.cs` - expose the shared lifecycle service to floating window code-behind.
- Modify `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs` - prove service injection/exposure.
- Modify `apps/studio/Shell/Views/MainWindow.axaml.cs` - publish main window lifecycle events from existing hooks.
- Modify `apps/studio/Shell/Views/EditorDockFloatingWindow.axaml.cs` - publish floating window lifecycle events from existing hooks.
- Create `apps/studio/Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs` - lightweight tests for hook helper routing and source-level hook presence.
- Modify `apps/studio/docs/Dock系统指南.md` - record lifecycle events v0 current fact.

## Task 1: Core Contract And In-Memory Service

**Files:**

- Create: `apps/studio/Core/Models/EditorLifecycleEventKind.cs`
- Create: `apps/studio/Core/Models/EditorLifecycleEventSnapshot.cs`
- Create: `apps/studio/Core/Abstractions/IEditorLifecycleEventService.cs`
- Create: `apps/studio/Shell/Services/EditorLifecycleEventService.cs`
- Create: `apps/studio/Tests/Editor.Tests/Shell/Services/EditorLifecycleEventServiceTests.cs`

- [ ] **Step 1: Write failing lifecycle service tests**

Create `apps/studio/Tests/Editor.Tests/Shell/Services/EditorLifecycleEventServiceTests.cs`:

```csharp
using System;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.Services;
using Xunit;

namespace Editor.Tests.Shell.Services;

public sealed class EditorLifecycleEventServiceTests
{
    [Fact]
    public void Publish_records_snapshot_and_raises_events_changed()
    {
        var service = new EditorLifecycleEventService();
        var changeCount = 0;
        service.EventsChanged += (_, _) => changeCount++;

        var snapshot = service.Publish(
            EditorLifecycleEventKind.ApplicationOpened,
            "main-window",
            "Opened");

        Assert.Equal(1, changeCount);
        Assert.Equal(1, snapshot.Sequence);
        Assert.Equal(EditorLifecycleEventKind.ApplicationOpened, snapshot.Kind);
        Assert.Equal("main-window", snapshot.Source);
        Assert.Equal("Opened", snapshot.Message);
        Assert.True(snapshot.OccurredAtUtc <= DateTimeOffset.UtcNow);
        Assert.Equal(snapshot, Assert.Single(service.GetRecentEvents()));
    }

    [Fact]
    public void Publish_rejects_blank_source()
    {
        var service = new EditorLifecycleEventService();

        var exception = Assert.Throws<ArgumentException>(
            () => service.Publish(EditorLifecycleEventKind.ApplicationOpened, " "));

        Assert.Equal("source", exception.ParamName);
        Assert.Empty(service.GetRecentEvents());
    }

    [Fact]
    public void Recent_events_keep_latest_100_snapshots()
    {
        var service = new EditorLifecycleEventService();

        for (var index = 0; index < 105; index++)
        {
            service.Publish(EditorLifecycleEventKind.HostActivated, "main-window", index.ToString());
        }

        var events = service.GetRecentEvents();

        Assert.Equal(100, events.Count);
        Assert.Equal(6, events[0].Sequence);
        Assert.Equal("5", events[0].Message);
        Assert.Equal(105, events[^1].Sequence);
        Assert.Equal("104", events[^1].Message);
        Assert.Equal(Enumerable.Range(6, 100).Select(static value => (long)value), events.Select(static snapshot => snapshot.Sequence));
    }

    [Fact]
    public void GetRecentEvents_returns_snapshot_copy()
    {
        var service = new EditorLifecycleEventService();
        service.Publish(EditorLifecycleEventKind.HostActivated, "main-window");

        var firstRead = service.GetRecentEvents();
        service.Publish(EditorLifecycleEventKind.HostDeactivated, "main-window");

        Assert.Single(firstRead);
        Assert.Equal(2, service.GetRecentEvents().Count);
    }
}
```

- [ ] **Step 2: Run failing service tests**

Run from `apps/studio`:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorLifecycleEventServiceTests"
```

Expected: FAIL because `EditorLifecycleEventService`, `EditorLifecycleEventKind`, and the lifecycle service contract do not exist.

- [ ] **Step 3: Add lifecycle event kind enum**

Create `apps/studio/Core/Models/EditorLifecycleEventKind.cs`:

```csharp
namespace Editor.Core.Models;

public enum EditorLifecycleEventKind
{
    ApplicationOpened,
    ApplicationClosing,
    ApplicationClosed,
    HostActivated,
    HostDeactivated,
    WorkspaceRestored,
    FloatingWindowOpened,
    FloatingWindowClosed,
    FloatingWindowActivated,
    FloatingWindowDeactivated,
}
```

- [ ] **Step 4: Add lifecycle event snapshot model**

Create `apps/studio/Core/Models/EditorLifecycleEventSnapshot.cs`:

```csharp
using System;

namespace Editor.Core.Models;

public sealed record EditorLifecycleEventSnapshot(
    long Sequence,
    EditorLifecycleEventKind Kind,
    string Source,
    string? Message,
    DateTimeOffset OccurredAtUtc);
```

- [ ] **Step 5: Add lifecycle service contract**

Create `apps/studio/Core/Abstractions/IEditorLifecycleEventService.cs`:

```csharp
using System;
using System.Collections.Generic;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorLifecycleEventService
{
    event EventHandler? EventsChanged;

    EditorLifecycleEventSnapshot Publish(
        EditorLifecycleEventKind kind,
        string source,
        string? message = null);

    IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents();
}
```

- [ ] **Step 6: Implement in-memory lifecycle service**

Create `apps/studio/Shell/Services/EditorLifecycleEventService.cs`:

```csharp
using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Services;

public sealed class EditorLifecycleEventService : IEditorLifecycleEventService
{
    private const int RecentEventCapacity = 100;
    private readonly List<EditorLifecycleEventSnapshot> events_ = [];
    private long nextSequence_;

    public event EventHandler? EventsChanged;

    public EditorLifecycleEventSnapshot Publish(
        EditorLifecycleEventKind kind,
        string source,
        string? message = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);

        var snapshot = new EditorLifecycleEventSnapshot(
            ++nextSequence_,
            kind,
            source,
            message,
            DateTimeOffset.UtcNow);
        events_.Add(snapshot);
        if (events_.Count > RecentEventCapacity)
        {
            events_.RemoveRange(0, events_.Count - RecentEventCapacity);
        }

        EventsChanged?.Invoke(this, EventArgs.Empty);
        return snapshot;
    }

    public IReadOnlyList<EditorLifecycleEventSnapshot> GetRecentEvents()
    {
        return events_.ToArray();
    }
}
```

- [ ] **Step 7: Run focused service tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorLifecycleEventServiceTests"
```

Expected: PASS.

- [ ] **Step 8: Commit task 1**

Run:

```powershell
git add Core\Models\EditorLifecycleEventKind.cs `
        Core\Models\EditorLifecycleEventSnapshot.cs `
        Core\Abstractions\IEditorLifecycleEventService.cs `
        Shell\Services\EditorLifecycleEventService.cs `
        Tests\Editor.Tests\Shell\Services\EditorLifecycleEventServiceTests.cs
git commit -m "feat: add studio lifecycle event service"
```

## Task 2: Lifecycle Service Propagation Through Shell ViewModels

**Files:**

- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/EditorDockWorkspaceViewModel.cs`
- Modify: `apps/studio/Shell/ViewModels/EditorDockFloatingWindowViewModel.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Write failing MainWindowViewModel tests**

In `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`, add these tests near the other constructor/service tests:

```csharp
[Fact]
public void Constructor_exposes_injected_lifecycle_event_service()
{
    var lifecycleEvents = new EditorLifecycleEventService();

    var viewModel = CreateMainWindowViewModel(lifecycleEvents: lifecycleEvents);

    Assert.Same(lifecycleEvents, viewModel.LifecycleEvents);
    Assert.Same(lifecycleEvents, viewModel.DockWorkspace.LifecycleEvents);
}

[Fact]
public void Restored_floating_window_requests_share_lifecycle_event_service()
{
    var lifecycleEvents = new EditorLifecycleEventService();
    var snapshot = new EditorDockLayoutSnapshot
    {
        Version = 1,
        FloatingWindows =
        {
            new EditorDockFloatingWindowSnapshot
            {
                X = 16,
                Y = 24,
                Width = 480,
                Height = 320,
                ActiveWindowId = "floating-inspector",
                Root = new EditorDockLayoutNodeSnapshot
                {
                    Kind = "Window",
                    Id = "node-floating-inspector",
                    WindowId = "floating-inspector",
                    WindowTitle = "Inspector",
                    WindowArea = DockArea.Right,
                    WindowRole = "Selection context",
                    TabIds = ["inspector"],
                    ActiveTabId = "inspector",
                },
            },
        },
    };
    var viewModel = new MainWindowViewModel(
        MainWindowViewModel.CreatePanelRegistry(),
        MainWindowViewModel.CreateWorkbenchActionRegistry(),
        snapshot,
        lifecycleEvents: lifecycleEvents);

    var request = Assert.Single(viewModel.ConsumeRestoredFloatingWindowRequests());

    Assert.Same(lifecycleEvents, request.Window.LifecycleEvents);
    Assert.Same(lifecycleEvents, request.Window.DockWorkspace.LifecycleEvents);
}
```

Update the local helper signature at the bottom of the same test file:

```csharp
private static MainWindowViewModel CreateMainWindowViewModel(
    IEditorBackgroundTaskService? backgroundTasks = null,
    IEditorUiDispatcher? uiDispatcher = null,
    IEditorLifecycleEventService? lifecycleEvents = null)
{
    uiDispatcher ??= new CapturingUiDispatcher(hasAccess: true);

    return new MainWindowViewModel(
        MainWindowViewModel.CreatePanelRegistry(),
        MainWindowViewModel.CreateWorkbenchActionRegistry(),
        savedLayout: null,
        backgroundTasks: backgroundTasks,
        uiDispatcher: uiDispatcher,
        lifecycleEvents: lifecycleEvents);
}
```

- [ ] **Step 2: Run failing ViewModel tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because `MainWindowViewModel` and `EditorDockWorkspaceViewModel` do not expose `LifecycleEvents`.

- [ ] **Step 3: Add lifecycle service to MainWindowViewModel**

In `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`, add the constructor parameter:

```csharp
IEditorLifecycleEventService? lifecycleEvents = null)
```

Assign the property before creating the dock workspace:

```csharp
LifecycleEvents = lifecycleEvents ?? new EditorLifecycleEventService();
DockWorkspace = new EditorDockWorkspaceViewModel(panelRegistry_, LifecycleEvents);
```

Add the public property near `SelectionService`:

```csharp
public IEditorLifecycleEventService LifecycleEvents { get; }
```

In `ConsumeRestoredFloatingWindowRequests`, call the lifecycle-aware overload and pass the service into the floating window ViewModel:

```csharp
if (!EditorDockWorkspaceViewModel.TryCreateFloatingWorkspace(
        panelRegistry_,
        snapshot,
        LifecycleEvents,
        out var floatingWorkspace))
{
    continue;
}

var window = new EditorDockFloatingWindowViewModel(floatingWorkspace, LifecycleEvents);
```

- [ ] **Step 4: Add lifecycle service to EditorDockWorkspaceViewModel**

In `apps/studio/Shell/ViewModels/EditorDockWorkspaceViewModel.cs`, add:

```csharp
public IEditorLifecycleEventService LifecycleEvents { get; }
```

Change the main constructor signature and first statements:

```csharp
public EditorDockWorkspaceViewModel(
    IPanelRegistry panelRegistry,
    IEditorLifecycleEventService? lifecycleEvents = null)
{
    panelRegistry_ = panelRegistry;
    LifecycleEvents = lifecycleEvents ?? new EditorLifecycleEventService();
```

Change the private floating constructor:

```csharp
private EditorDockWorkspaceViewModel(
    EditorDockWindowViewModel floatingDockWindow,
    IEditorLifecycleEventService lifecycleEvents)
{
    panelRegistry_ = null;
    LifecycleEvents = lifecycleEvents;
```

Change the private restore constructor:

```csharp
private EditorDockWorkspaceViewModel(
    IPanelRegistry panelRegistry,
    EditorDockFloatingWindowSnapshot snapshot,
    IEditorLifecycleEventService lifecycleEvents)
{
    panelRegistry_ = panelRegistry;
    LifecycleEvents = lifecycleEvents;
```

Keep the existing public `TryCreateFloatingWorkspace` overload, but route it through a lifecycle-aware overload:

```csharp
public static bool TryCreateFloatingWorkspace(
    IPanelRegistry panelRegistry,
    EditorDockFloatingWindowSnapshot snapshot,
    out EditorDockWorkspaceViewModel workspace)
{
    return TryCreateFloatingWorkspace(
        panelRegistry,
        snapshot,
        new EditorLifecycleEventService(),
        out workspace);
}

public static bool TryCreateFloatingWorkspace(
    IPanelRegistry panelRegistry,
    EditorDockFloatingWindowSnapshot snapshot,
    IEditorLifecycleEventService lifecycleEvents,
    out EditorDockWorkspaceViewModel workspace)
{
    workspace = new EditorDockWorkspaceViewModel(panelRegistry, snapshot, lifecycleEvents);
    if (workspace.RootNode is not null && workspace.HasDockContent())
    {
        return true;
    }

    workspace = null!;
    return false;
}
```

In `FloatTab`, pass the service into the floating workspace and window:

```csharp
var floatingWorkspace = new EditorDockWorkspaceViewModel(floatingDockWindow, LifecycleEvents);
var floatingWindow = new EditorDockFloatingWindowViewModel(floatingWorkspace, LifecycleEvents);
return new EditorDockFloatingWindowRequest(floatingWindow, bounds);
```

Add this using directive at the top for `EditorLifecycleEventService`:

```csharp
using Editor.Shell.Services;
```

- [ ] **Step 5: Add lifecycle service to floating window ViewModel**

Replace `apps/studio/Shell/ViewModels/EditorDockFloatingWindowViewModel.cs` with:

```csharp
using Editor.Core.Abstractions;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockFloatingWindowViewModel : ViewModelBase
{
    public EditorDockFloatingWindowViewModel(
        EditorDockWorkspaceViewModel dockWorkspace,
        IEditorLifecycleEventService? lifecycleEvents = null)
    {
        DockWorkspace = dockWorkspace;
        LifecycleEvents = lifecycleEvents ?? dockWorkspace.LifecycleEvents;
    }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IEditorLifecycleEventService LifecycleEvents { get; }
}
```

- [ ] **Step 6: Run focused ViewModel tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests"
```

Expected: PASS.

- [ ] **Step 7: Commit task 2**

Run:

```powershell
git add Shell\ViewModels\MainWindowViewModel.cs `
        Shell\ViewModels\EditorDockWorkspaceViewModel.cs `
        Shell\ViewModels\EditorDockFloatingWindowViewModel.cs `
        Tests\Editor.Tests\Shell\ViewModels\MainWindowViewModelTests.cs
git commit -m "feat: route lifecycle events through studio shell"
```

## Task 3: Publish Main And Floating Window Lifecycle Events

**Files:**

- Modify: `apps/studio/Shell/Views/MainWindow.axaml.cs`
- Modify: `apps/studio/Shell/Views/EditorDockFloatingWindow.axaml.cs`
- Create: `apps/studio/Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs`

- [ ] **Step 1: Write failing view hook tests**

Create `apps/studio/Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs`:

```csharp
using System;
using System.IO;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.Services;
using Editor.Shell.ViewModels;
using Editor.Shell.Views;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class EditorLifecycleViewHookTests
{
    [Fact]
    public void MainWindow_publish_helper_routes_to_view_model_lifecycle_service()
    {
        var lifecycleEvents = new EditorLifecycleEventService();
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            MainWindowViewModel.CreateWorkbenchActionRegistry(),
            savedLayout: null,
            lifecycleEvents: lifecycleEvents);

        var snapshot = MainWindow.PublishLifecycleEvent(
            viewModel,
            EditorLifecycleEventKind.ApplicationOpened,
            "main-window",
            "Opened");

        Assert.NotNull(snapshot);
        Assert.Equal(EditorLifecycleEventKind.ApplicationOpened, snapshot.Kind);
        Assert.Equal("main-window", snapshot.Source);
        Assert.Equal(snapshot, Assert.Single(lifecycleEvents.GetRecentEvents()));
    }

    [Fact]
    public void FloatingWindow_publish_helper_routes_to_view_model_lifecycle_service()
    {
        var lifecycleEvents = new EditorLifecycleEventService();
        var workspace = new EditorDockWorkspaceViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            lifecycleEvents);
        var viewModel = new EditorDockFloatingWindowViewModel(workspace, lifecycleEvents);

        var snapshot = EditorDockFloatingWindow.PublishLifecycleEvent(
            viewModel,
            EditorLifecycleEventKind.FloatingWindowOpened,
            "floating-window",
            "Opened");

        Assert.NotNull(snapshot);
        Assert.Equal(EditorLifecycleEventKind.FloatingWindowOpened, snapshot.Kind);
        Assert.Equal("floating-window", snapshot.Source);
        Assert.Equal(snapshot, Assert.Single(lifecycleEvents.GetRecentEvents()));
    }

    [Fact]
    public void MainWindow_source_contains_expected_lifecycle_publications()
    {
        var source = LoadSource("Shell", "Views", "MainWindow.axaml.cs");

        Assert.Contains("Closing += OnWindowClosing;", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationOpened", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosing", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosed", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.HostActivated", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.HostDeactivated", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.WorkspaceRestored", source);
    }

    [Fact]
    public void FloatingWindow_source_contains_expected_lifecycle_publications()
    {
        var source = LoadSource("Shell", "Views", "EditorDockFloatingWindow.axaml.cs");

        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowOpened", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowClosed", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowActivated", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowDeactivated", source);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        var fullPathParts = new string[pathParts.Length + 1];
        fullPathParts[0] = root;
        Array.Copy(pathParts, 0, fullPathParts, 1, pathParts.Length);
        return File.ReadAllText(Path.Combine(fullPathParts));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Editor.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
```

- [ ] **Step 2: Run failing view hook tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorLifecycleViewHookTests"
```

Expected: FAIL because the publish helpers and hook publications do not exist.

- [ ] **Step 3: Publish main window lifecycle events**

In `apps/studio/Shell/Views/MainWindow.axaml.cs`, add:

```csharp
using Editor.Core.Models;
```

Add a source constant:

```csharp
private const string MainWindowLifecycleSource = "main-window";
```

In the constructor, subscribe to closing:

```csharp
Closing += OnWindowClosing;
```

Update `OnOpened`:

```csharp
protected override void OnOpened(EventArgs e)
{
    base.OnOpened(e);
    PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationOpened);
    SetDockHostFocusState(IsActive);
    RestoreFloatingWindows();
}
```

Update `OnClosed`:

```csharp
protected override void OnClosed(EventArgs e)
{
    KeyDown -= OnMainWindowKeyDown;
    Closing -= OnWindowClosing;
    EditorDockFloatingWindowRegistry.DockContentChanged -= OnFloatingDockContentChanged;
    PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosed);
    base.OnClosed(e);
}
```

Add the closing handler:

```csharp
private void OnWindowClosing(object? sender, WindowClosingEventArgs e)
{
    PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosing);
}
```

Update activation handlers:

```csharp
private void OnWindowActivated(object? sender, EventArgs e)
{
    SetDockHostFocusState(true);
    PublishLifecycleEvent(EditorLifecycleEventKind.HostActivated);
}

private void OnWindowDeactivated(object? sender, EventArgs e)
{
    SetDockHostFocusState(false);
    PublishLifecycleEvent(EditorLifecycleEventKind.HostDeactivated);
}
```

Update `RestoreFloatingWindows` by publishing after the restore attempt:

```csharp
private void RestoreFloatingWindows()
{
    if (restoredFloatingWindows_ || DataContext is not MainWindowViewModel viewModel)
    {
        return;
    }

    restoredFloatingWindows_ = true;
    foreach (var request in viewModel.ConsumeRestoredFloatingWindowRequests())
    {
        ShowFloatingWindow(request);
    }

    PublishLifecycleEvent(EditorLifecycleEventKind.WorkspaceRestored);
}
```

Add helper methods near the other internal helpers:

```csharp
private void PublishLifecycleEvent(EditorLifecycleEventKind kind, string? message = null)
{
    if (DataContext is MainWindowViewModel viewModel)
    {
        PublishLifecycleEvent(viewModel, kind, MainWindowLifecycleSource, message);
    }
}

internal static EditorLifecycleEventSnapshot? PublishLifecycleEvent(
    MainWindowViewModel? viewModel,
    EditorLifecycleEventKind kind,
    string source,
    string? message = null)
{
    return viewModel?.LifecycleEvents.Publish(kind, source, message);
}
```

- [ ] **Step 4: Publish floating window lifecycle events**

In `apps/studio/Shell/Views/EditorDockFloatingWindow.axaml.cs`, add:

```csharp
using Editor.Core.Models;
```

Add a source constant:

```csharp
private const string FloatingWindowLifecycleSource = "floating-window";
```

Update `OnOpened`:

```csharp
protected override void OnOpened(EventArgs e)
{
    base.OnOpened(e);
    SetDockHostFocusState(IsActive);
    EditorDockFloatingWindowRegistry.Register(this);
    PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowOpened);
}
```

Update `OnClosed`:

```csharp
protected override void OnClosed(EventArgs e)
{
    EditorDockFloatingWindowRegistry.Unregister(this);
    PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowClosed);
    base.OnClosed(e);
}
```

Update activation handlers:

```csharp
private void OnWindowActivated(object? sender, EventArgs e)
{
    SetDockHostFocusState(true);
    PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowActivated);
}

private void OnWindowDeactivated(object? sender, EventArgs e)
{
    SetDockHostFocusState(false);
    PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowDeactivated);
}
```

Add helper methods:

```csharp
private void PublishLifecycleEvent(EditorLifecycleEventKind kind, string? message = null)
{
    if (DataContext is EditorDockFloatingWindowViewModel viewModel)
    {
        PublishLifecycleEvent(viewModel, kind, FloatingWindowLifecycleSource, message);
    }
}

internal static EditorLifecycleEventSnapshot? PublishLifecycleEvent(
    EditorDockFloatingWindowViewModel? viewModel,
    EditorLifecycleEventKind kind,
    string source,
    string? message = null)
{
    return viewModel?.LifecycleEvents.Publish(kind, source, message);
}
```

- [ ] **Step 5: Run focused view hook tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorLifecycleViewHookTests|FullyQualifiedName~MainWindowShortcutTests"
```

Expected: PASS.

- [ ] **Step 6: Commit task 3**

Run:

```powershell
git add Shell\Views\MainWindow.axaml.cs `
        Shell\Views\EditorDockFloatingWindow.axaml.cs `
        Tests\Editor.Tests\Shell\Views\EditorLifecycleViewHookTests.cs
git commit -m "feat: publish studio window lifecycle events"
```

## Task 4: Documentation And Full Validation

**Files:**

- Modify: `apps/studio/docs/Dock系统指南.md`

- [ ] **Step 1: Update Dock system current facts**

In `apps/studio/docs/Dock系统指南.md`, append this item to the "当前已实现" numbered list after the transaction service v0 item:

```text
43. Lifecycle events v0 由 `IEditorLifecycleEventService` 和 `EditorLifecycleEventService` 提供 UI-neutral recent event stream；当前只记录主窗口 opened/closing/closed/activated/deactivated、workspace restored 和 floating window opened/closed/activated/deactivated，不代表 feature unload、provider reload、Play Session 或 native runtime lifecycle。
```

- [ ] **Step 2: Run focused lifecycle tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorLifecycleEventServiceTests|FullyQualifiedName~EditorLifecycleViewHookTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests"
```

Expected: PASS.

- [ ] **Step 3: Run full Studio verification**

Run:

```powershell
dotnet test Editor.sln -c Release
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected:

- `dotnet test Editor.sln -c Release` passes.
- Encoding check reports no missing required UTF-8 BOM, no unexpected UTF-8 BOM, and no invalid UTF-8.
- `git diff --check` exits cleanly.

- [ ] **Step 4: Commit task 4**

Run:

```powershell
git add docs\Dock系统指南.md
git commit -m "docs: record studio lifecycle event surface"
```

## Final Validation Before PR

Run from `apps/studio`:

```powershell
dotnet test Editor.sln -c Release
dotnet test Editor.sln --artifacts-path "$env:TEMP\studio-lifecycle-test-artifacts"
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected:

- All tests pass.
- Encoding check reports zero violations.
- `git diff --check` exits cleanly.

For this plan, the CMake/Vulkan pre-commit gate is not required because the slice only touches `apps/studio` C#/Markdown editor framework files. If a later PR opens or edits GitHub Issues, PR links, labels, dependency edges, or Project fields, read `docs/planning/project-management.md` first and run the required Project sync/audit workflow.

## Scope Self-Review

- Spec coverage: Task 1 implements Core models/contract and Shell service; Task 2 propagates lifecycle service through Shell ViewModels and floating workspaces; Task 3 publishes main/floating window events; Task 4 updates docs and runs validation.
- Non-goals preserved: no native runtime, provider, Play Session, feature unload, plugin reload, persisted lifecycle log, or diagnostics panel is introduced.
- Type consistency: `EditorLifecycleEventKind`, `EditorLifecycleEventSnapshot`, `IEditorLifecycleEventService`, and `EditorLifecycleEventService` names match the design spec and all planned tests.
- TDD path: each implementation task starts with failing tests and focused verification before commits.
