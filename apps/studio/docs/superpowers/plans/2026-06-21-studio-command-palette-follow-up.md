# Studio Command Palette Follow-Up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add category grouping, in-memory recent commands, and local command result feedback to the Studio command palette without adding native/runtime/provider integration.

**Architecture:** Keep `WorkbenchActionDescriptor` as the only command metadata source and `WorkbenchCommandRouter` as the only execution route. `CommandPaletteViewModel` owns view-facing projection state: grouped display rows, a bounded recent command list, selection safety, and the last local execution result message. `CommandPaletteView.axaml` remains a binding-only surface for grouped rows and result text.

**Tech Stack:** C#/.NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit, existing Studio Core/Shell/ViewModel patterns.

---

## Source Spec

- `docs/superpowers/specs/2026-06-21-studio-command-palette-follow-up-design.md`

## File Structure

- Modify: `Shell/ViewModels/CommandPaletteItemViewModel.cs`
  - Adds command/header row metadata while preserving the existing command item projection from `WorkbenchActionDescriptor`.
- Modify: `Shell/ViewModels/CommandPaletteViewModel.cs`
  - Builds grouped display rows, selects only executable commands, records bounded in-memory recent commands, and exposes local result message state.
- Modify: `Shell/Views/CommandPaletteView.axaml`
  - Renders category header rows and local result feedback through bindings only.
- Modify: `Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs`
  - Covers grouping, safe selection, recent ordering, failure messaging, and search behavior.
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
  - Keeps route integration coverage for real main-window command palette execution.
- Modify: `docs/Dock系统指南.md`
  - Records the completed palette follow-up facts and keeps non-goals explicit.

## Task 1: Grouped Palette Rows

**Files:**
- Modify: `Shell/ViewModels/CommandPaletteItemViewModel.cs`
- Modify: `Shell/ViewModels/CommandPaletteViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs`

- [ ] **Step 1: Write failing grouped-row tests**

Update `Open_resets_query_and_selects_first_action` in `Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs` to expect category header rows:

```csharp
[Fact]
public void Open_resets_query_groups_actions_and_selects_first_command()
{
    var viewModel = CreatePalette();
    viewModel.Query = "console";

    viewModel.OpenCommand.Execute(null);

    Assert.True(viewModel.IsOpen);
    Assert.Equal(string.Empty, viewModel.Query);
    Assert.Equal(
        ["Window", "Scene View", "Console", "Disabled", "Tools", "Command Palette"],
        viewModel.FilteredItems.Select(item => item.Title));
    Assert.Equal([true, false, false, false, true, false], viewModel.FilteredItems.Select(item => item.IsHeader));
    Assert.Equal("Scene View", viewModel.SelectedItem?.Title);
    Assert.True(viewModel.SelectedItem?.IsCommand);
}
```

Add a header safety test:

```csharp
[Fact]
public void Category_header_rows_are_not_executable()
{
    string? executedCommandId = null;
    var viewModel = CreatePalette(commandId =>
    {
        executedCommandId = commandId;
        return WorkbenchCommandExecutionResult.Success(commandId);
    });
    viewModel.OpenCommand.Execute(null);
    viewModel.SelectedItem = viewModel.FilteredItems.First(item => item.IsHeader);

    viewModel.ExecuteSelectedCommand.Execute(null);

    Assert.Null(executedCommandId);
    Assert.True(viewModel.IsOpen);
}
```

Update query tests to inspect only command rows:

```csharp
private static IReadOnlyList<CommandPaletteItemViewModel> CommandRows(CommandPaletteViewModel viewModel)
{
    return viewModel.FilteredItems.Where(item => item.IsCommand).ToArray();
}
```

Then change single-command assertions from `viewModel.FilteredItems` to `CommandRows(viewModel)`.

Update `CreatePalette` to include a second category:

```csharp
new WorkbenchActionDescriptor(
    "workbench.commandPalette.open",
    "Command Palette",
    WorkbenchActionKind.OpenCommandPalette,
    "Tools/Command Palette",
    Category: "Tools",
    DefaultShortcut: "Ctrl+Shift+P",
    SearchText: "quick command launcher"),
```

- [ ] **Step 2: Run focused tests and verify RED**

