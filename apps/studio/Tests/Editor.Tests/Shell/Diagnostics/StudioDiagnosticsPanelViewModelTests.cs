using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Studio.Application.Diagnostics;
using Editor.Shell.Diagnostics;
using Xunit;

namespace Editor.Tests.Shell.Diagnostics;

public sealed class StudioDiagnosticsPanelViewModelTests
{
    [Fact]
    public void Projection_separates_timeline_logs_from_actionable_problems()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishDiagnostic(Problem("P100", "Fix the project setting."));
        hub.PublishDiagnostic(DebugDiagnostic("D100"));
        hub.PublishLog(Log("frame one"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        var console = Assert.Single(projection.Console.Rows);
        var problem = Assert.Single(projection.Problems.Rows);
        Assert.Equal("frame one", console.Message);
        Assert.Equal("P100", problem.Code);
        Assert.Equal("Fix the project setting.", problem.Remediation);
    }

    [Fact]
    public void Projection_coalesces_invalidations_until_the_ui_scheduler_drains()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(hub, scheduler);

        hub.PublishDiagnostic(Problem("P100"));
        hub.PublishDiagnostic(Problem("P200"));

        Assert.Equal(1, scheduler.PendingCount);
        Assert.Empty(projection.Problems.Rows);
        scheduler.DrainOne();
        Assert.Equal(2, projection.Problems.Rows.Count);
    }

    [Fact]
    public void Empty_truncated_window_without_cursor_progress_waits_for_source_invalidation()
    {
        var source = new EmptyTruncatedLogSource();
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(source, scheduler);

        Assert.Equal(1, source.LogReadCount);
        Assert.True(projection.Console.IsCatchingUp);
        Assert.Equal(0, scheduler.PendingCount);
    }

