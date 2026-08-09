using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.Docking.Splitters;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Views.Docking;

internal sealed class EditorDockStagedGridSplitter : GridSplitter
{
    private EditorDockSplitResizeCoordinator resizeCoordinator_;
    private readonly ViewportPresentationTransactionTelemetry transactionTelemetry_ = new();
    private ViewportPresentationTransactionCoordinator presentationTransactions_;
    private DragState? dragState_;
    private EditorDockSplitResizeCancellation? pendingCancellation_;
    private Task<ViewportPresentationTransactionReport>? latestRetirementCompletion_;
    private ulong nextTransactionId_;
    private ulong nextPresentationTransactionId_;

    public EditorDockStagedGridSplitter()
    {
        presentationTransactions_ = new ViewportPresentationTransactionCoordinator(
            transactionTelemetry_);
        ShowsPreview = true;
        ResizeBehavior = GridResizeBehavior.PreviousAndNext;
        resizeCoordinator_ = new EditorDockSplitResizeCoordinator(
            ProcessLatestResizeAsync,
            static _ => { });
    }

    protected override Type StyleKeyOverride => typeof(GridSplitter);

    internal EditorDockSplitResizeCoordinator ResizeCoordinator
    {
        get => resizeCoordinator_;
        set
        {
            ArgumentNullException.ThrowIfNull(value);
            if (ReferenceEquals(resizeCoordinator_, value))
            {
                return;
            }

            CancelPendingResize();
            resizeCoordinator_ = value;
        }
    }

    internal bool HasPendingResize => pendingCancellation_ is not null;

    internal EditorDockSplitResizeCoordinatorMetrics CaptureResizeCoordinatorMetrics() =>
        resizeCoordinator_.CaptureMetrics();

    internal ViewportPresentationTransactionTelemetryMetrics
        CapturePresentationTransactionTelemetry() =>
        transactionTelemetry_.Capture(Stopwatch.GetTimestamp());

    internal ViewportPresentationTransactionTelemetry PresentationTransactionTelemetry =>
        transactionTelemetry_;

    internal Task<ViewportPresentationTransactionReport>? LatestRetirementCompletion =>
        latestRetirementCompletion_;

    internal void ConfigurePresentationTransactionTestHooks(
        ViewportPresentationTransactionTestHooks testHooks)
    {
        ArgumentNullException.ThrowIfNull(testHooks);
        if (dragState_ is not null || pendingCancellation_ is not null)
        {
            throw new InvalidOperationException(
                "Viewport transaction hooks must be configured before resize begins.");
        }
        presentationTransactions_ = new ViewportPresentationTransactionCoordinator(
            transactionTelemetry_,
            testHooks);
    }

    internal AcceptanceResizeSession BeginAcceptanceResize()
    {
        Dispatcher.UIThread.VerifyAccess();
        if (dragState_ is not null)
        {
            throw new InvalidOperationException("A dock split resize is already active.");
        }

        CancelPendingResize();
        if (!TryCaptureResizeState(requirePreviewLayer: false, out var state))
        {
            throw new InvalidOperationException(
                "The dock splitter is not attached to a valid three-definition split layout.");
        }

        dragState_ = state;
        return new AcceptanceResizeSession(
            (cumulativeDelta, isFinal) =>
            {
                Dispatcher.UIThread.VerifyAccess();
                if (!ReferenceEquals(dragState_, state))
                {
                    throw new InvalidOperationException("The dock split resize is no longer current.");
                }

                state.CumulativeDelta = cumulativeDelta;
                PublishRequestedResize(state, cumulativeDelta, isFinal);
            },
            () => resizeCoordinator_.WhenIdleAsync(),
            () =>
            {
                Dispatcher.UIThread.VerifyAccess();
                EndAcceptanceResize(state);
            });
    }

