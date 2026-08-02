using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Xunit;

namespace Asharia.Studio.Application.Tests.Diagnostics;

public sealed class StudioDiagnosticHubTests
{
    [Fact]
    public void PublishDiagnostic_records_structured_process_context_and_notifies()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var changedCount = 0;
        using var subscription = hub.Subscribe(() => changedCount++);

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
        Assert.Equal(1, changedCount);
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
        Assert.True(window.TotalDropped >= producerCount * recordsPerProducer - hub.LogCapacity);
        Assert.True(window.CursorExpired);
        Assert.True(window.Items
            .Zip(window.Items.Skip(1))
            .All(pair => pair.First.SequenceId < pair.Second.SequenceId));
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
        using var failing = hub.Subscribe(() => throw new InvalidOperationException("boom"));
        using var healthy = hub.Subscribe(() => healthyCount++);

        PublishDiagnostic(hub, "studio.test.failure", StudioDiagnosticChannel.Problem);

        Assert.Equal(1, hub.SubscriberFailureCount);
        Assert.Equal(1, healthyCount);
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
            .Select(_ => hub.Subscribe(static () => { }))
            .ToArray();

        Assert.Throws<InvalidOperationException>(() => hub.Subscribe(static () => { }));

        subscriptions[0].Dispose();
        using var replacement = hub.Subscribe(static () => { });
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
        using var subscription = hub.Subscribe(() =>
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
        var subscription = hub.Subscribe(() => changedCount++);
        subscription.Dispose();

        PublishDiagnostic(hub, "studio.test.shutdown", StudioDiagnosticChannel.Debug);

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

        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        Assert.Throws<ArgumentException>(() => hub.PublishDiagnostic(
            new StudioDiagnosticWrite(
                StudioDiagnosticSeverity.Info,
                StudioDiagnosticChannel.Debug,
                " ",
                "test",
                ManagedContext(hub),
                "message")));
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

    private static StudioDiagnosticContext ManagedContext(
        IStudioDiagnosticHub hub) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.studio.tests",
            "diagnostic-hub",
            StudioDiagnosticScope.Process(hub.ProcessIdentity));

    private sealed record RingRecord(long SequenceId);
}
