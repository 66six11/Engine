# Studio Viewport Contract Scheduler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Phase 1 Studio viewport contract and pure managed scheduler so `apps/studio` can describe viewport identity, timing, render reasons, and render decisions without Avalonia composition, native bridge, or Vulkan ownership.

**Architecture:** Add UI-neutral immutable contracts under `Core/Models/Viewports` and a CPU-only scheduler under `Core/Services`. The scheduler consumes viewport state snapshots, returns render requests, and keeps all GPU/native/presentation details outside Core. Tests mirror the Core folders and lock the layer boundary before later Scene View and native slices connect to it.

**Tech Stack:** .NET 10, C# records/classes, xUnit, existing `Editor.Tests` project, PowerShell validation commands.

---

## File Structure

Create:

- `Core/Models/Viewports/ViewportId.cs` - stable viewport identity for one Studio session.
- `Core/Models/Viewports/ViewportKind.cs` - shared viewport kind vocabulary.
- `Core/Models/Viewports/ViewportExtent.cs` - pixel extent plus render scale.
- `Core/Models/Viewports/ViewportRenderReason.cs` - flagged render reason vocabulary.
- `Core/Models/Viewports/ViewportUpdatePolicy.cs` - scheduler policy vocabulary.
- `Core/Models/Viewports/ViewportClockMode.cs` - viewport time source vocabulary.
- `Core/Models/Viewports/ViewportClockSnapshot.cs` - immutable viewport clock packet.
- `Core/Models/Viewports/ViewportStateSnapshot.cs` - immutable scheduler input for one viewport.
- `Core/Models/Viewports/ViewportSchedulerContext.cs` - immutable scheduler tick context.
- `Core/Models/Viewports/ViewportRenderRequest.cs` - immutable scheduler output for one viewport render.
- `Core/Models/Viewports/ViewportRenderResult.cs` - immutable render completion or skipped-work snapshot.
- `Core/Models/Viewports/ViewportSchedulerOptions.cs` - scheduler target-rate options.
- `Core/Services/ViewportScheduler.cs` - pure managed render-request planner.
- `Tests/Editor.Tests/Core/Models/Viewports/ViewportModelTests.cs` - model validation and clock behavior.
- `Tests/Editor.Tests/Core/Models/Viewports/ViewportSchedulerContractTests.cs` - scheduler contract model validation.
- `Tests/Editor.Tests/Core/Services/ViewportSchedulerTests.cs` - render decision behavior.

Modify:

- `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs` - assert viewport contracts stay in `Core/Models/Viewports`.
- `docs/Studio代码分类.md` - classify the viewport contracts and scheduler after implementation.

Do not modify:

- `Features/SceneView/*` in this slice.
- `Shell/*` viewport/Avalonia control code in this slice.
- `Core/Interop/*` native bridge code in this slice.
- Any C++ package, Vulkan, RenderGraph, or native editor code in this slice.

---

### Task 1: Viewport Identity, Extent, Clock, And State Models

**Files:**
- Create: `Tests/Editor.Tests/Core/Models/Viewports/ViewportModelTests.cs`
- Create: `Core/Models/Viewports/ViewportId.cs`
- Create: `Core/Models/Viewports/ViewportKind.cs`
- Create: `Core/Models/Viewports/ViewportExtent.cs`
- Create: `Core/Models/Viewports/ViewportRenderReason.cs`
- Create: `Core/Models/Viewports/ViewportUpdatePolicy.cs`
- Create: `Core/Models/Viewports/ViewportClockMode.cs`
- Create: `Core/Models/Viewports/ViewportClockSnapshot.cs`
- Create: `Core/Models/Viewports/ViewportStateSnapshot.cs`

- [ ] **Step 1: Write the failing model tests**

Create `Tests/Editor.Tests/Core/Models/Viewports/ViewportModelTests.cs`:

