using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Diagnostics;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Asharia.Studio.Presentation.Avalonia.Windowing;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace Editor.Shell.Views.Docking;

internal readonly record struct EditorDockPresentationLayoutHostMetrics(
    ulong ObservedRequests,
    ulong ProcessedRequests,
    ulong QueuedSupersededRequests,
    ulong PublishedRequests,
    ulong FailedRequests,
    int MaximumPendingWork,
    bool HasActive,
    bool HasQueued,
    Size CommittedSize,
    Size RequestedSize);

/// <summary>
/// Coordinates an owned dock tree with an optional platform precommit adapter. The outer layout
/// remains at the last exact size while replacement viewport surfaces are prepared; the top-level
/// layout and all exact fronts then advance in one UI transaction. Resize sources that cannot be
/// intercepted before layout retain the ordinary exact-only fallback.
/// </summary>
public sealed class EditorDockPresentationLayoutHost : Decorator,
    IInteractiveTopLevelResizeSink
{
    private readonly IStudioDiagnosticHub diagnostics_;
    private readonly ViewportPresentationTransactionTelemetry transactionTelemetry_ = new();
    private readonly ViewportPresentationTransactionCoordinator presentationTransactions_;
    private TaskCompletionSource idleCompletion_ = CreateCompletedIdleCompletion();
    private CancellationTokenSource? attachmentCancellation_;
    private IInteractiveTopLevelResizeAttachment? interactiveResizeAttachment_;
    private LayoutRequest? queuedRequest_;
    private Task<ViewportPresentationTransactionReport>? latestRetirementCompletion_;
    private Size committedSize_;
    private Size requestedSize_;
    private ulong nextRequestSequence_;
    private ulong nextPresentationTransactionId_;
    private ulong observedRequests_;
    private ulong processedRequests_;
    private ulong queuedSupersededRequests_;
    private ulong publishedRequests_;
    private ulong failedRequests_;
    private int maximumPendingWork_;
    private bool hasCommittedSize_;
    private bool isAttached_;
    private bool isDrainPosted_;
    private bool isDrainRunning_;
    private Size? applyingCommittedSize_;

    public EditorDockPresentationLayoutHost()
        : this(StudioAvaloniaDiagnosticHubResolver.RequireCurrent())
    {
    }

    internal EditorDockPresentationLayoutHost(IStudioDiagnosticHub diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        diagnostics_ = diagnostics;
        ClipToBounds = true;
        presentationTransactions_ = new ViewportPresentationTransactionCoordinator(
            diagnostics,
            transactionTelemetry_);
    }

    internal ViewportPresentationTransactionTelemetry PresentationTransactionTelemetry =>
        transactionTelemetry_;

    internal IStudioDiagnosticHub DiagnosticHub => diagnostics_;

    internal Task<ViewportPresentationTransactionReport>? LatestRetirementCompletion =>
        latestRetirementCompletion_;

    internal Task WhenIdleAsync()
    {
        Dispatcher.UIThread.VerifyAccess();
        return idleCompletion_.Task;
    }

    internal EditorDockPresentationLayoutHostMetrics CaptureMetrics()
    {
        Dispatcher.UIThread.VerifyAccess();
        return new EditorDockPresentationLayoutHostMetrics(
            observedRequests_,
            processedRequests_,
            queuedSupersededRequests_,
            publishedRequests_,
            failedRequests_,
            maximumPendingWork_,
            isDrainRunning_,
            queuedRequest_ is not null,
            committedSize_,
            requestedSize_);
    }

    internal ViewportPresentationTransactionTelemetryMetrics CaptureTransactionTelemetry() =>
        transactionTelemetry_.Capture(Stopwatch.GetTimestamp());

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttached_ = true;
        attachmentCancellation_ = new CancellationTokenSource();
        if (Application.Current is IInteractiveTopLevelResizeAdapterProvider provider &&
            provider.InteractiveTopLevelResizeAdapterFactory is { } factory &&
            TopLevel.GetTopLevel(this) is { } topLevel)
        {
            interactiveResizeAttachment_ = factory.TryAttach(topLevel, this);
        }
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached_ = false;
        queuedRequest_ = null;
        isDrainPosted_ = false;
        hasCommittedSize_ = false;
        interactiveResizeAttachment_?.Dispose();
        interactiveResizeAttachment_ = null;
        var cancellation = attachmentCancellation_;
        cancellation?.Cancel();
        attachmentCancellation_ = null;
        if (!isDrainRunning_)
        {
            cancellation?.Dispose();
            idleCompletion_.TrySetResult();
        }
        base.OnDetachedFromVisualTree(e);
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        if (Child is not { } child)
        {
            return default;
        }

        var normalized = NormalizeAvailableSize(availableSize, child.DesiredSize);
        var childSize = !IsRenderable(normalized) && hasCommittedSize_
            ? committedSize_
            : normalized;
        child.Measure(childSize);
        return child.DesiredSize;
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        var normalized = NormalizeFinalSize(finalSize);
        if (applyingCommittedSize_ is { } applyingSize)
        {
            ArrangeChild(applyingSize);
            return finalSize;
        }
        if (!hasCommittedSize_)
        {
            hasCommittedSize_ = true;
            committedSize_ = normalized;
            requestedSize_ = normalized;
            ArrangeChild(normalized);
            return finalSize;
        }

        if (!IsRenderable(normalized))
        {
            // A minimized/collapsed host has no visible pixels. Retaining the last exact child
            // avoids destroying its front and lets a same-size restore remain exact.
            ArrangeChild(committedSize_);
            return finalSize;
        }

        // A non-precommitted source (Snap, maximize, programmatic resize or another platform) has
        // already changed the outer layout. Do not retain/crop the old dock tree and pretend the
        // exact transaction succeeded; let viewport controls use their explicit mismatch fallback.
        committedSize_ = normalized;
        requestedSize_ = normalized;
        ArrangeChild(normalized);
        return finalSize;
    }

    internal static bool AreLayoutSizesEqual(Size first, Size second) =>
        Math.Abs(first.Width - second.Width) <= LayoutHelper.LayoutEpsilon &&
        Math.Abs(first.Height - second.Height) <= LayoutHelper.LayoutEpsilon;

    internal bool TryQueuePrecommittedWindowResize(
        Size targetSize,
        IInteractiveTopLevelResizeCommit outerCommit)
    {
        ArgumentNullException.ThrowIfNull(outerCommit);
        Dispatcher.UIThread.VerifyAccess();
        var normalized = NormalizeFinalSize(targetSize);
        if (!isAttached_ || !hasCommittedSize_ || !IsRenderable(normalized))
        {
            return false;
        }

        QueueLatest(normalized, outerCommit, postDrain: false);
        if (!isDrainRunning_)
        {
            StartDrain();
        }
        return true;
    }

    internal bool CanStartPrecommittedWindowResize()
    {
        Dispatcher.UIThread.VerifyAccess();
        if (!isAttached_ || !hasCommittedSize_)
        {
            return false;
        }

        var viewports = EnumerateVisibleViewports().ToArray();
        return viewports.Length > 0 &&
            viewports.All(static viewport =>
                viewport.PresentationGeometryMetrics.CurrentSurfaceIsExact);
    }

    private void QueueLatest(
        Size targetSize,
        IInteractiveTopLevelResizeCommit? outerCommit = null,
        bool postDrain = true)
    {
        requestedSize_ = targetSize;
        observedRequests_ = checked(observedRequests_ + 1);
        var request = new LayoutRequest(
            checked(++nextRequestSequence_),
            targetSize,
            outerCommit);
        if (queuedRequest_ is not null)
        {
            queuedSupersededRequests_ = checked(queuedSupersededRequests_ + 1);
        }
        queuedRequest_ = request;
        if (idleCompletion_.Task.IsCompleted)
        {
            idleCompletion_ = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
        }
        maximumPendingWork_ = Math.Max(
            maximumPendingWork_,
            (isDrainRunning_ ? 1 : 0) + 1);
        if (postDrain)
        {
            PostDrainIfNeeded();
        }
    }

    private void PostDrainIfNeeded()
    {
        if (!isAttached_ || isDrainRunning_ || isDrainPosted_ || queuedRequest_ is null)
        {
            return;
        }

        isDrainPosted_ = true;
        Dispatcher.UIThread.Post(
            StartDrain,
            DispatcherPriority.Render);
    }

    private void StartDrain()
    {
        Dispatcher.UIThread.VerifyAccess();
        isDrainPosted_ = false;
        if (!isAttached_ || isDrainRunning_ || queuedRequest_ is null ||
            attachmentCancellation_ is not { } cancellation)
        {
            return;
        }

        isDrainRunning_ = true;
        _ = DrainAsync(cancellation);
    }

    private async Task DrainAsync(CancellationTokenSource cancellation)
    {
        var cancellationToken = cancellation.Token;
        try
        {
            while (isAttached_ && !cancellationToken.IsCancellationRequested &&
                   queuedRequest_ is { } request)
            {
                queuedRequest_ = null;
                processedRequests_ = checked(processedRequests_ + 1);
                await ProcessLatestAsync(request, cancellationToken);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            failedRequests_ = checked(failedRequests_ + 1);
            Trace.TraceError(
                "Dock presentation layout transaction failed: {0}",
                exception);
        }
        finally
        {
            isDrainRunning_ = false;
            if (cancellation.IsCancellationRequested ||
                !ReferenceEquals(attachmentCancellation_, cancellation))
            {
                cancellation.Dispose();
            }
            if (queuedRequest_ is null)
            {
                idleCompletion_.TrySetResult();
            }
            else
            {
                PostDrainIfNeeded();
            }
        }
    }

    private async Task ProcessLatestAsync(
        LayoutRequest request,
        CancellationToken cancellationToken)
    {
        Dispatcher.UIThread.VerifyAccess();
        cancellationToken.ThrowIfCancellationRequested();
        if (!isAttached_ || Child is null)
        {
            return;
        }
        if (request.OuterCommit?.IsCurrent() == false)
        {
            ResetRequestedSizeIfNoSuccessor(request);
            return;
        }
        if (request.OuterCommit is null &&
            AreLayoutSizesEqual(request.TargetSize, committedSize_))
        {
            return;
        }

        // A zero-sized top-level interval (for example minimization) cannot produce an exact
        // external image. Keep the retained front owned and let the next renderable request
        // supersede it instead of entering the ordinary blank fallback.
        if (!IsRenderable(request.TargetSize))
        {
            return;
        }

        var frontCommitted = committedSize_;
        if (!TryProbeViewportTargets(
                request.TargetSize,
                frontCommitted,
                out var viewportTargets))
        {
            failedRequests_ = checked(failedRequests_ + 1);
            await RetryAfterCompositionAsync(request, cancellationToken);
            return;
        }

        cancellationToken.ThrowIfCancellationRequested();
        var changedExactTargets = viewportTargets
            .Where(static target => target.TargetExtent != target.FrontExtent)
            .ToArray();
        if (changedExactTargets.Length == 0)
        {
            if (!TryApplyOuterOnly(
                    request,
                    viewportTargets.Select(static target => target.Control)))
            {
                failedRequests_ = checked(failedRequests_ + 1);
                await RetryAfterCompositionAsync(request, cancellationToken);
                return;
            }

            request.OuterCommit?.Accept(hasPublishedViewportBatch: false);
            publishedRequests_ = checked(publishedRequests_ + 1);
            return;
        }

        var transactionId = checked(++nextPresentationTransactionId_);
        var participants = changedExactTargets
            .Select((target, index) => new ViewportPresentationParticipant(
                $"workspace:{request.Sequence}:{index}",
                target.Control,
                target.TargetExtent))
            .ToArray();
        var rollbackCommitted = frontCommitted;
        var hasApplyBaseline = false;
        var execution = await presentationTransactions_.ExecuteAsync(
            new ViewportPresentationTransactionRequest(
                transactionId,
                participants,
                Stopwatch.GetTimestamp()),
            () =>
            {
                rollbackCommitted = committedSize_;
                hasApplyBaseline = true;
                ApplyCommittedLayout(
                    request.TargetSize,
                    request.OuterCommit is { } outerCommit
                        ? outerCommit.Apply
                        : null,
                    viewportTargets.Select(static target => target.Control));
            },
            () => ApplyCommittedLayout(
                hasApplyBaseline ? rollbackCommitted : committedSize_,
                request.OuterCommit is { } outerCommit
                    ? outerCommit.Rollback
                    : null),
            cancellationToken);
        latestRetirementCompletion_ = execution.RetirementCompletion;
        if (!execution.Published)
        {
            failedRequests_ = checked(failedRequests_ + 1);
            await RetryAfterCompositionAsync(request, cancellationToken);
            return;
        }

        request.OuterCommit?.Accept(hasPublishedViewportBatch: true);
        publishedRequests_ = checked(publishedRequests_ + 1);
        _ = ObserveTransactionCompletionAsync(execution);
    }

    private bool TryProbeViewportTargets(
        Size targetSize,
        Size frontCommitted,
        out IReadOnlyList<ViewportLayoutTarget> targets)
    {
        var controls = EnumerateVisibleViewports().ToArray();
        if (controls.Length == 0)
        {
            targets = Array.Empty<ViewportLayoutTarget>();
            return true;
        }

        var probes = new List<ViewportLayoutProbe>(controls.Length);
        try
        {
            foreach (var control in controls)
            {
                var metrics = control.PresentationGeometryMetrics;
                if (!metrics.CurrentSurfaceIsExact)
                {
                    targets = Array.Empty<ViewportLayoutTarget>();
                    return false;
                }
                probes.Add(new ViewportLayoutProbe(
                    control,
                    control.BeginPresentationLayoutProbe(),
                    metrics.LastPanelExtent));
            }

            ArrangeChild(targetSize);
            var captured = new List<ViewportLayoutTarget>(probes.Count);
            foreach (var probe in probes)
            {
                if (!probe.Probe.TryGetExactPixelExtent(out var extent) ||
                    extent.Width == 0 || extent.Height == 0)
                {
                    targets = Array.Empty<ViewportLayoutTarget>();
                    return false;
                }

                captured.Add(new ViewportLayoutTarget(
                    probe.Control,
                    probe.FrontExtent,
                    extent));
            }

            targets = captured;
            return true;
        }
        finally
        {
            try
            {
                ArrangeChild(frontCommitted);
            }
            finally
            {
                for (var index = probes.Count - 1; index >= 0; index--)
                {
                    probes[index].Probe.Dispose();
                }
            }
        }
    }

    private async Task RetryAfterCompositionAsync(
        LayoutRequest request,
        CancellationToken cancellationToken)
    {
        if (request.RetryCount >= 2 ||
            request.OuterCommit?.IsCurrent() == false ||
            !AreLayoutSizesEqual(request.TargetSize, requestedSize_) ||
            EnumerateVisibleViewports().FirstOrDefault() is not { } viewport)
        {
            ResetRequestedSizeIfNoSuccessor(request);
            return;
        }

        await viewport.RequestPresentationBatchRendered().WaitAsync(cancellationToken);
        if (!isAttached_ || cancellationToken.IsCancellationRequested ||
            !AreLayoutSizesEqual(request.TargetSize, requestedSize_) ||
            queuedRequest_ is not null)
        {
            return;
        }

        queuedRequest_ = request with { RetryCount = request.RetryCount + 1 };
    }

    private void ResetRequestedSizeIfNoSuccessor(LayoutRequest request)
    {
        if (queuedRequest_ is null &&
            AreLayoutSizesEqual(request.TargetSize, requestedSize_))
        {
            requestedSize_ = committedSize_;
        }
    }

    private bool TryApplyOuterOnly(
        LayoutRequest request,
        IEnumerable<ViewportCompositionControl> expectedViewports)
    {
        var rollbackCommitted = committedSize_;
        try
        {
            ApplyCommittedLayout(
                request.TargetSize,
                request.OuterCommit is { } outerCommit
                    ? outerCommit.Apply
                    : null,
                expectedViewports);
            return true;
        }
        catch (Exception applyException)
        {
            try
            {
                ApplyCommittedLayout(
                    rollbackCommitted,
                    request.OuterCommit is { } outerCommit
                        ? outerCommit.Rollback
                        : null);
            }
            catch (Exception rollbackException)
            {
                throw new AggregateException(
                    "The Window layout commit and rollback both failed.",
                    applyException,
                    rollbackException);
            }

            Trace.TraceError(
                "Window layout commit rolled back before viewport publication: {0}",
                applyException);
            return false;
        }
    }

    private IEnumerable<ViewportCompositionControl> EnumerateVisibleViewports()
    {
        if (Child is not { } child)
        {
            yield break;
        }

        if (child is ViewportCompositionControl rootViewport &&
            IsVisibleViewport(rootViewport))
        {
            yield return rootViewport;
        }

        foreach (var viewport in child.GetVisualDescendants().OfType<ViewportCompositionControl>())
        {
            if (IsVisibleViewport(viewport))
            {
                yield return viewport;
            }
        }
    }

    private static bool IsVisibleViewport(ViewportCompositionControl viewport) =>
        viewport.IsEffectivelyVisible &&
        viewport.Bounds.Width > 0 &&
        viewport.Bounds.Height > 0;

    private void ApplyCommittedLayout(
        Size size,
        Action? applyOuterLayout = null,
        IEnumerable<ViewportCompositionControl>? expectedChangedViewports = null)
    {
        var precedingSize = committedSize_;
        applyingCommittedSize_ = size;
        committedSize_ = size;
        hasCommittedSize_ = true;
        try
        {
            applyOuterLayout?.Invoke();
            ArrangeChild(size);
            if (applyOuterLayout is not null &&
                !AreLayoutSizesEqual(Bounds.Size, size))
            {
                throw new InvalidOperationException(
                    $"The committed Window layout produced {Bounds.Size} instead of {size}.");
            }
            if (expectedChangedViewports is not null)
            {
                ValidateVisibleEndpointSet(expectedChangedViewports);
            }
        }
        catch (Exception applyException)
        {
            committedSize_ = precedingSize;
            try
            {
                ArrangeChild(precedingSize);
            }
            catch (Exception restoreException)
            {
                throw new AggregateException(
                    "The committed dock layout and its in-turn restoration both failed.",
                    applyException,
                    restoreException);
            }
            throw;
        }
        finally
        {
            applyingCommittedSize_ = null;
        }
    }

    private void ValidateVisibleEndpointSet(
        IEnumerable<ViewportCompositionControl> expectedChangedViewports)
    {
        var expected = new HashSet<ViewportCompositionControl>(
            expectedChangedViewports,
            ReferenceEqualityComparer.Instance);
        var actual = EnumerateVisibleViewports().ToArray();
        if (actual.Length != expected.Count ||
            expected.Any(viewport => !actual.Contains(viewport, ReferenceEqualityComparer.Instance)))
        {
            throw new InvalidOperationException(
                "The visible viewport endpoint set changed during Window resize preparation.");
        }
    }

    private void ArrangeChild(Size size)
    {
        if (Child is not { } child)
        {
            return;
        }

        child.Measure(size);
        child.Arrange(new Rect(size));
    }

    private static async Task ObserveTransactionCompletionAsync(
        ViewportPresentationTransactionExecution execution)
    {
        var report = await execution.Completion;
        if (!report.Succeeded)
        {
            Trace.TraceError(
                "Workspace viewport presentation transaction {0} completed as {1}: {2}",
                report.TransactionId,
                report.Result,
                report.Failure);
        }

        var retirement = await execution.RetirementCompletion;
        if (!retirement.Succeeded)
        {
            Trace.TraceError(
                "Workspace viewport presentation transaction {0} retired as {1}: {2}",
                retirement.TransactionId,
                retirement.Result,
                retirement.Failure);
        }
    }

    private static Size NormalizeAvailableSize(Size available, Size desired) => new(
        NormalizeAvailableDimension(available.Width, desired.Width),
        NormalizeAvailableDimension(available.Height, desired.Height));

    private static double NormalizeAvailableDimension(double available, double desired) =>
        double.IsFinite(available) ? Math.Max(0, available) : Math.Max(0, desired);

    private static Size NormalizeFinalSize(Size size) => new(
        double.IsFinite(size.Width) ? Math.Max(0, size.Width) : 0,
        double.IsFinite(size.Height) ? Math.Max(0, size.Height) : 0);

    private static bool IsRenderable(Size size) => size.Width > 0 && size.Height > 0;

    private static TaskCompletionSource CreateCompletedIdleCompletion()
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        completion.SetResult();
        return completion;
    }

    Size IInteractiveTopLevelResizeSink.CurrentWorkspaceSize => Bounds.Size;

    bool IInteractiveTopLevelResizeSink.CanStartPrecommittedResize() =>
        CanStartPrecommittedWindowResize();

    bool IInteractiveTopLevelResizeSink.TryQueuePrecommittedResize(
        Size targetSize,
        IInteractiveTopLevelResizeCommit outerCommit) =>
        TryQueuePrecommittedWindowResize(targetSize, outerCommit);

    private readonly record struct LayoutRequest(
        ulong Sequence,
        Size TargetSize,
        IInteractiveTopLevelResizeCommit? OuterCommit,
        int RetryCount = 0);

    private sealed record ViewportLayoutProbe(
        ViewportCompositionControl Control,
        ViewportPresentationLayoutProbe Probe,
        ViewportExtent FrontExtent);

    private sealed record ViewportLayoutTarget(
        ViewportCompositionControl Control,
        ViewportExtent FrontExtent,
        ViewportExtent TargetExtent);
}
