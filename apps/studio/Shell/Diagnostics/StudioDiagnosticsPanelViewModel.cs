using System;
using System.Collections.Generic;
using System.Threading;
using Asharia.Studio.Application.Diagnostics;
using Avalonia.Threading;
using Editor.Shell.Docking.Panels;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Diagnostics;

internal interface IStudioDiagnosticsUiScheduler
{
    void Post(Action action);

    IDisposable Schedule(Action action, TimeSpan delay);
}

internal sealed class StudioAvaloniaDiagnosticsUiScheduler :
    IStudioDiagnosticsUiScheduler
{
    public static StudioAvaloniaDiagnosticsUiScheduler Instance { get; } = new();

    private StudioAvaloniaDiagnosticsUiScheduler()
    {
    }

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        Dispatcher.UIThread.Post(action, DispatcherPriority.Background);
    }

    public IDisposable Schedule(Action action, TimeSpan delay)
    {
        ArgumentNullException.ThrowIfNull(action);
        return DispatcherTimer.RunOnce(action, delay, DispatcherPriority.Background);
    }
}

internal sealed class StudioDiagnosticsPanelViewModel :
    ViewModelBase,
    IEditorPanelVisibilitySink,
    IDisposable
{
    private const int MaxCatchUpPasses = 4;
    private static readonly TimeSpan LogRefreshInterval =
        TimeSpan.FromMilliseconds(75);
    private static readonly TimeSpan SearchDebounceInterval =
        TimeSpan.FromMilliseconds(150);

    private readonly IStudioDiagnosticSource source_;
    private readonly IStudioDiagnosticsUiScheduler scheduler_;
    private readonly List<StudioLogRecord> consoleRecords_;
    private readonly List<StudioDiagnosticRecord> problemRecords_;
    private IReadOnlyList<StudioDiagnosticRecord> activeProblemRecords_ = [];
    private readonly IDisposable diagnosticSubscription_;
    private readonly IDisposable logSubscription_;
    private readonly object logRefreshGate_ = new();
    private readonly object searchGate_ = new();
    private IDisposable? consoleSearchRebuild_;
    private IDisposable? problemsSearchRebuild_;
    private IDisposable? logRefresh_;
    private long consoleCursor_;
    private long problemCursor_;
    private long consoleViewFloor_;
    private long consolePausedAtCursor_;
    private long problemViewFloor_;
    private long consoleDroppedBaseline_;
    private long problemDroppedBaseline_;
    private long consoleSourceTotalDropped_;
    private long problemSourceTotalDropped_;
    private long activeProblemVersion_ = -1;
    private int refreshScheduled_;
    private int consoleRefreshRequested_;
    private int problemsRefreshRequested_;
    private int disposed_;
    private bool isPanelVisible_ = true;
    private bool consoleProjectionDirty_;
    private bool problemsProjectionDirty_;

    public StudioDiagnosticsPanelViewModel(
        IStudioDiagnosticSource source,
        IStudioDiagnosticsUiScheduler? scheduler = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        source_ = source;
        scheduler_ = scheduler ?? StudioAvaloniaDiagnosticsUiScheduler.Instance;
        consoleRecords_ = new List<StudioLogRecord>(source.LogCapacity);
        problemRecords_ = new List<StudioDiagnosticRecord>(source.DiagnosticCapacity);
        Console = new StudioConsoleProjectionViewModel(
            ClearConsole,
            ToggleConsolePause,
            RebuildConsoleProjection,
            RequestConsoleSearchRebuild);
        Problems = new StudioProblemsProjectionViewModel(
            ClearProblems,
            RebuildProblemsProjection,
            RequestProblemsSearchRebuild);

        diagnosticSubscription_ = source.SubscribeDiagnostics(RequestProblemsRefresh);
        try
        {
            logSubscription_ = source.SubscribeLogs(RequestConsoleRefresh);
        }
        catch
        {
            diagnosticSubscription_.Dispose();
            throw;
        }

        Refresh();
    }

    public StudioConsoleProjectionViewModel Console { get; }

    public StudioProblemsProjectionViewModel Problems { get; }

    public void Refresh()
    {
        Refresh(forceConsoleProjection: true, forceProblemsProjection: true);
    }

    public void OnPanelShown(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        isPanelVisible_ = true;
        Refresh();
    }

    public void OnPanelHidden(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        isPanelVisible_ = false;
    }

    private void Refresh(
        bool forceConsoleProjection,
        bool forceProblemsProjection)
    {
        if (Volatile.Read(ref disposed_) != 0)
        {
            return;
        }

        var consoleChanged = CatchUpConsole(retainRecords: !Console.IsPaused);
        consoleProjectionDirty_ |= consoleChanged;
        if (isPanelVisible_
            && !Console.IsPaused
            && (forceConsoleProjection || consoleProjectionDirty_))
        {
            RebuildConsoleProjection();
            consoleProjectionDirty_ = false;
        }

        var problemsChanged = CatchUpProblems(retainRecords: true);
        problemsProjectionDirty_ |= problemsChanged;
        if (isPanelVisible_
            && (forceProblemsProjection || problemsProjectionDirty_))
        {
            RebuildProblemsProjection();
            problemsProjectionDirty_ = false;
        }
    }

    private void RefreshRequestedStreams(
        bool refreshConsole,
        bool refreshProblems)
    {
        if (refreshConsole)
        {
            var consoleChanged = CatchUpConsole(retainRecords: !Console.IsPaused);
            consoleProjectionDirty_ |= consoleChanged;
            if (isPanelVisible_ && !Console.IsPaused && consoleProjectionDirty_)
            {
                RebuildConsoleProjection();
            }
        }

        if (refreshProblems)
        {
            var problemsChanged = CatchUpProblems(retainRecords: true);
            problemsProjectionDirty_ |= problemsChanged;
            if (isPanelVisible_ && problemsProjectionDirty_)
            {
                RebuildProblemsProjection();
            }
        }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed_, 1) != 0)
        {
            return;
        }

        logSubscription_.Dispose();
        diagnosticSubscription_.Dispose();
        lock (logRefreshGate_)
        {
            logRefresh_?.Dispose();
            logRefresh_ = null;
        }

        lock (searchGate_)
        {
            consoleSearchRebuild_?.Dispose();
            consoleSearchRebuild_ = null;
            problemsSearchRebuild_?.Dispose();
            problemsSearchRebuild_ = null;
        }

        Interlocked.Exchange(ref refreshScheduled_, 0);
    }

    private void RequestProblemsRefresh() =>
        RequestRefresh(consoleDirty: false, problemsDirty: true);

    private void RequestConsoleRefresh()
    {
        if (Volatile.Read(ref disposed_) != 0)
        {
            return;
        }

        lock (logRefreshGate_)
        {
            if (logRefresh_ is not null)
            {
                return;
            }

            try
            {
                logRefresh_ = scheduler_.Schedule(
                    CompleteConsoleRefreshDelay,
                    LogRefreshInterval);
            }
            catch
            {
                logRefresh_ = null;
                // Log invalidation must never change publisher control flow.
            }
        }
    }

    private void CompleteConsoleRefreshDelay()
    {
        lock (logRefreshGate_)
        {
            logRefresh_ = null;
        }

        RequestRefresh(consoleDirty: true, problemsDirty: false);
    }

    private void RequestRefresh(bool consoleDirty, bool problemsDirty)
    {
        if (consoleDirty)
        {
            Volatile.Write(ref consoleRefreshRequested_, 1);
        }

        if (problemsDirty)
        {
            Volatile.Write(ref problemsRefreshRequested_, 1);
        }

        if (Volatile.Read(ref disposed_) != 0
            || Interlocked.CompareExchange(ref refreshScheduled_, 1, 0) != 0)
        {
            return;
        }

        try
        {
            scheduler_.Post(DrainRefresh);
        }
        catch
        {
            Interlocked.Exchange(ref refreshScheduled_, 0);
            // Diagnostic invalidation must never change publisher control flow.
        }
    }

    private void DrainRefresh()
    {
        Interlocked.Exchange(ref refreshScheduled_, 0);
        if (Volatile.Read(ref disposed_) != 0)
        {
            return;
        }

        var refreshConsole = Interlocked.Exchange(ref consoleRefreshRequested_, 0) != 0;
        var refreshProblems = Interlocked.Exchange(ref problemsRefreshRequested_, 0) != 0;
        RefreshRequestedStreams(refreshConsole, refreshProblems);
    }

    private void ClearConsole()
    {
        CatchUpConsole(retainRecords: false);
        consoleViewFloor_ = consoleCursor_;
        consoleDroppedBaseline_ = consoleSourceTotalDropped_;
        consoleRecords_.Clear();
        if (Console.IsPaused)
        {
            consolePausedAtCursor_ = consoleCursor_;
        }

        Console.ResetHubState();
        RebuildConsoleProjection();
    }

    private void ToggleConsolePause()
    {
        if (!Console.IsPaused)
        {
            // Keep the bounded raw window itself frozen while a second cursor
            // continues observing the Hub. This lets filters re-project the
            // exact paused view even if the source ring wraps.
            CatchUpConsole(retainRecords: true);
            RebuildConsoleProjection();
            consolePausedAtCursor_ = consoleCursor_;
            Console.SetPaused(true);
            return;
        }

        Console.SetPaused(false);
        consoleCursor_ = consolePausedAtCursor_;
        consolePausedAtCursor_ = 0;
        Console.ResetUnseenCount();
        CatchUpConsole(retainRecords: true);
        RebuildConsoleProjection();
    }

    private void ClearProblems()
    {
        if (Problems.IsActiveView)
        {
            return;
        }

        CatchUpProblems(retainRecords: false);
        problemViewFloor_ = problemCursor_;
        problemDroppedBaseline_ = problemSourceTotalDropped_;
        problemRecords_.Clear();
        Problems.ResetHubState();
        RebuildProblemsProjection();
    }

    private bool CatchUpConsole(bool retainRecords)
    {
        var startingCursor = consoleCursor_;
        var startingDropped = Console.TotalDropped;
        var startingGap = Console.HasHistoryGap;
        var startingTruncated = Console.IsCatchingUp;
        var cursorExpired = false;
        var readTruncated = false;
        var sourceTotalDropped = consoleSourceTotalDropped_;
        var newRecordCount = 0;
        var retainedRecordsChanged = false;
        for (var pass = 0; pass < MaxCatchUpPasses; pass++)
        {
            var window = source_.ReadLogs(consoleCursor_, source_.LogCapacity);
            sourceTotalDropped = Math.Max(sourceTotalDropped, window.TotalDropped);
            cursorExpired |= window.CursorExpired;
            if (window.CursorExpired && retainRecords)
            {
                consoleRecords_.Clear();
            }

            consoleCursor_ = window.NextCursor;
            newRecordCount += window.Items.Length;
            if (retainRecords)
            {
                retainedRecordsChanged |= PruneLogsBefore(
                    consoleRecords_,
                    window.OldestAvailableSequence);
                consoleRecords_.AddRange(window.Items);
                TrimOldest(consoleRecords_, source_.LogCapacity);
                retainedRecordsChanged |= !window.Items.IsEmpty;
            }

            readTruncated = window.Truncated;
            if (!window.Truncated || window.Items.IsEmpty)
            {
                break;
            }
        }

        Console.UpdateHubState(
            Math.Max(0, sourceTotalDropped - consoleDroppedBaseline_),
            cursorExpired,
            readTruncated);
        if (Console.IsPaused && newRecordCount > 0)
        {
            Console.AddUnseenCount(newRecordCount);
        }

        // A reserved-but-not-yet-published sequence produces an empty,
        // truncated window. Wait for the producer's completion notification
        // instead of continuously re-posting the UI drain with no progress.
        if (readTruncated && consoleCursor_ > startingCursor)
        {
            RequestRefresh(consoleDirty: true, problemsDirty: false);
        }

        consoleSourceTotalDropped_ = sourceTotalDropped;
        return retainedRecordsChanged
            || (retainRecords && cursorExpired)
            || startingDropped != Console.TotalDropped
            || startingGap != Console.HasHistoryGap
            || startingTruncated != Console.IsCatchingUp;
    }

    private bool CatchUpProblems(bool retainRecords)
    {
        var startingDropped = Problems.TotalDropped;
        var startingGap = Problems.HasHistoryGap;
        var startingTruncated = Problems.IsCatchingUp;
        var activeSnapshot = source_.ReadActiveProblems();
        var activeChanged = activeSnapshot.Version != activeProblemVersion_;
        if (activeChanged)
        {
            activeProblemVersion_ = activeSnapshot.Version;
            activeProblemRecords_ = activeSnapshot.Items;
        }

        Problems.UpdateActiveHubState(
            activeSnapshot.TotalDropped,
            activeSnapshot.IsIncomplete);

        var startingCursor = problemCursor_;
        var cursorExpired = false;
        var readTruncated = false;
        var sourceTotalDropped = problemSourceTotalDropped_;
        var retainedRecordsChanged = false;
        for (var pass = 0; pass < MaxCatchUpPasses; pass++)
        {
            var window = source_.ReadDiagnostics(
                problemCursor_,
                source_.DiagnosticCapacity,
                StudioDiagnosticChannel.Problem);
            sourceTotalDropped = Math.Max(sourceTotalDropped, window.TotalDropped);
            cursorExpired |= window.CursorExpired;
            if (window.CursorExpired && retainRecords)
            {
                problemRecords_.Clear();
            }

            problemCursor_ = window.NextCursor;
            if (retainRecords)
            {
                retainedRecordsChanged |= PruneDiagnosticsBefore(
                    problemRecords_,
                    window.OldestAvailableSequence);
                problemRecords_.AddRange(window.Items);
                TrimOldest(problemRecords_, source_.DiagnosticCapacity);
                retainedRecordsChanged |= !window.Items.IsEmpty;
            }

            readTruncated = window.Truncated;
            if (!window.Truncated || window.Items.IsEmpty)
            {
                break;
            }
        }

        Problems.UpdateHubState(
            Math.Max(0, sourceTotalDropped - problemDroppedBaseline_),
            cursorExpired,
            readTruncated);
        if (readTruncated && problemCursor_ > startingCursor)
        {
            RequestRefresh(consoleDirty: false, problemsDirty: true);
        }

        problemSourceTotalDropped_ = sourceTotalDropped;
        return activeChanged
            || retainedRecordsChanged
            || (retainRecords && cursorExpired)
            || startingDropped != Problems.TotalDropped
            || startingGap != Problems.HasHistoryGap
            || startingTruncated != Problems.IsCatchingUp;
    }

    private void RebuildConsoleProjection()
    {
        if (!isPanelVisible_)
        {
            consoleProjectionDirty_ = true;
            return;
        }

        Console.Rebuild(consoleRecords_, consoleViewFloor_);
        consoleProjectionDirty_ = false;
    }

    private void RebuildProblemsProjection()
    {
        if (!isPanelVisible_)
        {
            problemsProjectionDirty_ = true;
            return;
        }

        Problems.Rebuild(
            Problems.IsActiveView ? activeProblemRecords_ : problemRecords_,
            Problems.IsActiveView ? 0 : problemViewFloor_);
        problemsProjectionDirty_ = false;
    }

    private void RequestConsoleSearchRebuild()
    {
        lock (searchGate_)
        {
            consoleSearchRebuild_?.Dispose();
            consoleSearchRebuild_ = scheduler_.Schedule(
                CompleteConsoleSearchRebuild,
                SearchDebounceInterval);
        }
    }

    private void CompleteConsoleSearchRebuild()
    {
        lock (searchGate_)
        {
            consoleSearchRebuild_ = null;
        }

        if (Volatile.Read(ref disposed_) == 0)
        {
            RebuildConsoleProjection();
        }
    }

    private void RequestProblemsSearchRebuild()
    {
        lock (searchGate_)
        {
            problemsSearchRebuild_?.Dispose();
            problemsSearchRebuild_ = scheduler_.Schedule(
                CompleteProblemsSearchRebuild,
                SearchDebounceInterval);
        }
    }

    private void CompleteProblemsSearchRebuild()
    {
        lock (searchGate_)
        {
            problemsSearchRebuild_ = null;
        }

        if (Volatile.Read(ref disposed_) == 0)
        {
            RebuildProblemsProjection();
        }
    }

    private static void TrimOldest<T>(List<T> records, int capacity)
    {
        var overflow = records.Count - capacity;
        if (overflow > 0)
        {
            records.RemoveRange(0, overflow);
        }
    }

    private static bool PruneLogsBefore(
        List<StudioLogRecord> records,
        long oldestAvailableSequence) =>
        PruneBefore(records, oldestAvailableSequence, static record => record.SequenceId);

    private static bool PruneDiagnosticsBefore(
        List<StudioDiagnosticRecord> records,
        long oldestAvailableSequence) =>
        PruneBefore(records, oldestAvailableSequence, static record => record.SequenceId);

    private static bool PruneBefore<T>(
        List<T> records,
        long oldestAvailableSequence,
        Func<T, long> sequenceSelector)
    {
        var removeCount = 0;
        while (removeCount < records.Count
               && sequenceSelector(records[removeCount]) < oldestAvailableSequence)
        {
            removeCount++;
        }

        if (removeCount == 0)
        {
            return false;
        }

        records.RemoveRange(0, removeCount);
        return true;
    }
}
