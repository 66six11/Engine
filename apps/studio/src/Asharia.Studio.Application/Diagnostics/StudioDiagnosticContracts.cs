using System;
using System.Collections.Immutable;

namespace Asharia.Studio.Application.Diagnostics;

public readonly record struct StudioProcessIdentity(Guid Value)
{
    public static StudioProcessIdentity CreateNew() => new(Guid.NewGuid());

    public override string ToString() => Value.ToString("D");
}

public enum StudioDiagnosticSeverity
{
    Debug,
    Info,
    Warning,
    Error,
}

public enum StudioDiagnosticChannel
{
    Debug,
    Problem,
}

public enum StudioLogLevel
{
    Verbose,
    Debug,
    Information,
    Warning,
    Error,
    Fatal,
}

public enum StudioRecordOrigin
{
    Managed,
    Native,
    Framework,
    Subprocess,
}

public enum StudioDataSensitivity
{
    Public,
    ProjectPath,
    Sensitive,
}

public readonly record struct StudioDiagnosticAttribute(
    string Name,
    string Value);

public sealed record StudioDiagnosticScope(
    string Kind,
    string Identity,
    long Generation)
{
    public static StudioDiagnosticScope Process(StudioProcessIdentity identity) =>
        new("process", identity.ToString(), Generation: 1);
}

public sealed record StudioDiagnosticContext(
    StudioRecordOrigin Origin,
    string Package,
    string Component,
    StudioDiagnosticScope Scope,
    Guid? OperationId = null,
    Guid? CorrelationId = null,
    Guid? ParentCorrelationId = null,
    StudioDataSensitivity Sensitivity = StudioDataSensitivity.Public);

public sealed record StudioDiagnosticWrite(
    StudioDiagnosticSeverity Severity,
    StudioDiagnosticChannel Channel,
    string Code,
    string Category,
    StudioDiagnosticContext Context,
    string Message,
    string? Remediation = null,
    ImmutableArray<StudioDiagnosticAttribute> Attributes = default);

public sealed record StudioLogWrite(
    StudioLogLevel Level,
    string Channel,
    StudioDiagnosticContext Context,
    string MessageTemplate,
    string RenderedMessage,
    ImmutableArray<StudioDiagnosticAttribute> Attributes = default);

public sealed record StudioDiagnosticRecord(
    long SequenceId,
    DateTimeOffset TimestampUtc,
    long MonotonicTimestamp,
    StudioDiagnosticSeverity Severity,
    StudioDiagnosticChannel Channel,
    string Code,
    string Category,
    StudioDiagnosticContext Context,
    string Message,
    string? Remediation,
    ImmutableArray<StudioDiagnosticAttribute> Attributes,
    string Fingerprint,
    int RepeatCount,
    bool WasTruncated)
{
    public string Source => Context.Component;
}

public sealed record StudioLogRecord(
    long SequenceId,
    DateTimeOffset TimestampUtc,
    long MonotonicTimestamp,
    int ManagedThreadId,
    StudioLogLevel Level,
    string Channel,
    StudioDiagnosticContext Context,
    string MessageTemplate,
    string RenderedMessage,
    ImmutableArray<StudioDiagnosticAttribute> Attributes,
    bool WasTruncated)
{
    public string Source => Context.Component;
}

public sealed record StudioCursorWindow<T>(
    long OldestAvailableSequence,
    long NextCursor,
    long TotalDropped,
    bool CursorExpired,
    bool Truncated,
    ImmutableArray<T> Items);

public interface IStudioDiagnosticSource
{
    StudioProcessIdentity ProcessIdentity { get; }

    int DiagnosticCapacity { get; }

    int LogCapacity { get; }

    long SubscriberFailureCount { get; }

    StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
        long afterSequence = 0,
        int maxCount = StudioDiagnosticHub.DefaultReadLimit,
        StudioDiagnosticChannel? channel = null);

    StudioCursorWindow<StudioLogRecord> ReadLogs(
        long afterSequence = 0,
        int maxCount = StudioDiagnosticHub.DefaultReadLimit);

    StudioDiagnosticRecord? GetLatestDiagnostic();

    IDisposable Subscribe(Action invalidated);
}

public interface IStudioDiagnosticHub : IStudioDiagnosticSource
{
    StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write);

    StudioLogRecord PublishLog(StudioLogWrite write);
}

public interface IStudioDiagnosticHubProvider
{
    IStudioDiagnosticHub Diagnostics { get; }
}