```csharp
using System;
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Models.Viewports;

public sealed class ViewportModelTests
{
    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData("\t")]
    public void Viewport_id_rejects_blank_values(string value)
    {
        Assert.Throws<ArgumentException>(() => new ViewportId(value));
    }

    [Fact]
    public void Viewport_id_trims_stable_value()
    {
        var id = new ViewportId(" scene ");

        Assert.Equal("scene", id.Value);
        Assert.Equal("scene", id.ToString());
    }

    [Theory]
    [InlineData(0, 480, 1)]
    [InlineData(640, 0, 1)]
    [InlineData(-1, 480, 1)]
    [InlineData(640, -1, 1)]
    [InlineData(640, 480, 0)]
    [InlineData(640, 480, -1)]
    [InlineData(640, 480, double.NaN)]
    [InlineData(640, 480, double.PositiveInfinity)]
    public void Viewport_extent_rejects_invalid_values(
        int widthPixels,
        int heightPixels,
        double renderScale)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportExtent(widthPixels, heightPixels, renderScale));
    }

    [Theory]
    [InlineData((ViewportKind)42)]
    public void Viewport_state_rejects_unknown_kind(ViewportKind kind)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportStateSnapshot(
                new ViewportId("scene"),
                "Scene",
                kind,
                CreateExtent(),
                ViewportUpdatePolicy.DirtyOnly,
                CreateClock(),
                isVisible: true,
                isFocused: false,
                isDirty: false,
                lastRenderedAtUtc: null,
                interactiveBurstRemaining: TimeSpan.Zero,
                pendingReasons: ViewportRenderReason.None));
    }

    [Fact]
    public void Viewport_state_rejects_default_id()
    {
        Assert.Throws<ArgumentException>(
            () => new ViewportStateSnapshot(
                default,
                "Scene",
                ViewportKind.Scene,
                CreateExtent(),
                ViewportUpdatePolicy.DirtyOnly,
                CreateClock(),
                isVisible: true,
                isFocused: false,
                isDirty: false,
                lastRenderedAtUtc: null,
                interactiveBurstRemaining: TimeSpan.Zero,
                pendingReasons: ViewportRenderReason.None));
    }

    [Theory]
    [InlineData(-1, 0, 0, 1)]
    [InlineData(0, -1, 0, 1)]
    [InlineData(0, 0, 0, 0)]
    [InlineData(double.NaN, 0, 0, 1)]
    [InlineData(0, double.NaN, 0, 1)]
    [InlineData(0, 0, 0, double.PositiveInfinity)]
    public void Viewport_clock_rejects_invalid_values(
        double timeSeconds,
        double deltaSeconds,
        ulong frameIndex,
        double playbackSpeed)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportClockSnapshot(
                ViewportClockMode.EditorPreviewTime,
                timeSeconds,
                deltaSeconds,
                frameIndex,
                playbackSpeed));
    }

    [Fact]
    public void Viewport_clock_advance_uses_caller_supplied_time_step()
    {
        var clock = new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 2,
            deltaSeconds: 0,
            frameIndex: 7,
            playbackSpeed: 0.5);

        var advanced = clock.Advance(TimeSpan.FromSeconds(4));

        Assert.Equal(4, advanced.TimeSeconds);
        Assert.Equal(2, advanced.DeltaSeconds);
        Assert.Equal<ulong>(8, advanced.FrameIndex);
        Assert.Equal(0.5, advanced.PlaybackSpeed);
    }

    [Theory]
    [InlineData(ViewportClockMode.FrozenTime)]
    [InlineData(ViewportClockMode.ManualStepTime)]
    [InlineData(ViewportClockMode.CapturedFrameTime)]
    public void Viewport_clock_advance_keeps_manual_or_frozen_time_stable(
        ViewportClockMode mode)
    {
        var clock = new ViewportClockSnapshot(
            mode,
            timeSeconds: 2,
            deltaSeconds: 0.25,
            frameIndex: 7,
            playbackSpeed: 1);

        var advanced = clock.Advance(TimeSpan.FromSeconds(4));

        Assert.Equal(2, advanced.TimeSeconds);
        Assert.Equal(0, advanced.DeltaSeconds);
        Assert.Equal<ulong>(7, advanced.FrameIndex);
    }

    private static ViewportExtent CreateExtent()
    {
        return new ViewportExtent(640, 480, renderScale: 1);
    }

    private static ViewportClockSnapshot CreateClock()
    {
        return new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 0,
            deltaSeconds: 0,
            frameIndex: 0,
            playbackSpeed: 1);
    }
}
```

- [ ] **Step 2: Run the focused tests and confirm they fail because the viewport types do not exist**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportModelTests"
```

Expected: FAIL with compiler errors for missing `Editor.Core.Models.Viewports` types.

- [ ] **Step 3: Add the model implementation**

Create `Core/Models/Viewports/ViewportId.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public readonly record struct ViewportId
{
    public ViewportId(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException(
                "Viewport id must not be blank.",
                nameof(value));
        }

        Value = value.Trim();
    }

    public string Value { get; }

    public bool IsDefault => string.IsNullOrWhiteSpace(Value);

    public static ViewportId NewId()
    {
        return new ViewportId(Guid.NewGuid().ToString("N"));
    }

    public override string ToString()
    {
        return Value ?? string.Empty;
    }
}
```

Create `Core/Models/Viewports/ViewportKind.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportKind
{
    Scene,
    Game,
    ShaderPreview,
    MaterialPreview,
    CameraPreview,
    FrameDebug,
    Thumbnail,
}
```

Create `Core/Models/Viewports/ViewportExtent.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportExtent
{
    public ViewportExtent(
        int widthPixels,
        int heightPixels,
        double renderScale)
    {
        if (widthPixels <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(widthPixels),
                widthPixels,
                "Viewport width in pixels must be greater than zero.");
        }

        if (heightPixels <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(heightPixels),
                heightPixels,
                "Viewport height in pixels must be greater than zero.");
        }

        if (renderScale <= 0 || !double.IsFinite(renderScale))
        {
            throw new ArgumentOutOfRangeException(
                nameof(renderScale),
                renderScale,
                "Viewport render scale must be finite and greater than zero.");
        }

        WidthPixels = widthPixels;
        HeightPixels = heightPixels;
        RenderScale = renderScale;
    }

    public int WidthPixels { get; }

    public int HeightPixels { get; }

    public double RenderScale { get; }
}
```

Create `Core/Models/Viewports/ViewportRenderReason.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

[Flags]
public enum ViewportRenderReason
{
    None = 0,
    InitialFrameMissing = 1 << 0,
    VisibleExposed = 1 << 1,
    Resized = 1 << 2,
    CameraChanged = 1 << 3,
    InputActive = 1 << 4,
    TimeAdvanced = 1 << 5,
    AssetChanged = 1 << 6,
    ShaderChanged = 1 << 7,
    FrameDebugStep = 1 << 8,
    RuntimePlaying = 1 << 9,
    CaptureRequested = 1 << 10,
    All = InitialFrameMissing
        | VisibleExposed
        | Resized
        | CameraChanged
        | InputActive
        | TimeAdvanced
        | AssetChanged
        | ShaderChanged
        | FrameDebugStep
        | RuntimePlaying
        | CaptureRequested,
}
```

Create `Core/Models/Viewports/ViewportUpdatePolicy.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportUpdatePolicy
{
    DirtyOnly,
    InteractiveBurst,
    TimePlayback,
    RuntimePlay,
    FrameDebug,
    PerformancePreview,
}
```

Create `Core/Models/Viewports/ViewportClockMode.cs`:

```csharp
namespace Editor.Core.Models.Viewports;

