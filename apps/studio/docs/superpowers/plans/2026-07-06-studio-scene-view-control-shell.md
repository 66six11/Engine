# Studio Scene View Control Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Phase 2 Studio Scene View control shell so the Avalonia Scene View panel reports lifecycle, bounds, render scale, focus, visibility, and pointer input into UI-neutral viewport contracts and can preview scheduler decisions without creating GPU resources.
**Architecture:** Keep Core contracts immutable and UI-neutral, keep Avalonia event conversion inside `Features/SceneView/Views`, keep scheduler state in `SceneViewPanelViewModel`, and keep native bridge, Vulkan, shared images, renderer ownership, and swapchain present out of this slice.
**Tech Stack:** C# 13 / .NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, xUnit, existing `Editor.Core.Models.Viewports`, existing `ViewportScheduler`, existing panel lifecycle contracts.

---

## Scope

Implement:

- Core pointer input snapshot contracts under `Core/Models/Viewports`.
- Scene View view model state for attached/active/visible/focused surface, pixel extent, last pointer input, pending render reasons, and last scheduler request.
- Avalonia Scene View surface event bridge for attach/detach, size/render scale, focus, visibility, and pointer input.
- Focused tests for Core contracts, Scene View view model behavior, and XAML/code-behind wiring.
- Documentation classification updates for the new pointer input contracts and Scene View shell responsibilities.

Do not implement:

- Native viewport bridge.
- Vulkan resources, swapchains, shared images, semaphores, or composition interop.
- Renderer frame submission or real render result ingestion.
- Camera/tool mutation, picking, selection rays, or runtime input routing.
- Multi-viewport registry beyond the built-in Scene View instance.

## Files

Create:

- `apps/studio/Core/Models/Viewports/ViewportInputModifier.cs`
- `apps/studio/Core/Models/Viewports/ViewportPointerButton.cs`
- `apps/studio/Core/Models/Viewports/ViewportPointerEventKind.cs`
- `apps/studio/Core/Models/Viewports/ViewportPointerEventSnapshot.cs`
- `apps/studio/Tests/Editor.Tests/Core/Models/Viewports/ViewportInputModelTests.cs`
- `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewPanelViewModelTests.cs`
- `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewPanelViewXamlTests.cs`

Modify:

- `apps/studio/Features/SceneView/ViewModels/SceneViewPanelViewModel.cs`
- `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml`
- `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml.cs`
- `apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- `apps/studio/docs/Studio代码分类.md`

## Acceptance Criteria

- A detached or hidden Scene View produces no viewport render request.
- A visible Scene View with valid DIP bounds and render scale produces a valid `ViewportExtent`.
- DIP and pixel coordinates are both preserved in pointer input snapshots.
- Pointer activity marks the Scene View as interactive and creates a scheduler request with `ViewportRenderReason.InputActive`.
- Invalid, zero, negative, NaN, or infinite surface bounds do not produce invalid Core model instances.
- View attach/detach updates view model state and detach clears active/focus/input state.
- XAML/code-behind events are wired for attach/detach, size, focus, and pointer input.
- Core viewport models remain free of Avalonia, native handles, Vulkan handles, renderer objects, RenderGraph objects, and platform window handles.
- The user-facing surface still states that native Vulkan viewport rendering is a later integration slice.

## Task 1: Add UI-neutral pointer input contracts

- [ ] **Step 1: Add focused contract tests before implementation**

Create `apps/studio/Tests/Editor.Tests/Core/Models/Viewports/ViewportInputModelTests.cs`:

```csharp
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Models.Viewports;

public sealed class ViewportInputModelTests
{
    [Fact]
    public void Pointer_event_preserves_dip_pixel_scale_and_modifiers()
    {
        var timestamp = DateTimeOffset.Parse("2026-07-06T10:00:00Z");

        var input = new ViewportPointerEventSnapshot(
            new ViewportId("scene-view/main"),
            ViewportPointerEventKind.Pressed,
            positionXDip: 12.5,
            positionYDip: 7.25,
            positionXPixel: 25,
            positionYPixel: 14.5,
            deltaXDip: 1,
            deltaYDip: -2,
            deltaXPixel: 2,
            deltaYPixel: -4,
            renderScale: 2,
            ViewportPointerButton.Left,
            ViewportInputModifier.Shift | ViewportInputModifier.Control,
            timestamp);

        Assert.Equal("scene-view/main", input.ViewportId.Value);
        Assert.Equal(ViewportPointerEventKind.Pressed, input.Kind);
        Assert.Equal(12.5, input.PositionXDip);
        Assert.Equal(7.25, input.PositionYDip);
        Assert.Equal(25, input.PositionXPixel);
        Assert.Equal(14.5, input.PositionYPixel);
        Assert.Equal(1, input.DeltaXDip);
        Assert.Equal(-2, input.DeltaYDip);
        Assert.Equal(2, input.DeltaXPixel);
        Assert.Equal(-4, input.DeltaYPixel);
        Assert.Equal(2, input.RenderScale);
        Assert.Equal(ViewportPointerButton.Left, input.Button);
        Assert.True(input.Modifiers.HasFlag(ViewportInputModifier.Shift));
        Assert.True(input.Modifiers.HasFlag(ViewportInputModifier.Control));
        Assert.Equal(timestamp, input.TimestampUtc);
    }

