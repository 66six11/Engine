using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Xunit;
using Xunit.Abstractions;

namespace Asharia.Studio.WindowsCapture.Tests;

[CollectionDefinition(kCollectionName, DisableParallelization = true)]
public sealed class WgcDwmCompositedAcceptanceCollection
{
    public const string kCollectionName = "WGC DWM-composited acceptance";
}

internal static class WgcDwmCompositedAcceptanceGate
{
    internal const string EnvironmentVariable = "ASHARIA_RUN_STUDIO_WGC_DWM_ACCEPTANCE";

    internal static string? SkipReason => ResolveSkipReason(
        OperatingSystem.IsWindowsVersionAtLeast(10, 0, 26100),
        Environment.GetEnvironmentVariable(EnvironmentVariable));

    internal static string? ResolveSkipReason(bool isSupportedWindows, string? optInValue)
    {
        if (!isSupportedWindows)
        {
            return "WGC DWM-composited acceptance requires Windows build 26100 or newer.";
        }

        return string.Equals(optInValue, "1", StringComparison.Ordinal)
            ? null
            : $"WGC DWM-composited acceptance is opt-in; set {EnvironmentVariable}=1 to run it.";
    }

    internal static void RequireEnabled()
    {
        if (SkipReason is { } reason)
        {
            throw new InvalidOperationException(
                $"The WGC DWM-composited acceptance skip gate was bypassed: {reason}");
        }
    }
}

internal static class WgcDwmCompositedReleaseWindow
{
    internal static TimeSpan ConvertQpcToTimeSpan(long qpc, long frequency)
    {
        if (qpc < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(qpc));
        }
        if (frequency <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(frequency));
        }

        var wholeSeconds = Math.DivRem(qpc, frequency, out var remainder);
        var fractionalTicks = checked((long)Math.Round(
            remainder * ((double)TimeSpan.TicksPerSecond / frequency),
            MidpointRounding.ToEven));
        var ticks = checked(
            wholeSeconds * TimeSpan.TicksPerSecond + fractionalTicks);
        return TimeSpan.FromTicks(ticks);
    }

    internal static WgcDwmCompositedReleaseSelection Select(
        IReadOnlyList<DwmCompositedFrameObservation> observations,
        TimeSpan releaseCompositorTime,
        TimeSpan finalCompositorTime,
        long deliveryBaselineSequence)
    {
        ArgumentNullException.ThrowIfNull(observations);
        if (finalCompositorTime < releaseCompositorTime)
        {
            throw new ArgumentOutOfRangeException(
                nameof(finalCompositorTime),
                "The final compositor time must not precede the release marker.");
        }

        var releaseObservations = observations
            .Where(observation =>
                observation.CompositorRenderedTime >= releaseCompositorTime &&
                observation.CompositorRenderedTime <= finalCompositorTime)
            .ToArray();
        var delayedPreReleaseDeliveredAfterBaseline = observations.Count(observation =>
            observation.Sequence > deliveryBaselineSequence &&
            observation.CompositorRenderedTime < releaseCompositorTime);
        return new WgcDwmCompositedReleaseSelection(
            releaseObservations,
            delayedPreReleaseDeliveredAfterBaseline);
    }
}

internal readonly record struct WgcDwmCompositedReleaseSelection(
    IReadOnlyList<DwmCompositedFrameObservation> Observations,
    int DelayedPreReleaseDeliveredAfterBaseline);

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class WgcDwmCompositedFactAttribute : FactAttribute
{
    public WgcDwmCompositedFactAttribute()
    {
        Skip = WgcDwmCompositedAcceptanceGate.SkipReason;
    }
}

public sealed class WgcDwmCompositedAcceptanceGateTests
{
    [Fact]
    public void Gate_requires_supported_Windows()
    {
        Assert.Contains(
            "requires Windows",
            WgcDwmCompositedAcceptanceGate.ResolveSkipReason(false, "1"),
            StringComparison.Ordinal);
    }

    [Fact]
    public void Gate_requires_explicit_opt_in()
    {
        Assert.Contains(
            WgcDwmCompositedAcceptanceGate.EnvironmentVariable,
            WgcDwmCompositedAcceptanceGate.ResolveSkipReason(true, null),
            StringComparison.Ordinal);
    }

    [Fact]
    public void Gate_accepts_exact_opt_in_value()
    {
        Assert.Null(WgcDwmCompositedAcceptanceGate.ResolveSkipReason(true, "1"));
        Assert.NotNull(WgcDwmCompositedAcceptanceGate.ResolveSkipReason(true, "true"));
    }
}