Run from `D:\TechArt\VkEngine-studio-frontend\apps\studio`:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests"
```

Expected: FAIL because `CommandPaletteItemViewModel` does not expose `IsHeader` / `IsCommand`, and `CommandPaletteViewModel` still publishes a flat list.

- [ ] **Step 3: Add command/header row projection**

Update `Shell/ViewModels/CommandPaletteItemViewModel.cs`:

```csharp
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteItemViewModel
{
    internal CommandPaletteItemViewModel(WorkbenchActionDescriptor action)
    {
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
        IsCommand = true;
        IsHeader = false;
    }

    private CommandPaletteItemViewModel(string category)
    {
        Id = $"header:{category}";
        Title = category;
        Detail = string.Empty;
        Category = category;
        IconKey = null;
        DefaultShortcut = string.Empty;
        IsEnabled = false;
        DisabledReason = string.Empty;
        SearchText = string.Empty;
        RowOpacity = 1.0;
        IsCommand = false;
        IsHeader = true;
    }

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

    public bool IsCommand { get; }

    public bool IsHeader { get; }

    internal static CommandPaletteItemViewModel CreateHeader(string category)
    {
        return new CommandPaletteItemViewModel(category);
    }
}
```

- [ ] **Step 4: Group filtered commands by category**

Update `Shell/ViewModels/CommandPaletteViewModel.cs`:

```csharp
private readonly IReadOnlyList<CommandPaletteItemViewModel> allCommandItems_;
```

Initialize command rows only:

```csharp
allCommandItems_ = actions.Select(action => new CommandPaletteItemViewModel(action)).ToArray();
filteredItems_ = CreateGroupedRows(allCommandItems_);
selectedItem_ = SelectFirstCommand(filteredItems_);
hasNoMatches_ = allCommandItems_.Count == 0;
```

Update `ExecuteSelected` guard:

```csharp
if (SelectedItem is null || !SelectedItem.IsCommand || !SelectedItem.IsEnabled)
{
    return;
}
```

Replace `RefreshFilteredItems`:

```csharp
private void RefreshFilteredItems()
{
    var query = query_.Trim();
    var filteredCommands = string.IsNullOrEmpty(query)
        ? allCommandItems_
        : allCommandItems_.Where(item => MatchesQuery(item, query)).ToArray();

    FilteredItems = CreateGroupedRows(filteredCommands);
    HasNoMatches = filteredCommands.Count == 0;
    if (SelectedItem is null
        || !SelectedItem.IsCommand
        || !FilteredItems.Contains(SelectedItem))
    {
        SelectedItem = SelectFirstCommand(FilteredItems);
    }
}

private static IReadOnlyList<CommandPaletteItemViewModel> CreateGroupedRows(
    IReadOnlyList<CommandPaletteItemViewModel> commandItems)
{
    var rows = new List<CommandPaletteItemViewModel>();
    var categories = new HashSet<string>(StringComparer.Ordinal);
    foreach (var item in commandItems)
    {
        if (categories.Add(item.Category))
        {
            rows.Add(CommandPaletteItemViewModel.CreateHeader(item.Category));
        }

        rows.Add(item);
    }

    return rows;
}

private static CommandPaletteItemViewModel? SelectFirstCommand(
    IReadOnlyList<CommandPaletteItemViewModel> items)
{
    return items.FirstOrDefault(item => item.IsCommand && item.IsEnabled)
        ?? items.FirstOrDefault(item => item.IsCommand);
}
```

Update `Open`:

```csharp
internal void Open()
{
    Query = string.Empty;
    RefreshFilteredItems();
    SelectedItem = SelectFirstCommand(FilteredItems);
    IsOpen = true;
}
```

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests"
```

Expected: PASS.

Commit:

```powershell
git add Shell\ViewModels\CommandPaletteItemViewModel.cs Shell\ViewModels\CommandPaletteViewModel.cs Tests\Editor.Tests\Shell\ViewModels\CommandPaletteViewModelTests.cs
git commit -m "feat: group studio command palette results"
```

## Task 2: Recent Commands And Local Result Feedback

**Files:**
- Modify: `Shell/ViewModels/CommandPaletteViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/CommandPaletteViewModelTests.cs`