    protected override void OnDragStarted(VectorEventArgs e)
    {
        if (dragState_ is not null)
        {
            return;
        }

        CancelPendingResize();
        if (!TryCaptureResizeState(requirePreviewLayer: true, out var state))
        {
            return;
        }

        dragState_ = state;
        try
        {
            base.OnDragStarted(e);
        }
        catch
        {
            dragState_ = null;
            throw;
        }
    }

    protected override void OnDragDelta(VectorEventArgs e)
    {
        var state = dragState_;
        if (state is null)
        {
            return;
        }

        base.OnDragDelta(e);
        state.CumulativeDelta = ResolveCumulativeDelta(
            state.First.ActualLength - state.OriginFirst.ActualLength,
            GetCumulativeDelta(e.Vector));
        PublishRequestedResize(state, state.CumulativeDelta, isFinal: false);
    }

    protected override void OnDragCompleted(VectorEventArgs e)
    {
        var state = dragState_;
        if (state is null)
        {
            return;
        }

        try
        {
            CompleteDragCore(
                () => base.OnDragCompleted(e),
                () => RestoreCommitted(state),
                () => PublishRequestedResize(state, state.CumulativeDelta, isFinal: true));
        }
        finally
        {
            dragState_ = null;
        }
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        var shouldCancel = e.Key == Key.Escape
            && (dragState_ is not null || pendingCancellation_ is not null);
        if (!shouldCancel)
        {
            base.OnKeyDown(e);
            return;
        }

        var state = dragState_;
        CancelDragCore(
            () => base.OnKeyDown(e),
            () => RestoreCommitted(state),
            () => CancelPendingResize());
        dragState_ = null;
        e.Handled = true;
    }

    protected override void OnLostFocus(FocusChangedEventArgs e)
    {
        var state = dragState_;
        CancelDragCore(
            () => base.OnLostFocus(e),
            () => RestoreCommitted(state),
            () => CancelPendingResize());
        dragState_ = null;
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        var state = dragState_;
        try
        {
            RestoreCommitted(state);
            CancelPendingResize();
            dragState_ = null;
        }
        finally
        {
            base.OnDetachedFromVisualTree(e);
        }
    }

    internal static void CompleteDragCore(
        Action completeDefaultResize,
        Action restoreCommitted,
        Action publishFinalRequest)
    {
        try
        {
            completeDefaultResize();
        }
        finally
        {
            restoreCommitted();
        }

        publishFinalRequest();
    }

    internal static void CancelDragCore(
        Action cancelDefaultResize,
        Action restoreCommitted,
        Action cancelPending)
    {
        try
        {
            cancelDefaultResize();
        }
        finally
        {
            try
            {
                restoreCommitted();
            }
            finally
            {
                cancelPending();
            }
        }
    }

    internal static double ResolveCumulativeDelta(
        double committedLayoutDelta,
        double dragDeltaVector)
    {
        return committedLayoutDelta + dragDeltaVector;
    }

    private bool TryGetLayoutContext(out LayoutContext context)
    {
        context = default;
        if (DataContext is not EditorDockSplitNodeViewModel split
            || ResizeBehavior != GridResizeBehavior.PreviousAndNext
            || GetParentGrid() is not { } grid)
        {
            return false;
        }

        var source = GetPropertiesValueSource();
        if (ResizeDirection == GridResizeDirection.Columns
            && split.Orientation == Orientation.Horizontal
            && source.GetValue(Grid.ColumnSpanProperty) == 1
            && source.GetValue(Grid.ColumnProperty) == 1
            && grid.ColumnDefinitions.Count == 3)
        {
            context = new LayoutContext(
                split,
                grid,
                DefinitionTarget.FromColumn(grid.ColumnDefinitions[0]),
                DefinitionTarget.FromColumn(grid.ColumnDefinitions[2]),
                Orientation.Horizontal);
            return true;
        }

        if (ResizeDirection == GridResizeDirection.Rows
            && split.Orientation == Orientation.Vertical
            && source.GetValue(Grid.RowSpanProperty) == 1
            && source.GetValue(Grid.RowProperty) == 1
            && grid.RowDefinitions.Count == 3)
        {
            context = new LayoutContext(
                split,
                grid,
                DefinitionTarget.FromRow(grid.RowDefinitions[0]),
                DefinitionTarget.FromRow(grid.RowDefinitions[2]),
                Orientation.Vertical);
            return true;
        }

        return false;
    }