public sealed class WgcDwmCompositedAcceptanceSummaryTests
{
    [Fact]
    public void Qpc_conversion_preserves_the_system_relative_compositor_clock()
    {
        const long qpc = 7_618_983_569_941;

        Assert.Equal(
            TimeSpan.FromTicks(qpc),
            WgcDwmCompositedReleaseWindow.ConvertQpcToTimeSpan(
                qpc,
                TimeSpan.TicksPerSecond));
        Assert.Equal(
            TimeSpan.FromMilliseconds(1500),
            WgcDwmCompositedReleaseWindow.ConvertQpcToTimeSpan(
                qpc: 4_500_000,
                frequency: 3_000_000));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            WgcDwmCompositedReleaseWindow.ConvertQpcToTimeSpan(1, 0));
    }

    [Fact]
    public void Release_window_uses_compositor_time_and_only_uses_sequence_for_delivery_diagnostics()
    {
        var observations = new[]
        {
            CreateObservation(1, TimeSpan.FromMilliseconds(90), isGap: false),
            CreateObservation(2, TimeSpan.FromMilliseconds(100), isGap: false),
            CreateObservation(3, TimeSpan.FromMilliseconds(98), isGap: true),
            CreateObservation(4, TimeSpan.FromMilliseconds(110), isGap: false),
            CreateObservation(5, TimeSpan.FromMilliseconds(99), isGap: true),
            CreateObservation(6, TimeSpan.FromMilliseconds(111), isGap: false),
        };

        var selection = WgcDwmCompositedReleaseWindow.Select(
            observations,
            releaseCompositorTime: TimeSpan.FromMilliseconds(100),
            finalCompositorTime: TimeSpan.FromMilliseconds(110),
            deliveryBaselineSequence: 3);

        Assert.Equal(
            new long[] { 2, 4 },
            selection.Observations.Select(static observation => observation.Sequence));
        Assert.Equal(1, selection.DelayedPreReleaseDeliveredAfterBaseline);
    }

    [Fact]
    public void Summary_reports_allowed_gaps_and_WGC_delivered_cadence_without_scanout_claims()
    {
        var observations = new[]
        {
            CreateObservation(1, TimeSpan.Zero, isGap: false),
            CreateObservation(2, TimeSpan.FromMilliseconds(10), isGap: true),
            CreateObservation(3, TimeSpan.FromMilliseconds(30), isGap: false),
        };

        var summary = WgcDwmCompositedAcceptanceSummary.Create(
            "grow",
            observations,
            observations[1..],
            expectedReleaseWidth: 1280,
            expectedReleaseHeight: 720,
            releaseMarkerDeltaMilliseconds: 0.25,
            delayedPreReleaseDeliveredAfterBaseline: 1,
            new WgcDwmCompositedRecorderMetrics(1800, 1100, 0, 0, 0));

        Assert.Equal("wgc-dwm-composited-pixels", summary.EvidenceKind);
        Assert.True(summary.PixelEvidenceAvailable);
        Assert.False(summary.PhysicalDisplayedEvidenceAvailable);
        Assert.False(summary.EveryDwmFrameCapturedEvidenceAvailable);
        Assert.Equal(3, summary.ObservedFrameCount);
        Assert.Equal(2, summary.ExactFrameCount);
        Assert.Equal(0, summary.PixelContractViolationFrameCount);
        Assert.Equal(1, summary.AllowedGapFrameCount);
        Assert.Equal(1.0 / 3.0, summary.AllowedGapFrameRatio, precision: 8);
        Assert.Equal(30, summary.MaximumRightGapPixels);
        Assert.Equal(8, summary.MaximumBottomGapPixels);
        Assert.Equal(2, summary.ReleaseObserved);
        Assert.Equal(1, summary.ReleaseExact);
        Assert.Equal(1, summary.ReleaseNonExact);
        Assert.Equal(1, summary.ReleaseGap);
        Assert.Equal(0, summary.ReleaseBlank);
        Assert.Equal(0, summary.ReleaseCrop);
        Assert.Equal(0, summary.ReleaseStretch);
        Assert.Equal(0, summary.ReleaseAcceptedExtentMismatch);
        Assert.Equal(0.25, summary.ReleaseMarkerDeltaMilliseconds);
        Assert.Equal(1, summary.DelayedPreReleaseDeliveredAfterBaseline);
        Assert.Equal(2, summary.WgcDeliveredCadenceSampleCount);
        Assert.Equal(200.0 / 3.0, summary.WgcDeliveredFrameRateHz!.Value, precision: 8);
        Assert.Equal(20, summary.WgcDeliveredIntervalP95Milliseconds);
        Assert.Equal(20, summary.WgcDeliveredIntervalMaximumMilliseconds);

        var shrinkSummary = WgcDwmCompositedAcceptanceSummary.Create(
            "shrink",
            observations,
            observations[1..],
            expectedReleaseWidth: 1280,
            expectedReleaseHeight: 720,
            releaseMarkerDeltaMilliseconds: 0.25,
            delayedPreReleaseDeliveredAfterBaseline: 1,
            new WgcDwmCompositedRecorderMetrics(1800, 1100, 0, 0, 0));
        Assert.Equal(1, shrinkSummary.PixelContractViolationFrameCount);
    }

    [Fact]
    public void Summary_classifies_release_blank_crop_and_stretch_samples()
    {
        var baseline = CreateObservation(1, TimeSpan.Zero, isGap: false);
        var blank = CreateReleaseFailureObservation(
            2,
            new DwmCompositedSentinelObservation(
                Located: false,
                IsBlank: true,
                HasExactBlockSizes: false,
                HasAlignedCorners: false,
                Layout: default,
                Insets: default));
        var crop = CreateReleaseFailureObservation(
            3,
            new DwmCompositedSentinelObservation(
                Located: false,
                IsBlank: false,
                HasExactBlockSizes: false,
                HasAlignedCorners: false,
                Layout: default,
                Insets: default));
        var stretch = CreateReleaseFailureObservation(
            4,
            new DwmCompositedSentinelObservation(
                Located: true,
                IsBlank: false,
                HasExactBlockSizes: false,
                HasAlignedCorners: true,
                Layout: default,
                Insets: default));
        var release = new[] { blank, crop, stretch };

        var summary = WgcDwmCompositedAcceptanceSummary.Create(
            "grow",
            new[] { baseline, blank, crop, stretch },
            release,
            expectedReleaseWidth: 1280,
            expectedReleaseHeight: 720,
            releaseMarkerDeltaMilliseconds: 0,
            delayedPreReleaseDeliveredAfterBaseline: 0,
            new WgcDwmCompositedRecorderMetrics(1800, 1100, 0, 0, 0));

        Assert.Equal(3, summary.ReleaseObserved);
        Assert.Equal(0, summary.ReleaseExact);
        Assert.Equal(3, summary.ReleaseNonExact);
        Assert.Equal(0, summary.ReleaseGap);
        Assert.Equal(1, summary.ReleaseBlank);
        Assert.Equal(1, summary.ReleaseCrop);
        Assert.Equal(1, summary.ReleaseStretch);
        Assert.Equal(3, summary.ReleaseAcceptedExtentMismatch);
    }

    private static DwmCompositedFrameObservation CreateObservation(
        long sequence,
        TimeSpan compositorRenderedTime,
        bool isGap)
    {
        var sentinel = new DwmCompositedSentinelObservation(
            Located: true,
            IsBlank: false,
            HasExactBlockSizes: true,
            HasAlignedCorners: true,
            Layout: new DwmCompositedSentinelLayout(
                new PixelRectangle(0, 0, 24, 24),
                new PixelRectangle(1256, 0, 24, 24),
                new PixelRectangle(0, 696, 24, 24),
                new PixelRectangle(1256, 696, 24, 24)),
            Insets: default);
        var continuity = new DwmCompositedSentinelContinuity(
            CurrentIsExact: true,
            LeftTopInsetsMatch: true,
            RightBottomInsetsMatch: !isGap,
            RightBottomInsetsDoNotDecrease: true,
            RightGapPixels: isGap ? 30 : 0,
            BottomGapPixels: isGap ? 8 : 0);
        return new DwmCompositedFrameObservation(
            Sequence: sequence,
            CompositorRenderedTime: compositorRenderedTime,
            ContentWidth: 1280,
            ContentHeight: 720,
            SurfaceWidth: 1800,
            SurfaceHeight: 1100,
            PixelFormat: DirectXPixelFormat.B8G8R8A8UIntNormalized,
            Sentinel: sentinel,
            Continuity: continuity);
    }

    private static DwmCompositedFrameObservation CreateReleaseFailureObservation(
        long sequence,
        DwmCompositedSentinelObservation sentinel) =>
        new(
            Sequence: sequence,
            CompositorRenderedTime: TimeSpan.FromMilliseconds(sequence * 10),
            ContentWidth: 1280,
            ContentHeight: 720,
            SurfaceWidth: 1800,
            SurfaceHeight: 1100,
            PixelFormat: DirectXPixelFormat.B8G8R8A8UIntNormalized,
            Sentinel: sentinel,
            Continuity: default);
}

