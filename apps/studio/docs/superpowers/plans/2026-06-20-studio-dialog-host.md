# Studio Modal Dialog Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first Studio in-app modal dialog host with typed request/results and a catalog-backed Help > About entry point.

**Architecture:** Add UI-agnostic dialog request/result models in `Editor.Core.Models`, a single-active-dialog host view model in `Editor.Shell.ViewModels`, and a top-level Avalonia overlay view in `Editor.Shell.Views`. Wire `Help/About` through the existing workbench action catalog and `WorkbenchCommandRouter` so menu, command palette, and future shortcuts share the same command route.

**Tech Stack:** C# / .NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit.

---

## File Structure

- Create `apps/studio/Core/Models/EditorDialogKind.cs`
- Create `apps/studio/Core/Models/EditorDialogResultKind.cs`
- Create `apps/studio/Core/Models/EditorDialogButtonRole.cs`
- Create `apps/studio/Core/Models/EditorDialogButtonDescriptor.cs`
- Create `apps/studio/Core/Models/EditorDialogRequest.cs`
- Create `apps/studio/Core/Models/EditorDialogResult.cs`
- Create `apps/studio/Shell/ViewModels/EditorDialogButtonViewModel.cs`
- Create `apps/studio/Shell/ViewModels/EditorDialogHostViewModel.cs`
- Create `apps/studio/Shell/Views/EditorDialogHostView.axaml`
- Create `apps/studio/Shell/Views/EditorDialogHostView.axaml.cs`
- Modify `apps/studio/Core/Models/WorkbenchActionKind.cs`
- Modify `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`
- Modify `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs`
- Modify `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- Modify `apps/studio/Shell/Views/MainWindow.axaml`
- Modify `apps/studio/Shell/Views/MainWindow.axaml.cs`
- Add tests in `apps/studio/Tests/Editor.Tests/Shell/ViewModels/EditorDialogHostViewModelTests.cs`
- Update tests in `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
- Update tests in `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs`
- Update tests in `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`

---

### Task 1: Dialog Models And Host Tests

**Files:**
- Create: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/EditorDialogHostViewModelTests.cs`
- Create: `apps/studio/Core/Models/EditorDialogKind.cs`
- Create: `apps/studio/Core/Models/EditorDialogResultKind.cs`
- Create: `apps/studio/Core/Models/EditorDialogButtonRole.cs`
- Create: `apps/studio/Core/Models/EditorDialogButtonDescriptor.cs`
- Create: `apps/studio/Core/Models/EditorDialogRequest.cs`
- Create: `apps/studio/Core/Models/EditorDialogResult.cs`
- Create: `apps/studio/Shell/ViewModels/EditorDialogButtonViewModel.cs`
- Create: `apps/studio/Shell/ViewModels/EditorDialogHostViewModel.cs`

- [ ] **Step 1: Write failing host tests**

Create `EditorDialogHostViewModelTests.cs`:

```csharp
using System;
using System.Linq;
using System.Threading.Tasks;
using Editor.Core.Models;
using Editor.Shell.ViewModels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels;

public sealed class EditorDialogHostViewModelTests
{
    [Fact]
    public void Initial_state_is_closed()
    {
        var host = new EditorDialogHostViewModel();

        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public void ShowAsync_opens_request_and_projects_buttons()
    {
        var host = new EditorDialogHostViewModel();

        var resultTask = host.ShowAsync(EditorDialogRequest.Information("About", "Studio editor shell"));

        Assert.True(host.IsOpen);
        Assert.Equal("About", host.Title);
        Assert.Equal("Studio editor shell", host.Message);
        var button = Assert.Single(host.Buttons);
        Assert.Equal("ok", button.Id);
        Assert.Equal("OK", button.Text);
        Assert.True(button.IsDefault);
        Assert.False(resultTask.IsCompleted);
    }

    [Fact]
    public async Task Button_command_completes_result_and_closes_host()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(EditorDialogRequest.Information("About", "Studio editor shell"));

        host.Buttons.Single().Command.Execute(null);

        var result = await resultTask;
        Assert.Equal(EditorDialogResultKind.Accepted, result.Kind);
        Assert.Equal("ok", result.ButtonId);
        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public async Task TryCancel_completes_cancelable_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(EditorDialogRequest.Confirmation(
            "Close Tab",
            "Close the active tab?",
            acceptText: "Close",
            rejectText: "Keep Open"));

        Assert.True(host.TryCancel());

        var result = await resultTask;
        Assert.Equal(EditorDialogResultKind.Canceled, result.Kind);
        Assert.Null(result.ButtonId);
        Assert.False(host.IsOpen);
    }

