# Code-first UI V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the first practical Code-first UI vertical slice for trusted Studio panels without connecting native runtime or Vulkan backend data.

**Architecture:** Keep `Core/CodeFirstUI` UI-neutral: authoring API, immutable node tree, state store, event queue, validation, and rebuild vocabulary. Keep `Shell/CodeFirstUI` responsible for Avalonia control creation, host lifecycle, input forwarding, status placeholders, and request scheduling. Use `Features/UiStyle` as the acceptance slice so every v1 primitive has a visible and testable sample.

**Tech Stack:** C#/.NET 10, Avalonia 12, CommunityToolkit.Mvvm, xUnit, existing `Editor.sln` test project.

---

## Current Evidence

- `Core/CodeFirstUI` already has `EditorGui`, `GuiFrameBuilder`, `GuiNode`, `GuiNodeKind`, `GuiStateStore`, `GuiEventQueue`, and `GuiTreeValidator`.
- `Shell/CodeFirstUI` already has `CodeFirstPanelHostViewModel`, `CodeFirstPanelHostView`, `GuiAvaloniaControlFactory`, and text commit scheduling.
- `Features/UiStyle/UiStylePanel.cs` already exercises navigation, text, buttons, inputs, lists, foldouts, and feedback states.
- Existing focused tests already cover many primitives under:
  - `Tests/Editor.Tests/Core/CodeFirstUI`
  - `Tests/Editor.Tests/Shell/CodeFirstUI`
  - `Tests/Editor.Tests/Features/UiStyle`
- This plan deliberately does not connect native runtime, Vulkan viewport surfaces, renderer snapshots, or writable property handles.

## File Structure

- Modify `Core/CodeFirstUI/Authoring/EditorGui.cs`
  - Add a read-only `Property(...)` authoring helper.
- Modify `Core/CodeFirstUI/Building/GuiFrameBuilder.cs`
  - Add a `Property(...)` node builder.
- Modify `Core/CodeFirstUI/Models/GuiNodePayload.cs`
  - Add the read-only property value payload.
- Create `Core/CodeFirstUI/Models/GuiRebuildReason.cs`
  - Define host rebuild reason flags.
- Modify `Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModel.cs`
  - Add request scheduling/coalescing seam and reason tracking.
- Modify `Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactory.cs`
  - Render `GuiNodeKind.Property` as a compact read-only property row.
- Modify `Shell/CodeFirstUI/Views/CodeFirstPanelHostView.axaml`
  - Add styles for property value text.
- Modify `Features/UiStyle/UiStylePanel.cs`
  - Add property-row samples to the catalog.