[Collection(WgcDwmCompositedAcceptanceCollection.kCollectionName)]
public sealed class WgcDwmCompositedCaptureAcceptanceTests
{
    private const string kObserverPrefix =
        "viewport-transaction-window-resize-observer ";
    private const string kEvidencePrefix =
        "viewport-transaction-window-resize-evidence ";
    private static readonly TimeSpan kStartupTimeout = TimeSpan.FromSeconds(20);
    private static readonly TimeSpan kFrameTimeout = TimeSpan.FromSeconds(8);
    private static readonly TimeSpan kExitTimeout = TimeSpan.FromSeconds(20);
    private static readonly JsonSerializerOptions kSummaryJsonOptions =
        new(JsonSerializerDefaults.Web);
    private readonly ITestOutputHelper output_;

    public WgcDwmCompositedCaptureAcceptanceTests(ITestOutputHelper output)
    {
        output_ = output;
    }

    [WgcDwmCompositedFact]
    public Task Grow_release_matches_the_final_accepted_scene_extent_in_DWM_pixels() =>
        RunResizeReleaseAcceptanceAsync("grow");

    [WgcDwmCompositedFact]
    public Task Shrink_release_matches_the_final_accepted_scene_extent_in_DWM_pixels() =>
        RunResizeReleaseAcceptanceAsync("shrink");