    [Fact]
    public void TryCancel_ignores_non_cancelable_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(new EditorDialogRequest(
            EditorDialogKind.Information,
            "Blocking",
            "This dialog must be acknowledged.",
            isCancelable: false,
            Buttons:
            [
                new EditorDialogButtonDescriptor("ok", "OK", EditorDialogButtonRole.Accept, IsDefault: true),
            ]));

        Assert.False(host.TryCancel());
        Assert.True(host.IsOpen);
    }

    [Fact]
    public void ShowAsync_rejects_second_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(EditorDialogRequest.Information("First", "Already open"));

        var exception = Assert.Throws<InvalidOperationException>(
            () => host.ShowAsync(EditorDialogRequest.Information("Second", "Should fail")));
        Assert.Contains("already active", exception.Message);
    }
}
```

- [ ] **Step 2: Run tests and verify compile failure**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDialogHostViewModelTests"
```

Expected: FAIL because `EditorDialogHostViewModel` and dialog model types do not exist.

- [ ] **Step 3: Implement dialog model files**

Create these model files:

```csharp
namespace Editor.Core.Models;

public enum EditorDialogKind
{
    Information,
    Confirmation,
}
```

```csharp
namespace Editor.Core.Models;

public enum EditorDialogResultKind
{
    Accepted,
    Rejected,
    Canceled,
}
```

```csharp
namespace Editor.Core.Models;

public enum EditorDialogButtonRole
{
    Accept,
    Reject,
    Cancel,
}
```

```csharp
using System;

namespace Editor.Core.Models;

public sealed record EditorDialogButtonDescriptor(
    string Id,
    string Text,
    EditorDialogButtonRole Role,
    bool IsDefault = false)
{
    public EditorDialogButtonDescriptor
    {
        if (string.IsNullOrWhiteSpace(Id))
        {
            throw new ArgumentException("Dialog button id must not be empty.", nameof(Id));
        }

        if (string.IsNullOrWhiteSpace(Text))
        {
            throw new ArgumentException("Dialog button text must not be empty.", nameof(Text));
        }
    }
}
```

```csharp
using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.Models;

public sealed record EditorDialogRequest
{
    public EditorDialogRequest(
        EditorDialogKind kind,
        string title,
        string message,
        bool isCancelable,
        IReadOnlyList<EditorDialogButtonDescriptor> Buttons)
    {
        if (string.IsNullOrWhiteSpace(title))
        {
            throw new ArgumentException("Dialog title must not be empty.", nameof(title));
        }

        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException("Dialog message must not be empty.", nameof(message));
        }

        ArgumentNullException.ThrowIfNull(Buttons);
        if (Buttons.Count == 0)
        {
            throw new ArgumentException("Dialog requests require at least one button.", nameof(Buttons));
        }

        Kind = kind;
        Title = title;
        Message = message;
        IsCancelable = isCancelable;
        this.Buttons = Buttons.ToArray();
    }

    public EditorDialogKind Kind { get; }

    public string Title { get; }

    public string Message { get; }

    public bool IsCancelable { get; }

    public IReadOnlyList<EditorDialogButtonDescriptor> Buttons { get; }

    public static EditorDialogRequest Information(string title, string message)
    {
        return new EditorDialogRequest(
            EditorDialogKind.Information,
            title,
            message,
            isCancelable: true,
            Buttons:
            [
                new EditorDialogButtonDescriptor("ok", "OK", EditorDialogButtonRole.Accept, IsDefault: true),
            ]);
    }

    public static EditorDialogRequest Confirmation(
        string title,
        string message,
        string acceptText,
        string rejectText)
    {
        return new EditorDialogRequest(
            EditorDialogKind.Confirmation,
            title,
            message,
            isCancelable: true,
            Buttons:
            [
                new EditorDialogButtonDescriptor("accept", acceptText, EditorDialogButtonRole.Accept, IsDefault: true),
                new EditorDialogButtonDescriptor("reject", rejectText, EditorDialogButtonRole.Reject),
            ]);
    }
}
```