- [ ] **Step 1: Write failing recent/result tests**

Add tests:

```csharp
[Fact]
public void Successful_execution_records_recent_command_and_clears_result_message()
{
    var viewModel = CreatePalette();
    viewModel.OpenCommand.Execute(null);
    viewModel.Query = "console";

    viewModel.ExecuteSelectedCommand.Execute(null);
    viewModel.OpenCommand.Execute(null);

    Assert.False(viewModel.HasLastResultMessage);
    Assert.Equal(
        ["Recent", "Console", "Window", "Scene View", "Disabled", "Tools", "Command Palette"],
        viewModel.FilteredItems.Select(item => item.Title));
    Assert.Equal("Console", viewModel.SelectedItem?.Title);
}

[Fact]
public void Recent_promotion_is_disabled_while_query_is_active()
{
    var viewModel = CreatePalette();
    viewModel.OpenCommand.Execute(null);
    viewModel.Query = "console";
    viewModel.ExecuteSelectedCommand.Execute(null);
    viewModel.OpenCommand.Execute(null);

    viewModel.Query = "window";

    Assert.DoesNotContain(viewModel.FilteredItems, item => item.IsHeader && item.Title == "Recent");
    Assert.Equal(["Scene View", "Console", "Disabled"], CommandRows(viewModel).Select(item => item.Title));
}

[Fact]
public void Failed_execution_keeps_palette_open_and_publishes_result_message()
{
    var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.Failed(commandId, "Failed by test"));
    viewModel.OpenCommand.Execute(null);
    viewModel.Query = "console";

    viewModel.ExecuteSelectedCommand.Execute(null);

    Assert.True(viewModel.IsOpen);
    Assert.True(viewModel.HasLastResultMessage);
    Assert.Equal("Failed by test", viewModel.LastResultMessage);
    Assert.DoesNotContain(viewModel.FilteredItems, item => item.IsHeader && item.Title == "Recent");
}

[Fact]
public void Not_found_execution_keeps_palette_open_and_publishes_result_message()
{
    var viewModel = CreatePalette(commandId => WorkbenchCommandExecutionResult.NotFound(commandId));
    viewModel.OpenCommand.Execute(null);
    viewModel.Query = "console";

    viewModel.ExecuteSelectedCommand.Execute(null);

    Assert.True(viewModel.IsOpen);
    Assert.True(viewModel.HasLastResultMessage);
    Assert.Contains("is not registered", viewModel.LastResultMessage, StringComparison.Ordinal);
}
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests"
```

Expected: FAIL because `LastResultMessage`, `HasLastResultMessage`, and the Recent group do not exist.

- [ ] **Step 3: Add recent command and result message state**

Update `Shell/ViewModels/CommandPaletteViewModel.cs` fields:

```csharp
private const int MaxRecentCommands = 5;
private readonly List<string> recentCommandIds_ = [];
private string lastResultMessage_ = string.Empty;
```

Add properties:

```csharp
public string LastResultMessage
{
    get => lastResultMessage_;
    private set
    {
        if (SetProperty(ref lastResultMessage_, value))
        {
            OnPropertyChanged(nameof(HasLastResultMessage));
        }
    }
}

public bool HasLastResultMessage => !string.IsNullOrWhiteSpace(LastResultMessage);
```

Update `ExecuteSelected`:

```csharp
private void ExecuteSelected()
{
    if (SelectedItem is null || !SelectedItem.IsCommand || !SelectedItem.IsEnabled)
    {
        return;
    }

    var result = executeCommand_(SelectedItem.Id);
    if (result.Succeeded)
    {
        RecordRecentCommand(SelectedItem.Id);
        LastResultMessage = string.Empty;
        Close();
        return;
    }

    LastResultMessage = result.Message ?? $"Command '{SelectedItem.Id}' did not complete.";
}
```

Add helper:

```csharp
private void RecordRecentCommand(string commandId)
{
    recentCommandIds_.Remove(commandId);
    recentCommandIds_.Insert(0, commandId);
    if (recentCommandIds_.Count > MaxRecentCommands)
    {
        recentCommandIds_.RemoveRange(MaxRecentCommands, recentCommandIds_.Count - MaxRecentCommands);
    }
}
```

