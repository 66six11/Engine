using System;
using System.Collections.Immutable;

namespace Asharia.Studio.DevelopmentProtocol;

public sealed record DiagnosticsReadParameters(
    long AfterSequence,
    int MaxCount,
    string? Channel = null);

public sealed record LogsReadParameters(
    long AfterSequence,
    int MaxCount);

public sealed record ObservationCursorWindow<T>(
    long OldestAvailableSequence,
    long NextCursor,
    long TotalDropped,
    bool CursorExpired,
    bool Truncated,
    ImmutableArray<T> Items)
    where T : class;

public sealed record ObservationRecordContext(
    string Origin,
    string Package,
    string Component,
    ObservationScopeReference Scope,
    Guid? OperationId,
    Guid? CorrelationId,
    Guid? ParentCorrelationId,
    string Sensitivity);

public sealed record ObservationDiagnosticEvent(
    long Sequence,
    DateTimeOffset TimestampUtc,
    long MonotonicTimestamp,
    string Severity,
    string Channel,
    string Code,
    string Category,
    ObservationRecordContext Context,
    string Message,
    string? Remediation,
    ImmutableArray<ObservationSafeAttribute> Attributes,
    string Fingerprint,
    int RepeatCount,
    bool WasTruncated,
    string? ProblemId = null,
    string? ProblemTransition = null);

public sealed record ObservationLogEvent(
    long Sequence,
    DateTimeOffset TimestampUtc,
    long MonotonicTimestamp,
    int ManagedThreadId,
    string Level,
    string Channel,
    ObservationRecordContext Context,
    string MessageTemplate,
    string RenderedMessage,
    ImmutableArray<ObservationSafeAttribute> Attributes,
    bool WasTruncated);
