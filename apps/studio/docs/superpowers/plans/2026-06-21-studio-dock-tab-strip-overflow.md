# Studio Dock Tab Strip Overflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add single-line horizontal overflow, active-tab reveal, and drag-edge auto-scroll to Studio's custom Dock tab strip without touching runtime/native/provider layers.

**Architecture:** Keep `EditorDockWindowViewModel.Tabs` and `TabStripItems` as the only tab model state. Put deterministic scroll math in a small Shell/Docking helper with unit tests, then keep actual `ScrollViewer` offset changes in `EditorDockTabStripView` code-behind as view-only behavior. Preserve the existing pointer-captured drag state machine in `EditorDockWindowView` and adjust only the scrolled tab-strip geometry used by hit testing and local reorder.

**Tech Stack:** C#/.NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit, existing Studio Shell/Docking patterns.

---

## Source Spec

- `docs/superpowers/specs/2026-06-21-studio-dock-tab-strip-overflow-design.md`

## File Structure

- Create: `Shell/Docking/EditorDockTabStripScrollController.cs`
  - Pure helper for horizontal offset clamping, reveal-offset calculation, edge auto-scroll, and logical content origin calculation.
- Create: `Tests/Editor.Tests/Shell/Docking/EditorDockTabStripScrollControllerTests.cs`
  - Unit tests for deterministic scroll math.
- Modify: `Shell/Views/EditorDockTabStripView.axaml`
  - Wrap existing tab `ItemsControl` in a horizontal `ScrollViewer`; add non-interactive left/right overflow affordance borders.
- Modify: `Shell/Views/EditorDockTabStripView.axaml.cs`
  - Apply view-only scroll behavior, active-tab reveal, overflow-affordance state, and edge auto-scroll.
- Modify: `Shell/Views/EditorDockWindowView.axaml.cs`
  - Use tab strip content origin when building ideal tab bounds and refresh reorder entries after edge auto-scroll.
- Modify: `Shell/Docking/EditorDockWindowBounds.cs`
  - Add optional tab content origin metadata while preserving existing call sites.
- Modify: `Shell/Docking/EditorDockHitTestService.cs`
  - Build tab insertion entries from the logical tab content origin when present.
- Modify: `Tests/Editor.Tests/Shell/Docking/EditorDockHitTestServiceTests.cs`
  - Add coverage for scrolled tab-strip insertion target resolution.
- Modify: `docs/Dock系统指南.md`
  - Record single-line horizontal overflow and local edge auto-scroll as implemented; keep multi-row and advanced tab management as future work.

## Task 1: Scroll Geometry Helper

**Files:**
- Create: `Shell/Docking/EditorDockTabStripScrollController.cs`
- Create: `Tests/Editor.Tests/Shell/Docking/EditorDockTabStripScrollControllerTests.cs`

- [ ] **Step 1: Write failing helper tests**

Create `Tests/Editor.Tests/Shell/Docking/EditorDockTabStripScrollControllerTests.cs`:

```csharp
using Avalonia;
using Editor.Shell.Docking;
using Xunit;

namespace Editor.Tests.Shell.Docking;

public sealed class EditorDockTabStripScrollControllerTests
{
    [Fact]
    public void ClampOffset_limits_value_to_scrollable_range()
    {
        Assert.Equal(0, EditorDockTabStripScrollController.ClampOffset(-20, 500, 200));
        Assert.Equal(120, EditorDockTabStripScrollController.ClampOffset(120, 500, 200));
        Assert.Equal(300, EditorDockTabStripScrollController.ClampOffset(480, 500, 200));
    }

    [Fact]
    public void ClampOffset_returns_zero_when_content_fits()
    {
        Assert.Equal(0, EditorDockTabStripScrollController.ClampOffset(50, 180, 200));
    }

    [Fact]
    public void CalculateOffsetToReveal_scrolls_left_when_target_starts_before_viewport()
    {
        var offset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            new Rect(20, 0, 80, 22),
            currentOffset: 120,
            extentWidth: 500,
            viewportWidth: 200);

        Assert.Equal(12, offset);
    }

    [Fact]
    public void CalculateOffsetToReveal_scrolls_right_when_target_ends_after_viewport()
    {
        var offset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            new Rect(260, 0, 100, 22),
            currentOffset: 40,
            extentWidth: 500,
            viewportWidth: 200);

        Assert.Equal(168, offset);
    }

    [Fact]
    public void CalculateOffsetToReveal_keeps_offset_when_target_is_visible()
    {
        var offset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            new Rect(120, 0, 50, 22),
            currentOffset: 80,
            extentWidth: 500,
            viewportWidth: 200);

        Assert.Equal(80, offset);
    }

    [Fact]
    public void CalculateAutoScrollOffset_moves_near_edges_only_when_overflow_exists()
    {
        Assert.Equal(
            52,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 4,
                currentOffset: 100,
                extentWidth: 600,
                viewportWidth: 200));
        Assert.Equal(
            148,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 196,
                currentOffset: 100,
                extentWidth: 600,
                viewportWidth: 200));
        Assert.Equal(
            100,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 100,
                currentOffset: 100,
                extentWidth: 600,
                viewportWidth: 200));
        Assert.Equal(
            0,
            EditorDockTabStripScrollController.CalculateAutoScrollOffset(
                pointerX: 196,
                currentOffset: 0,
                extentWidth: 180,
                viewportWidth: 200));
    }

    [Fact]
    public void CalculateContentOriginX_offsets_viewport_origin_by_scroll_offset()
    {
        Assert.Equal(
            60,
            EditorDockTabStripScrollController.CalculateContentOriginX(
                viewportOriginX: 260,
                horizontalOffset: 200));
    }
}
```

- [ ] **Step 2: Run helper tests and verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockTabStripScrollControllerTests"
```

Expected: FAIL because `EditorDockTabStripScrollController` does not exist.

- [ ] **Step 3: Add minimal helper implementation**

Create `Shell/Docking/EditorDockTabStripScrollController.cs`:

```csharp
using Avalonia;

namespace Editor.Shell.Docking;

internal static class EditorDockTabStripScrollController
{
    public const double DefaultRevealPadding = 8.0;
    public const double DefaultEdgeAutoScrollZone = 24.0;
    public const double DefaultEdgeAutoScrollStep = 48.0;

    public static double ClampOffset(double requestedOffset, double extentWidth, double viewportWidth)
    {
        var maxOffset = GetMaxOffset(extentWidth, viewportWidth);
        if (maxOffset <= 0)
        {
            return 0;
        }

        if (double.IsNaN(requestedOffset) || double.IsInfinity(requestedOffset))
        {
            return 0;
        }

        return System.Math.Clamp(requestedOffset, 0, maxOffset);
    }

    public static double CalculateOffsetToReveal(
        Rect targetBounds,
        double currentOffset,
        double extentWidth,
        double viewportWidth,
        double padding = DefaultRevealPadding)
    {
        var normalizedOffset = ClampOffset(currentOffset, extentWidth, viewportWidth);
        if (targetBounds.Width <= 0 || viewportWidth <= 0 || extentWidth <= viewportWidth)
        {
            return normalizedOffset;
        }

        var normalizedPadding = NormalizeNonNegative(padding);
        var leftEdge = normalizedOffset + normalizedPadding;
        var rightEdge = normalizedOffset + viewportWidth - normalizedPadding;
        if (targetBounds.X < leftEdge)
        {
            return ClampOffset(targetBounds.X - normalizedPadding, extentWidth, viewportWidth);
        }

        if (targetBounds.Right > rightEdge)
        {
            return ClampOffset(
                targetBounds.Right - viewportWidth + normalizedPadding,
                extentWidth,
                viewportWidth);
        }

        return normalizedOffset;
    }