public enum ViewportClockMode
{
    EditorPreviewTime,
    GameTime,
    FixedStepTime,
    ManualStepTime,
    FrozenTime,
    ScrubTime,
    CapturedFrameTime,
}
```

Create `Core/Models/Viewports/ViewportClockSnapshot.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportClockSnapshot
{
    public ViewportClockSnapshot(
        ViewportClockMode mode,
        double timeSeconds,
        double deltaSeconds,
        ulong frameIndex,
        double playbackSpeed)
    {
        if (!Enum.IsDefined(mode))
        {
            throw new ArgumentOutOfRangeException(
                nameof(mode),
                mode,
                "Viewport clock mode is not defined.");
        }

        if (timeSeconds < 0 || !double.IsFinite(timeSeconds))
        {
            throw new ArgumentOutOfRangeException(
                nameof(timeSeconds),
                timeSeconds,
                "Viewport clock time must be finite and greater than or equal to zero.");
        }

        if (deltaSeconds < 0 || !double.IsFinite(deltaSeconds))
        {
            throw new ArgumentOutOfRangeException(
                nameof(deltaSeconds),
                deltaSeconds,
                "Viewport clock delta must be finite and greater than or equal to zero.");
        }

        if (playbackSpeed <= 0 || !double.IsFinite(playbackSpeed))
        {
            throw new ArgumentOutOfRangeException(
                nameof(playbackSpeed),
                playbackSpeed,
                "Viewport clock playback speed must be finite and greater than zero.");
        }

        Mode = mode;
        TimeSeconds = timeSeconds;
        DeltaSeconds = deltaSeconds;
        FrameIndex = frameIndex;
        PlaybackSpeed = playbackSpeed;
    }

    public ViewportClockMode Mode { get; }

    public double TimeSeconds { get; }

    public double DeltaSeconds { get; }

    public ulong FrameIndex { get; }

    public double PlaybackSpeed { get; }

    public ViewportClockSnapshot Advance(TimeSpan realElapsed)
    {
        if (realElapsed < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(realElapsed),
                realElapsed,
                "Viewport clock elapsed time must be greater than or equal to zero.");
        }

        if (Mode is ViewportClockMode.FrozenTime
            or ViewportClockMode.ManualStepTime
            or ViewportClockMode.CapturedFrameTime)
        {
            return new ViewportClockSnapshot(
                Mode,
                TimeSeconds,
                deltaSeconds: 0,
                FrameIndex,
                PlaybackSpeed);
        }

        var deltaSeconds = realElapsed.TotalSeconds * PlaybackSpeed;
        return new ViewportClockSnapshot(
            Mode,
            TimeSeconds + deltaSeconds,
            deltaSeconds,
            FrameIndex + 1,
            PlaybackSpeed);
    }
}
```

Create `Core/Models/Viewports/ViewportStateSnapshot.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportStateSnapshot
{
    public ViewportStateSnapshot(
        ViewportId id,
        string displayName,
        ViewportKind kind,
        ViewportExtent extent,
        ViewportUpdatePolicy updatePolicy,
        ViewportClockSnapshot clock,
        bool isVisible,
        bool isFocused,
        bool isDirty,
        DateTimeOffset? lastRenderedAtUtc,
        TimeSpan interactiveBurstRemaining,
        ViewportRenderReason pendingReasons)
    {
        if (id.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(id));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(
                nameof(kind),
                kind,
                "Viewport kind is not defined.");
        }

        if (!Enum.IsDefined(updatePolicy))
        {
            throw new ArgumentOutOfRangeException(
                nameof(updatePolicy),
                updatePolicy,
                "Viewport update policy is not defined.");
        }

        ArgumentNullException.ThrowIfNull(extent);
        ArgumentNullException.ThrowIfNull(clock);

        if (interactiveBurstRemaining < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(interactiveBurstRemaining),
                interactiveBurstRemaining,
                "Interactive burst remaining time must be greater than or equal to zero.");
        }

        if ((pendingReasons & ~ViewportRenderReason.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(pendingReasons),
                pendingReasons,
                "Viewport render reasons include undefined flags.");
        }

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName)
            ? id.Value
            : displayName.Trim();
        Kind = kind;
        Extent = extent;
        UpdatePolicy = updatePolicy;
        Clock = clock;
        IsVisible = isVisible;
        IsFocused = isFocused;
        IsDirty = isDirty;
        LastRenderedAtUtc = lastRenderedAtUtc;
        InteractiveBurstRemaining = interactiveBurstRemaining;
        PendingReasons = pendingReasons;
    }

    public ViewportId Id { get; }

    public string DisplayName { get; }

    public ViewportKind Kind { get; }

    public ViewportExtent Extent { get; }

    public ViewportUpdatePolicy UpdatePolicy { get; }

    public ViewportClockSnapshot Clock { get; }

    public bool IsVisible { get; }

    public bool IsFocused { get; }

    public bool IsDirty { get; }

    public DateTimeOffset? LastRenderedAtUtc { get; }

    public TimeSpan InteractiveBurstRemaining { get; }

    public ViewportRenderReason PendingReasons { get; }
}
```

- [ ] **Step 4: Run the focused tests and confirm they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportModelTests"
```

Expected: PASS.

- [ ] **Step 5: Commit the model contracts**

Run:

```powershell
git add Core\Models\Viewports Tests\Editor.Tests\Core\Models\Viewports\ViewportModelTests.cs
git commit -m "feat: add studio viewport contract models"
```

---

### Task 2: Scheduler Request, Context, And Options Contracts

