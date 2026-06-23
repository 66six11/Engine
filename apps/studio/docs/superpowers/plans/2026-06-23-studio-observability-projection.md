# Studio Observability Projection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Slice 5 of the Studio extension lifecycle design by adding a UI-neutral diagnostic stream that feeds latest status feedback plus Console/Problems panel view models.

**Architecture:** `IEditorDiagnosticService` owns in-memory editor diagnostic records with bounded recent history, latest status, and problem filtering. Command feedback publishes diagnostics into that service, while Console/Problems panels consume the same records as different display modes. This remains editor-framework-only: no native log ingestion, shell command line, external plugin diagnostics, writable scene editing, or full notification center.

**Tech Stack:** .NET 10, C#, xUnit, Avalonia MVVM ViewModels, existing `EditorCommandFeedbackSnapshot`, `WorkbenchCommandFeedbackRouter`, `ConsolePanelViewModel`, `ProblemsPanelViewModel`, `MainWindowViewModel`.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-extension-lifecycle-v0-design.md`
- Slice 5: Observability Projection.
- `docs/编辑器UI平台规范.md` section 8: status feedback contract.

## External Reference Notes

- Unity Console is the model for debug-style editor messages: log/warning/error records are collected and filtered in one debug surface.
- VS Code separates diagnostics from output/status surfaces, so Studio should keep diagnostic records UI-neutral and let status/Console/Problems project them differently.
- Avalonia UI updates must remain on the UI thread, so panel view models use `IEditorUiDispatcher` when diagnostics are published off-thread.

## Scope

In:

- Add `IEditorDiagnosticService`, `EditorDiagnosticService`, `EditorDiagnosticRecord`, `EditorDiagnosticSeverity`, and `EditorDiagnosticChannel`.
- Add bounded recent diagnostic history and latest diagnostic snapshot.
- Add problem filtering while keeping Console as the debug/all-record view.
- Wire `MainWindowViewModel` command feedback to publish diagnostic records and expose the latest diagnostic as status text.
- Make Console and Problems panel view models consume the same diagnostic service.
- Share one diagnostic service through `StudioCompositionRoot` into Workbench panels and the main window.

Out:

- Console shell command input.
- Native engine log ingestion.
- Provider/plugin/ALC diagnostics.
- Full Problems table UI, Console table UI, search/filter controls, persistence, or notification center.
- Modal error dialogs for normal command failures.

## Task 1: Core Diagnostic Service

**Files:**
- Create: `Core/Models/EditorDiagnosticSeverity.cs`
- Create: `Core/Models/EditorDiagnosticChannel.cs`
- Create: `Core/Models/EditorDiagnosticRecord.cs`
- Create: `Core/Abstractions/IEditorDiagnosticService.cs`
- Create: `Core/Services/EditorDiagnosticService.cs`
- Create: `Tests/Editor.Tests/Core/EditorDiagnosticServiceTests.cs`

- [x] **Step 1: Write failing diagnostic service tests**

Add tests proving publish, latest, capacity, eventing, problem filtering, and argument validation:

```csharp
[Fact]
public void Publish_records_latest_debug_diagnostic_and_raises_change()
{
    var service = new EditorDiagnosticService(capacity: 2);
    var changedCount = 0;
    service.DiagnosticsChanged += (_, _) => changedCount++;

    var record = service.Publish(
        EditorDiagnosticSeverity.Info,
        EditorDiagnosticChannel.Debug,
        "command",
        "workbench",
        "Command completed.");

    Assert.Equal(1, record.SequenceId);
    Assert.Equal(record, service.GetLatestDiagnostic());
    Assert.Equal([record], service.GetRecentDiagnostics());
    Assert.Equal(1, changedCount);
}
```

```csharp
[Fact]
public void Publish_keeps_bounded_recent_history()
{
    var service = new EditorDiagnosticService(capacity: 2);

    service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "one", "test", "One");
    var second = service.Publish(EditorDiagnosticSeverity.Warning, EditorDiagnosticChannel.Debug, "two", "test", "Two");
    var third = service.Publish(EditorDiagnosticSeverity.Error, EditorDiagnosticChannel.Problem, "three", "test", "Three");

    Assert.Equal([second, third], service.GetRecentDiagnostics());
}
```