- Modify `Tests/Editor.Tests/Core/CodeFirstUI/Building/GuiFrameBuilderTests.cs`
- Modify `Tests/Editor.Tests/Core/CodeFirstUI/Authoring/EditorGuiTests.cs`
- Modify `Tests/Editor.Tests/Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModelTests.cs`
- Modify `Tests/Editor.Tests/Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactoryTests.cs`
- Modify `Tests/Editor.Tests/Shell/CodeFirstUI/Views/CodeFirstPanelHostViewXamlTests.cs`
- Modify `Tests/Editor.Tests/Features/UiStyle/UiStylePanelTests.cs`
- Modify `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
  - Lock Code-first UI boundary rules.
- Modify `docs/Code-first UI设计.md`
  - Record the v1 status and the explicit non-goal of backend/native integration.

---

### Task 1: Add Read-only Property Node to Core Contract

**Files:**
- Modify: `Core/CodeFirstUI/Models/GuiNodePayload.cs`
- Modify: `Core/CodeFirstUI/Building/GuiFrameBuilder.cs`
- Modify: `Core/CodeFirstUI/Authoring/EditorGui.cs`
- Test: `Tests/Editor.Tests/Core/CodeFirstUI/Building/GuiFrameBuilderTests.cs`
- Test: `Tests/Editor.Tests/Core/CodeFirstUI/Authoring/EditorGuiTests.cs`

- [ ] **Step 1: Write failing builder test**

Add this test to `Tests/Editor.Tests/Core/CodeFirstUI/Building/GuiFrameBuilderTests.cs`:

```csharp
[Fact]
public void Build_preserves_property_payload()
{
    var builder = new GuiFrameBuilder("ui-style");

    builder.Property("triangles", "Triangles", "12,480");

    var node = Assert.Single(builder.Build().Root.Children);
    Assert.Equal(new GuiNodeId("ui-style", "triangles", GuiNodeKind.Property), node.Id);
    Assert.Equal(GuiNodeKind.Property, node.Kind);
    Assert.Equal("Triangles", node.Label);
    Assert.Equal("12,480", node.Payload.PropertyValue);
}
```

- [ ] **Step 2: Write failing authoring test**

Add this test to `Tests/Editor.Tests/Core/CodeFirstUI/Authoring/EditorGuiTests.cs`:

```csharp
[Fact]
public void Property_emits_read_only_property_node()
{
    var builder = new GuiFrameBuilder("ui-style");
    var gui = CreateGui(builder);

    gui.Property("draw-calls", "Draw Calls", 184);

    var node = Assert.Single(builder.Build().Root.Children);
    Assert.Equal(GuiNodeKind.Property, node.Kind);
    Assert.Equal("Draw Calls", node.Label);
    Assert.Equal("184", node.Payload.PropertyValue);
}
```

Use the existing `CreateGui(...)` helper in that test file. If the helper is named differently, use the local helper already used by adjacent tests instead of creating a second one.

- [ ] **Step 3: Run tests and verify they fail**

Run:

```powershell
dotnet test Editor.sln --filter "FullyQualifiedName~GuiFrameBuilderTests|FullyQualifiedName~EditorGuiTests"
```

Expected: fail with missing `GuiFrameBuilder.Property`, missing `EditorGui.Property`, or missing `GuiNodePayload.PropertyValue`.

- [ ] **Step 4: Add payload field**

In `Core/CodeFirstUI/Models/GuiNodePayload.cs`, add:

```csharp
public string? PropertyValue { get; init; }
```

Place it next to `TextValue` because it is display text, not mutable text input.

- [ ] **Step 5: Add builder method**

In `Core/CodeFirstUI/Building/GuiFrameBuilder.cs`, add:

```csharp
public GuiNodeId Property(
    string key,
    string label,
    string value)
{
    ArgumentNullException.ThrowIfNull(value);

    return AddLeaf(
        key,
        GuiNodeKind.Property,
        label,
        new GuiNodePayload
        {
            PropertyValue = value,
        });
}
```

- [ ] **Step 6: Add authoring helpers**

In `Core/CodeFirstUI/Authoring/EditorGui.cs`, add:

```csharp
public void Property(
    string key,
    string label,
    string value)
{
    builder_.Property(key, label, value);
}