```csharp
using System;

namespace Editor.Core.Models;

public sealed record EditorDialogResult(EditorDialogResultKind Kind, string? ButtonId)
{
    public static EditorDialogResult FromButton(EditorDialogButtonDescriptor button)
    {
        ArgumentNullException.ThrowIfNull(button);

        return button.Role switch
        {
            EditorDialogButtonRole.Accept => new EditorDialogResult(EditorDialogResultKind.Accepted, button.Id),
            EditorDialogButtonRole.Reject => new EditorDialogResult(EditorDialogResultKind.Rejected, button.Id),
            EditorDialogButtonRole.Cancel => new EditorDialogResult(EditorDialogResultKind.Canceled, button.Id),
            _ => new EditorDialogResult(EditorDialogResultKind.Canceled, button.Id),
        };
    }

    public static EditorDialogResult Canceled()
    {
        return new EditorDialogResult(EditorDialogResultKind.Canceled, null);
    }
}
```

- [ ] **Step 4: Implement host view models**

Create `EditorDialogButtonViewModel.cs` and `EditorDialogHostViewModel.cs`:

```csharp
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDialogButtonViewModel
{
    internal EditorDialogButtonViewModel(
        EditorDialogButtonDescriptor descriptor,
        IRelayCommand command)
    {
        Descriptor = descriptor;
        Command = command;
    }

    internal EditorDialogButtonDescriptor Descriptor { get; }

    public string Id => Descriptor.Id;

    public string Text => Descriptor.Text;

    public bool IsDefault => Descriptor.IsDefault;

    public IRelayCommand Command { get; }
}
```

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDialogHostViewModel : ViewModelBase
{
    private EditorDialogRequest? activeRequest_;
    private IReadOnlyList<EditorDialogButtonViewModel> buttons_ = [];
    private TaskCompletionSource<EditorDialogResult>? completion_;

    public bool IsOpen => ActiveRequest is not null;

    public EditorDialogRequest? ActiveRequest
    {
        get => activeRequest_;
        private set
        {
            if (SetProperty(ref activeRequest_, value))
            {
                OnPropertyChanged(nameof(IsOpen));
                OnPropertyChanged(nameof(Title));
                OnPropertyChanged(nameof(Message));
            }
        }
    }

    public string Title => ActiveRequest?.Title ?? string.Empty;

    public string Message => ActiveRequest?.Message ?? string.Empty;

    public IReadOnlyList<EditorDialogButtonViewModel> Buttons
    {
        get => buttons_;
        private set => SetProperty(ref buttons_, value);
    }

    public Task<EditorDialogResult> ShowAsync(EditorDialogRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (ActiveRequest is not null)
        {
            throw new InvalidOperationException("A dialog is already active.");
        }

        var completion = new TaskCompletionSource<EditorDialogResult>();
        completion_ = completion;
        ActiveRequest = request;
        Buttons = request.Buttons
            .Select(button => new EditorDialogButtonViewModel(
                button,
                new RelayCommand(() => Complete(EditorDialogResult.FromButton(button)))))
            .ToArray();
        return completion.Task;
    }

    public bool TryCancel()
    {
        if (ActiveRequest is null || !ActiveRequest.IsCancelable)
        {
            return false;
        }

        Complete(EditorDialogResult.Canceled());
        return true;
    }

    private void Complete(EditorDialogResult result)
    {
        var completion = completion_;
        completion_ = null;
        Buttons = [];
        ActiveRequest = null;
        completion?.SetResult(result);
    }
}
```

- [ ] **Step 5: Run focused tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDialogHostViewModelTests"
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add apps/studio/Core/Models/EditorDialog*.cs apps/studio/Shell/ViewModels/EditorDialog*.cs apps/studio/Tests/Editor.Tests/Shell/ViewModels/EditorDialogHostViewModelTests.cs
git commit -m "feat: add studio dialog host model"
```

---

### Task 2: About Command And Help Menu Projection