    public static double CalculateAutoScrollOffset(
        double pointerX,
        double currentOffset,
        double extentWidth,
        double viewportWidth,
        double edgeZone = DefaultEdgeAutoScrollZone,
        double step = DefaultEdgeAutoScrollStep)
    {
        var normalizedOffset = ClampOffset(currentOffset, extentWidth, viewportWidth);
        if (viewportWidth <= 0 || extentWidth <= viewportWidth)
        {
            return 0;
        }

        var normalizedZone = NormalizeNonNegative(edgeZone);
        var normalizedStep = NormalizeNonNegative(step);
        if (normalizedZone <= 0 || normalizedStep <= 0)
        {
            return normalizedOffset;
        }

        if (pointerX <= normalizedZone)
        {
            return ClampOffset(normalizedOffset - normalizedStep, extentWidth, viewportWidth);
        }

        if (pointerX >= viewportWidth - normalizedZone)
        {
            return ClampOffset(normalizedOffset + normalizedStep, extentWidth, viewportWidth);
        }

        return normalizedOffset;
    }

    public static double CalculateContentOriginX(double viewportOriginX, double horizontalOffset)
    {
        if (double.IsNaN(viewportOriginX) || double.IsInfinity(viewportOriginX))
        {
            return 0;
        }

        if (double.IsNaN(horizontalOffset) || double.IsInfinity(horizontalOffset))
        {
            return viewportOriginX;
        }

        return viewportOriginX - horizontalOffset;
    }

    private static double GetMaxOffset(double extentWidth, double viewportWidth)
    {
        if (double.IsNaN(extentWidth)
            || double.IsInfinity(extentWidth)
            || double.IsNaN(viewportWidth)
            || double.IsInfinity(viewportWidth)
            || extentWidth <= 0
            || viewportWidth <= 0)
        {
            return 0;
        }

        return System.Math.Max(0, extentWidth - viewportWidth);
    }

    private static double NormalizeNonNegative(double value)
    {
        return double.IsNaN(value) || double.IsInfinity(value) || value < 0 ? 0 : value;
    }
}
```

- [ ] **Step 4: Run helper tests and verify GREEN**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockTabStripScrollControllerTests"
```

Expected: PASS.

- [ ] **Step 5: Commit Task 1**

```powershell
git add Shell\Docking\EditorDockTabStripScrollController.cs Tests\Editor.Tests\Shell\Docking\EditorDockTabStripScrollControllerTests.cs
git commit -m "feat: add dock tab strip scroll math"
```

## Task 2: Scrollable Tab Strip View

**Files:**
- Modify: `Shell/Views/EditorDockTabStripView.axaml`
- Modify: `Shell/Views/EditorDockTabStripView.axaml.cs`

- [ ] **Step 1: Write failing view-facing test through build**

No existing headless view harness covers `ScrollViewer` layout here. Use the helper tests from Task 1 as RED/GREEN coverage for math, and make this task's first verification a build that should fail after XAML references not-yet-implemented code-behind members are added.

- [ ] **Step 2: Replace tab strip XAML**

Replace `Shell/Views/EditorDockTabStripView.axaml` with:

```xml
<UserControl xmlns="https://github.com/avaloniaui"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             xmlns:vm="using:Editor.Shell.ViewModels"
             xmlns:views="using:Editor.Shell.Views"
             x:Class="Editor.Shell.Views.EditorDockTabStripView"
             x:DataType="vm:EditorDockWindowViewModel">
    <UserControl.Styles>
        <Style Selector="ScrollViewer.owned-dock-tab-scroll-host">
            <Setter Property="MinHeight" Value="{DynamicResource EditorDockTabHeight}" />
            <Setter Property="Background" Value="{DynamicResource EditorBrushBase01}" />
            <Setter Property="BorderBrush" Value="{DynamicResource EditorBrushDivider}" />
            <Setter Property="BorderThickness" Value="0,1,0,1" />
        </Style>

        <Style Selector="ItemsControl.owned-dock-tabs.owned-dock-tab-well">
            <Setter Property="MinHeight" Value="{DynamicResource EditorDockTabHeight}" />
        </Style>

        <Style Selector="Border.owned-dock-tab-overflow-affordance">
            <Setter Property="Width" Value="14" />
            <Setter Property="Background" Value="{DynamicResource EditorBrushSurfaceOverlay}" />
            <Setter Property="Opacity" Value="0.72" />
            <Setter Property="IsHitTestVisible" Value="False" />
            <Setter Property="IsVisible" Value="False" />
        </Style>
    </UserControl.Styles>

    <Grid ClipToBounds="True">
        <ScrollViewer x:Name="DockTabStripScrollViewer"
                      Classes="owned-dock-tab-scroll-host"
                      HorizontalScrollBarVisibility="Hidden"
                      VerticalScrollBarVisibility="Disabled">
            <ItemsControl x:Name="DockTabStripItems"
                          Classes="owned-dock-tabs owned-dock-tab-well"
                          IsVisible="{Binding HasTabs}"
                          ItemsSource="{Binding TabStripItems}">
                <ItemsControl.ItemsPanel>
                    <ItemsPanelTemplate>
                        <StackPanel Orientation="Horizontal" />
                    </ItemsPanelTemplate>
                </ItemsControl.ItemsPanel>
                <ItemsControl.ItemTemplate>
                    <DataTemplate x:DataType="vm:EditorDockTabStripItemViewModel">
                        <views:EditorDockTabItemView />
                    </DataTemplate>
                </ItemsControl.ItemTemplate>
            </ItemsControl>
        </ScrollViewer>

        <Border x:Name="LeftOverflowAffordance"
                Classes="owned-dock-tab-overflow-affordance"
                HorizontalAlignment="Left" />
        <Border x:Name="RightOverflowAffordance"
                Classes="owned-dock-tab-overflow-affordance"
                HorizontalAlignment="Right" />
    </Grid>
</UserControl>
```

- [ ] **Step 3: Run build and verify RED**

Run:

```powershell
dotnet build Editor.sln -c Release
```

Expected: FAIL because the view does not yet subscribe to scroll/active changes and the next step's code-behind methods are not present only if the implementer wires event handlers before adding code. If the build passes after the XAML-only change, proceed and treat Step 4 as the GREEN implementation for behavior.

- [ ] **Step 4: Add view-only scroll behavior**

Replace `Shell/Views/EditorDockTabStripView.axaml.cs` with:

```csharp
using System;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockTabStripView : UserControl
{
    private EditorDockWindowViewModel? window_;

    public EditorDockTabStripView()
    {
        InitializeComponent();
        DockTabStripScrollViewer.GetObservable(ScrollViewer.OffsetProperty)
            .Subscribe(_ => UpdateOverflowAffordances());
        DockTabStripScrollViewer.GetObservable(ScrollViewer.ExtentProperty)
            .Subscribe(_ => UpdateOverflowAffordances());
        DockTabStripScrollViewer.GetObservable(ScrollViewer.ViewportProperty)
            .Subscribe(_ => UpdateOverflowAffordances());
        DataContextChanged += OnDataContextChanged;
    }

    internal double HorizontalOffset => DockTabStripScrollViewer.Offset.X;

    internal double ExtentWidth => DockTabStripScrollViewer.Extent.Width;

    internal double ViewportWidth => DockTabStripScrollViewer.Viewport.Width;

    internal bool TryGetContentOrigin(Visual relativeTo, out Point origin)
    {
        var viewportOrigin = DockTabStripScrollViewer.TranslatePoint(new Point(0, 0), relativeTo);
        if (viewportOrigin is null)
        {
            origin = default;
            return false;
        }

        origin = new Point(
            EditorDockTabStripScrollController.CalculateContentOriginX(
                viewportOrigin.Value.X,
                HorizontalOffset),
            viewportOrigin.Value.Y);
        return true;
    }

    internal bool AutoScrollNearHorizontalEdge(double pointerX)
    {
        var nextOffset = EditorDockTabStripScrollController.CalculateAutoScrollOffset(
            pointerX,
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth);
        return SetHorizontalOffset(nextOffset);
    }

    internal void BringTabIntoView(EditorDockTabViewModel tab)
    {
        if (!TryGetTabBoundsInContent(tab, out var bounds))
        {
            return;
        }

        var nextOffset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            bounds,
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth);
        SetHorizontalOffset(nextOffset);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        DetachWindow(window_);
        base.OnDetachedFromVisualTree(e);
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (ReferenceEquals(window_, DataContext))
        {
            return;
        }

        DetachWindow(window_);
        window_ = DataContext as EditorDockWindowViewModel;
        AttachWindow(window_);
        BringActiveTabIntoView();
        UpdateOverflowAffordances();
    }

    private void AttachWindow(EditorDockWindowViewModel? window)
    {
        if (window is null)
        {
            return;
        }

        window.PropertyChanged += OnWindowPropertyChanged;
        window.TabStripItems.CollectionChanged += OnTabStripItemsChanged;
    }

    private void DetachWindow(EditorDockWindowViewModel? window)
    {
        if (window is null)
        {
            return;
        }

        window.PropertyChanged -= OnWindowPropertyChanged;
        window.TabStripItems.CollectionChanged -= OnTabStripItemsChanged;
    }

    private void OnWindowPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(EditorDockWindowViewModel.ActiveTab))
        {
            BringActiveTabIntoView();
        }
    }

    private void OnTabStripItemsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        BringActiveTabIntoView();
        UpdateOverflowAffordances();
    }

    private void BringActiveTabIntoView()
    {
        if (window_?.ActiveTab is { } activeTab)
        {
            BringTabIntoView(activeTab);
        }
    }

    private bool TryGetTabBoundsInContent(EditorDockTabViewModel tab, out Rect bounds)
    {
        foreach (var host in DockTabStripItems.GetVisualDescendants().OfType<EditorDockTabItemView>())
        {
            if (!host.IsVisible
                || host.DataContext is not EditorDockTabStripItemViewModel item
                || !ReferenceEquals(item.Tab, tab)
                || host.TranslatePoint(new Point(0, 0), DockTabStripItems) is not { } origin
                || host.Bounds.Width <= 0
                || host.Bounds.Height <= 0)
            {
                continue;
            }

            bounds = new Rect(origin, host.Bounds.Size);
            return true;
        }

        bounds = default;
        return false;
    }

    private bool SetHorizontalOffset(double requestedOffset)
    {
        var nextOffset = EditorDockTabStripScrollController.ClampOffset(
            requestedOffset,
            ExtentWidth,
            ViewportWidth);
        if (Math.Abs(nextOffset - HorizontalOffset) < 0.5)
        {
            return false;
        }

        DockTabStripScrollViewer.Offset = new Vector(nextOffset, DockTabStripScrollViewer.Offset.Y);
        UpdateOverflowAffordances();
        return true;
    }

    private void UpdateOverflowAffordances()
    {
        var maxOffset = EditorDockTabStripScrollController.ClampOffset(
            double.MaxValue,
            ExtentWidth,
            ViewportWidth);
        LeftOverflowAffordance.IsVisible = HorizontalOffset > 0.5;
        RightOverflowAffordance.IsVisible = maxOffset - HorizontalOffset > 0.5;
    }
}
```