    [Fact]
    public void Clear_advances_only_the_selected_view_barrier_and_keeps_hub_truth()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishDiagnostic(Problem("P100"));
        hub.PublishLog(Log("before clear"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        projection.Console.ClearCommand.Execute(null);

        Assert.Empty(projection.Console.Rows);
        Assert.Single(projection.Problems.Rows);
        Assert.Single(hub.ReadLogs(maxCount: hub.LogCapacity).Items);
        hub.PublishLog(Log("after clear"));
        projection.Refresh();
        Assert.Equal("after clear", Assert.Single(projection.Console.Rows).Message);
    }

    [Fact]
    public void Pause_keeps_ingesting_but_freezes_rows_until_resume()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(Log("before pause"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        projection.Console.PauseCommand.Execute(null);
        hub.PublishLog(Log("while paused"));
        projection.Refresh();

        Assert.Equal("before pause", Assert.Single(projection.Console.Rows).Message);
        Assert.Equal(1, projection.Console.UnseenCount);
        projection.Console.PauseCommand.Execute(null);
        Assert.Equal(2, projection.Console.Rows.Count);
        Assert.Equal(0, projection.Console.UnseenCount);
    }

    [Fact]
    public void Pause_keeps_its_frozen_view_when_filters_change()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(Log("visible before pause"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        projection.Console.PauseCommand.Execute(null);
        hub.PublishLog(Log("matching while paused"));
        projection.Refresh();
        projection.Console.SearchText = "matching";

        Assert.Empty(projection.Console.Rows);
        Assert.Equal(1, projection.Console.UnseenCount);
        projection.Console.PauseCommand.Execute(null);
        Assert.Equal(
            "matching while paused",
            Assert.Single(projection.Console.Rows).Message);
    }

    [Fact]
    public void Pause_keeps_its_bounded_raw_window_when_the_source_ring_wraps()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        hub.PublishLog(Log("before pause A"));
        hub.PublishLog(Log("before pause B"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        projection.Console.PauseCommand.Execute(null);
        hub.PublishLog(Log("after pause C"));
        hub.PublishLog(Log("after pause D"));
        projection.Refresh();
        projection.Console.SearchText = "before pause";

        Assert.Equal(
            ["before pause A", "before pause B"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(2, projection.Console.UnseenCount);

        projection.Console.PauseCommand.Execute(null);
        projection.Console.SearchText = string.Empty;

        Assert.Equal(
            ["after pause C", "after pause D"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.False(projection.Console.HasHistoryGap);
        Assert.Equal(2, projection.Console.TotalDropped);
    }

    [Fact]
    public void Collapse_groups_identical_non_adjacent_records_in_first_seen_order()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(Log("same"));
        hub.PublishLog(Log("different"));
        hub.PublishLog(Log("same"));
        hub.PublishDiagnostic(Problem("P100", message: "same"));
        hub.PublishDiagnostic(Problem("P200", message: "different"));
        hub.PublishDiagnostic(Problem("P100", message: "same"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        Assert.Collection(
            projection.Console.Rows,
            first =>
            {
                Assert.Equal("same", first.Message);
                Assert.Equal(2, first.RepeatCount);
                Assert.Equal(3, first.LastSequenceId);
            },
            second => Assert.Equal("different", second.Message));
        Assert.Collection(
            projection.Problems.Rows,
            first =>
            {
                Assert.Equal("P100", first.Code);
                Assert.Equal(2, first.RepeatCount);
            },
            second => Assert.Equal("P200", second.Code));
    }

    [Fact]
    public void Problem_collapse_does_not_merge_changed_severity_or_message()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishDiagnostic(Problem("P100", message: "first"));
        hub.PublishDiagnostic(Problem("P100", message: "second"));
        hub.PublishDiagnostic(Problem(
            "P100",
            message: "first",
            severity: StudioDiagnosticSeverity.Error));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        Assert.Equal(3, projection.Problems.Rows.Count);
    }

    [Fact]
    public void Collapse_does_not_merge_distinct_operation_context_or_remediation()
    {
        var operationA = Guid.Parse("10000000-0000-0000-0000-000000000001");
        var operationB = Guid.Parse("10000000-0000-0000-0000-000000000002");
        var correlationA = Guid.Parse("20000000-0000-0000-0000-000000000001");
        var correlationB = Guid.Parse("20000000-0000-0000-0000-000000000002");
        var contexts = new[]
        {
            Context("project-service", operationId: operationA, correlationId: correlationA),
            Context("project-service", operationId: operationB, correlationId: correlationA),
            Context("project-service", operationId: operationA, correlationId: correlationB),
            Context(
                "project-service",
                operationId: operationA,
                correlationId: correlationA,
                sensitivity: StudioDataSensitivity.Sensitive),
            Context(
                "project-service",
                origin: StudioRecordOrigin.Native,
                operationId: operationA,
                correlationId: correlationA),
            Context(
                "project-service",
                package: "other-package",
                operationId: operationA,
                correlationId: correlationA),
        };
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 16, logCapacity: 16);
        foreach (var context in contexts)
        {
            hub.PublishLog(LogWithContext("same", context));
            hub.PublishDiagnostic(ProblemWithContext("P100", context));
        }
        hub.PublishDiagnostic(ProblemWithContext(
            "P100",
            contexts[0],
            remediation: "Use a different recovery action."));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        Assert.Equal(6, projection.Console.Rows.Count);
        Assert.Equal(7, projection.Problems.Rows.Count);
    }

    [Fact]
    public void Console_collapse_keeps_first_occurrence_as_the_monotonic_row_anchor()
    {
        var firstTime = new DateTimeOffset(2026, 8, 13, 1, 2, 3, TimeSpan.Zero);
        var secondTime = firstTime.AddMilliseconds(1);
        var thirdTime = secondTime.AddMilliseconds(1);
        var records = new[]
        {
            LogRecord(1, firstTime, "A"),
            LogRecord(2, secondTime, "B"),
            LogRecord(3, thirdTime, "A"),
        };
        var projection = new StudioConsoleProjectionViewModel(
            clear: static () => { },
            togglePause: static () => { },
            rebuild: static () => { });

        projection.Rebuild(records, viewFloor: 0);

        Assert.Collection(
            projection.Rows,
            first =>
            {
                Assert.Equal(1, first.SequenceId);
                Assert.Equal(3, first.LastSequenceId);
                Assert.Equal(firstTime, first.TimestampUtc);
            },
            second =>
            {
                Assert.Equal(2, second.SequenceId);
                Assert.Equal(secondTime, second.TimestampUtc);
            });
        Assert.True(projection.Rows[0].TimestampUtc <= projection.Rows[1].TimestampUtc);
    }

    [Fact]
    public void Enabling_collapse_keeps_selection_on_the_group_of_a_later_repeat()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(Log("A"));
        hub.PublishLog(Log("B"));
        hub.PublishLog(Log("A"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());
        projection.Console.CollapseRepeated = false;
        projection.Console.SelectedRow = projection.Console.Rows[2];

        projection.Console.CollapseRepeated = true;

        Assert.NotNull(projection.Console.SelectedRow);
        Assert.Equal(1, projection.Console.SelectedRow!.SequenceId);
        Assert.Equal(3, projection.Console.SelectedRow.LastSequenceId);
    }

    [Fact]
    public void Collapse_keys_do_not_collide_when_fields_contain_control_separators()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(LogWithContext(
            "same",
            Context("d", package: "p\u001fc")));
        hub.PublishLog(LogWithContext(
            "same",
            Context("c\u001fd", package: "p")));
        var problemContext = Context("project-service");
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Warning,
            StudioDiagnosticChannel.Problem,
            "P100",
            "cat\u001fmessage",
            problemContext,
            "value",
            "Take action."));
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Warning,
            StudioDiagnosticChannel.Problem,
            "P100",
            "cat",
            problemContext,
            "message\u001fvalue",
            "Take action."));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        Assert.Equal(2, projection.Console.Rows.Count);
        Assert.Equal(2, projection.Problems.Rows.Count);
    }

    [Fact]
    public void Cursor_expiry_is_sticky_until_clear_and_pre_clear_overwrite_does_not_reopen_gap()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());
        hub.PublishDiagnostic(Problem("P100"));
        hub.PublishDiagnostic(Problem("P200"));
        hub.PublishDiagnostic(Problem("P300"));
        projection.Refresh();

        Assert.True(projection.Problems.HasHistoryGap);
        Assert.Equal(1, projection.Problems.TotalDropped);
        projection.Refresh();
        Assert.True(projection.Problems.HasHistoryGap);

        projection.Problems.ClearCommand.Execute(null);
        Assert.False(projection.Problems.HasHistoryGap);
        Assert.Equal(0, projection.Problems.TotalDropped);
        hub.PublishDiagnostic(Problem("P400"));
        projection.Refresh();
        Assert.False(projection.Problems.HasHistoryGap);
        Assert.Equal(1, projection.Problems.TotalDropped);
    }

