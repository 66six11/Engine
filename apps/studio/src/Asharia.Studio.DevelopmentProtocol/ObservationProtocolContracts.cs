using System;
using System.Collections.Immutable;
using System.Text.Json.Serialization;

namespace Asharia.Studio.DevelopmentProtocol;

public static class ObservationProtocolLimits
{
    public const int MaxRequestBytes = 1024 * 1024;
    public const int MaxResponseBytes = 8 * 1024 * 1024;
    public const int MaxJsonDepth = 32;
    public const int MaxPageSize = 1000;
    public const int MaxWaitMilliseconds = 1000;
    public const int MaxRequestTimeoutMilliseconds = 30_000;
    public const int MaxMethodIdLength = 128;
    public const int MaxAttachTokenCharacters = 64;
    public const int MaxSessionManifestBytes = 64 * 1024;
    public const int MaxUiWindows = 16;
    public const int MaxUiNodes = 512;
    public const int MaxUiDepth = 16;
    public const int MaxUiElementIdCharacters = 128;
    public const int MaxUiNameCharacters = 256;
    public const int MaxUiTruncationReasonCharacters = 128;
    public const int MaxUiVisualsVisited = 4096;
    public const int MaxUiVisualDepth = 64;
}

public readonly record struct ObservationProtocolVersion(int Major, int Minor)
{
    public static ObservationProtocolVersion Current => new(Major: 1, Minor: 0);
}

public readonly record struct ObservationRequestId(Guid Value);

public readonly record struct StudioInstanceId(Guid Value);

public readonly record struct StudioSessionId(Guid Value);

public readonly record struct ObservationMethodId
{
    public ObservationMethodId(string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        if (value.Length > ObservationProtocolLimits.MaxMethodIdLength)
        {
            throw new ArgumentOutOfRangeException(
                nameof(value),
                $"Observation method IDs cannot exceed {ObservationProtocolLimits.MaxMethodIdLength} characters.");
        }

        Value = value;
    }

    public string Value { get; }

    [JsonIgnore]
    public ObservationMethodKind Kind => Value switch
    {
        "session.describe" => ObservationMethodKind.SessionDescribe,
        "session.listScopes" => ObservationMethodKind.SessionListScopes,
        "state.read" => ObservationMethodKind.StateRead,
        "diagnostics.read" => ObservationMethodKind.DiagnosticsRead,
        "logs.read" => ObservationMethodKind.LogsRead,
        "ui.listWindows" => ObservationMethodKind.UiListWindows,
        "ui.readTree" => ObservationMethodKind.UiReadTree,
        "ui.readElement" => ObservationMethodKind.UiReadElement,
        "ui.find" => ObservationMethodKind.UiFind,
        _ => ObservationMethodKind.Unknown,
    };

    public static ObservationMethodId SessionDescribe => new("session.describe");

    public static ObservationMethodId SessionListScopes => new("session.listScopes");

    public static ObservationMethodId StateRead => new("state.read");

    public static ObservationMethodId DiagnosticsRead => new("diagnostics.read");

    public static ObservationMethodId LogsRead => new("logs.read");

    public static ObservationMethodId UiListWindows => new("ui.listWindows");

    public static ObservationMethodId UiReadTree => new("ui.readTree");

    public static ObservationMethodId UiReadElement => new("ui.readElement");

    public static ObservationMethodId UiFind => new("ui.find");

    public override string ToString() => Value;
}

public enum ObservationMethodKind
{
    Unknown,
    SessionDescribe,
    SessionListScopes,
    StateRead,
    DiagnosticsRead,
    LogsRead,
    UiListWindows,
    UiReadTree,
    UiReadElement,
    UiFind,
}

public enum ObservationOutcome
{
    Unknown,
    Complete,
    Partial,
    Failed,
    Cancelled,
    TimedOut,
}

public sealed record ObservationRequest<TParameters>(
    ObservationProtocolVersion Protocol,
    ObservationRequestId RequestId,
    StudioInstanceId StudioInstanceId,
    long EndpointGeneration,
    ObservationMethodId Method,
    int TimeoutMilliseconds,
    TParameters Parameters)
    where TParameters : class;

public sealed record ObservationResponse<TValue>(
    ObservationProtocolVersion Protocol,
    ObservationRequestId RequestId,
    StudioInstanceId StudioInstanceId,
    long EndpointGeneration,
    ObservationOutcome Outcome,
    TValue? Value,
    ObservationFailure? Failure = null,
    ObservationTruncation? Truncation = null)
    where TValue : class;

public sealed record ObservationFailure(
    string Code,
    string Category,
    string Message,
    bool Retryable,
    string? Remediation = null,
    string? CapabilityId = null,
    ObservationScopeReference? Scope = null,
    Guid? OperationId = null,
    Guid? CorrelationId = null,
    ImmutableArray<ObservationSafeAttribute> Attributes = default);

public sealed record ObservationScopeReference(
    string Kind,
    Guid OwnerScopeId,
    long OwnerGeneration,
    long ProviderGeneration);

public readonly record struct ObservationSafeAttribute(
    string Name,
    string Value);

public sealed record ObservationTruncation(
    bool IsTruncated,
    string? Reason = null,
    string? ContinuationToken = null,
    long DroppedCount = 0);

public sealed record SessionDescribeParameters;

public sealed record ObservationHandshakeRequest(
    ObservationProtocolVersion Protocol,
    ObservationRequestId RequestId,
    StudioInstanceId StudioInstanceId,
    long EndpointGeneration,
    string AttachToken);

public sealed record DevelopmentSessionManifest(
    int SchemaVersion,
    ObservationProtocolVersion Protocol,
    StudioInstanceId StudioInstanceId,
    StudioSessionId StudioSessionId,
    int ProcessId,
    DateTimeOffset ProcessStartTimeUtc,
    long EndpointGeneration,
    string PipeName,
    string AttachToken,
    string BuildIdentity,
    string Configuration,
    string CapabilityDigest,
    DateTimeOffset CreatedAtUtc,
    DateTimeOffset HeartbeatUtc);

public sealed record ToolSessionDescriptor(
    StudioInstanceId StudioInstanceId,
    StudioSessionId StudioSessionId,
    int ProcessId,
    DateTimeOffset ProcessStartTimeUtc,
    string BuildIdentity,
    string Configuration,
    ObservationProtocolVersion Protocol,
    long EndpointGeneration,
    string State,
    DateTimeOffset StartedAtUtc,
    long UptimeMilliseconds,
    ImmutableArray<ObservationCapabilityDescriptor> Capabilities,
    Guid? EngineGenerationId = null);

public sealed record ObservationCapabilityDescriptor(
    string CapabilityId,
    int SchemaVersion,
    string Access,
    string Cost,
    string Availability,
    string OwnerScopeKind,
    long ProviderGeneration,
    ObservationCapabilityBounds Limits,
    string? RequiredGrant = null,
    string? UnavailableReason = null);

public sealed record ObservationCapabilityBounds(
    int MaxPageSize,
    int MaxResponseBytes,
    int MaxWaitMilliseconds);

public sealed record ObservationProtocolReadResult<T>(
    T? Value,
    ObservationFailure? Failure)
    where T : class
{
    [JsonIgnore]
    public bool Succeeded => Value is not null && Failure is null;

    internal static ObservationProtocolReadResult<T> Success(T value) =>
        new(value, Failure: null);

    internal static ObservationProtocolReadResult<T> Rejected(ObservationFailure failure) =>
        new(Value: null, failure);
}
