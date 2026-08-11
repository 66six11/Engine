using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Avalonia;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal enum ViewportCompositionControlTestPoint
{
    BeforeSurfaceCreate,
    BeforeStreamOpen,
    BeforeNativeSubmit,
    AfterNativeSubmit,
    AfterLeaseAcquired,
    BeforeImageImport,
    AfterImageImported,
    AfterWaitSemaphoreImported,
    AfterSignalSemaphoreImported,
    BeforeSurfaceUpdate,
    AfterSurfaceUpdateSubmitted,
    BeforeOldSurfaceDispose,
    BeforeApplySurface,
    AfterApplySurface,
    BeforeApplySize,
    AfterApplySize,
    BeforeApplyOpacity,
    AfterApplyOpacity,
    BeforeRestoreSurface,
    BeforeRestoreSize,
    BeforeRestoreOpacity,
}

/// <summary>
/// Process-smoke-only endpoint fault seam. Production construction never supplies hooks.
/// Hooks execute at ownership boundaries so a failure must still leave every acquired resource
/// with either the prepared operation, the retirement path, or the quarantine path.
/// </summary>
internal sealed class ViewportCompositionControlTestHooks
{
    public bool EnableFlashSentinelCorners { get; init; }

    public bool EnableSceneMeshEvidence { get; init; }

    internal ViewportRenderDiagnosticOverlay DiagnosticOverlay
    {
        get
        {
            var overlay = ViewportRenderDiagnosticOverlay.None;
            if (EnableFlashSentinelCorners)
            {
                overlay |= ViewportRenderDiagnosticOverlay.FlashSentinelCorners;
            }
            if (EnableSceneMeshEvidence)
            {
                overlay |= ViewportRenderDiagnosticOverlay.CaptureSceneMeshEvidence;
            }
            return overlay;
        }
    }

    public Func<
        ViewportCompositionControlTestPoint,
        CancellationToken,
        ValueTask>? BeforeStageAsync { get; init; }

    public Action<ViewportCompositionControlTestPoint>? AtSynchronousStage { get; init; }

    public Func<bool>? RejectPreparedValidation { get; init; }

    public Func<Task, Task>? WrapReplacedFrontRetirement { get; init; }

    public Action<ViewportFrameLease>? LeaseAcquired { get; init; }

    public ValueTask BeforeStageAsyncCore(
        ViewportCompositionControlTestPoint point,
        CancellationToken cancellationToken) =>
        BeforeStageAsync?.Invoke(point, cancellationToken) ?? ValueTask.CompletedTask;

    public bool ShouldRejectPreparedValidation() =>
        RejectPreparedValidation?.Invoke() ?? false;

    public void AtSynchronousStageCore(ViewportCompositionControlTestPoint point) =>
        AtSynchronousStage?.Invoke(point);

    public Task WrapReplacedFrontRetirementTask(Task retirement)
    {
        ArgumentNullException.ThrowIfNull(retirement);
        return WrapReplacedFrontRetirement?.Invoke(retirement) ?? retirement;
    }

    public void ObserveLease(ViewportFrameLease lease)
    {
        ArgumentNullException.ThrowIfNull(lease);
        LeaseAcquired?.Invoke(lease);
    }
}

internal readonly record struct ViewportPresentationTestSnapshot(
    bool HasPreparingPresentation,
    bool HasPreparedPresentation,
    int RetiringStreamTaskCount,
    int RetiringSurfaceTaskCount,
    int QuarantinedPresentationCount,
    int QuarantinedStreamCount,
    int QuarantinedSurfaceCount,
    int QuarantinedFrameCount,
    float VisualOpacity,
    object? VisualSurface,
    Vector VisualSize,
    ViewportExtent SurfaceExtent,
    ViewportExtent FrontExtent,
    ViewportExtent CandidateExtent,
    ViewportExtent CurrentExtent,
    ulong GeometryGeneration,
    ulong SurfaceGeneration,
    bool HasExactSurface,
    long CandidateSurfaceCreateAttempts,
    long CandidateSurfacesCreated,
    long CandidateStreamOpenAttempts,
    long CandidateStreamsOpened,
    long CandidateNativeSubmissions,
    long CandidateLeasesAcquired,
    long CandidateImageImportAttempts,
    long CandidateImagesImported,
    long CandidateSurfaceUpdateAttempts,
    long CandidateCleanupCompletions);