    private async Task RunResizeReleaseAcceptanceAsync(string pattern)
    {
        WgcDwmCompositedAcceptanceGate.RequireEnabled();
        Assert.True(
            GraphicsCaptureSession.IsSupported(),
            "Windows reports that GraphicsCaptureSession is unavailable.");

        var eventName = $"Local\\AshariaStudioWgcDwm-{Guid.NewGuid():N}";
        using var observerReady = new EventWaitHandle(
            initialState: false,
            EventResetMode.AutoReset,
            eventName,
            out var createdNew);
        Assert.True(createdNew);

        await using var editor = EditorAcceptanceProcess.Start(
            "--smoke-viewport-transaction-window-resize",
            "--viewport-window-evidence=continuous",
            $"--viewport-window-pattern={pattern}",
            "--viewport-window-input-hz=30",
            "--viewport-window-input-count=12",
            "--viewport-window-release-policy=immediate-exit",
            $"--viewport-window-observer-ready-event={eventName}");

        var readyLine = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "ready-waiting"),
            kStartupTimeout);
        AssertObserverEvent(readyLine, eventName, expectedExtentRequired: false);

        var windowHandle = editor.ReadMainWindowHandle();
        Assert.NotEqual(0, windowHandle);
        using var recorder = new WgcDwmCompositedRecorder(windowHandle);
        recorder.Start();

        var baseline = await recorder.WaitForExactFrameAsync(
            afterSequence: 0,
            predicate: null,
            kFrameTimeout);
        Assert.Equal(DwmCompositedFrameObservation.EvidenceKind, "wgc-dwm-composited-pixels");
        Assert.True(baseline.PixelEvidenceAvailable);
        Assert.False(baseline.PhysicalDisplayedEvidenceAvailable);

        Assert.True(observerReady.Set());
        _ = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "ready-signaled"),
            kFrameTimeout);

        var releaseImminentLine = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "release-imminent"),
            kExitTimeout);
        var releaseImminent = AssertObserverEvent(
            releaseImminentLine,
            eventName,
            expectedExtentRequired: false);
        var releaseWaitingLine = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "release-waiting"),
            kFrameTimeout);
        var releaseWaiting = AssertObserverEvent(
            releaseWaitingLine,
            eventName,
            expectedExtentRequired: false);
        Assert.True(
            releaseWaiting.CompositorTime >= releaseImminent.CompositorTime,
            "The post-WM_EXITSIZEMOVE release marker preceded the imminent marker.");
        var releaseBaseline = recorder.LatestSequence;
        Assert.True(observerReady.Set());
        _ = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "release-signaled"),
            kFrameTimeout);

        var completionLine = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "completion-waiting"),
            kExitTimeout);
        var completion = AssertObserverEvent(
            completionLine,
            eventName,
            expectedExtentRequired: true);
        var completionBaseline = recorder.LatestSequence;
        var final = await recorder.WaitForExactFrameAsync(
            completionBaseline,
            observation =>
                // Delivery sequence can advance for a frame captured before release.
                // Final evidence must also be newer on the shared compositor/QPC clock.
                observation.CompositorRenderedTime >= completion.CompositorTime &&
                observation.Sentinel.Layout.SceneBounds.Width == completion.ExpectedWidth &&
                observation.Sentinel.Layout.SceneBounds.Height == completion.ExpectedHeight,
            kFrameTimeout);

        Assert.True(observerReady.Set());
        _ = await editor.WaitForOutputLineAsync(
            line => IsObserverPhase(line, "completion-signaled"),
            kFrameTimeout);
        var evidenceLine = await editor.WaitForOutputLineAsync(
            line => line.StartsWith(kEvidencePrefix, StringComparison.Ordinal),
            kFrameTimeout);
        AssertImmediateExitDropEvidence(evidenceLine, pattern);
        var exitCode = await editor.WaitForExitAsync(kExitTimeout);
        Assert.Equal(0, exitCode);

        var observed = recorder.Capture()
            .Where(observation =>
                observation.Sequence >= baseline.Sequence &&
                observation.Sequence <= final.Sequence)
            .ToArray();
        var releaseSelection = WgcDwmCompositedReleaseWindow.Select(
            observed,
            releaseImminent.CompositorTime,
            final.CompositorRenderedTime,
            releaseBaseline);
        var releaseObserved = releaseSelection.Observations;
        Assert.True(observed.Length >= 2, editor.CapturedOutput);
        Assert.NotEmpty(releaseObserved);
        var metrics = recorder.CaptureMetrics();
        var summary = WgcDwmCompositedAcceptanceSummary.Create(
            pattern,
            observed,
            releaseObserved,
            completion.ExpectedWidth,
            completion.ExpectedHeight,
            (releaseWaiting.CompositorTime - releaseImminent.CompositorTime)
                .TotalMilliseconds,
            releaseSelection.DelayedPreReleaseDeliveredAfterBaseline,
            metrics);
        output_.WriteLine(
            $"wgc-dwm-composited-acceptance " +
            $"{JsonSerializer.Serialize(summary, kSummaryJsonOptions)}");

        Assert.All(observed, observation =>
        {
            Assert.True(observation.PixelEvidenceAvailable);
            Assert.False(observation.PhysicalDisplayedEvidenceAvailable);
            if (pattern == "grow")
            {
                Assert.True(
                    observation.IsAcceptableForGrow,
                    $"DWM frame {observation.Sequence} violated the {pattern} pixel contract: " +
                    $"content={observation.ContentWidth}x{observation.ContentHeight}, " +
                    $"sentinelExact={observation.Sentinel.IsExact}, " +
                    $"insets={observation.Sentinel.Insets}, " +
                    $"leftTop={observation.Continuity.LeftTopInsetsMatch}, " +
                    $"rightBottom={observation.Continuity.RightBottomInsetsMatch}, " +
                    $"rightBottomDoNotDecrease=" +
                    $"{observation.Continuity.RightBottomInsetsDoNotDecrease}.");
            }
        });
        Assert.All(releaseObserved, observation =>
        {
            var sceneBounds = observation.Sentinel.Layout.SceneBounds;
            Assert.True(
                observation.IsExact &&
                sceneBounds.Width == completion.ExpectedWidth &&
                sceneBounds.Height == completion.ExpectedHeight,
                $"DWM release frame {observation.Sequence} was not exact: " +
                $"content={observation.ContentWidth}x{observation.ContentHeight}, " +
                $"scene={sceneBounds.Width}x{sceneBounds.Height}, " +
                $"accepted={completion.ExpectedWidth}x{completion.ExpectedHeight}, " +
                $"sentinelLocated={observation.Sentinel.Located}, " +
                $"sentinelBlank={observation.Sentinel.IsBlank}, " +
                $"blockSizes={observation.Sentinel.HasExactBlockSizes}, " +
                $"alignedCorners={observation.Sentinel.HasAlignedCorners}, " +
                $"rightGap={observation.Continuity.RightGapPixels}, " +
                $"bottomGap={observation.Continuity.BottomGapPixels}.");
        });
        Assert.Equal(0, summary.ReleaseNonExact);
        Assert.Equal(0, summary.ReleaseGap);
        Assert.Equal(0, summary.ReleaseBlank);
        Assert.Equal(0, summary.ReleaseCrop);
        Assert.Equal(0, summary.ReleaseStretch);
        Assert.Equal(0, summary.ReleaseAcceptedExtentMismatch);
        Assert.True(
            observed.Select(observation => new
            {
                observation.Sentinel.Layout.SceneBounds.Width,
                observation.Sentinel.Layout.SceneBounds.Height,
            })
                .Distinct()
                .Count() >= 2,
            $"The WGC evidence window did not observe both initial and final {pattern} " +
            "Scene extents.");
        Assert.Equal(completion.ExpectedWidth, final.Sentinel.Layout.SceneBounds.Width);
        Assert.Equal(completion.ExpectedHeight, final.Sentinel.Layout.SceneBounds.Height);

        Assert.True(metrics.EnvelopeWidth >= baseline.ContentWidth + 1024);
        Assert.True(metrics.EnvelopeHeight >= baseline.ContentHeight + 512);
        Assert.True(metrics.HasNoDrops, metrics.ToString());
    }

    private static void AssertImmediateExitDropEvidence(string line, string pattern)
    {
        using var document = JsonDocument.Parse(line[kEvidencePrefix.Length..]);
        var root = document.RootElement;
        Assert.Equal(pattern, root.GetProperty("pattern").GetString());
        Assert.Equal("immediate-exit", root.GetProperty("releasePolicy").GetString());
        var win32 = root.GetProperty("win32");
        Assert.False(win32.GetProperty("rawFinalProposalAccepted").GetBoolean());
        Assert.True(win32.GetProperty("rawFinalProposalDropped").GetBoolean());
        Assert.True(win32.GetProperty("pendingRawFinalBeforeExit").GetBoolean());
        Assert.Equal("Cancelled", win32.GetProperty("finalRetirementResult").GetString());
        var lag = win32.GetProperty("rawProposalLagPx");
        Assert.NotEqual(
            0,
            Math.Abs(lag.GetProperty("width").GetInt32()) +
            Math.Abs(lag.GetProperty("height").GetInt32()));
    }

    private static bool IsObserverPhase(string line, string expectedPhase)
    {
        if (!line.StartsWith(kObserverPrefix, StringComparison.Ordinal))
        {
            return false;
        }

        using var document = JsonDocument.Parse(line[kObserverPrefix.Length..]);
        return string.Equals(
            document.RootElement.GetProperty("phase").GetString(),
            expectedPhase,
            StringComparison.Ordinal);
    }

    private static ObserverReceipt AssertObserverEvent(
        string line,
        string expectedEventName,
        bool expectedExtentRequired)
    {
        using var document = JsonDocument.Parse(line[kObserverPrefix.Length..]);
        var root = document.RootElement;
        Assert.Equal(expectedEventName, root.GetProperty("eventName").GetString());
        var qpc = root.GetProperty("qpc").GetInt64();
        var frequency = root.GetProperty("frequency").GetInt64();
        Assert.True(qpc > 0);
        var compositorTime = WgcDwmCompositedReleaseWindow.ConvertQpcToTimeSpan(
            qpc,
            frequency);
        var extent = root.GetProperty("expectedSceneExtent");
        if (!expectedExtentRequired)
        {
            Assert.Equal(JsonValueKind.Null, extent.ValueKind);
            return new ObserverReceipt(compositorTime, 0, 0);
        }

        Assert.Equal(JsonValueKind.Object, extent.ValueKind);
        var width = extent.GetProperty("width").GetInt32();
        var height = extent.GetProperty("height").GetInt32();
        Assert.True(width > 0);
        Assert.True(height > 0);
        return new ObserverReceipt(compositorTime, width, height);
    }

    private readonly record struct ObserverReceipt(
        TimeSpan CompositorTime,
        int ExpectedWidth,
        int ExpectedHeight);
}