    private bool TryCaptureResizeState(bool requirePreviewLayer, out DragState state)
    {
        state = null!;
        if (!TryGetLayoutContext(out var context)
            || requirePreviewLayer && AdornerLayer.GetAdornerLayer(context.Grid) is null)
        {
            return false;
        }

        var firstActualLength = context.First.ActualLength;
        var secondActualLength = context.Second.ActualLength;
        var layoutScale = LayoutHelper.GetLayoutScale(this);
        if (!double.IsFinite(firstActualLength)
            || firstActualLength < 0d
            || !double.IsFinite(secondActualLength)
            || secondActualLength < 0d
            || !double.IsFinite(layoutScale)
            || layoutScale <= 0d)
        {
            return false;
        }

        var transactionId = unchecked(++nextTransactionId_);
        if (transactionId == 0)
        {
            transactionId = unchecked(++nextTransactionId_);
        }

        state = new DragState(
            context.Split,
            context.First,
            context.Second,
            context.First.ToPolicyDefinition(context.Split.FirstLength),
            context.Second.ToPolicyDefinition(context.Split.SecondLength),
            new EditorDockSplitResizeCommittedSnapshot(
                context.Split.Id,
                context.Orientation,
                context.Split.FirstLength,
                context.Split.SecondLength,
                firstActualLength,
                secondActualLength,
                layoutScale),
            transactionId);
        return true;
    }

    private void PublishRequestedResize(DragState state, double cumulativeDelta, bool isFinal)
    {
        if (!TrySnapToDragIncrement(cumulativeDelta, out var snappedDelta)
            || !EditorDockSplitResizePolicy.TryResolve(
                new EditorDockSplitResizePolicyInput(
                    state.OriginFirst,
                    state.OriginSecond,
                    state.OriginCombinedActualLength,
                    snappedDelta,
                    UseLayoutRounding,
                    LayoutHelper.GetLayoutScale(this)),
                out var proposal))
        {
            return;
        }

        var scheduling = resizeCoordinator_.CaptureMetrics();
        if (!ShouldQueueProposal(
                state.Committed,
                proposal,
                scheduling.HasActive,
                scheduling.HasQueued))
        {
            return;
        }

        var request = new EditorDockSplitResizeRequest(
            state.TransactionId,
            ++state.Sequence,
            state.Committed,
            proposal,
            snappedDelta,
            isFinal);
        if (!resizeCoordinator_.RequestLatest(request))
        {
            return;
        }

        pendingCancellation_ = resizeCoordinator_.IsCurrent(request.TransactionId, request.Sequence)
            ? new EditorDockSplitResizeCancellation(
                request.TransactionId,
                request.Sequence,
                request.Committed.SplitId)
            : null;
    }

    internal static bool ShouldPublishProposal(
        EditorDockSplitResizeCommittedSnapshot committed,
        EditorDockSplitResizeProposal proposal) =>
        Math.Abs(proposal.FirstActualLength - committed.FirstActualLength) >
            LayoutHelper.LayoutEpsilon ||
        Math.Abs(proposal.SecondActualLength - committed.SecondActualLength) >
            LayoutHelper.LayoutEpsilon;

    internal static bool ShouldQueueProposal(
        EditorDockSplitResizeCommittedSnapshot committed,
        EditorDockSplitResizeProposal proposal,
        bool hasActive,
        bool hasQueued) =>
        ShouldPublishProposal(committed, proposal) || hasActive || hasQueued;