    [Theory]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    [InlineData(double.NegativeInfinity)]
    public void Pointer_event_rejects_non_finite_coordinates(double coordinate)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportPointerEventSnapshot(
                new ViewportId("scene-view/main"),
                ViewportPointerEventKind.Moved,
                coordinate,
                positionYDip: 0,
                positionXPixel: 0,
                positionYPixel: 0,
                deltaXDip: 0,
                deltaYDip: 0,
                deltaXPixel: 0,
                deltaYPixel: 0,
                renderScale: 1,
                ViewportPointerButton.None,
                ViewportInputModifier.None,
                DateTimeOffset.UnixEpoch));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Pointer_event_rejects_invalid_render_scale(double renderScale)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportPointerEventSnapshot(
                new ViewportId("scene-view/main"),
                ViewportPointerEventKind.Moved,
                positionXDip: 0,
                positionYDip: 0,
                positionXPixel: 0,
                positionYPixel: 0,
                deltaXDip: 0,
                deltaYDip: 0,
                deltaXPixel: 0,
                deltaYPixel: 0,
                renderScale,
                ViewportPointerButton.None,
                ViewportInputModifier.None,
                DateTimeOffset.UnixEpoch));
    }
}
```

Run and confirm the focused tests fail because the contracts do not exist:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter ViewportInputModelTests
```

- [ ] **Step 2: Add the pointer input enums**

Create `apps/studio/Core/Models/Viewports/ViewportInputModifier.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

[Flags]
public enum ViewportInputModifier
{
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
    Meta = 1 << 3,
    All = Shift | Control | Alt | Meta,
}
```

Create `apps/studio/Core/Models/Viewports/ViewportPointerButton.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportPointerButton
{
    None,
    Left,
    Right,
    Middle,
    XButton1,
    XButton2,
}
```

Create `apps/studio/Core/Models/Viewports/ViewportPointerEventKind.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportPointerEventKind
{
    Pressed,
    Moved,
    Released,
    Wheel,
    Exited,
}
```

- [ ] **Step 3: Add the immutable pointer input snapshot**

Create `apps/studio/Core/Models/Viewports/ViewportPointerEventSnapshot.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportPointerEventSnapshot
{
    public ViewportPointerEventSnapshot(
        ViewportId viewportId,
        ViewportPointerEventKind kind,
        double positionXDip,
        double positionYDip,
        double positionXPixel,
        double positionYPixel,
        double deltaXDip,
        double deltaYDip,
        double deltaXPixel,
        double deltaYPixel,
        double renderScale,
        ViewportPointerButton button,
        ViewportInputModifier modifiers,
        DateTimeOffset timestampUtc)
    {
        if (viewportId.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(viewportId));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(
                nameof(kind),
                kind,
                "Viewport pointer event kind is not defined.");
        }

        if (!Enum.IsDefined(button))
        {
            throw new ArgumentOutOfRangeException(
                nameof(button),
                button,
                "Viewport pointer button is not defined.");
        }

        if ((modifiers & ~ViewportInputModifier.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(modifiers),
                modifiers,
                "Viewport input modifiers include undefined flags.");
        }

        ValidateFinite(positionXDip, nameof(positionXDip));
        ValidateFinite(positionYDip, nameof(positionYDip));
        ValidateFinite(positionXPixel, nameof(positionXPixel));
        ValidateFinite(positionYPixel, nameof(positionYPixel));
        ValidateFinite(deltaXDip, nameof(deltaXDip));
        ValidateFinite(deltaYDip, nameof(deltaYDip));
        ValidateFinite(deltaXPixel, nameof(deltaXPixel));
        ValidateFinite(deltaYPixel, nameof(deltaYPixel));

        if (renderScale <= 0 || !double.IsFinite(renderScale))
        {
            throw new ArgumentOutOfRangeException(
                nameof(renderScale),
                renderScale,
                "Viewport input render scale must be finite and greater than zero.");
        }

        ViewportId = viewportId;
        Kind = kind;
        PositionXDip = positionXDip;
        PositionYDip = positionYDip;
        PositionXPixel = positionXPixel;
        PositionYPixel = positionYPixel;
        DeltaXDip = deltaXDip;
        DeltaYDip = deltaYDip;
        DeltaXPixel = deltaXPixel;
        DeltaYPixel = deltaYPixel;
        RenderScale = renderScale;
        Button = button;
        Modifiers = modifiers;
        TimestampUtc = timestampUtc;
    }

    public ViewportId ViewportId { get; }

    public ViewportPointerEventKind Kind { get; }

    public double PositionXDip { get; }

    public double PositionYDip { get; }

    public double PositionXPixel { get; }

    public double PositionYPixel { get; }

    public double DeltaXDip { get; }

    public double DeltaYDip { get; }

    public double DeltaXPixel { get; }

    public double DeltaYPixel { get; }

    public double RenderScale { get; }

    public ViewportPointerButton Button { get; }

    public ViewportInputModifier Modifiers { get; }

    public DateTimeOffset TimestampUtc { get; }

    private static void ValidateFinite(double value, string parameterName)
    {
        if (!double.IsFinite(value))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                value,
                "Viewport input coordinates must be finite.");
        }
    }
}
```

