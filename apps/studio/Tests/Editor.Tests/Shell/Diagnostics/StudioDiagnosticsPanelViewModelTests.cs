using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using Asharia.Studio.Application.Diagnostics;
using Editor.Shell.Diagnostics;
using Editor.Shell.Docking.Panels;
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
        projection.Problems.IsHistoryView = true;

        var console = Assert.Single(projection.Console.Rows);
        var problem = Assert.Single(projection.Problems.Rows);
        Assert.Equal("frame one", console.Message);
        Assert.Equal("P100", problem.Code);
        Assert.Equal("Fix the project setting.", problem.Remediation);
    }

    [Fact]
    public void Problems_default_to_active_incidents_and_history_includes_transitions()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        var problemId = new StudioProblemId("project/settings");
        hub.PublishDiagnostic(Problem(
            "P100",
            problemId: problemId,
            transition: StudioProblemTransition.Active));
        hub.PublishDiagnostic(Problem(
            "P100",
            problemId: problemId,
            transition: StudioProblemTransition.Resolved));
        hub.PublishDiagnostic(Problem(
            "P200",
            problemId: new StudioProblemId("project/other"),
            transition: StudioProblemTransition.Active));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        Assert.True(projection.Problems.IsActiveView);
        Assert.False(projection.Problems.ShowHistoryControls);
        Assert.Equal("P200", Assert.Single(projection.Problems.Rows).Code);

        projection.Problems.IsHistoryView = true;

        Assert.True(projection.Problems.ShowHistoryControls);
        Assert.Equal(
            ["P100", "P100", "P200"],
            projection.Problems.Rows.Select(static row => row.Code).ToArray());
        Assert.Equal(
            ["Active", "Resolved", "Active"],
            projection.Problems.Rows.Select(static row => row.StateText).ToArray());
    }

    [Fact]
    public void Active_problem_capacity_loss_is_visible_in_active_health()
    {
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 8,
            logCapacity: 8,
            activeProblemCapacity: 1);
        hub.PublishDiagnostic(Problem(
            "P100",
            problemId: new StudioProblemId("one"),
            transition: StudioProblemTransition.Active));
        hub.PublishDiagnostic(Problem(
            "P200",
            problemId: new StudioProblemId("two"),
            transition: StudioProblemTransition.Active));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        Assert.True(projection.Problems.IsActiveView);
        Assert.True(projection.Problems.HasHealthNotice);
        Assert.True(projection.Problems.HasDataLoss);
        Assert.Equal(1, projection.Problems.TotalDropped);
        Assert.Contains("1 activation(s) could not be retained", projection.Problems.HealthSummary, StringComparison.Ordinal);
        Assert.Contains("active list is incomplete", projection.Problems.HealthSummary, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("overwrote", projection.Problems.HealthSummary, StringComparison.OrdinalIgnoreCase);

        projection.Problems.IsHistoryView = true;

        Assert.False(projection.Problems.HasDataLoss);
    }

    [Fact]
    public void Projection_coalesces_invalidations_until_the_ui_scheduler_drains()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(hub, scheduler);
        projection.Problems.IsHistoryView = true;

        hub.PublishDiagnostic(Problem("P100"));
        hub.PublishDiagnostic(Problem("P200"));

        Assert.True(SpinWait.SpinUntil(
            () => scheduler.PendingCount == 1,
            TimeSpan.FromSeconds(2)));
        Assert.Equal(1, scheduler.PendingCount);
        Assert.Empty(projection.Problems.Rows);
        scheduler.DrainOne();
        Assert.Equal(2, projection.Problems.Rows.Count);
    }

    [Fact]
    public void Diagnostic_invalidation_refreshes_only_problems()
    {
        var source = new CountingDiagnosticSource();
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(source, scheduler);
        var startingLogReads = source.LogReadCount;
        var startingDiagnosticReads = source.DiagnosticReadCount;

        source.InvalidateDiagnostics();
        scheduler.DrainOne();

        Assert.Equal(startingLogReads, source.LogReadCount);
        Assert.Equal(startingDiagnosticReads + 1, source.DiagnosticReadCount);
    }

    [Fact]
    public void Log_invalidations_share_one_delayed_refresh_and_do_not_read_problems()
    {
        var source = new CountingDiagnosticSource();
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(source, scheduler);
        var startingLogReads = source.LogReadCount;
        var startingDiagnosticReads = source.DiagnosticReadCount;

        source.InvalidateLogs();
        source.InvalidateLogs();
        source.InvalidateLogs();

        Assert.Equal(1, scheduler.DelayedPendingCount);
        Assert.Equal(0, scheduler.PendingCount);
        scheduler.DrainDelayedOne();
        scheduler.DrainOne();

        Assert.Equal(startingLogReads + 1, source.LogReadCount);
        Assert.Equal(startingDiagnosticReads, source.DiagnosticReadCount);
    }

    [Fact]
    public void Search_rebuild_is_debounced_and_uses_the_latest_text()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(hub, scheduler);
        hub.PublishLog(Log("alpha"));
        hub.PublishLog(Log("alphabet"));
        hub.PublishLog(Log("beta"));
        Assert.True(SpinWait.SpinUntil(
            () => scheduler.DelayedPendingCount == 1,
            TimeSpan.FromSeconds(2)));
        scheduler.DrainDelayedOne();
        scheduler.DrainOne();

        projection.Console.SearchText = "a";
        projection.Console.SearchText = "al";
        projection.Console.SearchText = "alpha";

        Assert.Equal(3, projection.Console.Rows.Count);
        Assert.Equal(1, scheduler.DelayedPendingCount);
        scheduler.DrainDelayedOne();
        Assert.Equal(
            ["alpha", "alphabet"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
    }

    [Fact]
    public void Hidden_keep_alive_panel_advances_raw_state_without_rebuilding_rows()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(Log("before hide"));
        hub.PublishDiagnostic(Problem("P100"));
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(hub, scheduler);
        projection.Problems.IsHistoryView = true;
        var context = new EditorPanelLifecycleContext(
            "diagnostics",
            "Diagnostics",
            EditorDockArea.Bottom,
            IsFloatingWorkspace: false);

        projection.OnPanelHidden(context);
        hub.PublishLog(Log("while hidden"));
        hub.PublishDiagnostic(Problem("P200"));
        projection.Refresh();

        Assert.Equal(
            ["before hide"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(
            ["P100"],
            projection.Problems.Rows.Select(static row => row.Code).ToArray());

        projection.OnPanelShown(context);

        Assert.Equal(
            ["before hide", "while hidden"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(
            ["P100", "P200"],
            projection.Problems.Rows.Select(static row => row.Code).ToArray());
    }

    [Fact]
    public void Byte_budget_eviction_prunes_visible_timelines_to_the_source_retention_floor()
    {
        var hub = CreateHubWithTwoRecordByteBudgets();
        hub.PublishLog(Log("console 1"));
        hub.PublishDiagnostic(Problem("P100", message: "problem 1"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());
        projection.Problems.IsHistoryView = true;

        hub.PublishLog(Log("console 2"));
        hub.PublishLog(Log("console 3"));
        hub.PublishDiagnostic(Problem("P200", message: "problem 2"));
        hub.PublishDiagnostic(Problem("P300", message: "problem 3"));

        var logs = hub.ReadLogs(afterSequence: 1, maxCount: hub.LogCapacity);
        var problems = hub.ReadDiagnostics(
            afterSequence: 1,
            maxCount: hub.DiagnosticCapacity,
            StudioDiagnosticChannel.Problem);
        Assert.Equal(2, logs.OldestAvailableSequence);
        Assert.Equal(2, problems.OldestAvailableSequence);
        Assert.False(logs.CursorExpired);
        Assert.False(problems.CursorExpired);

        projection.Refresh();

        Assert.Equal(
            ["console 2", "console 3"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(
            ["P200", "P300"],
            projection.Problems.Rows.Select(static row => row.Code).ToArray());
    }

    [Fact]
    public void Hidden_keep_alive_prunes_byte_evicted_timelines_before_the_panel_is_shown()
    {
        var hub = CreateHubWithTwoRecordByteBudgets();
        hub.PublishLog(Log("console 1"));
        hub.PublishDiagnostic(Problem("P100", message: "problem 1"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());
        projection.Problems.IsHistoryView = true;
        var context = new EditorPanelLifecycleContext(
            "diagnostics",
            "Diagnostics",
            EditorDockArea.Bottom,
            IsFloatingWorkspace: false);

        projection.OnPanelHidden(context);
        hub.PublishLog(Log("console 2"));
        hub.PublishLog(Log("console 3"));
        hub.PublishDiagnostic(Problem("P200", message: "problem 2"));
        hub.PublishDiagnostic(Problem("P300", message: "problem 3"));
        projection.Refresh();

        Assert.Equal(
            "console 1",
            Assert.Single(projection.Console.Rows).Message);
        Assert.Equal("P100", Assert.Single(projection.Problems.Rows).Code);

        projection.OnPanelShown(context);

        Assert.Equal(
            ["console 2", "console 3"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(
            ["P200", "P300"],
            projection.Problems.Rows.Select(static row => row.Code).ToArray());
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
        projection.Problems.IsHistoryView = true;

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
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            scheduler);

        projection.Console.PauseCommand.Execute(null);
        hub.PublishLog(Log("matching while paused"));
        projection.Refresh();
        projection.Console.SearchText = "matching";
        scheduler.DrainAllDelayed();

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
        var scheduler = new QueuedScheduler();
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            scheduler);

        projection.Console.PauseCommand.Execute(null);
        hub.PublishLog(Log("after pause C"));
        hub.PublishLog(Log("after pause D"));
        projection.Refresh();
        projection.Console.SearchText = "before pause";
        scheduler.DrainAllDelayed();

        Assert.Equal(
            ["before pause A", "before pause B"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(2, projection.Console.UnseenCount);

        projection.Console.PauseCommand.Execute(null);
        projection.Console.SearchText = string.Empty;
        scheduler.DrainAllDelayed();

        Assert.Equal(
            ["after pause C", "after pause D"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.False(projection.Console.HasHistoryGap);
        Assert.Equal(2, projection.Console.TotalDropped);
    }

    [Fact]
    public void Pause_preserves_its_snapshot_across_byte_eviction_until_resume()
    {
        var hub = CreateHubWithTwoRecordByteBudgets();
        hub.PublishLog(Log("console 1"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        projection.Console.PauseCommand.Execute(null);
        hub.PublishLog(Log("console 2"));
        hub.PublishLog(Log("console 3"));
        projection.Refresh();

        Assert.Equal(
            "console 1",
            Assert.Single(projection.Console.Rows).Message);
        Assert.Equal(2, projection.Console.UnseenCount);
        Assert.Equal(2, hub.ReadLogs(1, hub.LogCapacity).OldestAvailableSequence);

        projection.Console.PauseCommand.Execute(null);

        Assert.Equal(
            ["console 2", "console 3"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
        Assert.Equal(0, projection.Console.UnseenCount);
    }

    [Fact]
    public void Console_defaults_to_strict_chronological_order_and_collapse_only_groups_adjacent_runs()
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
        projection.Problems.IsHistoryView = true;

        Assert.False(projection.Console.CollapseRepeated);
        Assert.Equal(
            ["same", "different", "same"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());

        projection.Console.CollapseRepeated = true;

        Assert.Equal(
            ["same", "different", "same"],
            projection.Console.Rows.Select(static row => row.Message).ToArray());
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
        projection.Problems.IsHistoryView = true;

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
        projection.Problems.IsHistoryView = true;

        Assert.Equal(6, projection.Console.Rows.Count);
        Assert.Equal(7, projection.Problems.Rows.Count);
    }

    [Fact]
    public void Console_collapse_preserves_monotonic_order_and_groups_only_adjacent_runs()
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

        projection.CollapseRepeated = true;
        projection.Rebuild(records, viewFloor: 0);

        Assert.Collection(
            projection.Rows,
            first =>
            {
                Assert.Equal(1, first.SequenceId);
                Assert.Equal(1, first.LastSequenceId);
                Assert.Equal(firstTime, first.TimestampUtc);
            },
            second =>
            {
                Assert.Equal(2, second.SequenceId);
                Assert.Equal(secondTime, second.TimestampUtc);
            },
            third =>
            {
                Assert.Equal(3, third.SequenceId);
                Assert.Equal(thirdTime, third.TimestampUtc);
            });
        Assert.True(projection.Rows[0].TimestampUtc <= projection.Rows[1].TimestampUtc);
        Assert.True(projection.Rows[1].TimestampUtc <= projection.Rows[2].TimestampUtc);
    }

    [Fact]
    public void Enabling_collapse_keeps_selection_on_a_non_adjacent_repeat()
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
        Assert.Equal(3, projection.Console.SelectedRow!.SequenceId);
        Assert.Equal(3, projection.Console.SelectedRow.LastSequenceId);
    }

    [Fact]
    public void Console_collapse_groups_an_adjacent_run_without_reordering_later_records()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 8);
        hub.PublishLog(Log("A"));
        hub.PublishLog(Log("A"));
        hub.PublishLog(Log("B"));
        using var projection = new StudioDiagnosticsPanelViewModel(
            hub,
            new QueuedScheduler());

        projection.Console.CollapseRepeated = true;

        Assert.Collection(
            projection.Console.Rows,
            first =>
            {
                Assert.Equal("A", first.Message);
                Assert.Equal(2, first.RepeatCount);
                Assert.Equal(1, first.SequenceId);
                Assert.Equal(2, first.LastSequenceId);
            },
            second => Assert.Equal("B", second.Message));
    }

    [Fact]
    public void Row_details_are_materialized_only_when_requested()
    {
        var log = new StudioConsoleRowViewModel(LogRecord(
            1,
            DateTimeOffset.UtcNow,
            "message"), 1);
        var problemRecord = new StudioDiagnosticHub(
            diagnosticCapacity: 1,
            logCapacity: 1).PublishDiagnostic(Problem("P100"));
        var problem = new StudioProblemRowViewModel(problemRecord, 1);

        Assert.False(log.IsDetailsMaterialized);
        Assert.False(problem.IsDetailsMaterialized);

        Assert.Contains("message", log.DetailsText, StringComparison.Ordinal);
        Assert.Contains("P100", problem.DetailsText, StringComparison.Ordinal);
        Assert.True(log.IsDetailsMaterialized);
        Assert.True(problem.IsDetailsMaterialized);
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
        projection.Problems.IsHistoryView = true;

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
        projection.Problems.IsHistoryView = true;
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
        Assert.Equal(0, scheduler.PendingCount);

        Assert.Empty(projection.Problems.Rows);
    }

    private static StudioDiagnosticWrite Problem(
        string code,
        string? remediation = "Take action.",
        string message = "Problem occurred.",
        StudioDiagnosticSeverity severity = StudioDiagnosticSeverity.Warning,
        StudioProblemId? problemId = null,
        StudioProblemTransition? transition = null) =>
        new(
            severity,
            StudioDiagnosticChannel.Problem,
            code,
            "project",
            Context("project-service"),
            message,
            remediation,
            ProblemId: problemId,
            ProblemTransition: transition);

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

    private static StudioDiagnosticHub CreateHubWithTwoRecordByteBudgets()
    {
        var calibration = new StudioDiagnosticHub(
            diagnosticCapacity: 8,
            logCapacity: 8);
        calibration.PublishDiagnostic(Problem("P000", message: "problem 0"));
        calibration.PublishLog(Log("console 0"));

        return new StudioDiagnosticHub(
            diagnosticCapacity: 8,
            logCapacity: 8,
            diagnosticByteCapacity:
                calibration.DiagnosticBufferState.EstimatedResidentPayloadBytes * 2,
            logByteCapacity:
                calibration.LogBufferState.EstimatedResidentPayloadBytes * 2);
    }

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

        public StudioDiagnosticBufferState DiagnosticBufferState => default;

        public StudioDiagnosticBufferState LogBufferState => default;

        public long DiagnosticSubscriberFailureCount => 0;

        public long LogSubscriberFailureCount => 0;

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

        public StudioActiveProblemSnapshot ReadActiveProblems() =>
            new(
                Version: 0,
                CountCapacity: DiagnosticCapacity,
                PayloadByteCapacity: 0,
                ResidentCount: 0,
                EstimatedResidentPayloadBytes: 0,
                TotalDropped: 0,
                IsIncomplete: false,
                Items: []);

        public IDisposable SubscribeDiagnostics(Action invalidated) =>
            NoOpSubscription.Instance;

        public IDisposable SubscribeLogs(Action invalidated) =>
            NoOpSubscription.Instance;
    }

    private sealed class CountingDiagnosticSource : IStudioDiagnosticSource
    {
        private Action? diagnosticInvalidated_;
        private Action? logInvalidated_;

        public StudioProcessIdentity ProcessIdentity { get; } =
            StudioProcessIdentity.CreateNew();

        public int DiagnosticCapacity => 4;

        public int LogCapacity => 4;

        public long SubscriberFailureCount => 0;

        public StudioDiagnosticBufferState DiagnosticBufferState => default;

        public StudioDiagnosticBufferState LogBufferState => default;

        public long DiagnosticSubscriberFailureCount => 0;

        public long LogSubscriberFailureCount => 0;

        public int DiagnosticReadCount { get; private set; }

        public int LogReadCount { get; private set; }

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null)
        {
            DiagnosticReadCount++;
            return EmptyWindow<StudioDiagnosticRecord>(afterSequence);
        }

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit)
        {
            LogReadCount++;
            return EmptyWindow<StudioLogRecord>(afterSequence);
        }

        public StudioDiagnosticRecord? GetLatestDiagnostic() => null;

        public StudioActiveProblemSnapshot ReadActiveProblems() =>
            new(
                Version: 0,
                CountCapacity: DiagnosticCapacity,
                PayloadByteCapacity: 0,
                ResidentCount: 0,
                EstimatedResidentPayloadBytes: 0,
                TotalDropped: 0,
                IsIncomplete: false,
                Items: []);

        public IDisposable SubscribeDiagnostics(Action invalidated)
        {
            diagnosticInvalidated_ = invalidated;
            return NoOpSubscription.Instance;
        }

        public IDisposable SubscribeLogs(Action invalidated)
        {
            logInvalidated_ = invalidated;
            return NoOpSubscription.Instance;
        }

        public void InvalidateDiagnostics() => diagnosticInvalidated_?.Invoke();

        public void InvalidateLogs() => logInvalidated_?.Invoke();

        private static StudioCursorWindow<T> EmptyWindow<T>(long afterSequence) =>
            new(
                OldestAvailableSequence: 1,
                NextCursor: afterSequence,
                TotalDropped: 0,
                CursorExpired: false,
                Truncated: false,
                Items: []);
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
        private readonly Queue<ScheduledAction> delayedActions_ = [];
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

        public int DelayedPendingCount
        {
            get
            {
                lock (gate_)
                {
                    return delayedActions_.Count(static item => !item.IsCancelled);
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


        public IDisposable Schedule(Action action, TimeSpan delay)
        {
            var scheduled = new ScheduledAction(action);
            lock (gate_)
            {
                delayedActions_.Enqueue(scheduled);
            }

            return scheduled;
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

        public void DrainDelayedOne()
        {
            ScheduledAction scheduled;
            lock (gate_)
            {
                scheduled = delayedActions_.First(static item => !item.IsCancelled);
                while (!ReferenceEquals(delayedActions_.Dequeue(), scheduled))
                {
                }
            }

            scheduled.Invoke();
        }

        public void DrainAllDelayed()
        {
            while (DelayedPendingCount > 0)
            {
                DrainDelayedOne();
            }
        }

        private sealed class ScheduledAction(Action action) : IDisposable
        {
            public bool IsCancelled { get; private set; }

            public void Dispose() => IsCancelled = true;

            public void Invoke()
            {
                if (!IsCancelled)
                {
                    action();
                }
            }
        }
    }
}