    private async Task ProcessLatestResizeAsync(
        EditorDockSplitResizeRequest request,
        CancellationToken cancellationToken)
    {
        Dispatcher.UIThread.VerifyAccess();
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!TryGetLayoutContext(out var context)
                || context.Split.Id != request.Committed.SplitId)
            {
                return;
            }

            var frontCommitted = CaptureCommitted(context);
            if (!TryProbeViewportTargets(
                    context,
                    request.Requested.FirstLength,
                    request.Requested.SecondLength,
                    frontCommitted,
                    out var viewportTargets))
            {
                return;
            }

            cancellationToken.ThrowIfCancellationRequested();
            var changedExactTargets = viewportTargets
                .Where(target => target.FrontWasExact && target.TargetExtent != target.FrontExtent)
                .ToArray();
            if (changedExactTargets.Length == 0)
            {
                CommitLayout(
                    context,
                    request.Requested.FirstLength,
                    request.Requested.SecondLength);
                AcceptCommitted(request, CaptureCommitted(context));
                return;
            }

            var transactionId = checked(++nextPresentationTransactionId_);
            var participants = changedExactTargets
                .Select((target, index) => new ViewportPresentationParticipant(
                    $"{request.Committed.SplitId}:{index}",
                    target.Control,
                    target.TargetExtent))
                .ToArray();
            var execution = await presentationTransactions_.ExecuteAsync(
                new ViewportPresentationTransactionRequest(
                    transactionId,
                    participants,
                    Stopwatch.GetTimestamp()),
                () => CommitLayout(
                    context,
                    request.Requested.FirstLength,
                    request.Requested.SecondLength),
                () => CommitLayout(
                    context,
                    frontCommitted.FirstLength,
                    frontCommitted.SecondLength),
                cancellationToken);
            latestRetirementCompletion_ = execution.RetirementCompletion;
            if (!execution.Published)
            {
                return;
            }