- [ ] **Step 4: Update architecture placement tests**

Modify `apps/studio/Tests/Editor.Tests/Architecture/StudioLayeringTests.cs` in `Viewport_core_models_live_in_viewport_model_folder()` so the expected file list includes:

```csharp
"ViewportInputModifier.cs",
"ViewportPointerButton.cs",
"ViewportPointerEventKind.cs",
"ViewportPointerEventSnapshot.cs",
```

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportInputModelTests|Viewport_core_models_live_in_viewport_model_folder"
```

Commit after this task:

```powershell
git add apps\studio\Core\Models\Viewports apps\studio\Tests\Editor.Tests\Core\Models\Viewports apps\studio\Tests\Editor.Tests\Architecture\StudioLayeringTests.cs
git commit -m "feat: add studio viewport input contracts"
```

## Task 2: Add Scene View scheduler shell state

- [ ] **Step 1: Add Scene View view model behavior tests before implementation**

Create `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewPanelViewModelTests.cs`:

```csharp
using Editor.Core.Models.Panels;
using Editor.Core.Models.Viewports;
using Editor.Features.SceneView.ViewModels;
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPanelViewModelTests
{
    [Fact]
    public void Initial_snapshot_is_hidden_until_surface_is_attached_and_sized()
    {
        var viewModel = CreateViewModel();

        var snapshot = viewModel.CurrentViewportSnapshot;

        Assert.Equal("scene-view/main", snapshot.Id.Value);
        Assert.False(snapshot.IsVisible);
        Assert.False(snapshot.IsFocused);
        Assert.Equal(ViewportUpdatePolicy.InteractiveBurst, snapshot.UpdatePolicy);
        Assert.Equal("detached", viewModel.ViewportStatusText);
    }

    [Fact]
    public void Bounds_update_converts_dip_to_pixel_extent()
    {
        var viewModel = CreateViewModel();
        viewModel.OnPanelAttached(CreateLifecycleContext());

        viewModel.UpdateViewportSurface(
            isSurfaceAttached: true,
            isSurfaceVisible: true,
            widthDip: 320,
            heightDip: 180,
            renderScale: 1.5);

        var snapshot = viewModel.CurrentViewportSnapshot;
        Assert.True(snapshot.IsVisible);
        Assert.Equal(480, snapshot.Extent.WidthPixels);
        Assert.Equal(270, snapshot.Extent.HeightPixels);
        Assert.Equal(1.5, snapshot.Extent.RenderScale);
        Assert.True(snapshot.PendingReasons.HasFlag(ViewportRenderReason.Resized));
        Assert.Contains("480 x 270", viewModel.ViewportStateMessage);
    }

    [Theory]
    [InlineData(0, 100, 1)]
    [InlineData(100, 0, 1)]
    [InlineData(double.NaN, 100, 1)]
    [InlineData(100, double.PositiveInfinity, 1)]
    [InlineData(100, 100, 0)]
    public void Invalid_surface_metrics_keep_viewport_hidden(
        double widthDip,
        double heightDip,
        double renderScale)
    {
        var viewModel = CreateViewModel();
        viewModel.OnPanelAttached(CreateLifecycleContext());

        viewModel.UpdateViewportSurface(
            isSurfaceAttached: true,
            isSurfaceVisible: true,
            widthDip,
            heightDip,
            renderScale);

        Assert.False(viewModel.CurrentViewportSnapshot.IsVisible);
        Assert.Equal("waiting for valid surface", viewModel.ViewportStatusText);
        Assert.Empty(viewModel.BuildViewportRenderPlan(DateTimeOffset.Parse("2026-07-06T10:00:00Z")));
    }

    [Fact]
    public void Pointer_activity_preserves_last_input_and_requests_interactive_render()
    {
        var now = DateTimeOffset.Parse("2026-07-06T10:00:00Z");
        var viewModel = CreateViewModel();
        viewModel.OnPanelAttached(CreateLifecycleContext());
        viewModel.OnPanelActivated(CreateLifecycleContext());
        viewModel.UpdateViewportSurface(
            isSurfaceAttached: true,
            isSurfaceVisible: true,
            widthDip: 200,
            heightDip: 100,
            renderScale: 2);

        viewModel.RecordPointerInput(new ViewportPointerEventSnapshot(
            viewModel.ViewportId,
            ViewportPointerEventKind.Pressed,
            positionXDip: 25,
            positionYDip: 10,
            positionXPixel: 50,
            positionYPixel: 20,
            deltaXDip: 0,
            deltaYDip: 0,
            deltaXPixel: 0,
            deltaYPixel: 0,
            renderScale: 2,
            ViewportPointerButton.Left,
            ViewportInputModifier.Shift,
            now));

        var requests = viewModel.BuildViewportRenderPlan(now);

        var request = Assert.Single(requests);
        Assert.Equal(viewModel.ViewportId, request.Id);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.InputActive));
        Assert.Equal(ViewportPointerEventKind.Pressed, viewModel.LastPointerInput?.Kind);
        Assert.Equal(50, viewModel.LastPointerInput?.PositionXPixel);
    }

    [Fact]
    public void Detached_panel_clears_focus_input_and_scheduler_requests()
    {
        var now = DateTimeOffset.Parse("2026-07-06T10:00:00Z");
        var viewModel = CreateViewModel();
        viewModel.OnPanelAttached(CreateLifecycleContext());
        viewModel.OnPanelActivated(CreateLifecycleContext());
        viewModel.UpdateViewportSurface(
            isSurfaceAttached: true,
            isSurfaceVisible: true,
            widthDip: 100,
            heightDip: 100,
            renderScale: 1);
        viewModel.SetViewportFocus(true);
        viewModel.RecordPointerInput(new ViewportPointerEventSnapshot(
            viewModel.ViewportId,
            ViewportPointerEventKind.Moved,
            positionXDip: 5,
            positionYDip: 5,
            positionXPixel: 5,
            positionYPixel: 5,
            deltaXDip: 1,
            deltaYDip: 1,
            deltaXPixel: 1,
            deltaYPixel: 1,
            renderScale: 1,
            ViewportPointerButton.None,
            ViewportInputModifier.None,
            now));

        viewModel.OnPanelDetached(CreateLifecycleContext());

        Assert.False(viewModel.CurrentViewportSnapshot.IsVisible);
        Assert.False(viewModel.CurrentViewportSnapshot.IsFocused);
        Assert.Null(viewModel.LastPointerInput);
        Assert.Empty(viewModel.BuildViewportRenderPlan(now.AddMilliseconds(16)));
    }

    private static SceneViewPanelViewModel CreateViewModel()
    {
        return new SceneViewPanelViewModel(new EditorSelectionService());
    }

    private static EditorPanelLifecycleContext CreateLifecycleContext()
    {
        return new EditorPanelLifecycleContext(
            "scene-view",
            "Scene View",
            DockArea.Center,
            IsFloatingWorkspace: false);
    }
}
```

Run and confirm the focused tests fail because the new behavior does not exist:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter SceneViewPanelViewModelTests
```