    [Fact]
    public void Dispose_ignores_already_scheduled_tail_refresh()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 4);
        var scheduler = new QueuedScheduler();
        var projection = new StudioDiagnosticsPanelViewModel(hub, scheduler);
        hub.PublishDiagnostic(Problem("P100"));

        projection.Dispose();
        scheduler.DrainOne();

        Assert.Empty(projection.Problems.Rows);
    }

    private static StudioDiagnosticWrite Problem(
        string code,
        string? remediation = "Take action.",
        string message = "Problem occurred.",
        StudioDiagnosticSeverity severity = StudioDiagnosticSeverity.Warning) =>
        new(
            severity,
            StudioDiagnosticChannel.Problem,
            code,
            "project",
            Context("project-service"),
            message,
            remediation);

    private static StudioDiagnosticWrite DebugDiagnostic(string code) =>
        new(
            StudioDiagnosticSeverity.Debug,
            StudioDiagnosticChannel.Debug,
            code,
            "debug",
            Context("debug-service"),
            "Debug detail.");

    private static StudioDiagnosticWrite ProblemWithContext(
        string code,
        StudioDiagnosticContext context,
        string remediation = "Take action.") =>
        new(
            StudioDiagnosticSeverity.Warning,
            StudioDiagnosticChannel.Problem,
            code,
            "project",
            context,
            "Problem occurred.",
            remediation);

    private static StudioLogWrite Log(string message) =>
        new(
            StudioLogLevel.Information,
            "runtime",
            Context("frame-loop"),
            message,
            message);

    private static StudioLogWrite LogWithContext(
        string message,
        StudioDiagnosticContext context) =>
        new(
            StudioLogLevel.Information,
            "runtime",
            context,
            message,
            message);

    private static StudioLogRecord LogRecord(
        long sequence,
        DateTimeOffset timestamp,
        string message) =>
        new(
            sequence,
            timestamp,
            sequence,
            ManagedThreadId: 1,
            StudioLogLevel.Information,
            "runtime",
            Context("frame-loop"),
            message,
            message,
            [],
            WasTruncated: false);

    private static StudioDiagnosticContext Context(
        string component,
        StudioRecordOrigin origin = StudioRecordOrigin.Managed,
        string package = "studio",
        Guid? operationId = null,
        Guid? correlationId = null,
        StudioDataSensitivity sensitivity = StudioDataSensitivity.Public) =>
        new(
            origin,
            package,
            component,
            new StudioDiagnosticScope("project", "test", 1),
            operationId,
            correlationId,
            Sensitivity: sensitivity);

    private sealed class EmptyTruncatedLogSource : IStudioDiagnosticSource
    {
        public StudioProcessIdentity ProcessIdentity { get; } =
            StudioProcessIdentity.CreateNew();

        public int DiagnosticCapacity => 4;

        public int LogCapacity => 4;

        public long SubscriberFailureCount => 0;

        public int LogReadCount { get; private set; }

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            new(
                OldestAvailableSequence: 1,
                NextCursor: afterSequence,
                TotalDropped: 0,
                CursorExpired: false,
                Truncated: false,
                Items: []);

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit)
        {
            LogReadCount++;
            return new StudioCursorWindow<StudioLogRecord>(
                OldestAvailableSequence: 1,
                NextCursor: afterSequence,
                TotalDropped: 0,
                CursorExpired: false,
                Truncated: true,
                Items: []);
        }

        public StudioDiagnosticRecord? GetLatestDiagnostic() => null;

        public IDisposable Subscribe(Action invalidated) => NoOpSubscription.Instance;
    }

    private sealed class NoOpSubscription : IDisposable
    {
        public static NoOpSubscription Instance { get; } = new();

        public void Dispose()
        {
        }
    }

    private sealed class QueuedScheduler : IStudioDiagnosticsUiScheduler
    {
        private readonly Queue<Action> actions_ = [];
        private readonly object gate_ = new();

        public int PendingCount
        {
            get
            {
                lock (gate_)
                {
                    return actions_.Count;
                }
            }
        }

        public void Post(Action action)
        {
            lock (gate_)
            {
                actions_.Enqueue(action);
            }
        }

        public void DrainOne()
        {
            Action action;
            lock (gate_)
            {
                action = actions_.Dequeue();
            }

            action();
        }
    }
}
