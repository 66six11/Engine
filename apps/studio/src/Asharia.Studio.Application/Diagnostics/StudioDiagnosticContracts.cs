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

public enum StudioProblemTransition
{
    Active,
    Resolved,
    Stale,
}

public readonly record struct StudioProblemId
{
    public const int MaxLength = 256;

    public StudioProblemId(string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        if (!string.Equals(value, value.Trim(), StringComparison.Ordinal))
        {
            throw new ArgumentException(
                "Studio problem identity cannot have leading or trailing whitespace.",
                nameof(value));
        }

        if (value.Length > MaxLength)
        {
            throw new ArgumentException(
                $"Studio problem identity cannot exceed {MaxLength} characters.",
                nameof(value));
        }

        Value = value;
    }

    public string Value { get; }

    public override string ToString() => Value ?? string.Empty;
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
    ImmutableArray<StudioDiagnosticAttribute> Attributes = default,
    StudioProblemId? ProblemId = null,
    StudioProblemTransition? ProblemTransition = null);

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
    bool WasTruncated,
    StudioProblemId? ProblemId = null,
    StudioProblemTransition? ProblemTransition = null)
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

public readonly record struct StudioDiagnosticBufferState(
    int CountCapacity,
    long PayloadByteCapacity,
    int ResidentCount,
    // Sum of normalized retained string payloads measured as UTF-8 bytes.
    // CLR object, collection, and allocator overhead are intentionally excluded.
    long EstimatedResidentPayloadBytes,
    long TotalDropped);

public sealed record StudioActiveProblemSnapshot(
    long Version,
    int CountCapacity,
    long PayloadByteCapacity,
    int ResidentCount,
    // Uses the same normalized UTF-8 payload estimate as the history buffers.
    long EstimatedResidentPayloadBytes,
    long TotalDropped,
    bool IsIncomplete,
    ImmutableArray<StudioDiagnosticRecord> Items);

public interface IStudioDiagnosticSource
{
    StudioProcessIdentity ProcessIdentity { get; }

    int DiagnosticCapacity { get; }

    int LogCapacity { get; }

    long SubscriberFailureCount { get; }

    StudioDiagnosticBufferState DiagnosticBufferState { get; }

    StudioDiagnosticBufferState LogBufferState { get; }

    long DiagnosticSubscriberFailureCount { get; }

    long LogSubscriberFailureCount { get; }

    StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
        long afterSequence = 0,
        int maxCount = StudioDiagnosticHub.DefaultReadLimit,
        StudioDiagnosticChannel? channel = null);

    StudioCursorWindow<StudioLogRecord> ReadLogs(
        long afterSequence = 0,
        int maxCount = StudioDiagnosticHub.DefaultReadLimit);

    StudioDiagnosticRecord? GetLatestDiagnostic();

    StudioActiveProblemSnapshot ReadActiveProblems();

    IDisposable SubscribeDiagnostics(Action invalidated);

    IDisposable SubscribeLogs(Action invalidated);
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
