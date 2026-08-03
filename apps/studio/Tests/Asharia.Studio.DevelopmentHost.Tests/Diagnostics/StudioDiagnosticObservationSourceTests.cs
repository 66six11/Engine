using System;
using System.Collections.Immutable;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Diagnostics;
using Asharia.Studio.DevelopmentProtocol;
using Xunit;

namespace Asharia.Studio.DevelopmentHost.Tests.Diagnostics;

public sealed class StudioDiagnosticObservationSourceTests
{
    [Fact]
    public void Diagnostics_projection_reads_the_existing_ring_and_preserves_cursor_loss()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var source = new StudioDiagnosticObservationSource(hub, providerGeneration: 7);
        PublishDiagnostic(hub, "studio.test.one", StudioDiagnosticChannel.Debug);
        PublishDiagnostic(hub, "studio.test.two", StudioDiagnosticChannel.Problem);
        PublishDiagnostic(hub, "studio.test.three", StudioDiagnosticChannel.Problem);

        var result = source.ReadDiagnostics(new DiagnosticsReadParameters(
            AfterSequence: 0,
            MaxCount: 2));

        Assert.True(result.Succeeded);
        Assert.True(result.Value!.CursorExpired);
        Assert.Equal(1, result.Value.TotalDropped);
        Assert.Equal(2, result.Value.OldestAvailableSequence);
        Assert.Equal(3, result.Value.NextCursor);
        Assert.Collection(
            result.Value.Items,
            item => Assert.Equal("studio.test.two", item.Code),
            item => Assert.Equal("studio.test.three", item.Code));
        Assert.All(result.Value.Items, item => Assert.Equal(7, item.Context.Scope.ProviderGeneration));
    }

    [Fact]
    public void Diagnostic_projection_preserves_structured_context_and_problem_filter()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var source = new StudioDiagnosticObservationSource(hub, providerGeneration: 3);
        PublishDiagnostic(hub, "studio.test.debug", StudioDiagnosticChannel.Debug);
        var operationId = Guid.NewGuid();
        var correlationId = Guid.NewGuid();
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Warning,
            StudioDiagnosticChannel.Problem,
            "studio.test.problem",
            "test",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Framework,
                "avalonia",
                "log-sink",
                StudioDiagnosticScope.Process(hub.ProcessIdentity),
                operationId,
                correlationId,
                Sensitivity: StudioDataSensitivity.ProjectPath),
            "Problem message.",
            "Inspect the source.",
            [new StudioDiagnosticAttribute("kind", "structured")]));

        var result = source.ReadDiagnostics(new DiagnosticsReadParameters(
            AfterSequence: 0,
            MaxCount: 4,
            Channel: "problem"));

        var record = Assert.Single(result.Value!.Items);
        Assert.Equal("warning", record.Severity);
        Assert.Equal("problem", record.Channel);
        Assert.Equal("framework", record.Context.Origin);
        Assert.Equal("avalonia", record.Context.Package);
        Assert.Equal(operationId, record.Context.OperationId);
        Assert.Equal(correlationId, record.Context.CorrelationId);
        Assert.Equal("projectPath", record.Context.Sensitivity);
        Assert.Equal("structured", Assert.Single(record.Attributes).Value);
        Assert.Equal(hub.ProcessIdentity.Value, record.Context.Scope.OwnerScopeId);
        Assert.Equal(1, record.Context.Scope.OwnerGeneration);
        Assert.Equal(3, record.Context.Scope.ProviderGeneration);
    }

    [Fact]
    public void Log_projection_preserves_sequence_thread_channel_and_message_contract()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var source = new StudioDiagnosticObservationSource(hub, providerGeneration: 2);
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Error,
            "stderr",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Subprocess,
                "asharia.tooling",
                "compiler",
                StudioDiagnosticScope.Process(hub.ProcessIdentity),
                Sensitivity: StudioDataSensitivity.Sensitive),
            "Compiler exited with {ExitCode}.",
            "Compiler exited with 1.",
            [new StudioDiagnosticAttribute("exitCode", "1")]));

        var result = source.ReadLogs(new LogsReadParameters(AfterSequence: 0, MaxCount: 2));

        var record = Assert.Single(result.Value!.Items);
        Assert.Equal(1, record.Sequence);
        Assert.True(record.ManagedThreadId > 0);
        Assert.Equal("error", record.Level);
        Assert.Equal("stderr", record.Channel);
        Assert.Equal("subprocess", record.Context.Origin);
        Assert.Equal("sensitive", record.Context.Sensitivity);
        Assert.Equal("Compiler exited with {ExitCode}.", record.MessageTemplate);
        Assert.Equal("Compiler exited with 1.", record.RenderedMessage);
        Assert.Equal("1", Assert.Single(record.Attributes).Value);
    }

    [Theory]
    [InlineData(-1, 1, null)]
    [InlineData(0, 0, null)]
    [InlineData(0, 1001, null)]
    [InlineData(0, 1, "native")]
    public void Invalid_cursor_limit_or_channel_returns_typed_failure(
        long afterSequence,
        int maxCount,
        string? channel)
    {
        var source = new StudioDiagnosticObservationSource(
            new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2),
            providerGeneration: 1);

        var result = source.ReadDiagnostics(new DiagnosticsReadParameters(
            afterSequence,
            maxCount,
            channel));

        Assert.False(result.Succeeded);
        Assert.Equal("observation.request.invalid", result.Failure!.Code);
    }

    [Fact]
    public void Provider_exception_is_isolated_as_typed_failure()
    {
        var source = new StudioDiagnosticObservationSource(
            new ThrowingDiagnosticHub(),
            providerGeneration: 1);

        var result = source.ReadLogs(new LogsReadParameters(AfterSequence: 0, MaxCount: 1));

        Assert.False(result.Succeeded);
        Assert.Equal("observation.provider.faulted", result.Failure!.Code);
        Assert.Equal("logs.read", result.Failure.CapabilityId);
        Assert.DoesNotContain("boom", result.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Projection_has_no_host_transport_subscription_or_second_ring()
    {
        var source = typeof(StudioDiagnosticObservationSource).Assembly
            .GetName()
            .Name;

        Assert.Equal("Asharia.Studio.DevelopmentHost", source);
        Assert.Null(typeof(StudioDiagnosticObservationSource).GetField(
            "ring_",
            System.Reflection.BindingFlags.Instance
            | System.Reflection.BindingFlags.NonPublic));
        Assert.DoesNotContain(
            typeof(StudioDiagnosticObservationSource).GetFields(
                System.Reflection.BindingFlags.Instance
                | System.Reflection.BindingFlags.NonPublic),
            field => typeof(IDisposable).IsAssignableFrom(field.FieldType));
    }

    private static void PublishDiagnostic(
        StudioDiagnosticHub hub,
        string code,
        StudioDiagnosticChannel channel) =>
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Info,
            channel,
            code,
            "test",
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                "asharia.tests",
                "projection",
                StudioDiagnosticScope.Process(hub.ProcessIdentity)),
            code));

    private sealed class ThrowingDiagnosticHub : IStudioDiagnosticHub
    {
        public StudioProcessIdentity ProcessIdentity { get; } =
            StudioProcessIdentity.CreateNew();

        public int DiagnosticCapacity => 2;

        public int LogCapacity => 2;

        public long SubscriberFailureCount => 0;

        public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
            throw new NotSupportedException();

        public StudioLogRecord PublishLog(StudioLogWrite write) =>
            throw new NotSupportedException();

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            throw new InvalidOperationException("boom");

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit) =>
            throw new InvalidOperationException("boom");

        public StudioDiagnosticRecord? GetLatestDiagnostic() => null;

        public IDisposable Subscribe(Action invalidated) =>
            throw new NotSupportedException();
    }
}