internal sealed record WgcDwmCompositedAcceptanceSummary(
    string Pattern,
    string EvidenceKind,
    bool PixelEvidenceAvailable,
    bool PhysicalDisplayedEvidenceAvailable,
    bool EveryDwmFrameCapturedEvidenceAvailable,
    int ObservedFrameCount,
    int ExactFrameCount,
    int PixelContractViolationFrameCount,
    int AllowedGapFrameCount,
    double AllowedGapFrameRatio,
    int MaximumRightGapPixels,
    int MaximumBottomGapPixels,
    int ReleaseObserved,
    int ReleaseExact,
    int ReleaseNonExact,
    int ReleaseGap,
    int ReleaseBlank,
    int ReleaseCrop,
    int ReleaseStretch,
    int ReleaseAcceptedExtentMismatch,
    double ReleaseMarkerDeltaMilliseconds,
    int DelayedPreReleaseDeliveredAfterBaseline,
    int WgcDeliveredCadenceSampleCount,
    double? WgcDeliveredFrameRateHz,
    double? WgcDeliveredIntervalP95Milliseconds,
    double? WgcDeliveredIntervalMaximumMilliseconds,
    WgcDwmCompositedRecorderMetrics RecorderMetrics)
{
    public static WgcDwmCompositedAcceptanceSummary Create(
        string pattern,
        IReadOnlyList<DwmCompositedFrameObservation> observations,
        IReadOnlyList<DwmCompositedFrameObservation> releaseObservations,
        int expectedReleaseWidth,
        int expectedReleaseHeight,
        double releaseMarkerDeltaMilliseconds,
        int delayedPreReleaseDeliveredAfterBaseline,
        WgcDwmCompositedRecorderMetrics recorderMetrics)
    {
        ArgumentException.ThrowIfNullOrEmpty(pattern);
        ArgumentNullException.ThrowIfNull(observations);
        ArgumentNullException.ThrowIfNull(releaseObservations);
        if (expectedReleaseWidth <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(expectedReleaseWidth));
        }
        if (expectedReleaseHeight <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(expectedReleaseHeight));
        }
        if (!double.IsFinite(releaseMarkerDeltaMilliseconds) ||
            releaseMarkerDeltaMilliseconds < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(releaseMarkerDeltaMilliseconds));
        }
        if (delayedPreReleaseDeliveredAfterBaseline < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(delayedPreReleaseDeliveredAfterBaseline));
        }
        var allowsGrowGap = pattern switch
        {
            "grow" => true,
            "shrink" => false,
            _ => throw new ArgumentOutOfRangeException(
                nameof(pattern),
                pattern,
                "The WGC acceptance summary supports grow and shrink cases."),
        };
        if (observations.Count == 0)
        {
            throw new ArgumentException(
                "At least one WGC observation is required.",
                nameof(observations));
        }
        if (releaseObservations.Count == 0)
        {
            throw new ArgumentException(
                "At least one release-phase WGC observation is required.",
                nameof(releaseObservations));
        }

        var ordered = observations.OrderBy(static observation => observation.Sequence).ToArray();
        var releaseOrdered = releaseObservations
            .OrderBy(static observation => observation.Sequence)
            .ToArray();
        var allowedGapFrames = ordered
            .Where(static observation => observation.IsAllowedGrowGap)
            .ToArray();
        var intervals = ordered
            .Zip(
                ordered.Skip(1),
                static (first, second) =>
                    (second.CompositorRenderedTime - first.CompositorRenderedTime)
                    .TotalMilliseconds)
            .Where(static interval => interval > 0)
            .OrderBy(static interval => interval)
            .ToArray();
        var elapsed =
            ordered[^1].CompositorRenderedTime - ordered[0].CompositorRenderedTime;
        var deliveredFrameRate = ordered.Length > 1 && elapsed > TimeSpan.Zero
            ? (ordered.Length - 1) / elapsed.TotalSeconds
            : (double?)null;

        return new WgcDwmCompositedAcceptanceSummary(
            Pattern: pattern,
            EvidenceKind: DwmCompositedFrameObservation.EvidenceKind,
            PixelEvidenceAvailable: true,
            PhysicalDisplayedEvidenceAvailable: false,
            EveryDwmFrameCapturedEvidenceAvailable: false,
            ObservedFrameCount: ordered.Length,
            ExactFrameCount: ordered.Count(static observation => observation.IsExact),
            PixelContractViolationFrameCount: ordered.Count(observation =>
                allowsGrowGap
                    ? !observation.IsAcceptableForGrow
                    : !observation.IsExact),
            AllowedGapFrameCount: allowedGapFrames.Length,
            AllowedGapFrameRatio: (double)allowedGapFrames.Length / ordered.Length,
            MaximumRightGapPixels: allowedGapFrames.Length == 0
                ? 0
                : allowedGapFrames.Max(static observation =>
                    observation.Continuity.RightGapPixels),
            MaximumBottomGapPixels: allowedGapFrames.Length == 0
                ? 0
                : allowedGapFrames.Max(static observation =>
                    observation.Continuity.BottomGapPixels),
            ReleaseObserved: releaseOrdered.Length,
            ReleaseExact: releaseOrdered.Count(static observation =>
                observation.IsExact),
            ReleaseNonExact: releaseOrdered.Count(static observation =>
                !observation.IsExact),
            ReleaseGap: releaseOrdered.Count(static observation =>
                observation.IsAllowedGrowGap ||
                observation.Continuity.RightGapPixels > 0 ||
                observation.Continuity.BottomGapPixels > 0),
            ReleaseBlank: releaseOrdered.Count(static observation =>
                observation.Sentinel.IsBlank),
            ReleaseCrop: releaseOrdered.Count(static observation =>
                !observation.IsExact &&
                !observation.Sentinel.IsBlank &&
                (!observation.Sentinel.Located ||
                 !observation.Sentinel.HasAlignedCorners)),
            ReleaseStretch: releaseOrdered.Count(static observation =>
                !observation.IsExact &&
                observation.Sentinel.Located &&
                !observation.Sentinel.HasExactBlockSizes),
            ReleaseAcceptedExtentMismatch: releaseOrdered.Count(observation =>
                observation.Sentinel.Layout.SceneBounds.Width != expectedReleaseWidth ||
                observation.Sentinel.Layout.SceneBounds.Height != expectedReleaseHeight),
            ReleaseMarkerDeltaMilliseconds: releaseMarkerDeltaMilliseconds,
            DelayedPreReleaseDeliveredAfterBaseline:
                delayedPreReleaseDeliveredAfterBaseline,
            WgcDeliveredCadenceSampleCount: intervals.Length,
            WgcDeliveredFrameRateHz: deliveredFrameRate,
            WgcDeliveredIntervalP95Milliseconds: Percentile95(intervals),
            WgcDeliveredIntervalMaximumMilliseconds: intervals.Length == 0
                ? null
                : intervals[^1],
            RecorderMetrics: recorderMetrics);
    }

    private static double? Percentile95(IReadOnlyList<double> sortedValues)
    {
        if (sortedValues.Count == 0)
        {
            return null;
        }

        var index = Math.Max(0, (int)Math.Ceiling(sortedValues.Count * 0.95) - 1);
        return sortedValues[index];
    }
}