**Files:**
- Modify: `apps/studio/Core/Models/WorkbenchActionKind.cs`
- Modify: `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `apps/studio/Shell/Commands/WorkbenchActionExecutor.cs`
- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Write failing command/menu tests**

Add these tests:

```csharp
[Fact]
public void Execute_open_about_dialog_action_invokes_callback()
{
    var openCount = 0;
    var executor = new WorkbenchActionExecutor(
        new PanelCommandService(CreateWorkspace()),
        openCommandPalette: null,
        openAboutDialog: () =>
        {
            openCount++;
            return true;
        });
    var action = new WorkbenchActionDescriptor(
        "workbench.about.open",
        "About",
        WorkbenchActionKind.OpenAboutDialog,
        "Help/About");

    Assert.True(executor.Execute(action));
    Assert.Equal(1, openCount);
}
```

```csharp
[Fact]
public void HelpMenuItems_follow_registered_workbench_actions()
{
    var viewModel = CreateMainWindowViewModel();

    var item = Assert.Single(viewModel.HelpMenuItems);
    Assert.Equal("workbench.about.open", item.CommandId);
    Assert.Equal("About", item.Header);
    Assert.Equal("Help/About", item.MenuPath);
}

[Fact]
public void HelpMenuItems_open_about_dialog_through_command_route()
{
    var viewModel = CreateMainWindowViewModel();
    var item = Assert.Single(viewModel.HelpMenuItems);

    item.OpenCommand.Execute(null);

    Assert.True(viewModel.DialogHost.IsOpen);
    Assert.Equal("About Studio", viewModel.DialogHost.Title);
}
```

Update `WorkbenchFeatureModuleTests.RegisterActions_registers_stable_workbench_panel_actions` expected actions to include:

```csharp
new WorkbenchActionDescriptor(
    "workbench.about.open",
    "About",
    WorkbenchActionKind.OpenAboutDialog,
    "Help/About",
    Category: "Help",
    SearchText: "about studio version information"),
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchActionExecutorTests|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because `OpenAboutDialog`, `DialogHost`, and `HelpMenuItems` do not exist.

- [ ] **Step 3: Add action kind and registered action**

Update `WorkbenchActionKind.cs`:

```csharp
namespace Editor.Core.Models;

public enum WorkbenchActionKind
{
    OpenPanel,
    OpenCommandPalette,
    OpenAboutDialog,
}
```

Add the About action in `WorkbenchFeatureModule.RegisterActions` after the command palette action:

```csharp
actions.Register(new WorkbenchActionDescriptor(
    "workbench.about.open",
    "About",
    WorkbenchActionKind.OpenAboutDialog,
    "Help/About",
    Category: "Help",
    SearchText: "about studio version information"));
```

- [ ] **Step 4: Extend action executor**

Update constructor and switch:

```csharp
private readonly Func<bool>? openAboutDialog_;

public WorkbenchActionExecutor(
    PanelCommandService panelCommandService,
    Func<bool>? openCommandPalette = null,
    Func<bool>? openAboutDialog = null)
{
    ArgumentNullException.ThrowIfNull(panelCommandService);

    panelCommandService_ = panelCommandService;
    openCommandPalette_ = openCommandPalette;
    openAboutDialog_ = openAboutDialog;
}
```

```csharp
return action.Kind switch
{
    WorkbenchActionKind.OpenPanel => panelCommandService_.OpenOrFocusPanel(action.TargetId),
    WorkbenchActionKind.OpenCommandPalette => openCommandPalette_?.Invoke() ?? false,
    WorkbenchActionKind.OpenAboutDialog => openAboutDialog_?.Invoke() ?? false,
    _ => false,
};
```

- [ ] **Step 5: Wire dialog host and Help menu in MainWindowViewModel**

Add `DialogHost` and `HelpMenuItems`:

```csharp
DialogHost = new EditorDialogHostViewModel();
var actionExecutor = new WorkbenchActionExecutor(
    panelCommandService_,
    OpenCommandPaletteFromCommand,
    OpenAboutDialogFromCommand);
```

```csharp
public EditorDialogHostViewModel DialogHost { get; }

public IReadOnlyList<WorkbenchMenuItemViewModel> HelpMenuItems { get; }
```

Set menu items:

```csharp
ToolsMenuItems = CreateCommandMenuItems(actions, "Tools/", commandRouter);
HelpMenuItems = CreateCommandMenuItems(actions, "Help/", commandRouter);
```

Add About command callback:

```csharp
private bool OpenAboutDialogFromCommand()
{
    _ = DialogHost.ShowAsync(EditorDialogRequest.Information(
        "About Studio",
        "Studio editor shell for VkEngine."));
    return true;
}
```

