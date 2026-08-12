using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Xunit;

namespace Asharia.Studio.Application.Tests.Diagnostics;

public sealed class StudioDiagnosticHubTests
{
    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData(" leading")]
    [InlineData("trailing ")]
    public void Problem_identity_rejects_noncanonical_text(string value)
    {
        Assert.Throws<ArgumentException>(() => new StudioProblemId(value));
    }

    [Fact]
    public void Problem_identity_rejects_oversized_text()
    {
        Assert.Throws<ArgumentException>(() =>
            new StudioProblemId(new string('x', StudioProblemId.MaxLength + 1)));
    }

    [Fact]
    public void PublishDiagnostic_records_structured_process_context_and_notifies()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var changedCount = 0;
        using var subscription = hub.SubscribeDiagnostics(() => changedCount++);

        var operationId = Guid.NewGuid();
        var correlationId = Guid.NewGuid();
        var record = hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Warning,
            StudioDiagnosticChannel.Problem,
            "studio.test.warning",
            "test",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                "asharia.studio.tests",
                "diagnostic-hub",
                StudioDiagnosticScope.Process(hub.ProcessIdentity),
                operationId,
                correlationId),
            "Warning message.",
            "Inspect the test input.",
            [new StudioDiagnosticAttribute("case", "structured")]));

        Assert.Equal(1, record.SequenceId);
        Assert.NotEqual(default, record.TimestampUtc);
        Assert.True(record.MonotonicTimestamp > 0);
        Assert.Equal("studio.test.warning", record.Code);
        Assert.Equal(operationId, record.Context.OperationId);
        Assert.Equal(correlationId, record.Context.CorrelationId);
        Assert.Equal(hub.ProcessIdentity.ToString(), record.Context.Scope.Identity);
        Assert.Equal(64, record.Fingerprint.Length);
        Assert.Same(record, Assert.Single(hub.ReadDiagnostics(maxCount: 2).Items));
        Assert.True(SpinWait.SpinUntil(
            () => Volatile.Read(ref changedCount) == 1,
            TimeSpan.FromSeconds(2)));
    }

    [Fact]
    public void Diagnostic_ring_overwrites_in_constant_space_and_exposes_cursor_loss()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);

        PublishDiagnostic(hub, "studio.test.one", StudioDiagnosticChannel.Debug);
        var second = PublishDiagnostic(hub, "studio.test.two", StudioDiagnosticChannel.Debug);
        var third = PublishDiagnostic(hub, "studio.test.three", StudioDiagnosticChannel.Problem);

        var window = hub.ReadDiagnostics(afterSequence: 0, maxCount: 2);

        Assert.Collection(
            window.Items,
            item => Assert.Same(second, item),
            item => Assert.Same(third, item));
        Assert.Equal(2, window.OldestAvailableSequence);
        Assert.Equal(3, window.NextCursor);
        Assert.True(window.CursorExpired);
        Assert.Equal(1, window.TotalDropped);
    }

    [Fact]
    public void Diagnostic_filter_advances_cursor_without_building_a_second_store()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        PublishDiagnostic(hub, "studio.test.debug", StudioDiagnosticChannel.Debug);
        var problem = PublishDiagnostic(
            hub,
            "studio.test.problem",
            StudioDiagnosticChannel.Problem);

        var window = hub.ReadDiagnostics(
            maxCount: 4,
            channel: StudioDiagnosticChannel.Problem);

        Assert.Same(problem, Assert.Single(window.Items));
        Assert.Equal(2, window.NextCursor);
    }

    [Fact]
    public void Active_problem_survives_history_wrap_until_explicit_resolution()
    {
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 2,
            logCapacity: 2,
            activeProblemCapacity: 2);
        var problemId = new StudioProblemId("studio.test.problem:project");
        var active = PublishProblem(
            hub,
            problemId,
            StudioProblemTransition.Active,
            "studio.test.problem.active");

        PublishDiagnostic(hub, "studio.test.history.one", StudioDiagnosticChannel.Debug);
        PublishDiagnostic(hub, "studio.test.history.two", StudioDiagnosticChannel.Debug);

        var beforeResolution = hub.ReadActiveProblems();
        Assert.Equal(1, beforeResolution.Version);
        Assert.Equal(2, beforeResolution.CountCapacity);
        Assert.Equal(0, beforeResolution.TotalDropped);
        Assert.False(beforeResolution.IsIncomplete);
        Assert.True(beforeResolution.EstimatedResidentPayloadBytes > 0);
        Assert.Same(active, Assert.Single(beforeResolution.Items));
        Assert.DoesNotContain(active, hub.ReadDiagnostics(maxCount: 2).Items);

        PublishProblem(
            hub,
            problemId,
            StudioProblemTransition.Resolved,
            "studio.test.problem.resolved");

        var afterResolution = hub.ReadActiveProblems();
        Assert.Equal(2, afterResolution.Version);
        Assert.Empty(afterResolution.Items);
        Assert.Equal(0, afterResolution.EstimatedResidentPayloadBytes);
        Assert.Equal(0, afterResolution.TotalDropped);
        Assert.False(afterResolution.IsIncomplete);
    }

    [Fact]
    public void Active_problem_capacity_overflow_is_fail_visible_without_eviction()
    {
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 4,
            logCapacity: 2,
            activeProblemCapacity: 1);
        var retained = PublishProblem(
            hub,
            new StudioProblemId("studio.test.problem:retained"),
            StudioProblemTransition.Active,
            "studio.test.problem.retained");

        PublishProblem(
            hub,
            new StudioProblemId("studio.test.problem:dropped"),
            StudioProblemTransition.Active,
            "studio.test.problem.dropped");

        var snapshot = hub.ReadActiveProblems();
        Assert.Equal(2, snapshot.Version);
        Assert.Equal(1, snapshot.CountCapacity);
        Assert.Equal(1, snapshot.TotalDropped);
        Assert.True(snapshot.IsIncomplete);
        Assert.Same(retained, Assert.Single(snapshot.Items));
        Assert.Equal(2, hub.ReadDiagnostics(maxCount: 4).Items.Length);
    }

    [Fact]
    public void Active_problem_payload_budget_overflow_is_fail_visible_and_resolution_releases_bytes()
    {
        var calibration = new StudioDiagnosticHub(
            diagnosticCapacity: 4,
            logCapacity: 2,
            activeProblemCapacity: 2,
            activeProblemByteCapacity: 1024 * 1024);
        var retainedId = new StudioProblemId("studio.test.problem:payload-retained");
        PublishProblem(
            calibration,
            retainedId,
            StudioProblemTransition.Active,
            "studio.test.problem.payload");
        var recordBytes = calibration.ReadActiveProblems()
            .EstimatedResidentPayloadBytes;
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 4,
            logCapacity: 2,
            activeProblemCapacity: 2,
            activeProblemByteCapacity: recordBytes);
        var retained = PublishProblem(
            hub,
            retainedId,
            StudioProblemTransition.Active,
            "studio.test.problem.payload");

        PublishProblem(
            hub,
            new StudioProblemId("studio.test.problem:payload-dropped"),
            StudioProblemTransition.Active,
            "studio.test.problem.payload");

        var saturated = hub.ReadActiveProblems();
        Assert.Equal(recordBytes, saturated.PayloadByteCapacity);
        Assert.Equal(recordBytes, saturated.EstimatedResidentPayloadBytes);
        Assert.Equal(1, saturated.TotalDropped);
        Assert.True(saturated.IsIncomplete);
        Assert.Same(retained, Assert.Single(saturated.Items));

        PublishProblem(
            hub,
            retainedId,
            StudioProblemTransition.Resolved,
            "studio.test.problem.resolved");

        var resolved = hub.ReadActiveProblems();
        Assert.Empty(resolved.Items);
        Assert.Equal(0, resolved.EstimatedResidentPayloadBytes);
        Assert.Equal(1, resolved.TotalDropped);
        Assert.True(resolved.IsIncomplete);
    }

    [Fact]
    public void History_only_problem_record_does_not_mutate_active_index()
    {
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 2,
            logCapacity: 2,
            activeProblemCapacity: 1);

        PublishDiagnostic(hub, "studio.test.problem.history", StudioDiagnosticChannel.Problem);

        var snapshot = hub.ReadActiveProblems();
        Assert.Equal(0, snapshot.Version);
        Assert.Equal(0, snapshot.TotalDropped);
        Assert.Empty(snapshot.Items);
    }

    [Fact]
    public async Task Log_ingress_is_thread_safe_bounded_and_ordered_by_sequence()
    {
        const int producerCount = 8;
        const int recordsPerProducer = 2000;
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 2,
            logCapacity: 128);

        await Task.WhenAll(Enumerable.Range(0, producerCount).Select(producer =>
            Task.Run(() =>
            {
                for (var index = 0; index < recordsPerProducer; index++)
                {
                    PublishLog(hub, $"producer-{producer}-{index}");
                }
            })));

        var window = hub.ReadLogs(maxCount: hub.LogCapacity);
        Assert.Equal(hub.LogCapacity, window.Items.Length);
        Assert.Equal(
            producerCount * recordsPerProducer - hub.LogCapacity,
            window.TotalDropped);
        Assert.Equal(window.TotalDropped, hub.LogBufferState.TotalDropped);
        Assert.True(window.CursorExpired);
        Assert.True(window.Items
            .Zip(window.Items.Skip(1))
            .All(pair => pair.First.SequenceId < pair.Second.SequenceId));
    }

    [Fact]
    public void Buffer_state_reports_exact_normalized_utf8_payload_bytes()
    {
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 2,
            logCapacity: 2,
            diagnosticByteCapacity: 1024 * 1024,
            logByteCapacity: 1024 * 1024);
        var problemId = new StudioProblemId("studio.test:多字节");
        var diagnostic = hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Error,
            StudioDiagnosticChannel.Problem,
            "studio.test.utf8",
            "诊断",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                "asharia.测试",
                "组件",
                new StudioDiagnosticScope("场景", "身份", Generation: 1)),
            "消息🙂",
            "修复",
            [new StudioDiagnosticAttribute("键", "值")],
            problemId,
            StudioProblemTransition.Active));
        var log = hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "日志",
            diagnostic.Context,
            "模板 {值}",
            "渲染🙂",
            [new StudioDiagnosticAttribute("参数", "内容")]));

        Assert.Equal(
            DiagnosticPayloadBytes(diagnostic),
            hub.DiagnosticBufferState.EstimatedResidentPayloadBytes);
        Assert.Equal(
            LogPayloadBytes(log),
            hub.LogBufferState.EstimatedResidentPayloadBytes);
        Assert.Equal(
            DiagnosticPayloadBytes(diagnostic),
            hub.ReadActiveProblems().EstimatedResidentPayloadBytes);
    }

    [Fact]
    public async Task Concurrent_problem_transitions_follow_their_reserved_sequence_order()
    {
        const int transitionCount = 100;
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 128,
            logCapacity: 2,
            activeProblemCapacity: 2);
        var problemId = new StudioProblemId("studio.test.problem:concurrent");

        var records = await Task.WhenAll(
            Enumerable.Range(0, transitionCount)
                .Select(index => Task.Run(() => PublishProblem(
                    hub,
                    problemId,
                    index % 2 == 0
                        ? StudioProblemTransition.Active
                        : StudioProblemTransition.Resolved,
                    "studio.test.problem.concurrent"))));

        var latest = records.MaxBy(record => record.SequenceId)!;
        var snapshot = hub.ReadActiveProblems();
        Assert.Equal(transitionCount, snapshot.Version);
        Assert.Equal(0, snapshot.TotalDropped);
        if (latest.ProblemTransition == StudioProblemTransition.Active)
        {
            Assert.Same(latest, Assert.Single(snapshot.Items));
        }
        else
        {
            Assert.Empty(snapshot.Items);
            Assert.Equal(0, snapshot.EstimatedResidentPayloadBytes);
        }
    }

    [Fact]
    public void Diagnostic_ring_evicts_oldest_records_at_the_byte_budget()
    {
        var calibration = new StudioDiagnosticHub(
            diagnosticCapacity: 4,
            logCapacity: 2,
            diagnosticByteCapacity: 1024 * 1024);
        PublishDiagnostic(
            calibration,
            "studio.test.byte-budget",
            StudioDiagnosticChannel.Debug);
        var recordBytes = calibration.DiagnosticBufferState.EstimatedResidentPayloadBytes;
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 4,
            logCapacity: 2,
            diagnosticByteCapacity: recordBytes * 2);

        PublishDiagnostic(hub, "studio.test.byte-budget", StudioDiagnosticChannel.Debug);
        var second = PublishDiagnostic(
            hub,
            "studio.test.byte-budget",
            StudioDiagnosticChannel.Debug);
        var third = PublishDiagnostic(
            hub,
            "studio.test.byte-budget",
            StudioDiagnosticChannel.Debug);

        var state = hub.DiagnosticBufferState;
        var window = hub.ReadDiagnostics(maxCount: 4);
        Assert.Equal(4, state.CountCapacity);
        Assert.Equal(recordBytes * 2, state.PayloadByteCapacity);
        Assert.Equal(2, state.ResidentCount);
        Assert.Equal(recordBytes * 2, state.EstimatedResidentPayloadBytes);
        Assert.Equal(1, state.TotalDropped);
        Assert.Collection(
            window.Items,
            item => Assert.Same(second, item),
            item => Assert.Same(third, item));
        Assert.Equal(state.TotalDropped, window.TotalDropped);
    }

    [Fact]
    public void Oversized_log_is_dropped_without_consuming_resident_bytes()
    {
        var calibration = new StudioDiagnosticHub(
            diagnosticCapacity: 2,
            logCapacity: 2,
            logByteCapacity: 1024 * 1024);
        PublishLog(calibration, "oversized");
        var recordBytes = calibration.LogBufferState.EstimatedResidentPayloadBytes;
        var hub = new StudioDiagnosticHub(
            diagnosticCapacity: 2,
            logCapacity: 2,
            logByteCapacity: recordBytes - 1);

        PublishLog(hub, "oversized");

        var state = hub.LogBufferState;
        var window = hub.ReadLogs(maxCount: 2);
        Assert.Equal(0, state.ResidentCount);
        Assert.Equal(0, state.EstimatedResidentPayloadBytes);
        Assert.Equal(1, state.TotalDropped);
        Assert.Empty(window.Items);
        Assert.Equal(1, window.NextCursor);
        Assert.Equal(state.TotalDropped, window.TotalDropped);
    }

    [Fact]
    public async Task Older_inflight_record_cannot_displace_a_newer_record_at_the_byte_budget()
    {
        var ring = new BoundedConcurrentRing<WeightedRingRecord>(
            capacity: 4,
            byteCapacity: 4,
            record => record.SequenceId,
            record => record.ByteCount);
        using var firstReserved = new ManualResetEventSlim();
        using var releaseFirst = new ManualResetEventSlim();
        var firstPublish = Task.Run(() => ring.Publish(sequence =>
        {
            firstReserved.Set();
            Assert.True(releaseFirst.Wait(TimeSpan.FromSeconds(5)));
            return new WeightedRingRecord(sequence, ByteCount: 4);
        }));
        Assert.True(firstReserved.Wait(TimeSpan.FromSeconds(2)));
        var second = ring.Publish(sequence => new WeightedRingRecord(
            sequence,
            ByteCount: 4));

        releaseFirst.Set();
        await firstPublish;

        var state = ring.GetState();
        var window = ring.Read(afterSequence: 0, maxCount: 4);
        Assert.Equal(1, state.ResidentCount);
        Assert.Equal(4, state.EstimatedResidentPayloadBytes);
        Assert.Equal(1, state.TotalDropped);
        Assert.Same(second, Assert.Single(window.Items));
        Assert.Equal(2, window.NextCursor);
        Assert.Equal(state.TotalDropped, window.TotalDropped);
    }

    [Fact]
    public async Task Ring_reader_does_not_advance_past_an_inflight_sequence()
    {
        var ring = new BoundedConcurrentRing<RingRecord>(
            capacity: 4,
            record => record.SequenceId);
        using var firstReserved = new ManualResetEventSlim();
        using var releaseFirst = new ManualResetEventSlim();
        var firstPublish = Task.Run(() => ring.Publish(sequence =>
        {
            firstReserved.Set();
            Assert.True(releaseFirst.Wait(TimeSpan.FromSeconds(5)));
            return new RingRecord(sequence);
        }));
        Assert.True(firstReserved.Wait(TimeSpan.FromSeconds(2)));

        var second = ring.Publish(sequence => new RingRecord(sequence));
        var during = ring.Read(afterSequence: 0, maxCount: 4);

        Assert.Equal(2, second.SequenceId);
        Assert.Empty(during.Items);
        Assert.Equal(0, during.NextCursor);
        Assert.True(during.Truncated);
        Assert.Equal(0, during.TotalDropped);
        Assert.Equal(1, ring.PublicationVersion);

        releaseFirst.Set();
        var first = await firstPublish;
        var after = ring.Read(afterSequence: during.NextCursor, maxCount: 4);

        Assert.Equal(1, first.SequenceId);
        Assert.Collection(
            after.Items,
            item => Assert.Same(first, item),
            item => Assert.Same(second, item));
        Assert.Equal(2, after.NextCursor);
        Assert.False(after.Truncated);
        Assert.Equal(0, after.TotalDropped);
        Assert.Equal(2, ring.PublicationVersion);
    }

    [Fact]
    public async Task Ring_reservations_do_not_expire_a_committed_record_before_overwrite()
    {
        var ring = new BoundedConcurrentRing<RingRecord>(
            capacity: 2,
            record => record.SequenceId);
        var first = ring.Publish(sequence => new RingRecord(sequence));
        using var bothReserved = new CountdownEvent(2);
        using var release = new ManualResetEventSlim();

        Task<RingRecord> ReserveAndWait() => Task.Run(() => ring.Publish(sequence =>
        {
            bothReserved.Signal();
            Assert.True(release.Wait(TimeSpan.FromSeconds(5)));
            return new RingRecord(sequence);
        }));

        var secondPublish = ReserveAndWait();
        var thirdPublish = ReserveAndWait();
        Assert.True(bothReserved.Wait(TimeSpan.FromSeconds(2)));

        StudioCursorWindow<RingRecord> during;
        try
        {
            during = ring.Read(afterSequence: 0, maxCount: 2);

            Assert.Same(first, Assert.Single(during.Items));
            Assert.Equal(1, during.OldestAvailableSequence);
            Assert.Equal(1, during.NextCursor);
            Assert.Equal(0, during.TotalDropped);
            Assert.False(during.CursorExpired);
            Assert.True(during.Truncated);
            Assert.Same(first, ring.GetLatest());
        }
        finally
        {
            release.Set();
        }

        await Task.WhenAll(secondPublish, thirdPublish);
        var after = ring.Read(afterSequence: during.NextCursor, maxCount: 2);

        Assert.Equal([2L, 3L], after.Items.Select(item => item.SequenceId).ToArray());
        Assert.Equal(2, after.OldestAvailableSequence);
        Assert.Equal(3, after.NextCursor);
        Assert.Equal(1, after.TotalDropped);
        Assert.False(after.CursorExpired);
        Assert.False(after.Truncated);
    }

    [Fact]
    public void Ring_read_keeps_a_future_cursor_monotonic()
    {
        var ring = new BoundedConcurrentRing<RingRecord>(
            capacity: 2,
            record => record.SequenceId);
        ring.Publish(sequence => new RingRecord(sequence));

        var window = ring.Read(afterSequence: 100, maxCount: 2);

        Assert.Empty(window.Items);
        Assert.Equal(100, window.NextCursor);
        Assert.False(window.CursorExpired);
        Assert.False(window.Truncated);
    }

    [Fact]
    public async Task Ring_high_completion_expires_only_the_fixed_recent_window()
    {
        var ring = new BoundedConcurrentRing<RingRecord>(
            capacity: 2,
            record => record.SequenceId);
        using var bothReserved = new CountdownEvent(2);
        using var release = new ManualResetEventSlim();

        Task<RingRecord> ReserveAndWait() => Task.Run(() => ring.Publish(sequence =>
        {
            bothReserved.Signal();
            Assert.True(release.Wait(TimeSpan.FromSeconds(5)));
            return new RingRecord(sequence);
        }));

        var firstPublish = ReserveAndWait();
        var secondPublish = ReserveAndWait();
        Assert.True(bothReserved.Wait(TimeSpan.FromSeconds(2)));
        var third = ring.Publish(sequence => new RingRecord(sequence));

        try
        {
            var during = ring.Read(afterSequence: 0, maxCount: 2);

            Assert.Empty(during.Items);
            Assert.Equal(2, during.OldestAvailableSequence);
            Assert.Equal(0, during.NextCursor);
            Assert.Equal(1, during.TotalDropped);
            Assert.True(during.CursorExpired);
            Assert.True(during.Truncated);
            Assert.Same(third, ring.GetLatest());
        }
        finally
        {
            release.Set();
        }

        await Task.WhenAll(firstPublish, secondPublish);
        var after = ring.Read(afterSequence: 0, maxCount: 2);

        Assert.Equal([2L, 3L], after.Items.Select(item => item.SequenceId).ToArray());
        Assert.Equal(1, after.TotalDropped);
    }

    [Fact]
    public void Ring_factory_failure_is_counted_and_does_not_stall_the_cursor()
    {
        var ring = new BoundedConcurrentRing<RingRecord>(
            capacity: 4,
            record => record.SequenceId);

        Assert.Throws<InvalidOperationException>(() =>
            ring.Publish(_ => throw new InvalidOperationException("factory failed")));
        var second = ring.Publish(sequence => new RingRecord(sequence));
        var window = ring.Read(afterSequence: 0, maxCount: 4);

        Assert.Same(second, Assert.Single(window.Items));
        Assert.Equal(2, window.NextCursor);
        Assert.Equal(1, window.TotalDropped);
        Assert.False(window.Truncated);
    }

    [Fact]
    public void Subscriber_failure_is_isolated_and_healthy_subscriber_runs()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var healthyCount = 0;
        using var failing = hub.SubscribeDiagnostics(
            () => throw new InvalidOperationException("boom"));
        using var healthy = hub.SubscribeDiagnostics(() => healthyCount++);

        PublishDiagnostic(hub, "studio.test.failure", StudioDiagnosticChannel.Problem);

        Assert.True(SpinWait.SpinUntil(
            () => hub.SubscriberFailureCount == 1
                && Volatile.Read(ref healthyCount) == 1,
            TimeSpan.FromSeconds(2)));
    }

    [Fact]
    public void Diagnostic_and_log_invalidation_failures_are_independent()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        using var diagnosticFailure = hub.SubscribeDiagnostics(
            () => throw new InvalidOperationException("diagnostic failure"));
        using var logFailure = hub.SubscribeLogs(
            () => throw new InvalidOperationException("log failure"));

        PublishDiagnostic(hub, "studio.test.failure", StudioDiagnosticChannel.Problem);

        Assert.True(SpinWait.SpinUntil(
            () => hub.DiagnosticSubscriberFailureCount == 1,
            TimeSpan.FromSeconds(2)));
        Assert.Equal(0, hub.LogSubscriberFailureCount);

        PublishLog(hub, "failure");
        Assert.True(SpinWait.SpinUntil(
            () => hub.LogSubscriberFailureCount == 1,
            TimeSpan.FromSeconds(2)));
        Assert.Equal(1, hub.DiagnosticSubscriberFailureCount);
        Assert.Equal(2, hub.SubscriberFailureCount);
    }

    [Fact]
    public void Diagnostic_publish_does_not_wait_for_a_blocked_subscriber()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 64, logCapacity: 2);
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var subscription = hub.SubscribeDiagnostics(() =>
        {
            entered.Set();
            release.Wait(TimeSpan.FromSeconds(5));
        });

        PublishDiagnostic(hub, "studio.test.prime", StudioDiagnosticChannel.Debug);
        Assert.True(entered.Wait(TimeSpan.FromSeconds(2)));

        var stopwatch = Stopwatch.StartNew();
        for (var index = 0; index < 1000; index++)
        {
            PublishDiagnostic(
                hub,
                index.ToString(System.Globalization.CultureInfo.InvariantCulture),
                StudioDiagnosticChannel.Debug);
        }

        stopwatch.Stop();
        release.Set();

        Assert.True(stopwatch.Elapsed < TimeSpan.FromSeconds(1));
        Assert.Equal(
            hub.DiagnosticCapacity,
            hub.ReadDiagnostics(maxCount: hub.DiagnosticCapacity).Items.Length);
    }

    [Fact]
    public void Diagnostic_invalidation_repeats_after_a_complete_problem_commit_while_dispatch_is_blocked()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        using var firstNotificationEntered = new ManualResetEventSlim();
        using var releaseFirstNotification = new ManualResetEventSlim();
        var notificationCount = 0;
        using var subscription = hub.SubscribeDiagnostics(() =>
        {
            if (Interlocked.Increment(ref notificationCount) == 1)
            {
                firstNotificationEntered.Set();
                releaseFirstNotification.Wait(TimeSpan.FromSeconds(5));
            }
        });

        PublishDiagnostic(hub, "studio.test.prime", StudioDiagnosticChannel.Debug);
        Assert.True(firstNotificationEntered.Wait(TimeSpan.FromSeconds(2)));

        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Error,
            StudioDiagnosticChannel.Problem,
            "studio.test.active-while-dispatch-blocked",
            "test",
            ManagedContext(hub),
            "active",
            ProblemId: new StudioProblemId("studio.test:active-while-dispatch-blocked"),
            ProblemTransition: StudioProblemTransition.Active));
        releaseFirstNotification.Set();

        Assert.True(SpinWait.SpinUntil(
            () => Volatile.Read(ref notificationCount) >= 2,
            TimeSpan.FromSeconds(2)));
        Assert.Equal(
            "studio.test:active-while-dispatch-blocked",
            Assert.Single(hub.ReadActiveProblems().Items).ProblemId?.Value);
    }

    [Fact]
    public void Subprocess_output_mapping_preserves_stream_operation_and_correlation()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var operationId = Guid.NewGuid();
        var correlationId = Guid.NewGuid();

        var record = hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Error,
            "stderr",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Subprocess,
                "asharia.tooling",
                "compiler",
                StudioDiagnosticScope.Process(hub.ProcessIdentity),
                operationId,
                correlationId,
                Sensitivity: StudioDataSensitivity.ProjectPath),
            "Compiler exited with {ExitCode}.",
            "Compiler exited with 1.",
            [new StudioDiagnosticAttribute("exitCode", "1")]));

        Assert.Equal("stderr", record.Channel);
        Assert.Equal(StudioRecordOrigin.Subprocess, record.Context.Origin);
        Assert.Equal(operationId, record.Context.OperationId);
        Assert.Equal(correlationId, record.Context.CorrelationId);
        Assert.Equal(StudioDataSensitivity.ProjectPath, record.Context.Sensitivity);
    }

    [Fact]
    public void Subscriber_capacity_is_hard_bounded_and_leases_release_slots()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var subscriptions = Enumerable.Range(0, StudioDiagnosticHub.MaxSubscriberCount)
            .Select(_ => hub.SubscribeDiagnostics(static () => { }))
            .ToArray();

        Assert.Throws<InvalidOperationException>(() =>
            hub.SubscribeDiagnostics(static () => { }));

        subscriptions[0].Dispose();
        using var replacement = hub.SubscribeDiagnostics(static () => { });
        foreach (var subscription in subscriptions)
        {
            subscription.Dispose();
        }
    }

    [Fact]
    public void Log_publish_does_not_wait_for_a_blocked_subscriber()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 64);
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var subscription = hub.SubscribeLogs(() =>
        {
            entered.Set();
            release.Wait(TimeSpan.FromSeconds(5));
        });

        PublishLog(hub, "prime");
        Assert.True(entered.Wait(TimeSpan.FromSeconds(2)));

        var stopwatch = Stopwatch.StartNew();
        for (var index = 0; index < 1000; index++)
        {
            PublishLog(hub, index.ToString(System.Globalization.CultureInfo.InvariantCulture));
        }

        stopwatch.Stop();
        release.Set();

        Assert.True(stopwatch.Elapsed < TimeSpan.FromSeconds(1));
        Assert.Equal(hub.LogCapacity, hub.ReadLogs(maxCount: hub.LogCapacity).Items.Length);
    }

    [Fact]
    public void Disposed_subscription_receives_no_shutdown_tail_invalidation()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var changedCount = 0;
        var subscription = hub.SubscribeDiagnostics(() => changedCount++);
        subscription.Dispose();
        using var dispatched = new ManualResetEventSlim();
        using var healthy = hub.SubscribeDiagnostics(dispatched.Set);

        PublishDiagnostic(hub, "studio.test.shutdown", StudioDiagnosticChannel.Debug);
        Assert.True(dispatched.Wait(TimeSpan.FromSeconds(2)));

        Assert.Equal(0, changedCount);
        Assert.Single(hub.ReadDiagnostics(maxCount: 2).Items);
    }

    [Fact]
    public void Oversized_fields_and_attributes_are_truncated_with_evidence()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var attributes = Enumerable.Range(0, 20)
            .Select(index => new StudioDiagnosticAttribute(
                $"attribute-{index}",
                new string('x', 300)))
            .ToImmutableArray();

        var record = hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Info,
            StudioDiagnosticChannel.Debug,
            new string('c', 200),
            "test",
            ManagedContext(hub),
            new string('m', 5000),
            Attributes: attributes));

        Assert.True(record.WasTruncated);
        Assert.Equal(128, record.Code.Length);
        Assert.Equal(4096, record.Message.Length);
        Assert.Equal(16, record.Attributes.Length);
        Assert.All(record.Attributes, attribute => Assert.True(attribute.Value.Length <= 256));
    }

    [Fact]
    public void Constructor_and_publish_reject_invalid_bounds_and_required_fields()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new StudioDiagnosticHub(diagnosticCapacity: 0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new StudioDiagnosticHub(logCapacity: 0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new StudioDiagnosticHub(diagnosticByteCapacity: 0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new StudioDiagnosticHub(logByteCapacity: 0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new StudioDiagnosticHub(activeProblemCapacity: 0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new StudioDiagnosticHub(activeProblemByteCapacity: 0));

        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        Assert.Throws<ArgumentException>(() => hub.PublishDiagnostic(
            new StudioDiagnosticWrite(
                StudioDiagnosticSeverity.Info,
                StudioDiagnosticChannel.Debug,
                " ",
                "test",
                ManagedContext(hub),
                "message")));
        Assert.Throws<ArgumentException>(() => hub.PublishDiagnostic(
            new StudioDiagnosticWrite(
                StudioDiagnosticSeverity.Error,
                StudioDiagnosticChannel.Debug,
                "studio.test.invalid-problem-channel",
                "test",
                ManagedContext(hub),
                "message",
                ProblemId: new StudioProblemId("studio.test:invalid"),
                ProblemTransition: StudioProblemTransition.Active)));
        Assert.Throws<ArgumentException>(() => hub.PublishDiagnostic(
            new StudioDiagnosticWrite(
                StudioDiagnosticSeverity.Error,
                StudioDiagnosticChannel.Problem,
                "studio.test.incomplete-problem-transition",
                "test",
                ManagedContext(hub),
                "message",
                ProblemId: new StudioProblemId("studio.test:invalid"))));
        Assert.Throws<ArgumentException>(() => hub.PublishDiagnostic(
            new StudioDiagnosticWrite(
                StudioDiagnosticSeverity.Error,
                StudioDiagnosticChannel.Problem,
                "studio.test.empty-problem-id",
                "test",
                ManagedContext(hub),
                "message",
                ProblemId: default(StudioProblemId),
                ProblemTransition: StudioProblemTransition.Active)));
        Assert.Throws<ArgumentOutOfRangeException>(() => hub.PublishDiagnostic(
            new StudioDiagnosticWrite(
                StudioDiagnosticSeverity.Error,
                StudioDiagnosticChannel.Problem,
                "studio.test.invalid-problem-transition",
                "test",
                ManagedContext(hub),
                "message",
                ProblemId: new StudioProblemId("studio.test:invalid"),
                ProblemTransition: (StudioProblemTransition)int.MaxValue)));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            hub.ReadLogs(maxCount: 0));
    }

    private static StudioDiagnosticRecord PublishDiagnostic(
        IStudioDiagnosticHub hub,
        string code,
        StudioDiagnosticChannel channel) =>
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Info,
            channel,
            code,
            "test",
            ManagedContext(hub),
            code));

    private static StudioLogRecord PublishLog(
        IStudioDiagnosticHub hub,
        string message) =>
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "test",
            ManagedContext(hub),
            message,
            message));

    private static StudioDiagnosticRecord PublishProblem(
        IStudioDiagnosticHub hub,
        StudioProblemId problemId,
        StudioProblemTransition transition,
        string code) =>
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Error,
            StudioDiagnosticChannel.Problem,
            code,
            "test",
            ManagedContext(hub),
            code,
            ProblemId: problemId,
            ProblemTransition: transition));

    private static long DiagnosticPayloadBytes(StudioDiagnosticRecord record) =>
        Utf8Bytes(
            record.Code,
            record.Category,
            record.Context.Package,
            record.Context.Component,
            record.Context.Scope.Kind,
            record.Context.Scope.Identity,
            record.Message,
            record.Remediation,
            record.Fingerprint,
            record.ProblemId?.Value)
        + record.Attributes.Sum(attribute => Utf8Bytes(
            attribute.Name,
            attribute.Value));

    private static long LogPayloadBytes(StudioLogRecord record) =>
        Utf8Bytes(
            record.Channel,
            record.Context.Package,
            record.Context.Component,
            record.Context.Scope.Kind,
            record.Context.Scope.Identity,
            record.MessageTemplate,
            record.RenderedMessage)
        + record.Attributes.Sum(attribute => Utf8Bytes(
            attribute.Name,
            attribute.Value));

    private static long Utf8Bytes(params string?[] values) =>
        values.Sum(value => value is null ? 0 : Encoding.UTF8.GetByteCount(value));

    private static StudioDiagnosticContext ManagedContext(
        IStudioDiagnosticHub hub) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.studio.tests",
            "diagnostic-hub",
            StudioDiagnosticScope.Process(hub.ProcessIdentity));

    private sealed record RingRecord(long SequenceId);

    private sealed record WeightedRingRecord(
        long SequenceId,
        long ByteCount);
}