internal sealed class EditorAcceptanceProcess : IAsyncDisposable
{
    private static readonly TimeSpan kPollInterval = TimeSpan.FromMilliseconds(20);
    private readonly object gate_ = new();
    private readonly List<string> standardOutput_ = [];
    private readonly List<string> standardError_ = [];
    private readonly Process process_;
    private bool disposed_;

    private EditorAcceptanceProcess(Process process)
    {
        process_ = process;
        process_.OutputDataReceived += (_, args) => AddLine(standardOutput_, args.Data);
        process_.ErrorDataReceived += (_, args) => AddLine(standardError_, args.Data);
        process_.BeginOutputReadLine();
        process_.BeginErrorReadLine();
    }

    public string CapturedOutput
    {
        get
        {
            lock (gate_)
            {
                return string.Join(Environment.NewLine, standardOutput_.Concat(standardError_));
            }
        }
    }

    public static EditorAcceptanceProcess Start(params string[] arguments)
    {
        var executablePath = Path.Combine(AppContext.BaseDirectory, "Editor.exe");
        if (!File.Exists(executablePath))
        {
            throw new FileNotFoundException(
                "The Windows capture project reference did not copy the Editor apphost.",
                executablePath);
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = executablePath,
            WorkingDirectory = AppContext.BaseDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        var process = new Process { StartInfo = startInfo };
        if (!process.Start())
        {
            process.Dispose();
            throw new InvalidOperationException("The production Editor process did not start.");
        }
        return new EditorAcceptanceProcess(process);
    }

    public nint ReadMainWindowHandle()
    {
        process_.Refresh();
        if (process_.HasExited)
        {
            throw new InvalidOperationException(
                $"Editor exited before WGC attached.{Environment.NewLine}{CapturedOutput}");
        }
        return process_.MainWindowHandle;
    }

    public async Task<string> WaitForOutputLineAsync(
        Func<string, bool> predicate,
        TimeSpan timeout)
    {
        using var deadline = new CancellationTokenSource(timeout);
        try
        {
            while (true)
            {
                lock (gate_)
                {
                    var match = standardOutput_.FirstOrDefault(predicate);
                    if (match is not null)
                    {
                        return match;
                    }
                }

                if (process_.HasExited)
                {
                    throw new InvalidOperationException(
                        $"Editor exited with code {process_.ExitCode} before the expected output." +
                        $"{Environment.NewLine}{CapturedOutput}");
                }
                await Task.Delay(kPollInterval, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"Editor did not produce the expected output within {timeout}." +
                $"{Environment.NewLine}{CapturedOutput}");
        }
    }

    public async Task<int> WaitForExitAsync(TimeSpan timeout)
    {
        using var deadline = new CancellationTokenSource(timeout);
        try
        {
            await process_.WaitForExitAsync(deadline.Token);
            process_.WaitForExit();
            return process_.ExitCode;
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"Editor did not exit within {timeout}.{Environment.NewLine}{CapturedOutput}");
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (disposed_)
        {
            return;
        }

        disposed_ = true;
        try
        {
            if (!process_.HasExited)
            {
                process_.Kill(entireProcessTree: true);
                using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(5));
                await process_.WaitForExitAsync(deadline.Token);
            }
        }
        finally
        {
            process_.Dispose();
        }
    }

    private void AddLine(List<string> lines, string? line)
    {
        if (line is null)
        {
            return;
        }

        lock (gate_)
        {
            lines.Add(line);
        }
    }
}