- [ ] **Step 4: Add Recent group display rows**

Add helper:

```csharp
private IReadOnlyList<CommandPaletteItemViewModel> CreateDisplayRows(
    IReadOnlyList<CommandPaletteItemViewModel> filteredCommands,
    bool includeRecent)
{
    if (!includeRecent || recentCommandIds_.Count == 0)
    {
        return CreateGroupedRows(filteredCommands);
    }

    var rows = new List<CommandPaletteItemViewModel>();
    var recentItems = recentCommandIds_
        .Select(commandId => allCommandItems_.FirstOrDefault(item => item.Id == commandId))
        .Where(item => item is not null)
        .Cast<CommandPaletteItemViewModel>()
        .ToArray();
    if (recentItems.Length > 0)
    {
        rows.Add(CommandPaletteItemViewModel.CreateHeader("Recent"));
        rows.AddRange(recentItems);
    }

    var recentIds = new HashSet<string>(recentItems.Select(item => item.Id), StringComparer.Ordinal);
    rows.AddRange(CreateGroupedRows(filteredCommands.Where(item => !recentIds.Contains(item.Id)).ToArray()));
    return rows;
}
```

Update `RefreshFilteredItems`:

```csharp
private void RefreshFilteredItems()
{
    var query = query_.Trim();
    var filteredCommands = string.IsNullOrEmpty(query)
        ? allCommandItems_
        : allCommandItems_.Where(item => MatchesQuery(item, query)).ToArray();

    FilteredItems = CreateDisplayRows(filteredCommands, string.IsNullOrEmpty(query));
    HasNoMatches = filteredCommands.Count == 0;
    if (SelectedItem is null
        || !SelectedItem.IsCommand
        || !FilteredItems.Contains(SelectedItem))
    {
        SelectedItem = SelectFirstCommand(FilteredItems);
    }
}
```

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests"
```

Expected: PASS.

Commit:

```powershell
git add Shell\ViewModels\CommandPaletteViewModel.cs Tests\Editor.Tests\Shell\ViewModels\CommandPaletteViewModelTests.cs
git commit -m "feat: remember recent palette commands"
```

## Task 3: Palette View, Integration Coverage, And Dock Guide

**Files:**
- Modify: `Shell/Views/CommandPaletteView.axaml`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
- Modify: `docs/Dock系统指南.md`

- [ ] **Step 1: Add integration test coverage**

Add this test to `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`:

```csharp
[Fact]
public void CommandPalette_records_recent_command_after_main_window_route_success()
{
    var viewModel = CreateMainWindowViewModel();
    var hierarchy = viewModel.DockWorkspace.LeftWindow.Tabs.Single(tab => tab.Id == "hierarchy");
    Assert.True(viewModel.DockWorkspace.CloseTab(hierarchy));

    viewModel.CommandPalette.OpenCommand.Execute(null);
    viewModel.CommandPalette.Query = "hierarchy";
    viewModel.CommandPalette.ExecuteSelectedCommand.Execute(null);
    viewModel.CommandPalette.OpenCommand.Execute(null);

    Assert.Equal("Recent", viewModel.CommandPalette.FilteredItems[0].Title);
    Assert.Equal("Hierarchy", viewModel.CommandPalette.FilteredItems[1].Title);
    Assert.True(viewModel.DockWorkspace.ContainsPanel("hierarchy"));
}
```

- [ ] **Step 2: Run integration tests and verify RED if Task 2 was not applied**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~CommandPaletteViewModelTests"
```

Expected: PASS after Tasks 1 and 2. If it fails, the failure identifies missing recent/group behavior in the main-window route.

- [ ] **Step 3: Bind category headers and result feedback in XAML**

Update `Shell/Views/CommandPaletteView.axaml` styles:

```xml
<Style Selector="TextBlock.command-palette-section">
    <Setter Property="Margin" Value="8,8,8,2" />
    <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextMuted}" />
    <Setter Property="FontSize" Value="{DynamicResource EditorFontSizeSmall}" />
</Style>

<Style Selector="TextBlock.command-palette-result">
    <Setter Property="Margin" Value="8,6,8,0" />
    <Setter Property="Foreground" Value="{DynamicResource EditorBrushWarning}" />
    <Setter Property="FontSize" Value="{DynamicResource EditorFontSizeSmall}" />
    <Setter Property="TextTrimming" Value="CharacterEllipsis" />
</Style>
```