- [ ] **Step 5: Run focused build and helper tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockTabStripScrollControllerTests"
dotnet build Editor.sln -c Release
```

Expected: PASS.

- [ ] **Step 6: Commit Task 2**

```powershell
git add Shell\Views\EditorDockTabStripView.axaml Shell\Views\EditorDockTabStripView.axaml.cs
git commit -m "feat: add scrollable dock tab strip"
```

## Task 3: Scrolled Hit Testing, Auto-Scroll, And Docs

**Files:**
- Modify: `Shell/Docking/EditorDockWindowBounds.cs`
- Modify: `Shell/Docking/EditorDockHitTestService.cs`
- Modify: `Shell/Views/EditorDockWindowView.axaml.cs`
- Modify: `Tests/Editor.Tests/Shell/Docking/EditorDockHitTestServiceTests.cs`
- Modify: `docs/Dock系统指南.md`

- [ ] **Step 1: Write failing hit-test coverage**

Add this test to `Tests/Editor.Tests/Shell/Docking/EditorDockHitTestServiceTests.cs`:

```csharp
[Fact]
public void HitTest_insert_tab_at_index_uses_scrolled_tab_content_origin()
{
    var window = new EditorDockWindowBounds(
        "window",
        DockArea.Center,
        new Rect(0, 0, 300, 200),
        new Rect(0, 0, 120, 24),
        4,
        [
            new EditorDockTabBounds("tab-0", 0, new Rect(-200, 0, 100, 24), false),
            new EditorDockTabBounds("tab-1", 1, new Rect(-100, 0, 100, 24), false),
            new EditorDockTabBounds("tab-2", 2, new Rect(0, 0, 100, 24), false),
            new EditorDockTabBounds("tab-3", 3, new Rect(100, 0, 100, 24), false),
        ],
        DragSourceTabIndex: null,
        AllowsWindowInsertion: true,
        IsDragSource: false,
        TabContentOriginX: -200);

    var target = EditorDockHitTestService.HitTest(
        new Point(50, 12),
        new Rect(0, 0, 300, 200),
        [window],
        [],
        tabInsertProbeX: 50,
        tabInsertCurrentTargetIndex: 0,
        tabInsertDraggedTabWidth: 100);

    Assert.Equal(EditorDockDropOperation.InsertTabAtIndex, target.Operation);
    Assert.Equal(2, target.TargetIndex);
}
```

- [ ] **Step 2: Run hit-test tests and verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockHitTestServiceTests"
```

Expected: FAIL because `EditorDockWindowBounds` does not expose `TabContentOriginX`, or because hit testing still starts tab entries at the visible tab well origin.

- [ ] **Step 3: Add tab content origin metadata**

Update `Shell/Docking/EditorDockWindowBounds.cs` so the positional record has an optional final parameter:

```csharp
using System.Collections.Generic;
using Avalonia;
using Editor.Core.Models;

namespace Editor.Shell.Docking;

public readonly record struct EditorDockWindowBounds(
    string WindowId,
    DockArea Area,
    Rect Bounds,
    Rect TabWellBounds,
    int TabCount,
    IReadOnlyList<EditorDockTabBounds> TabBounds,
    int? DragSourceTabIndex,
    bool AllowsWindowInsertion,
    bool IsDragSource,
    double TabContentOriginX = double.NaN);
```

- [ ] **Step 4: Use logical tab content origin in hit testing**

In `Shell/Docking/EditorDockHitTestService.cs`, update the start of `CreateTabInsertEntries`:

```csharp
var currentX = double.IsNaN(window.TabContentOriginX)
    || double.IsInfinity(window.TabContentOriginX)
        ? window.TabWellBounds.X
        : window.TabContentOriginX;
```

Keep the existing loop body intact.

- [ ] **Step 5: Pass content origin from the view and refresh reorder entries after auto-scroll**

In `Shell/Views/EditorDockWindowView.axaml.cs`, update `GetWindowBounds()` so `EditorDockWindowBounds` receives the tab content origin:

```csharp
var tabWellBounds = GetTabWellBounds(host);
var tabContentOriginX = GetTabContentOriginX(host);
...
windows.Add(new EditorDockWindowBounds(
    window.Id,
    window.Area,
    bounds,
    tabWellBounds,
    window.Tabs.Count,
    GetTabBounds(host, window),
    GetDragSourceTabIndex(window),
    AllowsWindowInsertion: true,
    IsDragSource: window.IsDragSourceWindow,
    tabContentOriginX));
```

Add this helper near `GetTabWellBounds`:

```csharp
private double GetTabContentOriginX(EditorDockWindowView host)
{
    var tabStrip = FindTabStripHost(host);
    return tabStrip is not null && tabStrip.TryGetContentOrigin(DockRoot, out var origin)
        ? origin.X
        : GetTabWellBounds(host).X;
}
```

Add this helper near `FindTabWellHost`:

```csharp
private static EditorDockTabStripView? FindTabStripHost(EditorDockWindowView host)
{
    return host.GetVisualDescendants()
        .OfType<EditorDockTabStripView>()
        .FirstOrDefault(view => view.IsVisible);
}
```

In `GetIdealTabStripEntries`, replace the origin calculation:

```csharp
if (!DockTabStrip.TryGetContentOrigin(relativeTo, out var origin))
{
    return [];
}
```

In `UpdateLocalTabReorder`, call auto-scroll before resolving the target:

```csharp
var pointer = e.GetPosition(this);
var scrolled = DockTabStrip.AutoScrollNearHorizontalEdge(pointer.X);
if (scrolled && capturedWindow_ is not null)
{
    reorderTabEntries_ = CaptureReorderTabEntries(capturedWindow_, this);
}

UpdateDraggedTabPreview(pointer);
```

The existing `draggedTabCenterX`, target resolution, and preview logic should stay after this block.

- [ ] **Step 6: Update Dock guide facts**

In `docs/Dock系统指南.md`, update the implemented list by adding a new item after current item 24:

```text
25. Tab strip overflow v0 将 docked/floating tab strip 保持为单行水平滚动容器，active tab 和本地 reorder 目标会通过 view-only scroll offset 保持可见；hit-test 使用逻辑 tab content origin 保持 scrolled tab strip 的插入目标稳定，滚动状态不写入 layout snapshot
```

Renumber subsequent implemented items.

Update the current missing list item from:

```text
1. tab strip overflow、自滚动和多行策略
```

to:

```text
1. tab strip advanced strategy：多行 tab strip、隐藏 tab 菜单、pin/preview tab 和更完整的 overflow 操作
```

Update the later follow-up slice item from:

```text
1. Tab strip overflow：支持 overflow、自滚动、多行策略和更明确的拒绝状态。
```

to:

```text
1. Tab strip advanced strategy：在单行 overflow v0 之后补充多行策略、隐藏 tab 菜单、pin/preview tab 和更明确的拒绝状态。
```

- [ ] **Step 7: Run focused Dock tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockTabStripScrollControllerTests|FullyQualifiedName~EditorDockHitTestServiceTests|FullyQualifiedName~EditorDockTabReorderResolverTests|FullyQualifiedName~EditorDockWindowViewModelTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests"
```

Expected: PASS.

- [ ] **Step 8: Commit Task 3**

```powershell
git add Shell\Docking\EditorDockWindowBounds.cs Shell\Docking\EditorDockHitTestService.cs Shell\Views\EditorDockWindowView.axaml.cs Tests\Editor.Tests\Shell\Docking\EditorDockHitTestServiceTests.cs docs\Dock系统指南.md
git commit -m "feat: preserve dock tab overflow hit testing"
```

## Final Verification

- [ ] **Step 1: Run focused Dock and shell command tests**

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockTabStripScrollControllerTests|FullyQualifiedName~EditorDockHitTestServiceTests|FullyQualifiedName~EditorDockTabReorderResolverTests|FullyQualifiedName~EditorDockWindowViewModelTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests|FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~WorkbenchCommandRouterTests|FullyQualifiedName~WorkbenchShortcutRouterTests"
```

Expected: PASS.

- [ ] **Step 2: Run Release solution tests**

```powershell
dotnet test Editor.sln -c Release
```

Expected: PASS.

- [ ] **Step 3: Run default solution tests**

```powershell
dotnet test Editor.sln
```

Expected: PASS. If Avalonia Designer locks `bin\Debug\net10.0\Editor.dll`, rerun with a temporary artifacts path under `%TEMP%` and report the lock clearly:

```powershell
$artifactPath = Join-Path $env:TEMP "studio-debug-artifacts-$([guid]::NewGuid().ToString('N'))"
dotnet test Editor.sln --artifacts-path $artifactPath
```

- [ ] **Step 4: Run encoding and whitespace gates**

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected: both pass.

## Self-Review Checklist

- Spec coverage: helper math, single-line scroll host, active reveal, local reorder auto-scroll, scrolled hit-test origin, docs, and non-goals are covered.
- Placeholder scan: no task contains placeholder or fill-in wording.
- Type consistency: `EditorDockTabStripScrollController`, `TabContentOriginX`, `TryGetContentOrigin`, and `AutoScrollNearHorizontalEdge` names are consistent across tasks.