**Files:**
- Create: `Tests/Editor.Tests/Core/Models/Viewports/ViewportSchedulerContractTests.cs`
- Create: `Core/Models/Viewports/ViewportSchedulerContext.cs`
- Create: `Core/Models/Viewports/ViewportRenderRequest.cs`
- Create: `Core/Models/Viewports/ViewportRenderResult.cs`
- Create: `Core/Models/Viewports/ViewportSchedulerOptions.cs`

- [ ] **Step 1: Write the failing scheduler contract tests**

Create `Tests/Editor.Tests/Core/Models/Viewports/ViewportSchedulerContractTests.cs`:

```csharp
using System;
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Models.Viewports;

public sealed class ViewportSchedulerContractTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void Scheduler_context_rejects_non_positive_render_limit(
        int maxViewportRendersThisTick)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerContext(
                DateTimeOffset.UnixEpoch,
                maxViewportRendersThisTick));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Scheduler_options_reject_invalid_frame_rates(double framesPerSecond)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                interactiveBurstFramesPerSecond: framesPerSecond));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                sceneIdleFramesPerSecond: framesPerSecond));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                previewFramesPerSecond: framesPerSecond));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportSchedulerOptions(
                runtimeFramesPerSecond: framesPerSecond));
    }

    [Fact]
    public void Scheduler_options_expose_ceiling_based_intervals()
    {
        var options = new ViewportSchedulerOptions(
            interactiveBurstFramesPerSecond: 60,
            sceneIdleFramesPerSecond: 5,
            previewFramesPerSecond: 15,
            runtimeFramesPerSecond: 30);

        Assert.Equal(TimeSpan.FromTicks(166667), options.InteractiveBurstInterval);
        Assert.Equal(TimeSpan.FromMilliseconds(200), options.SceneIdleInterval);
        Assert.Equal(TimeSpan.FromTicks(666667), options.PreviewInterval);
        Assert.Equal(TimeSpan.FromTicks(333334), options.RuntimeInterval);
    }

    [Fact]
    public void Render_request_rejects_empty_reason()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportRenderRequest(
                new ViewportId("scene"),
                ViewportKind.Scene,
                CreateExtent(),
                CreateClock(),
                ViewportUpdatePolicy.DirtyOnly,
                ViewportRenderReason.None,
                priority: 10,
                DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void Render_request_rejects_negative_priority()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportRenderRequest(
                new ViewportId("scene"),
                ViewportKind.Scene,
                CreateExtent(),
                CreateClock(),
                ViewportUpdatePolicy.DirtyOnly,
                ViewportRenderReason.InitialFrameMissing,
                priority: -1,
                DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void Render_result_rejects_default_id()
    {
        Assert.Throws<ArgumentException>(
            () => new ViewportRenderResult(
                default,
                ViewportKind.Scene,
                CreateExtent(),
                ViewportRenderReason.InitialFrameMissing,
                wasRendered: true,
                "Rendered",
                DateTimeOffset.UnixEpoch,
                cpuMilliseconds: 0.25));
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Render_result_rejects_invalid_cpu_duration(double cpuMilliseconds)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportRenderResult(
                new ViewportId("scene"),
                ViewportKind.Scene,
                CreateExtent(),
                ViewportRenderReason.InitialFrameMissing,
                wasRendered: true,
                "Rendered",
                DateTimeOffset.UnixEpoch,
                cpuMilliseconds));
    }

    private static ViewportExtent CreateExtent()
    {
        return new ViewportExtent(640, 480, renderScale: 1);
    }

    private static ViewportClockSnapshot CreateClock()
    {
        return new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 0,
            deltaSeconds: 0,
            frameIndex: 0,
            playbackSpeed: 1);
    }
}
```