```csharp
[Fact]
public void GetProblemDiagnostics_returns_problem_channel_records_only()
{
    var service = new EditorDiagnosticService();
    service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "debug", "command", "Debug");
    var problem = service.Publish(EditorDiagnosticSeverity.Error, EditorDiagnosticChannel.Problem, "validation", "scene", "Problem");

    Assert.Equal([problem], service.GetProblemDiagnostics());
}
```

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDiagnosticServiceTests"
```

Expected: compile failure because diagnostic service types do not exist.

- [x] **Step 3: Implement the service**

Implement:

```csharp
public enum EditorDiagnosticSeverity { Debug, Info, Warning, Error }
public enum EditorDiagnosticChannel { Debug, Problem }
```

```csharp
public sealed record EditorDiagnosticRecord(
    long SequenceId,
    EditorDiagnosticSeverity Severity,
    EditorDiagnosticChannel Channel,
    string Source,
    string Category,
    string Message);
```

`EditorDiagnosticService` keeps a bounded list, increments `SequenceId`, trims old records, publishes `DiagnosticsChanged`, and validates non-empty `Source`, `Category`, and `Message`.

- [x] **Step 4: Verify GREEN**

Run the same focused command. Expected: diagnostic service tests pass.

## Task 2: Console And Problems View Models

**Files:**
- Modify: `Features/Console/ViewModels/ConsolePanelViewModel.cs`
- Modify: `Features/Problems/ViewModels/ProblemsPanelViewModel.cs`
- Create: `Tests/Editor.Tests/Features/Console/ConsolePanelViewModelTests.cs`
- Create: `Tests/Editor.Tests/Features/Problems/ProblemsPanelViewModelTests.cs`

- [x] **Step 1: Write failing panel view model tests**

Console shows all recent diagnostics:

```csharp
[Fact]
public void Console_panel_tracks_recent_diagnostics()
{
    var diagnostics = new EditorDiagnosticService();
    var viewModel = new ConsolePanelViewModel(diagnostics, new CapturingUiDispatcher(hasAccess: true));

    var record = diagnostics.Publish(
        EditorDiagnosticSeverity.Info,
        EditorDiagnosticChannel.Debug,
        "command",
        "workbench",
        "Command completed.");

    Assert.Equal([record], viewModel.Records);
    Assert.Equal("1", viewModel.RecordCountText);
}
```

Problems shows only problem-channel diagnostics:

```csharp
[Fact]
public void Problems_panel_tracks_problem_diagnostics_only()
{
    var diagnostics = new EditorDiagnosticService();
    var viewModel = new ProblemsPanelViewModel(diagnostics, new CapturingUiDispatcher(hasAccess: true));
    diagnostics.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "debug", "command", "Debug");
    var problem = diagnostics.Publish(EditorDiagnosticSeverity.Error, EditorDiagnosticChannel.Problem, "validation", "scene", "Problem");

    Assert.Equal([problem], viewModel.Records);
    Assert.Equal("1", viewModel.RecordCountText);
}
```

Both panels post refresh work through `IEditorUiDispatcher` when diagnostics are published off the UI thread.

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~ConsolePanelViewModelTests|FullyQualifiedName~ProblemsPanelViewModelTests"
```

Expected: compile failure because panel properties and constructors do not exist.

- [x] **Step 3: Implement panel consumption**

Each panel view model:

- accepts `IEditorDiagnosticService?` and `IEditorUiDispatcher?`;
- exposes `IReadOnlyList<EditorDiagnosticRecord> Records`;
- exposes `RecordCountText`;
- subscribes to `DiagnosticsChanged`;
- refreshes immediately on UI thread or posts through dispatcher;
- implements `IDisposable` to unsubscribe.

Console uses `GetRecentDiagnostics()`. Problems uses `GetProblemDiagnostics()`.

- [x] **Step 4: Verify GREEN**

Run the same focused command. Expected: panel view model tests pass.

## Task 3: Workbench And Status Projection Integration

**Files:**
- Modify: `Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `Shell/Composition/EditorFeatureCatalog.cs`
- Modify: `Shell/Composition/StudioCompositionRoot.cs`
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/StudioCompositionRootTests.cs`

- [x] **Step 1: Write failing integration tests**

Workbench should inject the same diagnostics service into Console and Problems panel content:

```csharp
[Fact]
public void Compose_injects_shared_diagnostics_into_console_and_problems_panels()
{
    var diagnostics = new EditorDiagnosticService();
    var composition = new EditorExtensionHost(
        [new WorkbenchFeatureModule(new EditorSelectionService(), diagnostics)]).Compose();

    var console = Assert.IsType<ConsolePanelViewModel>(
        composition.PanelRegistry.GetRequired("console").CreateContent());
    var problems = Assert.IsType<ProblemsPanelViewModel>(
        composition.PanelRegistry.GetRequired("problems").CreateContent());

    var record = diagnostics.Publish(EditorDiagnosticSeverity.Error, EditorDiagnosticChannel.Problem, "validation", "scene", "Missing reference.");

    Assert.Equal([record], console.Records);
    Assert.Equal([record], problems.Records);
}
```