public void Property<T>(
    string key,
    string label,
    T value)
{
    builder_.Property(key, label, value?.ToString() ?? string.Empty);
}
```

- [ ] **Step 7: Run focused tests and verify green**

Run:

```powershell
dotnet test Editor.sln --filter "FullyQualifiedName~GuiFrameBuilderTests|FullyQualifiedName~EditorGuiTests"
```

Expected: all selected tests pass.

- [ ] **Step 8: Commit**

```powershell
git add Core\CodeFirstUI\Models\GuiNodePayload.cs Core\CodeFirstUI\Building\GuiFrameBuilder.cs Core\CodeFirstUI\Authoring\EditorGui.cs Tests\Editor.Tests\Core\CodeFirstUI\Building\GuiFrameBuilderTests.cs Tests\Editor.Tests\Core\CodeFirstUI\Authoring\EditorGuiTests.cs
git commit -m "feat: add code-first property node contract"
```

---

### Task 2: Render Property Node in Avalonia and UiStyle

**Files:**
- Modify: `Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactory.cs`
- Modify: `Shell/CodeFirstUI/Views/CodeFirstPanelHostView.axaml`
- Modify: `Features/UiStyle/UiStylePanel.cs`
- Test: `Tests/Editor.Tests/Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactoryTests.cs`
- Test: `Tests/Editor.Tests/Shell/CodeFirstUI/Views/CodeFirstPanelHostViewXamlTests.cs`
- Test: `Tests/Editor.Tests/Features/UiStyle/UiStylePanelTests.cs`

- [ ] **Step 1: Write failing factory test**

Add this test to `Tests/Editor.Tests/Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactoryTests.cs`:

```csharp
[Fact]
public void Build_maps_property_to_read_only_property_row()
{
    var builder = new GuiFrameBuilder("ui-style");
    builder.Property("draw-calls", "Draw Calls", "184");
    var factory = new GuiAvaloniaControlFactory(new NoopCodeFirstPanelHost());

    var control = factory.Build(builder.Build());

    var row = Assert.IsType<Grid>(control);
    Assert.Contains("code-first-property-row", row.Classes);
    Assert.NotNull(FindDescendant<TextBlock>(row, text => text.Text == "Draw Calls"));
    var value = FindDescendant<TextBlock>(row, text => text.Text == "184");
    Assert.NotNull(value);
    Assert.Contains("code-first-property-value", value!.Classes);
}
```

- [ ] **Step 2: Write failing XAML style test**

Add this test to `Tests/Editor.Tests/Shell/CodeFirstUI/Views/CodeFirstPanelHostViewXamlTests.cs`:

```csharp
[Fact]
public void Code_first_property_value_uses_read_only_text_style()
{
    var xaml = LoadSource("Shell", "CodeFirstUI", "Views", "CodeFirstPanelHostView.axaml");

    Assert.Contains("TextBlock.code-first-property-value", xaml);
    Assert.Contains("EditorBrushTextPrimary", xaml);
}
```

- [ ] **Step 3: Write failing UiStyle acceptance test**

Add this assertion to `Inputs_page_contains_code_first_text_field_toggle_combo_box_radio_slider_color_vector_and_number_input_samples` in `Tests/Editor.Tests/Features/UiStyle/UiStylePanelTests.cs`:

```csharp
var property = Assert.Single(navigation.Children, child => child.Id.KeyPath == "catalog/draw-calls");
Assert.Equal(GuiNodeKind.Property, property.Kind);
Assert.Equal("Draw Calls", property.Label);
Assert.Equal("184", property.Payload.PropertyValue);
```

- [ ] **Step 4: Run tests and verify they fail**

Run:

```powershell
dotnet test Editor.sln --filter "FullyQualifiedName~GuiAvaloniaControlFactoryTests|FullyQualifiedName~CodeFirstPanelHostViewXamlTests|FullyQualifiedName~UiStylePanelTests"
```

Expected: fail because `GuiAvaloniaControlFactory` does not render `GuiNodeKind.Property` and UiStyle does not declare `draw-calls`.

- [ ] **Step 5: Add factory rendering**

In `Shell/CodeFirstUI/Adapters/GuiAvaloniaControlFactory.cs`, add `GuiNodeKind.Property => BuildProperty(node),` to the `BuildNode` switch.

Add this method near the other property-row controls:

```csharp
private static Control BuildProperty(GuiNode node)
{
    var label = new TextBlock
    {
        Text = node.Label ?? string.Empty,
        VerticalAlignment = VerticalAlignment.Center,
        TextWrapping = TextWrapping.NoWrap,
    };
    label.Classes.Add("code-first-input-label");

    var value = new TextBlock
    {
        Text = node.Payload.PropertyValue ?? string.Empty,
        VerticalAlignment = VerticalAlignment.Center,
        TextWrapping = TextWrapping.NoWrap,
    };
    value.Classes.Add("code-first-property-value");

    var grid = new Grid
    {
        ColumnDefinitions = new ColumnDefinitions("120,*"),
    };
    grid.Classes.Add("code-first-property-row");
    Grid.SetColumn(label, 0);
    Grid.SetColumn(value, 1);
    grid.Children.Add(label);
    grid.Children.Add(value);
    return grid;
}
```

- [ ] **Step 6: Add XAML style**

In `Shell/CodeFirstUI/Views/CodeFirstPanelHostView.axaml`, add:

```xml
<Style Selector="TextBlock.code-first-property-value">
    <Setter Property="Foreground" Value="{DynamicResource EditorBrushTextPrimary}" />
    <Setter Property="FontSize" Value="{DynamicResource EditorFontSizeDefault}" />
    <Setter Property="TextTrimming" Value="CharacterEllipsis" />