Change the palette surface grid row definitions:

```xml
<Grid RowDefinitions="Auto,*,Auto">
```

Change the item template to render headers and commands separately:

```xml
<DataTemplate x:DataType="vm:CommandPaletteItemViewModel">
    <Grid>
        <TextBlock Classes="command-palette-section"
                   Text="{Binding Title}"
                   IsVisible="{Binding IsHeader}" />

        <Grid ColumnDefinitions="Auto,*,Auto"
              ColumnSpacing="8"
              Margin="8,3"
              Opacity="{Binding RowOpacity}"
              IsVisible="{Binding IsCommand}">
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
    </Grid>
</DataTemplate>
```

Add result feedback below the result grid:

```xml
<TextBlock Grid.Row="2"
           Classes="command-palette-result"
           Text="{Binding LastResultMessage}"
           IsVisible="{Binding HasLastResultMessage}" />
```

- [ ] **Step 4: Update Dock guide facts**

Update `docs/Dock系统指南.md` current implementation item 30 to:

```text
30. Command Palette follow-up 继续复用 `WorkbenchActionDescriptor` 和 `WorkbenchCommandRouter`，支持 category 分组、in-memory recent commands 和 local command result feedback；当前仍只执行已注册的 workbench actions，不引入插件命令 API、完整快捷键编辑器、真实 provider 数据源或 native ABI
```

Update the `Command Palette v0` block:

```text
Search   匹配 Title、MenuPath、action Id、Category、DefaultShortcut 或 SearchText
Group    按 WorkbenchActionDescriptor.Category 生成 header row；空查询时成功执行过的命令提升到 in-memory Recent group
Execute  通过 WorkbenchCommandRouter 执行 command id；失败、禁用或缺失结果只写入 palette-local message
Shortcut Ctrl+Shift+P 打开 Command Palette；不支持用户自定义或冲突检测
State    打开/关闭、查询文本、选中项、recent commands 和 local result message 只属于 MainWindow UI 状态，不写入 Dock layout snapshot
```

Update `## 后续切片` item 4 to:

```text
4. Command palette follow-up：更多 action kind、命令结果弹出/日志反馈、用户可编辑快捷键策略和快捷键冲突 UI；暂不做插件命令 API。
```

- [ ] **Step 5: Run focused tests and commit**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

Commit:

```powershell
git add Shell\Views\CommandPaletteView.axaml Tests\Editor.Tests\Shell\ViewModels\MainWindowViewModelTests.cs docs\Dock系统指南.md
git commit -m "feat: surface grouped command palette feedback"
```

## Final Validation

Run after all tasks:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~CommandPaletteViewModelTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~WorkbenchShortcutRouterTests|FullyQualifiedName~WorkbenchCommandRouterTests"
dotnet test Editor.sln -c Release
dotnet test Editor.sln
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected:

- focused tests pass
- Release solution tests pass
- Debug solution tests pass
- encoding check reports 0 missing BOM, 0 unexpected BOM, and 0 invalid UTF-8
- `git diff --check` prints no errors

No CMake/Vulkan pre-commit gate is required for this plan because all changes stay under `apps/studio` editor framework files and do not touch C/C++, shaders, CMake, Conan, renderer, native ABI, runtime scene providers, or engine packages.

## Self-Review

- Spec coverage: Task 1 covers category grouping and safe header rows; Task 2 covers recent commands and local result feedback; Task 3 covers view bindings, integration coverage, and Dock guide facts.
- Boundary check: the plan keeps `WorkbenchActionDescriptor` and `WorkbenchCommandRouter` as the command surface, and explicitly avoids native ABI, runtime scene queries, asset index integration, plugin command APIs, and Transform writeback.
- Type consistency: planned properties are `IsHeader`, `IsCommand`, `LastResultMessage`, and `HasLastResultMessage`; tests and XAML use the same names.
- Verification scope: focused VM/integration tests cover behavior, full solution tests cover regressions, and encoding/diff checks cover repository hygiene.