- [ ] **Step 6: Run command/menu tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchActionExecutorTests|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```powershell
git add apps/studio/Core/Models/WorkbenchActionKind.cs apps/studio/Features/Workbench/WorkbenchFeatureModule.cs apps/studio/Shell/Commands/WorkbenchActionExecutor.cs apps/studio/Shell/ViewModels/MainWindowViewModel.cs apps/studio/Tests/Editor.Tests/Shell/Commands/WorkbenchActionExecutorTests.cs apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs
git commit -m "feat: route studio about dialog through commands"
```

---

### Task 3: Avalonia Dialog Host View

**Files:**
- Create: `apps/studio/Shell/Views/EditorDialogHostView.axaml`
- Create: `apps/studio/Shell/Views/EditorDialogHostView.axaml.cs`
- Modify: `apps/studio/Shell/Views/MainWindow.axaml`
- Modify: `apps/studio/Shell/Views/MainWindow.axaml.cs`

- [ ] **Step 1: Create dialog host XAML**

Create `EditorDialogHostView.axaml`:

```xml
<UserControl xmlns="https://github.com/avaloniaui"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:vm="using:Editor.Shell.ViewModels"
             x:Class="Editor.Shell.Views.EditorDialogHostView"
             x:DataType="vm:EditorDialogHostViewModel"
             IsVisible="{Binding IsOpen}"
             Focusable="True"
             KeyDown="OnDialogHostKeyDown">
    <UserControl.Styles>
        <Style Selector="Border.dialog-host-scrim">
            <Setter Property="Background" Value="{DynamicResource EditorBrushOverlayScrim}" />
        </Style>

        <Style Selector="Border.dialog-host-surface">
            <Setter Property="Width" Value="440" />
            <Setter Property="MaxWidth" Value="560" />
            <Setter Property="Padding" Value="16" />
            <Setter Property="HorizontalAlignment" Value="Center" />
            <Setter Property="VerticalAlignment" Value="Center" />
            <Setter Property="Background" Value="{DynamicResource EditorBrushSurfaceOverlay}" />
            <Setter Property="BorderBrush" Value="{DynamicResource EditorBrushBorderDefault}" />
            <Setter Property="BorderThickness" Value="1" />
            <Setter Property="CornerRadius" Value="6" />
        </Style>

        <Style Selector="TextBlock.dialog-host-title">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextPrimary}" />
            <Setter Property="FontSize" Value="15" />
            <Setter Property="FontWeight" Value="SemiBold" />
            <Setter Property="TextTrimming" Value="CharacterEllipsis" />
        </Style>

        <Style Selector="TextBlock.dialog-host-message">
            <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextSecondary}" />
            <Setter Property="FontSize" Value="{DynamicResource EditorFontSizeDefault}" />
            <Setter Property="TextWrapping" Value="Wrap" />
        </Style>

        <Style Selector="Button.dialog-host-button">
            <Setter Property="MinWidth" Value="78" />
            <Setter Property="MinHeight" Value="28" />
            <Setter Property="Padding" Value="12,4" />
        </Style>
    </UserControl.Styles>

    <Grid>
        <Border Classes="dialog-host-scrim" />

        <Border Classes="dialog-host-surface">
            <Grid RowDefinitions="Auto,Auto,Auto"
                  RowSpacing="12">
                <TextBlock Classes="dialog-host-title"
                           Text="{Binding Title}" />

                <TextBlock Grid.Row="1"
                           Classes="dialog-host-message"
                           Text="{Binding Message}" />

                <ItemsControl Grid.Row="2"
                              ItemsSource="{Binding Buttons}"
                              HorizontalAlignment="Right">
                    <ItemsControl.ItemsPanel>
                        <ItemsPanelTemplate>
                            <StackPanel Orientation="Horizontal"
                                        Spacing="8" />
                        </ItemsPanelTemplate>
                    </ItemsControl.ItemsPanel>
                    <ItemsControl.ItemTemplate>
                        <DataTemplate x:DataType="vm:EditorDialogButtonViewModel">
                            <Button Classes="dialog-host-button"
                                    Content="{Binding Text}"
                                    Command="{Binding Command}" />
                        </DataTemplate>
                    </ItemsControl.ItemTemplate>
                </ItemsControl>
            </Grid>
        </Border>
    </Grid>
</UserControl>
```

- [ ] **Step 2: Create Escape bridge**