- [ ] **Step 2: Run the focused tests and confirm they fail because scheduler contract types do not exist**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportSchedulerContractTests"
```

Expected: FAIL with compiler errors for missing `ViewportSchedulerContext`, `ViewportSchedulerOptions`, `ViewportRenderRequest`, and `ViewportRenderResult`.

- [ ] **Step 3: Add the scheduler contract implementation**

Create `Core/Models/Viewports/ViewportSchedulerContext.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportSchedulerContext
{
    public ViewportSchedulerContext(
        DateTimeOffset nowUtc,
        int maxViewportRendersThisTick = 2)
    {
        if (maxViewportRendersThisTick <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maxViewportRendersThisTick),
                maxViewportRendersThisTick,
                "Maximum viewport renders per tick must be greater than zero.");
        }

        NowUtc = nowUtc;
        MaxViewportRendersThisTick = maxViewportRendersThisTick;
    }

    public DateTimeOffset NowUtc { get; }

    public int MaxViewportRendersThisTick { get; }
}
```

Create `Core/Models/Viewports/ViewportRenderRequest.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportRenderRequest
{
    public ViewportRenderRequest(
        ViewportId id,
        ViewportKind kind,
        ViewportExtent extent,
        ViewportClockSnapshot clock,
        ViewportUpdatePolicy updatePolicy,
        ViewportRenderReason reason,
        int priority,
        DateTimeOffset requestedAtUtc)
    {
        if (id.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(id));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(
                nameof(kind),
                kind,
                "Viewport kind is not defined.");
        }

        if (!Enum.IsDefined(updatePolicy))
        {
            throw new ArgumentOutOfRangeException(
                nameof(updatePolicy),
                updatePolicy,
                "Viewport update policy is not defined.");
        }

        ArgumentNullException.ThrowIfNull(extent);
        ArgumentNullException.ThrowIfNull(clock);

        if (reason == ViewportRenderReason.None
            || (reason & ~ViewportRenderReason.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Viewport render request must have one or more defined reasons.");
        }

        if (priority < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(priority),
                priority,
                "Viewport render request priority must be greater than or equal to zero.");
        }

        Id = id;
        Kind = kind;
        Extent = extent;
        Clock = clock;
        UpdatePolicy = updatePolicy;
        Reason = reason;
        Priority = priority;
        RequestedAtUtc = requestedAtUtc;
    }

    public ViewportId Id { get; }

    public ViewportKind Kind { get; }

    public ViewportExtent Extent { get; }

    public ViewportClockSnapshot Clock { get; }

    public ViewportUpdatePolicy UpdatePolicy { get; }

    public ViewportRenderReason Reason { get; }

    public int Priority { get; }

    public DateTimeOffset RequestedAtUtc { get; }
}
```

Create `Core/Models/Viewports/ViewportRenderResult.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportRenderResult
{
    public ViewportRenderResult(
        ViewportId id,
        ViewportKind kind,
        ViewportExtent requestedExtent,
        ViewportRenderReason reason,
        bool wasRendered,
        string statusMessage,
        DateTimeOffset completedAtUtc,
        double? cpuMilliseconds)
    {
        if (id.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(id));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(
                nameof(kind),
                kind,
                "Viewport kind is not defined.");
        }

        ArgumentNullException.ThrowIfNull(requestedExtent);

        if ((reason & ~ViewportRenderReason.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Viewport render result contains undefined reason flags.");
        }

        if (wasRendered && reason == ViewportRenderReason.None)
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Rendered viewport results must preserve the render reason.");
        }

        if (cpuMilliseconds is { } duration
            && (duration < 0 || !double.IsFinite(duration)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(cpuMilliseconds),
                cpuMilliseconds,
                "Viewport render CPU duration must be finite and greater than or equal to zero.");
        }

        Id = id;
        Kind = kind;
        RequestedExtent = requestedExtent;
        Reason = reason;
        WasRendered = wasRendered;
        StatusMessage = string.IsNullOrWhiteSpace(statusMessage)
            ? id.Value
            : statusMessage.Trim();
        CompletedAtUtc = completedAtUtc;
        CpuMilliseconds = cpuMilliseconds;
    }

    public ViewportId Id { get; }

    public ViewportKind Kind { get; }

    public ViewportExtent RequestedExtent { get; }

    public ViewportRenderReason Reason { get; }

    public bool WasRendered { get; }

    public string StatusMessage { get; }

    public DateTimeOffset CompletedAtUtc { get; }

    public double? CpuMilliseconds { get; }
}
```

Create `Core/Models/Viewports/ViewportSchedulerOptions.cs`:

```csharp
using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportSchedulerOptions
{
    public ViewportSchedulerOptions(
        double interactiveBurstFramesPerSecond = 60,
        double sceneIdleFramesPerSecond = 5,
        double previewFramesPerSecond = 15,
        double runtimeFramesPerSecond = 60)
    {
        ValidateFrameRate(
            interactiveBurstFramesPerSecond,
            nameof(interactiveBurstFramesPerSecond));
        ValidateFrameRate(
            sceneIdleFramesPerSecond,
            nameof(sceneIdleFramesPerSecond));
        ValidateFrameRate(
            previewFramesPerSecond,
            nameof(previewFramesPerSecond));
        ValidateFrameRate(
            runtimeFramesPerSecond,
            nameof(runtimeFramesPerSecond));

        InteractiveBurstFramesPerSecond = interactiveBurstFramesPerSecond;
        SceneIdleFramesPerSecond = sceneIdleFramesPerSecond;
        PreviewFramesPerSecond = previewFramesPerSecond;
        RuntimeFramesPerSecond = runtimeFramesPerSecond;
    }

    public static ViewportSchedulerOptions Default { get; } = new();

    public double InteractiveBurstFramesPerSecond { get; }

    public double SceneIdleFramesPerSecond { get; }

    public double PreviewFramesPerSecond { get; }

    public double RuntimeFramesPerSecond { get; }

    public TimeSpan InteractiveBurstInterval =>
        GetTargetFrameInterval(InteractiveBurstFramesPerSecond);

    public TimeSpan SceneIdleInterval =>
        GetTargetFrameInterval(SceneIdleFramesPerSecond);

    public TimeSpan PreviewInterval =>
        GetTargetFrameInterval(PreviewFramesPerSecond);

    public TimeSpan RuntimeInterval =>
        GetTargetFrameInterval(RuntimeFramesPerSecond);

    private static void ValidateFrameRate(
        double framesPerSecond,
        string parameterName)
    {
        if (framesPerSecond <= 0 || !double.IsFinite(framesPerSecond))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                framesPerSecond,
                "Viewport scheduler frame rate must be finite and greater than zero.");
        }
    }

    private static TimeSpan GetTargetFrameInterval(double framesPerSecond)
    {
        return TimeSpan.FromTicks(
            (long)Math.Ceiling(TimeSpan.TicksPerSecond / framesPerSecond));
    }
}
```

- [ ] **Step 4: Run the contract tests and confirm they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportSchedulerContractTests|ViewportModelTests"
```

Expected: PASS.

- [ ] **Step 5: Commit the scheduler contract models**

Run:

```powershell
git add Core\Models\Viewports Tests\Editor.Tests\Core\Models\Viewports
git commit -m "feat: add studio viewport scheduler contracts"
```

---

### Task 3: Pure Managed Viewport Scheduler

**Files:**
- Create: `Tests/Editor.Tests/Core/Services/ViewportSchedulerTests.cs`
- Create: `Core/Services/ViewportScheduler.cs`

- [ ] **Step 1: Write the failing scheduler behavior tests**

Create `Tests/Editor.Tests/Core/Services/ViewportSchedulerTests.cs`:

```csharp
using System;
using System.Linq;
using Editor.Core.Models.Viewports;
using Editor.Core.Services;
using Xunit;

namespace Editor.Tests.Core.Services;

public sealed class ViewportSchedulerTests
{
    [Fact]
    public void Hidden_viewport_is_skipped()
    {
        var scheduler = new ViewportScheduler();
        var viewport = CreateViewport(
            isVisible: false,
            isDirty: true,
            pendingReasons: ViewportRenderReason.Resized);

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(DateTimeOffset.UnixEpoch));

        Assert.Empty(requests);
    }

    [Fact]
    public void Dirty_only_viewport_renders_only_when_dirty_or_reasoned()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var clean = CreateViewport(
            id: "clean",
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            lastRenderedAtUtc: now);
        var dirty = CreateViewport(
            id: "dirty",
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            isDirty: true,
            lastRenderedAtUtc: now);
        var reasoned = CreateViewport(
            id: "reasoned",
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            lastRenderedAtUtc: now,
            pendingReasons: ViewportRenderReason.CameraChanged);

        var requests = scheduler.BuildRenderPlan(
            [clean, dirty, reasoned],
            new ViewportSchedulerContext(now.AddMilliseconds(16)));

        Assert.Equal(["dirty", "reasoned"], requests.Select(request => request.Id.Value));
        Assert.Contains(
            requests,
            request => request.Id.Value == "dirty"
                && request.Reason.HasFlag(ViewportRenderReason.AssetChanged));
        Assert.Contains(
            requests,
            request => request.Id.Value == "reasoned"
                && request.Reason.HasFlag(ViewportRenderReason.CameraChanged));
    }

    [Fact]
    public void Interactive_viewport_bursts_then_uses_idle_interval()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var active = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            isFocused: true,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.FromSeconds(1));
        var idle = CreateViewport(
            id: "idle",
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.Zero);

        var activeRequests = scheduler.BuildRenderPlan(
            [active],
            new ViewportSchedulerContext(now.AddMilliseconds(16)));
        var idleTooSoon = scheduler.BuildRenderPlan(
            [idle],
            new ViewportSchedulerContext(now.AddMilliseconds(100)));
        var idleAtInterval = scheduler.BuildRenderPlan(
            [idle],
            new ViewportSchedulerContext(now.AddMilliseconds(200)));

        var activeRequest = Assert.Single(activeRequests);
        Assert.True(activeRequest.Reason.HasFlag(ViewportRenderReason.InputActive));
        Assert.Empty(idleTooSoon);

        var idleRequest = Assert.Single(idleAtInterval);
        Assert.True(idleRequest.Reason.HasFlag(ViewportRenderReason.VisibleExposed));
    }

    [Fact]
    public void Time_playback_request_keeps_caller_supplied_clock()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var clock = new ViewportClockSnapshot(
            ViewportClockMode.EditorPreviewTime,
            timeSeconds: 4.5,
            deltaSeconds: 0.5,
            frameIndex: 17,
            playbackSpeed: 1);
        var viewport = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.TimePlayback,
            clock: clock,
            lastRenderedAtUtc: now);

        var requests = scheduler.BuildRenderPlan(
            [viewport],
            new ViewportSchedulerContext(now.AddMilliseconds(100)));

        var request = Assert.Single(requests);
        Assert.Same(clock, request.Clock);
        Assert.True(request.Reason.HasFlag(ViewportRenderReason.TimeAdvanced));
    }

    [Fact]
    public void Frame_debug_viewport_renders_only_initial_step_capture_or_dirty_reasons()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var clean = CreateViewport(
            updatePolicy: ViewportUpdatePolicy.FrameDebug,
            lastRenderedAtUtc: now);
        var stepped = CreateViewport(
            id: "stepped",
            updatePolicy: ViewportUpdatePolicy.FrameDebug,
            lastRenderedAtUtc: now,
            pendingReasons: ViewportRenderReason.FrameDebugStep);

        var cleanRequests = scheduler.BuildRenderPlan(
            [clean],
            new ViewportSchedulerContext(now.AddSeconds(1)));
        var steppedRequests = scheduler.BuildRenderPlan(
            [stepped],
            new ViewportSchedulerContext(now.AddSeconds(1)));

        Assert.Empty(cleanRequests);
        var steppedRequest = Assert.Single(steppedRequests);
        Assert.Equal(ViewportRenderReason.FrameDebugStep, steppedRequest.Reason);
    }

    [Fact]
    public void Scheduler_limits_requests_and_orders_hot_viewports_first()
    {
        var scheduler = new ViewportScheduler();
        var now = DateTimeOffset.UnixEpoch;
        var low = CreateViewport(
            id: "material",
            kind: ViewportKind.MaterialPreview,
            updatePolicy: ViewportUpdatePolicy.DirtyOnly,
            isDirty: true,
            lastRenderedAtUtc: now);
        var focused = CreateViewport(
            id: "scene",
            updatePolicy: ViewportUpdatePolicy.InteractiveBurst,
            isFocused: true,
            lastRenderedAtUtc: now,
            interactiveBurstRemaining: TimeSpan.FromSeconds(1));
        var capture = CreateViewport(
            id: "frame",
            kind: ViewportKind.FrameDebug,
            updatePolicy: ViewportUpdatePolicy.FrameDebug,
            lastRenderedAtUtc: now,
            pendingReasons: ViewportRenderReason.CaptureRequested);

        var requests = scheduler.BuildRenderPlan(
            [low, focused, capture],
            new ViewportSchedulerContext(now.AddMilliseconds(16), maxViewportRendersThisTick: 2));

        Assert.Equal(["frame", "scene"], requests.Select(request => request.Id.Value));
    }

    private static ViewportStateSnapshot CreateViewport(
        string id = "scene",
        ViewportKind kind = ViewportKind.Scene,
        ViewportUpdatePolicy updatePolicy = ViewportUpdatePolicy.DirtyOnly,
        bool isVisible = true,
        bool isFocused = false,
        bool isDirty = false,
        DateTimeOffset? lastRenderedAtUtc = null,
        TimeSpan? interactiveBurstRemaining = null,
        ViewportRenderReason pendingReasons = ViewportRenderReason.None,
        ViewportClockSnapshot? clock = null)
    {
        return new ViewportStateSnapshot(
            new ViewportId(id),
            id,
            kind,
            updatePolicy == ViewportUpdatePolicy.FrameDebug
                ? new ViewportExtent(320, 180, renderScale: 1)
                : new ViewportExtent(640, 480, renderScale: 1),
            updatePolicy,
            clock ?? new ViewportClockSnapshot(
                ViewportClockMode.EditorPreviewTime,
                timeSeconds: 0,
                deltaSeconds: 0,
                frameIndex: 0,
                playbackSpeed: 1),
            isVisible,
            isFocused,
            isDirty,
            lastRenderedAtUtc,
            interactiveBurstRemaining ?? TimeSpan.Zero,
            pendingReasons);
    }
}
```