            AcceptCommitted(request, CaptureCommitted(context));
            _ = ObserveTransactionCompletionAsync(execution);
        }
        finally
        {
            ClearPendingResize(request.TransactionId, request.Sequence);
        }
    }

    private bool TryProbeViewportTargets(
        LayoutContext context,
        GridLength firstLength,
        GridLength secondLength,
        EditorDockSplitResizeCommittedSnapshot frontCommitted,
        out IReadOnlyList<ViewportResizeTarget> targets)
    {
        var controls = context.Grid.GetVisualDescendants()
            .OfType<ViewportCompositionControl>()
            .ToArray();
        if (controls.Length == 0)
        {
            targets = Array.Empty<ViewportResizeTarget>();
            return true;
        }

        var probes = new List<ViewportProbe>(controls.Length);
        try
        {
            foreach (var control in controls)
            {
                var metrics = control.PresentationGeometryMetrics;
                probes.Add(new ViewportProbe(
                    control,
                    control.BeginPresentationLayoutProbe(),
                    metrics.CurrentSurfaceIsExact,
                    metrics.LastPanelExtent));
            }

            ApplyProbeLayout(context, firstLength, secondLength);
            var captured = new List<ViewportResizeTarget>(probes.Count);
            foreach (var probe in probes)
            {
                if (!probe.Probe.TryGetExactPixelExtent(out var extent)
                    || extent.Width == 0
                    || extent.Height == 0)
                {
                    targets = Array.Empty<ViewportResizeTarget>();
                    return false;
                }

                captured.Add(new ViewportResizeTarget(
                    probe.Control,
                    extent,
                    probe.FrontWasExact,
                    probe.FrontExtent));
            }

            targets = captured;
            return true;
        }
        finally
        {
            ApplyProbeLayout(
                context,
                frontCommitted.FirstLength,
                frontCommitted.SecondLength);
            for (var index = probes.Count - 1; index >= 0; index--)
            {
                probes[index].Probe.Dispose();
            }
        }
    }

    private static async Task ObserveTransactionCompletionAsync(
        ViewportPresentationTransactionExecution execution)
    {
        var report = await execution.Completion;
        if (!report.Succeeded)
        {
            Trace.TraceError(
                "Viewport presentation transaction {0} completed as {1}: {2}",
                report.TransactionId,
                report.Result,
                report.Failure);
        }
        var finalReport = await execution.RetirementCompletion;
        if (!finalReport.Succeeded)
        {
            Trace.TraceError(
                "Viewport presentation transaction {0} final ownership outcome is {1}: {2}",
                finalReport.TransactionId,
                finalReport.Result,
                finalReport.Failure);
        }
    }

    private static void ApplyProbeLayout(
        LayoutContext context,
        GridLength firstLength,
        GridLength secondLength)
    {
        context.First.SetCurrentLength(firstLength);
        context.Second.SetCurrentLength(secondLength);
        context.Grid.UpdateLayout();
    }

    private static void CommitLayout(
        LayoutContext context,
        GridLength firstLength,
        GridLength secondLength)
    {
        context.Split.FirstLength = firstLength;
        context.Split.SecondLength = secondLength;
        ApplyProbeLayout(context, firstLength, secondLength);
    }

    private EditorDockSplitResizeCommittedSnapshot CaptureCommitted(LayoutContext context)
    {
        return new EditorDockSplitResizeCommittedSnapshot(
            context.Split.Id,
            context.Orientation,
            context.Split.FirstLength,
            context.Split.SecondLength,
            context.First.ActualLength,
            context.Second.ActualLength,
            LayoutHelper.GetLayoutScale(this));
    }

    private void AcceptCommitted(
        EditorDockSplitResizeRequest request,
        EditorDockSplitResizeCommittedSnapshot committed)
    {
        if (dragState_ is { } state
            && state.TransactionId == request.TransactionId)
        {
            state.Committed = committed;
        }

        ClearPendingResize(request.TransactionId, request.Sequence);
    }

    private void ClearPendingResize(ulong transactionId, ulong sequence)
    {
        if (pendingCancellation_ is { } pending
            && pending.TransactionId == transactionId
            && pending.LastSequence == sequence)
        {
            pendingCancellation_ = null;
        }
    }

    private void EndAcceptanceResize(DragState state)
    {
        if (!ReferenceEquals(dragState_, state))
        {
            return;
        }

        CancelPendingResize(state.TransactionId);
        dragState_ = null;
    }

    private double GetCumulativeDelta(Vector vector)
    {
        return ResizeDirection == GridResizeDirection.Columns
            ? vector.X
            : vector.Y;
    }

    private bool TrySnapToDragIncrement(double cumulativeDelta, out double snappedDelta)
    {
        snappedDelta = default;
        if (!double.IsFinite(cumulativeDelta)
            || !double.IsFinite(DragIncrement)
            || DragIncrement <= 0d)
        {
            return false;
        }

        snappedDelta = Math.Round(cumulativeDelta / DragIncrement) * DragIncrement;
        return double.IsFinite(snappedDelta);
    }

    private void CancelPendingResize(ulong? transactionId = null)
    {
        if (pendingCancellation_ is not { } cancellation
            || transactionId is { } expectedTransactionId
                && cancellation.TransactionId != expectedTransactionId)
        {
            return;
        }

        pendingCancellation_ = null;
        resizeCoordinator_.Cancel(cancellation);
    }

    private static void RestoreCommitted(DragState? state)
    {
        if (state is null)
        {
            return;
        }

        state.Split.FirstLength = state.Committed.FirstLength;
        state.Split.SecondLength = state.Committed.SecondLength;
        state.First.SetCurrentLength(state.Committed.FirstLength);
        state.Second.SetCurrentLength(state.Committed.SecondLength);
    }

    private sealed class DragState
    {
        public DragState(
            EditorDockSplitNodeViewModel split,
            DefinitionTarget first,
            DefinitionTarget second,
            EditorDockSplitResizeDefinition originFirst,
            EditorDockSplitResizeDefinition originSecond,
            EditorDockSplitResizeCommittedSnapshot committed,
            ulong transactionId)
        {
            Split = split;
            First = first;
            Second = second;
            OriginFirst = originFirst;
            OriginSecond = originSecond;
            Committed = committed;
            TransactionId = transactionId;
        }

        public EditorDockSplitNodeViewModel Split { get; }

        public DefinitionTarget First { get; }

        public DefinitionTarget Second { get; }

        public EditorDockSplitResizeDefinition OriginFirst { get; }

        public EditorDockSplitResizeDefinition OriginSecond { get; }

        public double OriginCombinedActualLength =>
            OriginFirst.ActualLength + OriginSecond.ActualLength;

        public EditorDockSplitResizeCommittedSnapshot Committed { get; set; }

        public ulong TransactionId { get; }

        public ulong Sequence { get; set; }

        public double CumulativeDelta { get; set; }
    }

    internal sealed class AcceptanceResizeSession : IDisposable
    {
        private readonly Action<double, bool> requestCumulative_;
        private readonly Func<Task> whenIdle_;
        private Action? dispose_;

        internal AcceptanceResizeSession(
            Action<double, bool> requestCumulative,
            Func<Task> whenIdle,
            Action dispose)
        {
            requestCumulative_ = requestCumulative;
            whenIdle_ = whenIdle;
            dispose_ = dispose;
        }

        public void RequestCumulative(double cumulativeDelta, bool isFinal)
        {
            if (dispose_ is null)
            {
                throw new ObjectDisposedException(nameof(AcceptanceResizeSession));
            }

            requestCumulative_(cumulativeDelta, isFinal);
        }

        public Task WhenIdleAsync()
        {
            return dispose_ is null
                ? throw new ObjectDisposedException(nameof(AcceptanceResizeSession))
                : whenIdle_();
        }

        public void Dispose()
        {
            Interlocked.Exchange(ref dispose_, null)?.Invoke();
        }
    }

    private readonly record struct LayoutContext(
        EditorDockSplitNodeViewModel Split,
        Grid Grid,
        DefinitionTarget First,
        DefinitionTarget Second,
        Orientation Orientation);

    private sealed record ViewportProbe(
        ViewportCompositionControl Control,
        ViewportPresentationLayoutProbe Probe,
        bool FrontWasExact,
        ViewportExtent FrontExtent);

    private sealed record ViewportResizeTarget(
        ViewportCompositionControl Control,
        ViewportExtent TargetExtent,
        bool FrontWasExact,
        ViewportExtent FrontExtent);

    private readonly struct DefinitionTarget
    {
        private readonly ColumnDefinition? column_;
        private readonly RowDefinition? row_;

        private DefinitionTarget(ColumnDefinition column)
        {
            column_ = column;
            row_ = null;
        }

        private DefinitionTarget(RowDefinition row)
        {
            column_ = null;
            row_ = row;
        }

        public double ActualLength => column_?.ActualWidth ?? row_!.ActualHeight;

        public static DefinitionTarget FromColumn(ColumnDefinition definition)
        {
            return new DefinitionTarget(definition);
        }

        public static DefinitionTarget FromRow(RowDefinition definition)
        {
            return new DefinitionTarget(definition);
        }

        public EditorDockSplitResizeDefinition ToPolicyDefinition(GridLength userLength)
        {
            return column_ is not null
                ? new EditorDockSplitResizeDefinition(
                    userLength,
                    column_.ActualWidth,
                    column_.MinWidth,
                    column_.MaxWidth)
                : new EditorDockSplitResizeDefinition(
                    userLength,
                    row_!.ActualHeight,
                    row_.MinHeight,
                    row_.MaxHeight);
        }

        public void SetCurrentLength(GridLength length)
        {
            if (column_ is not null)
            {
                column_.SetCurrentValue(ColumnDefinition.WidthProperty, length);
            }
            else
            {
                row_!.SetCurrentValue(RowDefinition.HeightProperty, length);
            }
        }
    }
}