Create `EditorDialogHostView.axaml.cs`:

```csharp
using Avalonia.Controls;
using Avalonia.Input;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDialogHostView : UserControl
{
    public EditorDialogHostView()
    {
        InitializeComponent();
    }

    private void OnDialogHostKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape
            && DataContext is EditorDialogHostViewModel viewModel
            && viewModel.TryCancel())
        {
            e.Handled = true;
        }
    }
}
```

- [ ] **Step 3: Add Help menu generation and dialog overlay**

In `MainWindow.axaml`, name Help menu and remove static About:

```xml
<MenuItem x:Name="HelpMenu"
          Header="Help"
          Classes="editor-menu-item">
    <MenuItem Header="Documentation" Classes="editor-menu-item" IsEnabled="False" />
</MenuItem>
```

Add the dialog host as the final overlay child:

```xml
<views:CommandPaletteView Grid.Row="0"
                          Grid.RowSpan="3"
                          DataContext="{Binding CommandPalette}" />

<views:EditorDialogHostView Grid.Row="0"
                            Grid.RowSpan="3"
                            DataContext="{Binding DialogHost}" />
```

In `MainWindow.axaml.cs`, add:

```csharp
private readonly List<MenuItem> generatedHelpMenuItems_ = [];
```

Rebuild on data context change:

```csharp
RebuildToolsMenu(viewModel);
RebuildHelpMenu(viewModel);
RebuildPanelsMenu(viewModel);
```

```csharp
RebuildToolsMenu(null);
RebuildHelpMenu(null);
RebuildPanelsMenu(null);
```

Add helper:

```csharp
private void RebuildHelpMenu(MainWindowViewModel? viewModel)
{
    foreach (var menuItem in generatedHelpMenuItems_)
    {
        HelpMenu.Items.Remove(menuItem);
    }

    generatedHelpMenuItems_.Clear();
    if (viewModel is null)
    {
        return;
    }

    var insertIndex = HelpMenu.Items.Count;
    foreach (var commandItem in viewModel.HelpMenuItems)
    {
        var menuItem = CreateCommandMenuItem(commandItem);
        generatedHelpMenuItems_.Add(menuItem);
        HelpMenu.Items.Insert(insertIndex, menuItem);
        insertIndex++;
    }
}
```

- [ ] **Step 4: Build and focused tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~Dialog|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~MainWindowShortcutTests"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add apps/studio/Shell/Views/EditorDialogHostView.axaml apps/studio/Shell/Views/EditorDialogHostView.axaml.cs apps/studio/Shell/Views/MainWindow.axaml apps/studio/Shell/Views/MainWindow.axaml.cs
git commit -m "feat: add studio dialog host view"
```

---

### Task 4: Full Validation And PR

**Files:**
- No new source files unless validation exposes a defect.

- [ ] **Step 1: Run Release solution tests**

```powershell
dotnet test apps\studio\Editor.sln -c Release
```

Expected: 0 failures.

- [ ] **Step 2: Run default solution tests**

```powershell
dotnet test apps\studio\Editor.sln
```

Expected: 0 failures.

- [ ] **Step 3: Run encoding and whitespace gates**

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: no encoding violations and no whitespace errors.

- [ ] **Step 4: Push and open PR**

```powershell
$token = gh auth token
$basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("x-access-token:$token"))
git -c "http.extraHeader=AUTHORIZATION: basic $basic" push -u https://github.com:443/66six11/Engine.git codex/studio-dialog-host
```

Open PR:

```powershell
gh pr create --base main --head codex/studio-dialog-host --title "feat: add studio modal dialog host" --body "<body with Closes #190 and validation results>"
```

- [ ] **Step 5: Project sync**

After PR creation and after merge, audit #190 and comment on #20 with:

```text
TL;DR: #190 advanced through PR <number>.

Since last update:
- <PR state, validation, project fields>

Next: <merge or next editor UI slice>

Blocked: None.
```

## Self-Review

- Spec coverage: model, host, command route, Help menu, XAML overlay, cancellation, validation, and follow-up boundaries are covered.
- Placeholder scan: no task depends on unspecified behavior.
- Type consistency: `EditorDialogHostViewModel`, `EditorDialogRequest`, `EditorDialogResult`, `HelpMenuItems`, and `OpenAboutDialog` are named consistently across tests and implementation.