- [ ] **Step 2: Run the scheduler tests and confirm they fail because `ViewportScheduler` does not exist**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportSchedulerTests"
```

Expected: FAIL with compiler errors for missing `Editor.Core.Services.ViewportScheduler`.

- [ ] **Step 3: Add the scheduler implementation**

Create `Core/Services/ViewportScheduler.cs`:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models.Viewports;

namespace Editor.Core.Services;

public sealed class ViewportScheduler
{
    private readonly ViewportSchedulerOptions options_;

    public ViewportScheduler(ViewportSchedulerOptions? options = null)
    {
        options_ = options ?? ViewportSchedulerOptions.Default;
    }

    public IReadOnlyList<ViewportRenderRequest> BuildRenderPlan(
        IReadOnlyList<ViewportStateSnapshot> viewports,
        ViewportSchedulerContext context)
    {
        ArgumentNullException.ThrowIfNull(viewports);
        ArgumentNullException.ThrowIfNull(context);

        var requests = new List<ViewportRenderRequest>();
        foreach (var viewport in viewports)
        {
            ArgumentNullException.ThrowIfNull(viewport);

            var reason = GetRenderReason(viewport, context.NowUtc);
            if (reason == ViewportRenderReason.None)
            {
                continue;
            }

            requests.Add(new ViewportRenderRequest(
                viewport.Id,
                viewport.Kind,
                viewport.Extent,
                viewport.Clock,
                viewport.UpdatePolicy,
                reason,
                GetPriority(viewport, reason),
                context.NowUtc));
        }

        return requests
            .OrderByDescending(request => request.Priority)
            .ThenBy(request => request.Id.Value, StringComparer.Ordinal)
            .Take(context.MaxViewportRendersThisTick)
            .ToArray();
    }

    private ViewportRenderReason GetRenderReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc)
    {
        if (!viewport.IsVisible)
        {
            return ViewportRenderReason.None;
        }

        var reason = viewport.PendingReasons;
        if (viewport.LastRenderedAtUtc is null)
        {
            reason |= ViewportRenderReason.InitialFrameMissing;
        }

        if (viewport.IsDirty)
        {
            reason |= ViewportRenderReason.AssetChanged;
        }

        return viewport.UpdatePolicy switch
        {
            ViewportUpdatePolicy.DirtyOnly => reason,
            ViewportUpdatePolicy.InteractiveBurst =>
                GetInteractiveBurstReason(viewport, nowUtc, reason),
            ViewportUpdatePolicy.TimePlayback =>
                GetTimePlaybackReason(viewport, nowUtc, reason),
            ViewportUpdatePolicy.RuntimePlay =>
                GetRuntimeReason(viewport, nowUtc, reason),
            ViewportUpdatePolicy.FrameDebug =>
                GetFrameDebugReason(reason),
            ViewportUpdatePolicy.PerformancePreview =>
                GetRuntimeReason(viewport, nowUtc, reason),
            _ => throw new ArgumentOutOfRangeException(
                nameof(viewport),
                viewport.UpdatePolicy,
                "Viewport update policy is not defined."),
        };
    }

    private ViewportRenderReason GetInteractiveBurstReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc,
        ViewportRenderReason reason)
    {
        if (viewport.IsFocused
            && viewport.InteractiveBurstRemaining > TimeSpan.Zero)
        {
            reason |= ViewportRenderReason.InputActive;
        }

        if (reason == ViewportRenderReason.None
            && ShouldTick(viewport.LastRenderedAtUtc, nowUtc, options_.SceneIdleInterval))
        {
            reason |= ViewportRenderReason.VisibleExposed;
        }

        return reason;
    }

    private ViewportRenderReason GetTimePlaybackReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc,
        ViewportRenderReason reason)
    {
        if (ShouldTick(viewport.LastRenderedAtUtc, nowUtc, options_.PreviewInterval))
        {
            reason |= ViewportRenderReason.TimeAdvanced;
        }

        return reason;
    }

    private ViewportRenderReason GetRuntimeReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc,
        ViewportRenderReason reason)
    {
        if (ShouldTick(viewport.LastRenderedAtUtc, nowUtc, options_.RuntimeInterval))
        {
            reason |= ViewportRenderReason.RuntimePlaying;
        }

        return reason;
    }

    private static ViewportRenderReason GetFrameDebugReason(
        ViewportRenderReason reason)
    {
        return reason
            & (ViewportRenderReason.InitialFrameMissing
                | ViewportRenderReason.VisibleExposed
                | ViewportRenderReason.Resized
                | ViewportRenderReason.AssetChanged
                | ViewportRenderReason.ShaderChanged
                | ViewportRenderReason.FrameDebugStep
                | ViewportRenderReason.CaptureRequested);
    }

    private static bool ShouldTick(
        DateTimeOffset? lastRenderedAtUtc,
        DateTimeOffset nowUtc,
        TimeSpan interval)
    {
        return lastRenderedAtUtc is null
            || nowUtc - lastRenderedAtUtc.Value >= interval;
    }

    private static int GetPriority(
        ViewportStateSnapshot viewport,
        ViewportRenderReason reason)
    {
        if (reason.HasFlag(ViewportRenderReason.CaptureRequested))
        {
            return 100;
        }

        if (reason.HasFlag(ViewportRenderReason.FrameDebugStep))
        {
            return 90;
        }

        if (reason.HasFlag(ViewportRenderReason.RuntimePlaying)
            && viewport.IsFocused)
        {
            return 85;
        }

        if (reason.HasFlag(ViewportRenderReason.InputActive))
        {
            return 80;
        }

        if (reason.HasFlag(ViewportRenderReason.InitialFrameMissing))
        {
            return 70;
        }

        if (viewport.IsFocused)
        {
            return 60;
        }

        return 40;
    }
}
```