- [ ] **Step 2: Implement Scene View state and scheduler integration**

Replace `apps/studio/Features/SceneView/ViewModels/SceneViewPanelViewModel.cs` with this structure:

```csharp
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Selection;
using Editor.Core.Models.Viewports;
using Editor.Core.Services;
using Editor.UI.ViewModels;

namespace Editor.Features.SceneView.ViewModels;

public sealed class SceneViewPanelViewModel :
    ViewModelBase,
    IEditorPanelLifecycleSink
{
    private const string SelectionContextId = "scene-view";
    private static readonly TimeSpan PointerBurstDuration = TimeSpan.FromMilliseconds(250);

    private readonly IEditorSelectionService selectionService_;
    private readonly ViewportScheduler scheduler_;
    private ViewportExtent? extent_;
    private bool isPanelAttached_;
    private bool isPanelActive_;
    private bool isSurfaceAttached_;
    private bool isSurfaceVisible_;
    private bool isSurfaceFocused_;
    private bool hasValidSurfaceMetrics_;
    private DateTimeOffset? lastRenderDecisionAtUtc_;
    private TimeSpan interactiveBurstRemaining_;
    private ViewportRenderReason pendingReasons_ = ViewportRenderReason.InitialFrameMissing;
    private ViewportPointerEventSnapshot? lastPointerInput_;
    private ViewportRenderRequest? lastRenderRequest_;
    private string viewportStatusText_ = "detached";

    public SceneViewPanelViewModel(IEditorSelectionService selectionService)
        : this(selectionService, new ViewportScheduler())
    {
    }

    internal SceneViewPanelViewModel(
        IEditorSelectionService selectionService,
        ViewportScheduler scheduler)
    {
        selectionService_ = selectionService;
        scheduler_ = scheduler;
    }

    public ViewportId ViewportId { get; } = new("scene-view/main");

    public string ViewportStateTitle => "Viewport backend deferred";

    public string ViewportStateMessage =>
        CurrentViewportSnapshot.IsVisible
            ? $"Scene View shell is reporting {CurrentViewportSnapshot.Extent.WidthPixels} x {CurrentViewportSnapshot.Extent.HeightPixels} at {CurrentViewportSnapshot.Extent.RenderScale:0.##}x; native Vulkan viewport rendering is a separate integration slice."
            : "Scene snapshot and selection shell are available; native Vulkan viewport is a separate integration slice.";

    public string ViewportStatusText
    {
        get => viewportStatusText_;
        private set => SetProperty(ref viewportStatusText_, value);
    }

    public ViewportPointerEventSnapshot? LastPointerInput
    {
        get => lastPointerInput_;
        private set => SetProperty(ref lastPointerInput_, value);
    }

    public ViewportRenderRequest? LastRenderRequest
    {
        get => lastRenderRequest_;
        private set => SetProperty(ref lastRenderRequest_, value);
    }

    public ViewportStateSnapshot CurrentViewportSnapshot =>
        new(
            ViewportId,
            "Scene View",
            ViewportKind.Scene,
            extent_ ?? new ViewportExtent(1, 1, renderScale: 1),
            ViewportUpdatePolicy.InteractiveBurst,
            new ViewportClockSnapshot(
                ViewportClockMode.EditorPreviewTime,
                timeSeconds: 0,
                deltaSeconds: 0,
                frameIndex: 0,
                playbackSpeed: 1),
            IsSchedulerVisible,
            isPanelActive_ || isSurfaceFocused_,
            pendingReasons_ != ViewportRenderReason.None,
            lastRenderDecisionAtUtc_,
            interactiveBurstRemaining_,
            pendingReasons_);

    public void OnPanelAttached(EditorPanelLifecycleContext context)
    {
        isPanelAttached_ = true;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public void OnPanelActivated(EditorPanelLifecycleContext context)
    {
        isPanelActive_ = true;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public void OnPanelDeactivated(EditorPanelLifecycleContext context)
    {
        isPanelActive_ = false;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public void OnPanelDetached(EditorPanelLifecycleContext context)
    {
        isPanelAttached_ = false;
        isPanelActive_ = false;
        isSurfaceAttached_ = false;
        isSurfaceVisible_ = false;
        isSurfaceFocused_ = false;
        hasValidSurfaceMetrics_ = false;
        interactiveBurstRemaining_ = TimeSpan.Zero;
        pendingReasons_ = ViewportRenderReason.None;
        LastPointerInput = null;
        LastRenderRequest = null;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public void UpdateViewportSurface(
        bool isSurfaceAttached,
        bool isSurfaceVisible,
        double widthDip,
        double heightDip,
        double renderScale)
    {
        isSurfaceAttached_ = isSurfaceAttached;
        isSurfaceVisible_ = isSurfaceVisible;

        if (!TryCreateExtent(widthDip, heightDip, renderScale, out var extent))
        {
            hasValidSurfaceMetrics_ = false;
            pendingReasons_ = ViewportRenderReason.None;
            RefreshStatus();
            RaiseViewportPropertiesChanged();
            return;
        }

        hasValidSurfaceMetrics_ = true;
        if (extent_ != extent)
        {
            pendingReasons_ |= ViewportRenderReason.Resized;
        }

        extent_ = extent;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public void SetViewportFocus(bool isFocused)
    {
        isSurfaceFocused_ = isFocused;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public void RecordPointerInput(ViewportPointerEventSnapshot input)
    {
        ArgumentNullException.ThrowIfNull(input);

        if (input.ViewportId != ViewportId)
        {
            throw new ArgumentException(
                "Pointer input viewport id must match the Scene View viewport.",
                nameof(input));
        }

        LastPointerInput = input;
        interactiveBurstRemaining_ = PointerBurstDuration;
        pendingReasons_ |= ViewportRenderReason.InputActive;
        RefreshStatus();
        RaiseViewportPropertiesChanged();
    }

    public IReadOnlyList<ViewportRenderRequest> BuildViewportRenderPlan(DateTimeOffset nowUtc)
    {
        var requests = scheduler_.BuildRenderPlan(
            [CurrentViewportSnapshot],
            new ViewportSchedulerContext(nowUtc, maxViewportRendersThisTick: 1));

        LastRenderRequest = requests.Count == 0 ? null : requests[0];
        if (LastRenderRequest is not null)
        {
            lastRenderDecisionAtUtc_ = nowUtc;
            pendingReasons_ = ViewportRenderReason.None;
            interactiveBurstRemaining_ = TimeSpan.Zero;
        }

        RefreshStatus();
        RaiseViewportPropertiesChanged();
        return requests;
    }

    public void SelectItem(EditorSelectionItem item)
    {
        selectionService_.ReplaceSelection(SelectionContextId, [item]);
    }

    public void ClearSelection()
    {
        selectionService_.ClearSelection(SelectionContextId);
    }

    private bool IsSchedulerVisible =>
        isPanelAttached_
        && isSurfaceAttached_
        && isSurfaceVisible_
        && hasValidSurfaceMetrics_;

    private void RefreshStatus()
    {
        ViewportStatusText =
            !isPanelAttached_ ? "detached"
            : !isSurfaceAttached_ ? "surface detached"
            : !hasValidSurfaceMetrics_ ? "waiting for valid surface"
            : !isSurfaceVisible_ ? "hidden"
            : LastRenderRequest is not null ? "scheduled"
            : "deferred";
    }

    private void RaiseViewportPropertiesChanged()
    {
        OnPropertyChanged(nameof(CurrentViewportSnapshot));
        OnPropertyChanged(nameof(ViewportStateMessage));
    }

    private static bool TryCreateExtent(
        double widthDip,
        double heightDip,
        double renderScale,
        out ViewportExtent? extent)
    {
        extent = null;
        if (widthDip <= 0
            || heightDip <= 0
            || renderScale <= 0
            || !double.IsFinite(widthDip)
            || !double.IsFinite(heightDip)
            || !double.IsFinite(renderScale))
        {
            return false;
        }

        var widthPixels = Math.Max(1, (int)Math.Round(widthDip * renderScale, MidpointRounding.AwayFromZero));
        var heightPixels = Math.Max(1, (int)Math.Round(heightDip * renderScale, MidpointRounding.AwayFromZero));
        extent = new ViewportExtent(widthPixels, heightPixels, renderScale);
        return true;
    }
}
```

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter SceneViewPanelViewModelTests
```

Commit after this task:

```powershell
git add apps\studio\Features\SceneView\ViewModels\SceneViewPanelViewModel.cs apps\studio\Tests\Editor.Tests\Features\SceneView\SceneViewPanelViewModelTests.cs
git commit -m "feat: add scene view viewport shell state"
```

## Task 3: Wire Avalonia Scene View control events

- [ ] **Step 1: Add XAML/code-behind wiring tests before implementation**

Create `apps/studio/Tests/Editor.Tests/Features/SceneView/SceneViewPanelViewXamlTests.cs`:

```csharp
using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPanelViewXamlTests
{
    [Fact]
    public void Scene_view_surface_reports_lifecycle_size_focus_and_pointer_events()
    {
        var xaml = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml");
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml.cs");

        Assert.Contains("x:Name=\"ViewportSurface\"", xaml);
        Assert.Contains("Focusable=\"True\"", xaml);
        Assert.Contains("AttachedToVisualTree=\"OnViewportSurfaceAttachedToVisualTree\"", xaml);
        Assert.Contains("DetachedFromVisualTree=\"OnViewportSurfaceDetachedFromVisualTree\"", xaml);
        Assert.Contains("SizeChanged=\"OnViewportSurfaceSizeChanged\"", xaml);
        Assert.Contains("GotFocus=\"OnViewportSurfaceGotFocus\"", xaml);
        Assert.Contains("LostFocus=\"OnViewportSurfaceLostFocus\"", xaml);
        Assert.Contains("PointerPressed=\"OnViewportSurfacePointerPressed\"", xaml);
        Assert.Contains("PointerMoved=\"OnViewportSurfacePointerMoved\"", xaml);
        Assert.Contains("PointerReleased=\"OnViewportSurfacePointerReleased\"", xaml);
        Assert.Contains("PointerWheelChanged=\"OnViewportSurfacePointerWheelChanged\"", xaml);
        Assert.Contains("PointerExited=\"OnViewportSurfacePointerExited\"", xaml);

        Assert.Contains("ReportViewportSurface(isAttached: true)", source);
        Assert.Contains("UpdateViewportSurface(", source);
        Assert.Contains("RecordPointerInput(new ViewportPointerEventSnapshot(", source);
        Assert.Contains("TopLevel.GetTopLevel(ViewportSurface)?.RenderScaling ?? 1d", source);
        Assert.Contains("position.X * renderScale", source);
        Assert.Contains("MapModifiers(e.KeyModifiers)", source);
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

Run and confirm the focused tests fail because the view is not wired yet:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter SceneViewPanelViewXamlTests
```

- [ ] **Step 2: Update the Scene View XAML surface**

Modify `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml` so the root grid contains a focusable viewport surface below the status bar:

```xml
<Grid Background="{DynamicResource EditorBrushBase00}"
      RowDefinitions="Auto,*">
    <Border Grid.Row="0"
            Classes="scene-view-status"
            VerticalAlignment="Top">
        <Grid ColumnDefinitions="*,Auto"
              ColumnSpacing="12">
            <StackPanel Spacing="3">
                <TextBlock Classes="scene-view-title"
                           Text="{Binding ViewportStateTitle}" />
                <TextBlock Classes="scene-view-message"
                           Text="{Binding ViewportStateMessage}" />
            </StackPanel>
            <TextBlock Grid.Column="1"
                       Classes="scene-view-status-text"
                       Text="{Binding ViewportStatusText}" />
        </Grid>
    </Border>

    <Border x:Name="ViewportSurface"
            Grid.Row="1"
            Background="{DynamicResource EditorBrushBase01}"
            Focusable="True"
            AttachedToVisualTree="OnViewportSurfaceAttachedToVisualTree"
            DetachedFromVisualTree="OnViewportSurfaceDetachedFromVisualTree"
            SizeChanged="OnViewportSurfaceSizeChanged"
            GotFocus="OnViewportSurfaceGotFocus"
            LostFocus="OnViewportSurfaceLostFocus"
            PointerPressed="OnViewportSurfacePointerPressed"
            PointerMoved="OnViewportSurfacePointerMoved"
            PointerReleased="OnViewportSurfacePointerReleased"
            PointerWheelChanged="OnViewportSurfacePointerWheelChanged"
            PointerExited="OnViewportSurfacePointerExited">
        <Grid>
            <TextBlock Classes="scene-view-message"
                       HorizontalAlignment="Center"
                       VerticalAlignment="Center"
                       Text="Native viewport deferred" />
        </Grid>
    </Border>
</Grid>
```

Keep the existing styles and add a modest style for the surface if needed:

```xml
<Style Selector="Border.scene-view-surface">
    <Setter Property="Background" Value="{DynamicResource EditorBrushBase01}" />
</Style>
```

- [ ] **Step 3: Add Avalonia event conversion in code-behind**

Replace `apps/studio/Features/SceneView/Views/SceneViewPanelView.axaml.cs` with:

```csharp
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Editor.Core.Models.Viewports;
using Editor.Features.SceneView.ViewModels;

namespace Editor.Features.SceneView.Views;

public partial class SceneViewPanelView : UserControl
{
    public SceneViewPanelView()
    {
        InitializeComponent();
    }

    private SceneViewPanelViewModel? ViewModel =>
        DataContext as SceneViewPanelViewModel;

    private void OnViewportSurfaceAttachedToVisualTree(object? sender, VisualTreeAttachmentEventArgs e)
    {
        ReportViewportSurface(isAttached: true);
    }

    private void OnViewportSurfaceDetachedFromVisualTree(object? sender, VisualTreeAttachmentEventArgs e)
    {
        ViewModel?.UpdateViewportSurface(
            isSurfaceAttached: false,
            isSurfaceVisible: false,
            widthDip: 0,
            heightDip: 0,
            renderScale: 1);
        ViewModel?.SetViewportFocus(false);
    }

    private void OnViewportSurfaceSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        ReportViewportSurface(isAttached: true);
    }

    private void OnViewportSurfaceGotFocus(object? sender, GotFocusEventArgs e)
    {
        ViewModel?.SetViewportFocus(true);
    }

    private void OnViewportSurfaceLostFocus(object? sender, RoutedEventArgs e)
    {
        ViewModel?.SetViewportFocus(false);
    }

    private void OnViewportSurfacePointerPressed(object? sender, PointerPressedEventArgs e)
    {
        ViewportSurface.Focus(NavigationMethod.Pointer);
        ReportPointerEvent(ViewportPointerEventKind.Pressed, e, MapButton(e));
        e.Handled = true;
    }

    private void OnViewportSurfacePointerMoved(object? sender, PointerEventArgs e)
    {
        ReportPointerEvent(ViewportPointerEventKind.Moved, e, ViewportPointerButton.None);
    }

    private void OnViewportSurfacePointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        ReportPointerEvent(ViewportPointerEventKind.Released, e, MapButton(e));
        e.Handled = true;
    }

    private void OnViewportSurfacePointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        ReportPointerEvent(ViewportPointerEventKind.Wheel, e, ViewportPointerButton.None);
        e.Handled = true;
    }

    private void OnViewportSurfacePointerExited(object? sender, PointerEventArgs e)
    {
        ReportPointerEvent(ViewportPointerEventKind.Exited, e, ViewportPointerButton.None);
    }

    private void ReportViewportSurface(bool isAttached)
    {
        var renderScale = TopLevel.GetTopLevel(ViewportSurface)?.RenderScaling ?? 1d;
        ViewModel?.UpdateViewportSurface(
            isAttached,
            ViewportSurface.IsVisible,
            ViewportSurface.Bounds.Width,
            ViewportSurface.Bounds.Height,
            renderScale);
    }

    private void ReportPointerEvent(
        ViewportPointerEventKind kind,
        PointerEventArgs e,
        ViewportPointerButton button)
    {
        var viewModel = ViewModel;
        if (viewModel is null)
        {
            return;
        }

        ReportViewportSurface(isAttached: true);

        var renderScale = TopLevel.GetTopLevel(ViewportSurface)?.RenderScaling ?? 1d;
        var position = e.GetPosition(ViewportSurface);
        var delta = e is PointerWheelEventArgs wheelEvent
            ? wheelEvent.Delta
            : new Vector(0, 0);

        viewModel.RecordPointerInput(new ViewportPointerEventSnapshot(
            viewModel.ViewportId,
            kind,
            position.X,
            position.Y,
            position.X * renderScale,
            position.Y * renderScale,
            delta.X,
            delta.Y,
            delta.X * renderScale,
            delta.Y * renderScale,
            renderScale,
            button,
            MapModifiers(e.KeyModifiers),
            DateTimeOffset.UtcNow));
    }

    private static ViewportPointerButton MapButton(PointerEventArgs e)
    {
        var point = e.GetCurrentPoint(null);
        return point.Properties.PointerUpdateKind switch
        {
            PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased =>
                ViewportPointerButton.Left,
            PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased =>
                ViewportPointerButton.Right,
            PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased =>
                ViewportPointerButton.Middle,
            PointerUpdateKind.XButton1Pressed or PointerUpdateKind.XButton1Released =>
                ViewportPointerButton.XButton1,
            PointerUpdateKind.XButton2Pressed or PointerUpdateKind.XButton2Released =>
                ViewportPointerButton.XButton2,
            _ => ViewportPointerButton.None,
        };
    }

    private static ViewportInputModifier MapModifiers(KeyModifiers modifiers)
    {
        var result = ViewportInputModifier.None;
        if (modifiers.HasFlag(KeyModifiers.Shift))
        {
            result |= ViewportInputModifier.Shift;
        }

        if (modifiers.HasFlag(KeyModifiers.Control))
        {
            result |= ViewportInputModifier.Control;
        }

        if (modifiers.HasFlag(KeyModifiers.Alt))
        {
            result |= ViewportInputModifier.Alt;
        }

        if (modifiers.HasFlag(KeyModifiers.Meta))
        {
            result |= ViewportInputModifier.Meta;
        }

        return result;
    }
}
```

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter SceneViewPanelViewXamlTests
```

