using System;
using System.Collections.Generic;
using System.Threading;
using Asharia.Studio.Application.Diagnostics;
using Avalonia.Threading;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Diagnostics;

internal interface IStudioDiagnosticsUiScheduler
{
    void Post(Action action);
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
}

internal sealed class StudioDiagnosticsPanelViewModel : ViewModelBase, IDisposable
{
    private const int MaxCatchUpPasses = 4;

    private readonly IStudioDiagnosticSource source_;
    private readonly IStudioDiagnosticsUiScheduler scheduler_;
    private readonly List<StudioLogRecord> consoleRecords_;
    private readonly List<StudioDiagnosticRecord> problemRecords_;
    private readonly IDisposable subscription_;
    private long consoleCursor_;
    private long problemCursor_;
    private long consoleViewFloor_;
    private long consolePausedAtCursor_;
    private long problemViewFloor_;
    private long consoleDroppedBaseline_;
    private long problemDroppedBaseline_;
    private long consoleSourceTotalDropped_;
    private long problemSourceTotalDropped_;
    private int refreshScheduled_;
    private int disposed_;

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
            RebuildConsoleProjection);
        Problems = new StudioProblemsProjectionViewModel(
            ClearProblems,
            RebuildProblemsProjection);

        subscription_ = source.Subscribe(RequestRefresh);
        Refresh();
    }

    public StudioConsoleProjectionViewModel Console { get; }

    public StudioProblemsProjectionViewModel Problems { get; }

    public void Refresh()
    {
        if (Volatile.Read(ref disposed_) != 0)
        {
            return;
        }

        CatchUpConsole(retainRecords: !Console.IsPaused);
        if (!Console.IsPaused)
        {
            RebuildConsoleProjection();
        }

        CatchUpProblems(retainRecords: true);
        RebuildProblemsProjection();
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed_, 1) != 0)
        {
            return;
        }

        subscription_.Dispose();
        Interlocked.Exchange(ref refreshScheduled_, 0);
    }

    private void RequestRefresh()
    {
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

        Refresh();
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
        CatchUpProblems(retainRecords: false);
        problemViewFloor_ = problemCursor_;
        problemDroppedBaseline_ = problemSourceTotalDropped_;
        problemRecords_.Clear();
        Problems.ResetHubState();
        RebuildProblemsProjection();
    }

    private void CatchUpConsole(bool retainRecords)
    {
        var startingCursor = consoleCursor_;
        var cursorExpired = false;
        var readTruncated = false;
        var sourceTotalDropped = consoleSourceTotalDropped_;
        var newRecordCount = 0;
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
                consoleRecords_.AddRange(window.Items);
                TrimOldest(consoleRecords_, source_.LogCapacity);
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
            RequestRefresh();
        }

        consoleSourceTotalDropped_ = sourceTotalDropped;
    }

    private void CatchUpProblems(bool retainRecords)
    {
        var startingCursor = problemCursor_;
        var cursorExpired = false;
        var readTruncated = false;
        var sourceTotalDropped = problemSourceTotalDropped_;
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
                problemRecords_.AddRange(window.Items);
                TrimOldest(problemRecords_, source_.DiagnosticCapacity);
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
            RequestRefresh();
        }

        problemSourceTotalDropped_ = sourceTotalDropped;
    }

    private void RebuildConsoleProjection() =>
        Console.Rebuild(consoleRecords_, consoleViewFloor_);

    private void RebuildProblemsProjection() =>
        Problems.Rebuild(problemRecords_, problemViewFloor_);

    private static void TrimOldest<T>(List<T> records, int capacity)
    {
        var overflow = records.Count - capacity;
        if (overflow > 0)
        {
            records.RemoveRange(0, overflow);
        }
    }
}