</Style>
```

- [ ] **Step 7: Add UiStyle sample**

In `Features/UiStyle/UiStylePanel.cs`, inside `DrawInputsPage`, add:

```csharp
gui.Property("draw-calls", "Draw Calls", 184);
```

Place it before mutable inputs so the page clearly shows read-only values separate from editable controls.

- [ ] **Step 8: Run focused tests and verify green**

Run:

```powershell
dotnet test Editor.sln --filter "FullyQualifiedName~GuiAvaloniaControlFactoryTests|FullyQualifiedName~CodeFirstPanelHostViewXamlTests|FullyQualifiedName~UiStylePanelTests"
```

Expected: all selected tests pass.

- [ ] **Step 9: Commit**

```powershell
git add Shell\CodeFirstUI\Adapters\GuiAvaloniaControlFactory.cs Shell\CodeFirstUI\Views\CodeFirstPanelHostView.axaml Features\UiStyle\UiStylePanel.cs Tests\Editor.Tests\Shell\CodeFirstUI\Adapters\GuiAvaloniaControlFactoryTests.cs Tests\Editor.Tests\Shell\CodeFirstUI\Views\CodeFirstPanelHostViewXamlTests.cs Tests\Editor.Tests\Features\UiStyle\UiStylePanelTests.cs
git commit -m "feat: render code-first property rows"
```

---

### Task 3: Add Rebuild Reasons and Coalescing Seam

**Files:**
- Create: `Core/CodeFirstUI/Models/GuiRebuildReason.cs`
- Modify: `Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModel.cs`
- Test: `Tests/Editor.Tests/Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModelTests.cs`

- [ ] **Step 1: Write failing coalescing test**

Add this test to `Tests/Editor.Tests/Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModelTests.cs`:

```csharp
[Fact]
public void Request_rebuild_coalesces_multiple_reasons_until_dispatcher_runs()
{
    var dispatcher = new RecordingEditorUiDispatcher();
    var panel = new RecordingCodeFirstPanel();
    var host = new CodeFirstPanelHostViewModel(panel, uiDispatcher: dispatcher);
    host.OnPanelAttached(CreateLifecycleContext());
    dispatcher.RunPending();
    var initialBuildCount = panel.GuiBuildCount;

    host.RequestRebuild(GuiRebuildReason.InputEvent);
    host.RequestRebuild(GuiRebuildReason.FrameTick);

    Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    Assert.Equal(1, dispatcher.PendingCount);

    dispatcher.RunPending();

    Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
    Assert.True(host.LastRebuildReasons.HasFlag(GuiRebuildReason.InputEvent));
    Assert.True(host.LastRebuildReasons.HasFlag(GuiRebuildReason.FrameTick));
}
```

Add this helper class to the same test file:

```csharp
private sealed class RecordingEditorUiDispatcher : IEditorUiDispatcher
{
    private readonly Queue<Action> pendingActions_ = [];

    public int PendingCount => pendingActions_.Count;

    public bool CheckAccess() => true;

    public void Post(Action action)
    {
        pendingActions_.Enqueue(action);
    }

    public void RunPending()
    {
        while (pendingActions_.TryDequeue(out var action))
        {
            action();
        }
    }
}
```

Add `using Editor.Core.Abstractions;` to the test file if it is not already present.

- [ ] **Step 2: Write failing reason routing test**

Add:

```csharp
[Fact]
public void Input_events_request_input_rebuild_reason()
{
    var dispatcher = new RecordingEditorUiDispatcher();
    var panel = new InputDrivenCodeFirstPanel();
    var host = new CodeFirstPanelHostViewModel(panel, uiDispatcher: dispatcher);
    host.OnPanelAttached(CreateLifecycleContext("ui.style"));
    dispatcher.RunPending();

    host.CommitText(new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField), "gbuffer");
    dispatcher.RunPending();

    Assert.Equal("gbuffer", panel.FilterText);
    Assert.Equal(GuiRebuildReason.InputEvent, host.LastRebuildReasons);
}
```

- [ ] **Step 3: Run tests and verify they fail**

Run:

```powershell
dotnet test Editor.sln --filter FullyQualifiedName~CodeFirstPanelHostViewModelTests
```

Expected: fail because `GuiRebuildReason`, `RequestRebuild(...)`, the dispatcher constructor overload, and `LastRebuildReasons` do not exist.

- [ ] **Step 4: Create rebuild reason enum**

Create `Core/CodeFirstUI/Models/GuiRebuildReason.cs`:

```csharp
using System;

namespace Editor.Core.CodeFirstUI;