Commit after this task:

```powershell
git add apps\studio\Features\SceneView\Views apps\studio\Tests\Editor.Tests\Features\SceneView\SceneViewPanelViewXamlTests.cs
git commit -m "feat: wire scene view viewport surface events"
```

## Task 4: Update documentation and final verification

- [ ] **Step 1: Update code classification**

Add these bullets to the viewport section in `apps/studio/docs/Studio代码分类.md`:

```markdown
- `Core/Models/Viewports/ViewportPointerEventSnapshot.cs` owns UI-neutral pointer input packets for viewport routing. It stores DIP coordinates, pixel coordinates, deltas, render scale, button, modifiers, timestamp, and viewport id without referencing Avalonia input types.
- `Features/SceneView/ViewModels/SceneViewPanelViewModel.cs` owns the Scene View shell projection from panel lifecycle, surface metrics, focus, and pointer input into `ViewportStateSnapshot` and `ViewportRenderRequest` previews. It does not own native viewport, renderer, Vulkan, swapchain, or shared-image resources.
- `Features/SceneView/Views/SceneViewPanelView.axaml.cs` owns Avalonia event conversion for the Scene View surface. It may reference Avalonia controls and input events, but it must emit Core viewport snapshots instead of mutating engine state directly.
```

- [ ] **Step 2: Run focused managed verification**

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportInput|Viewport_core_models_live_in_viewport_model_folder"
```

- [ ] **Step 3: Run full managed verification**

```powershell
dotnet test apps\studio\Editor.sln -c Release
```

- [ ] **Step 4: Run repository hygiene checks**

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

- [ ] **Step 5: Run full pre-commit builds before PR**

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

- [ ] **Step 6: Commit docs and final integration**

```powershell
git add apps\studio\docs\Studio代码分类.md
git commit -m "docs: classify scene view viewport shell"
```

If implementation changes are already committed task-by-task, run a final status check:

```powershell
git status -sb
git log --oneline --decorate -5
```

## Final Review Checklist

- [ ] No `Editor.Core.Models.Viewports` file references `Avalonia`, `Vulkan`, `RenderGraph`, `renderer`, `native`, `IntPtr`, `nint`, or platform window handles.
- [ ] `SceneViewPanelView.axaml.cs` is the only new file that converts Avalonia pointer events.
- [ ] `SceneViewPanelViewModel` has no Avalonia namespace imports.
- [ ] Hidden/detached/invalid surfaces produce no scheduler request.
- [ ] Pointer input snapshots preserve both DIP and pixel coordinates.
- [ ] The UI still labels native viewport rendering as deferred.
- [ ] All focused tests, full `dotnet test`, encoding check, `git diff --check`, and pre-commit builds pass.