Main window command feedback should publish a debug diagnostic and use latest diagnostic status:

```csharp
[Fact]
public void Command_feedback_publishes_debug_diagnostic_and_updates_latest_status()
{
    var diagnostics = new EditorDiagnosticService();
    var viewModel = CreateMainWindowViewModel(diagnostics: diagnostics);

    viewModel.ToolsMenuItems.Single().OpenCommand.Execute(null);

    var record = Assert.Single(diagnostics.GetRecentDiagnostics());
    Assert.Equal(EditorDiagnosticChannel.Debug, record.Channel);
    Assert.Equal("workbench.commandPalette.open", record.Source);
    Assert.Equal(record.Message, viewModel.CommandFeedbackMessage);
}
```

`StudioCompositionRoot.CreateMainWindowSession()` should share diagnostics between the main window and Workbench panels.

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~StudioCompositionRootTests"
```

Expected: compile or assertion failure because diagnostics are not wired through the app composition.

- [x] **Step 3: Wire diagnostics through composition**

Add optional `IEditorDiagnosticService` parameters where needed:

- `WorkbenchFeatureModule(IEditorSelectionService selectionService, IEditorDiagnosticService? diagnostics = null)`
- `EditorFeatureCatalog.CreateDefaultModules(selectionService, diagnostics)`
- `StudioCompositionRoot.CreateMainWindowSession` creates one `EditorDiagnosticService` and passes it to both host modules and `MainWindowViewModel`.
- `MainWindowViewModel` accepts `IEditorDiagnosticService? diagnostics = null`, subscribes to `DiagnosticsChanged`, updates the same command/status properties from `GetLatestDiagnostic()`, and publishes command feedback diagnostics.

Command feedback maps to `EditorDiagnosticChannel.Debug`; severity maps from `EditorCommandFeedbackSeverity`.

- [x] **Step 4: Verify GREEN**

Run the same focused command. Expected: integration tests pass.

## Task 4: Documentation And Verification

**Files:**
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`
- Modify: `docs/superpowers/plans/2026-06-23-studio-observability-projection.md`

- [x] **Step 1: Document observability boundary**

Record that diagnostic projection v0 is UI-neutral and feeds latest status, Console and Problems view models only. It does not add native logs, shell command input, full Console/Problems UI, provider/plugin reload diagnostics, or persisted logs.

- [x] **Step 2: Run focused and full verification**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDiagnosticServiceTests|FullyQualifiedName~ConsolePanelViewModelTests|FullyQualifiedName~ProblemsPanelViewModelTests|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~StudioCompositionRootTests|FullyQualifiedName~EditorCommandFeedbackSnapshotTests"
dotnet test Editor.sln -c Release
```

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

From `apps/studio`:

```powershell
git diff --check
rg -n "AssemblyLoadContext|NativeEditorBridge|C\+\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly|ProcessStartInfo|Start\\(|Console\\.Read|Console\\.Write" Core Shell Features Tests
```

Expected: tests pass; encoding has 0 issues; diff check is clean; non-goal search has no implementation matches except existing background task `Start(...)` methods.

- [x] **Step 3: Commit**

```powershell
git add Core\Models\EditorDiagnosticSeverity.cs Core\Models\EditorDiagnosticChannel.cs Core\Models\EditorDiagnosticRecord.cs Core\Abstractions\IEditorDiagnosticService.cs Core\Services\EditorDiagnosticService.cs Features\Console\ViewModels\ConsolePanelViewModel.cs Features\Problems\ViewModels\ProblemsPanelViewModel.cs Features\Workbench\WorkbenchFeatureModule.cs Shell\Composition\EditorFeatureCatalog.cs Shell\Composition\StudioCompositionRoot.cs Shell\ViewModels\MainWindowViewModel.cs Tests\Editor.Tests\Core\EditorDiagnosticServiceTests.cs Tests\Editor.Tests\Features\Console\ConsolePanelViewModelTests.cs Tests\Editor.Tests\Features\Problems\ProblemsPanelViewModelTests.cs Tests\Editor.Tests\Features\Workbench\WorkbenchFeatureModuleTests.cs Tests\Editor.Tests\Shell\ViewModels\MainWindowViewModelTests.cs Tests\Editor.Tests\Shell\Composition\StudioCompositionRootTests.cs docs\Dock系统指南.md docs\编辑器UI平台规范.md docs\superpowers\plans\2026-06-23-studio-observability-projection.md
git commit -m "feat: project studio diagnostics to status panels"
```