- [ ] **Step 4: Run the scheduler tests and model tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "ViewportSchedulerTests|ViewportSchedulerContractTests|ViewportModelTests"
```

Expected: PASS.

- [ ] **Step 5: Commit the scheduler service**

Run:

```powershell
git add Core\Services\ViewportScheduler.cs Tests\Editor.Tests\Core\Services\ViewportSchedulerTests.cs
git commit -m "feat: add studio viewport scheduler"
```

---

### Task 4: Architecture Classification, Docs, And Validation

**Files:**
- Modify: `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`
- Modify: `docs/Studio代码分类.md`

- [ ] **Step 1: Add the architecture test for viewport model placement**

In `Tests/Editor.Tests/Architecture/StudioLayeringTests.cs`, add this test near the existing Core model folder tests:

```csharp
    [Fact]
    public void Viewport_core_models_live_in_viewport_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Viewports;";

        var viewportFiles = new[]
        {
            "ViewportClockMode.cs",
            "ViewportClockSnapshot.cs",
            "ViewportExtent.cs",
            "ViewportId.cs",
            "ViewportKind.cs",
            "ViewportRenderReason.cs",
            "ViewportRenderRequest.cs",
            "ViewportRenderResult.cs",
            "ViewportSchedulerContext.cs",
            "ViewportSchedulerOptions.cs",
            "ViewportStateSnapshot.cs",
            "ViewportUpdatePolicy.cs",
        };

        foreach (var fileName in viewportFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Viewports", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is viewport contract data and should live under Core/Models/Viewports.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }
```

- [ ] **Step 2: Run the focused architecture test**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "Viewport_core_models_live_in_viewport_model_folder"
```

Expected: PASS.

- [ ] **Step 3: Add the docs classification entry**

Append this section to `docs/Studio代码分类.md`:

```markdown
## 2026-07-05 Viewport contract and scheduler classification

- `Core/Models/Viewports` owns immutable, UI-neutral viewport identity, extent, clock, render reason, update policy, scheduler context, render request, and render result contracts.
- `Core/Services/ViewportScheduler.cs` owns the pure managed render-request planner for Phase 1. It consumes viewport snapshots and emits `ViewportRenderRequest` snapshots only.
- The Phase 1 viewport scheduler is CPU-only and must not reference Avalonia controls, composition surfaces, platform window handles, native pointers, Vulkan handles, RenderGraph objects, or renderer live state.
- `Features/SceneView` may consume these contracts in a later slice, but it must still not own native Vulkan viewport lifetime or renderer handles.
- Native viewport bridge, Avalonia composition import, and GamePreview swapchain work remain separate implementation slices.
```

- [ ] **Step 4: Run focused tests**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "Viewport|Architecture"
```

Expected: PASS.

- [ ] **Step 5: Run full managed tests**

Run:

```powershell
dotnet test Editor.sln -c Release
```

Expected: PASS.

- [ ] **Step 6: Run text and diff validation**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\tools\check-text-encoding.ps1
git diff --check
```

Expected:

- Encoding check reports zero missing BOM, zero unexpected BOM, and zero invalid UTF-8.
- `git diff --check` exits with code 0.

- [ ] **Step 7: Commit the classification and docs**

Run:

```powershell
git add Tests\Editor.Tests\Architecture\StudioLayeringTests.cs docs\Studio代码分类.md
git commit -m "docs: classify studio viewport scheduler contracts"
```

---

## Self-Review Notes

- Phase 1 scope is covered by Tasks 1-3: viewport id/kind/extent, clock state and clock modes, render reasons and update policies, render request/result snapshots, and scheduler unit tests.
- Hidden viewport, dirty-only viewport, interactive burst, time playback clock handoff, and frame-debug manual behavior are covered in `ViewportSchedulerTests`.
- No task introduces Avalonia controls, native bridge code, Vulkan handles, RenderGraph types, or GamePreview.
- Type names and namespaces are consistent: all viewport contracts live under `Editor.Core.Models.Viewports`; the scheduler service lives under `Editor.Core.Services`.
- Later phases remain separate: Scene View control shell, native bridge, Avalonia composition, multi-viewport GPU presentation, and GamePreview native swapchain host.

## Execution Options

Plan complete and saved to `docs/superpowers/plans/2026-07-05-studio-viewport-contract-scheduler.md`. Two execution options:

**1. Subagent-Driven (recommended)** - dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** - execute tasks in this session using executing-plans, batch execution with checkpoints.