[Flags]
public enum GuiRebuildReason
{
    None = 0,
    InitialOpen = 1 << 0,
    LifecycleChanged = 1 << 1,
    InputEvent = 1 << 2,
    FrameTick = 1 << 3,
    ExplicitRefresh = 1 << 4,
}
```

- [ ] **Step 5: Add scheduling fields and constructor seam**

In `Shell/CodeFirstUI/Hosting/CodeFirstPanelHostViewModel.cs`, add `using Editor.Core.Abstractions;`.

Add fields:

```csharp
private readonly IEditorUiDispatcher uiDispatcher_;
private bool isRebuildScheduled_;
private GuiRebuildReason pendingRebuildReasons_;
```

Change the constructor signature:

```csharp
public CodeFirstPanelHostViewModel(
    CodeFirstEditorPanel panel,
    IEditorGuiCommandExecutor? commandExecutor = null,
    IEditorUiDispatcher? uiDispatcher = null)
```

Set:

```csharp
uiDispatcher_ = uiDispatcher ?? ImmediateCodeFirstUiDispatcher.Instance;
```

Add a private nested dispatcher:

```csharp
private sealed class ImmediateCodeFirstUiDispatcher : IEditorUiDispatcher
{
    public static ImmediateCodeFirstUiDispatcher Instance { get; } = new();

    public bool CheckAccess() => true;

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        action();
    }
}
```

- [ ] **Step 6: Add request and flush methods**

Add:

```csharp
public GuiRebuildReason LastRebuildReasons { get; private set; }

internal void RequestRebuild(GuiRebuildReason reason)
{
    ThrowIfDisposed();
    if (reason == GuiRebuildReason.None)
    {
        return;
    }

    pendingRebuildReasons_ |= reason;
    if (isRebuildScheduled_)
    {
        return;
    }

    isRebuildScheduled_ = true;
    uiDispatcher_.Post(FlushRebuild);
}

private void FlushRebuild()
{
    if (isDisposed_)
    {
        return;
    }

    var reasons = pendingRebuildReasons_;
    pendingRebuildReasons_ = GuiRebuildReason.None;
    isRebuildScheduled_ = false;
    LastRebuildReasons = reasons;
    Rebuild();
}
```

- [ ] **Step 7: Replace direct rebuild calls**

In `CodeFirstPanelHostViewModel`, replace these calls:

```csharp
Rebuild();
```

Use these reasons:

```csharp
// OnPanelAttached
RequestRebuild(GuiRebuildReason.InitialOpen);

// OnPanelActivated
RequestRebuild(GuiRebuildReason.LifecycleChanged);

// OnEditorPanelFrame when repaint requested
RequestRebuild(GuiRebuildReason.FrameTick);

// ClickButton, SelectItem, SelectNavigationRoute, SetNavigationRouteExpanded,
// CommitText, SetToggle, SetFoldoutExpanded
RequestRebuild(GuiRebuildReason.InputEvent);
```

Keep `SetText`, `ResizeSplit`, `SetSliderValue`, `SetNumberInputValue`, `SetColorValue`, `SetVector2Value`, `SetVector3Value`, and `SetVector4Value` as state-only updates with no immediate rebuild, matching existing tests.

- [ ] **Step 8: Run focused tests and verify green**

Run:

```powershell
dotnet test Editor.sln --filter FullyQualifiedName~CodeFirstPanelHostViewModelTests
```

Expected: all selected tests pass.

- [ ] **Step 9: Commit**

```powershell
git add Core\CodeFirstUI\Models\GuiRebuildReason.cs Shell\CodeFirstUI\Hosting\CodeFirstPanelHostViewModel.cs Tests\Editor.Tests\Shell\CodeFirstUI\Hosting\CodeFirstPanelHostViewModelTests.cs
git commit -m "feat: coalesce code-first rebuild requests"
```

---

### Task 4: Lock Code-first UI Boundaries

**Files:**
- Modify: `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- Modify: `docs/Code-first UI设计.md`

- [ ] **Step 1: Add architecture tests**

Add these tests to `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`:

```csharp
[Fact]
public void Core_code_first_ui_does_not_reference_avalonia_or_shell()
{
    var root = FindRepositoryRoot();
    var files = Directory.EnumerateFiles(
        Path.Combine(root, "Core", "CodeFirstUI"),
        "*.cs",
        SearchOption.AllDirectories);

    var offenders = files
        .Where(path =>
        {
            var text = File.ReadAllText(path);
            return text.Contains("Avalonia", StringComparison.Ordinal)
                || text.Contains("Editor.Shell", StringComparison.Ordinal);
        })
        .Select(path => Path.GetRelativePath(root, path))
        .Order(StringComparer.Ordinal)
        .ToArray();

    Assert.Empty(offenders);
}

[Fact]
public void Shell_code_first_ui_does_not_reference_features_or_native_runtime()
{
    var root = FindRepositoryRoot();
    var files = Directory.EnumerateFiles(
        Path.Combine(root, "Shell", "CodeFirstUI"),
        "*.cs",
        SearchOption.AllDirectories);

    var offenders = files
        .Where(path =>
        {
            var text = File.ReadAllText(path);
            return text.Contains("Editor.Features", StringComparison.Ordinal)
                || text.Contains("Vulkan", StringComparison.Ordinal)
                || text.Contains("Native", StringComparison.Ordinal);
        })
        .Select(path => Path.GetRelativePath(root, path))
        .Order(StringComparer.Ordinal)
        .ToArray();

    Assert.Empty(offenders);
}
```

- [ ] **Step 2: Run architecture tests**

Run:

```powershell
dotnet test Editor.sln --filter FullyQualifiedName~StudioLayeringTests
```

Expected: pass. If this fails, fix the offending reference instead of weakening the test.

- [ ] **Step 3: Update design doc status**

In `docs/Code-first UI设计.md`, add a short section near the MVP/acceptance area:

```markdown
### v1 implementation status

The current v1 path is an internal Studio-only vertical slice. It covers the UI-neutral node contract, state store, event queue, validation, Shell-owned Avalonia control creation, lifecycle host, and the `Features/UiStyle` sample panel.

Backend/native/runtime integration is intentionally outside v1. Runtime data must first enter Studio as Core snapshots, diagnostics, provider status, or command results before a Code-first panel consumes it.
```

- [ ] **Step 4: Run architecture tests again**

Run:

```powershell
dotnet test Editor.sln --filter FullyQualifiedName~StudioLayeringTests
```

Expected: pass.

- [ ] **Step 5: Commit**

```powershell
git add Tests\Editor.Tests\Architecture\StudioLayeringTests.cs "docs\Code-first UI设计.md"
git commit -m "test: lock code-first ui boundaries"
```

---

### Task 5: Full Verification Gate

**Files:**
- No production edits unless a verification failure identifies a concrete issue.

- [ ] **Step 1: Run focused Code-first test suite**

Run:

```powershell
dotnet test Editor.sln --filter "FullyQualifiedName~CodeFirstUI|FullyQualifiedName~UiStylePanelTests|FullyQualifiedName~StudioLayeringTests"
```

Expected: all selected tests pass.

- [ ] **Step 2: Run full test suite**

Run:

```powershell
dotnet test Editor.sln
```

Expected: all tests pass.

- [ ] **Step 3: Run whitespace gate**

Run:

```powershell
git diff --check
```

Expected: no output and exit code 0.

- [ ] **Step 4: Run encoding gate**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
```

Expected:

```text
Missing required UTF-8 BOM: 0
Unexpected UTF-8 BOM: 0
Invalid UTF-8: 0
```

- [ ] **Step 5: Summarize residual scope**

Record in the final implementation summary:

```text
Not included: native runtime bridge, Vulkan viewport surface, writable property handles, external plugin ABI, full virtual list API, or source-generated page registry.
```

- [ ] **Step 6: Commit if requested**

Only run this if the user asks for commits:

```powershell
git status --short
git add <verified files>
git commit -m "feat: complete code-first ui v1 slice"
```

---

## Self-review

- Spec coverage: The plan covers the existing design's v1 needs that are not already closed by current tests: property/read-only row, rebuild request reason vocabulary, scheduling/coalescing seam, boundary guards, and UiStyle acceptance coverage.
- Placeholder scan: No incomplete markers, omitted implementation sections, or unspecified test commands remain.
- Type consistency: `GuiRebuildReason`, `GuiNodePayload.PropertyValue`, `GuiFrameBuilder.Property`, `EditorGui.Property`, and `GuiNodeKind.Property` are named consistently across tasks.
- Scope check: The plan intentionally excludes backend/native/runtime integration and writable property handles. Those require separate specs because they affect runtime data ownership, threading, transactions, and failure handling.
