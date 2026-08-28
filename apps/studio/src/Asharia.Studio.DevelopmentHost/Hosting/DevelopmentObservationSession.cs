using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Diagnostics;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Hosting;

internal sealed class DevelopmentObservationSession
{
    private const int MaxBuildIdentityLength = 256;
    private const int MaxConfigurationLength = 64;

    private readonly StudioDiagnosticObservationSource diagnostics_;
    private readonly IStudioUiObservationSource? uiObservationSource_;
    private readonly long startedTimestamp_;

    public DevelopmentObservationSession(
        IStudioDiagnosticHub diagnosticHub,
        StudioInstanceId studioInstanceId,
        StudioSessionId studioSessionId,
        int processId,
        DateTimeOffset processStartTimeUtc,
        string buildIdentity,
        string configuration,
        long endpointGeneration,
        long providerGeneration,
        IStudioUiObservationSource? uiObservationSource)
    {
        ArgumentNullException.ThrowIfNull(diagnosticHub);
        if (studioInstanceId.Value == Guid.Empty
            || studioSessionId.Value == Guid.Empty
            || diagnosticHub.ProcessIdentity.Value != studioInstanceId.Value)
        {
            throw new ArgumentException(
                "Studio instance/session identities must be non-empty and the instance must own the diagnostic hub.");
        }

        if (processId <= 0
            || endpointGeneration <= 0
            || providerGeneration <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(processId),
                "Process and generation values must be positive.");
        }

        ValidateBoundedText(
            buildIdentity,
            MaxBuildIdentityLength,
            nameof(buildIdentity));
        ValidateBoundedText(
            configuration,
            MaxConfigurationLength,
            nameof(configuration));

        StudioInstanceId = studioInstanceId;
        StudioSessionId = studioSessionId;
        ProcessId = processId;
        ProcessStartTimeUtc = processStartTimeUtc.ToUniversalTime();
        BuildIdentity = buildIdentity;
        Configuration = configuration;
        EndpointGeneration = endpointGeneration;
        ProviderGeneration = providerGeneration;
        StartedAtUtc = DateTimeOffset.UtcNow;
        startedTimestamp_ = Stopwatch.GetTimestamp();
        diagnostics_ = new StudioDiagnosticObservationSource(
            diagnosticHub,
            providerGeneration);
        uiObservationSource_ = uiObservationSource;
    }

    public StudioInstanceId StudioInstanceId { get; }

    public StudioSessionId StudioSessionId { get; }

    public int ProcessId { get; }

    public DateTimeOffset ProcessStartTimeUtc { get; }

    public string BuildIdentity { get; }

    public string Configuration { get; }

    public long EndpointGeneration { get; }

    public long ProviderGeneration { get; }

    public DateTimeOffset StartedAtUtc { get; }

    public ToolSessionDescriptor Describe(string state) =>
        new(
            StudioInstanceId,
            StudioSessionId,
            ProcessId,
            ProcessStartTimeUtc,
            BuildIdentity,
            Configuration,
            ObservationProtocolVersion.Current,
            EndpointGeneration,
            state,
            StartedAtUtc,
            Math.Max(
                0,
                (long)Stopwatch.GetElapsedTime(startedTimestamp_).TotalMilliseconds),
            Capabilities());

    public ObservationProtocolReadResult<ObservationCursorWindow<ObservationDiagnosticEvent>>
        ReadDiagnostics(DiagnosticsReadParameters parameters) =>
        diagnostics_.ReadDiagnostics(parameters);

    public ObservationProtocolReadResult<ObservationCursorWindow<ObservationLogEvent>>
        ReadLogs(LogsReadParameters parameters) =>
        diagnostics_.ReadLogs(parameters);

    public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
        UiListWindowsParameters parameters,
        CancellationToken cancellationToken) =>
        uiObservationSource_ is null
            ? ValueTask.FromResult(Unavailable<UiWindowListResult>("ui.listWindows"))
            : uiObservationSource_.ListWindowsAsync(parameters, cancellationToken);

    public ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
        UiReadTreeParameters parameters,
        CancellationToken cancellationToken) =>
        uiObservationSource_ is null
            ? ValueTask.FromResult(Unavailable<UiTreeReadResult>("ui.readTree"))
            : uiObservationSource_.ReadTreeAsync(parameters, cancellationToken);

    private ImmutableArray<ObservationCapabilityDescriptor> Capabilities()
    {
        var bounds = new ObservationCapabilityBounds(
            ObservationProtocolLimits.MaxPageSize,
            ObservationProtocolLimits.MaxResponseBytes,
            ObservationProtocolLimits.MaxWaitMilliseconds);
        var capabilities = ImmutableArray.CreateBuilder<ObservationCapabilityDescriptor>(
            uiObservationSource_ is null ? 3 : 5);
        capabilities.Add(new ObservationCapabilityDescriptor(
                "session.describe",
                SchemaVersion: 1,
                Access: "observe",
                Cost: "constant",
                Availability: "available",
                OwnerScopeKind: "process",
                ProviderGeneration,
                bounds));
        capabilities.Add(new ObservationCapabilityDescriptor(
                "diagnostics.read",
                SchemaVersion: 2,
                Access: "observe",
                Cost: "boundedPage",
                Availability: "available",
                OwnerScopeKind: "process",
                ProviderGeneration,
                bounds));
        capabilities.Add(new ObservationCapabilityDescriptor(
                "logs.read",
                SchemaVersion: 1,
                Access: "observe",
                Cost: "boundedPage",
                Availability: "available",
                OwnerScopeKind: "process",
                ProviderGeneration,
                bounds));
        if (uiObservationSource_ is not null)
        {
            capabilities.Add(new ObservationCapabilityDescriptor(
                "ui.listWindows",
                SchemaVersion: 1,
                Access: "observe",
                Cost: "boundedList",
                Availability: "available",
                OwnerScopeKind: "process",
                ProviderGeneration,
                new ObservationCapabilityBounds(
                    ObservationProtocolLimits.MaxUiWindows,
                    ObservationProtocolLimits.MaxResponseBytes,
                    ObservationProtocolLimits.MaxWaitMilliseconds)));
            capabilities.Add(new ObservationCapabilityDescriptor(
                "ui.readTree",
                SchemaVersion: 1,
                Access: "observe",
                Cost: "boundedTree",
                Availability: "available",
                OwnerScopeKind: "window",
                ProviderGeneration,
                new ObservationCapabilityBounds(
                    ObservationProtocolLimits.MaxUiNodes,
                    ObservationProtocolLimits.MaxResponseBytes,
                    ObservationProtocolLimits.MaxWaitMilliseconds)));
        }

        return capabilities.ToImmutable();
    }

    private static ObservationProtocolReadResult<T> Unavailable<T>(string capabilityId)
        where T : class =>
        new(
            Value: null,
            new ObservationFailure(
                "observation.capability.unavailable",
                "unavailable",
                "The requested UI observation capability is not available in this Studio session.",
                Retryable: false,
                CapabilityId: capabilityId));

    private static void ValidateBoundedText(
        string value,
        int maxLength,
        string parameterName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value, parameterName);
        if (value.Length > maxLength)
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                $"{parameterName} cannot exceed {maxLength} characters.");
        }
    }
}
