using System;
using System.Collections.Immutable;
using System.Linq;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Diagnostics;

internal sealed class StudioDiagnosticObservationSource
{
    private const string DiagnosticsCapability = "diagnostics.read";
    private const string LogsCapability = "logs.read";

    private readonly IStudioDiagnosticHub hub_;
    private readonly long providerGeneration_;

    public StudioDiagnosticObservationSource(
        IStudioDiagnosticHub hub,
        long providerGeneration)
    {
        ArgumentNullException.ThrowIfNull(hub);
        if (providerGeneration <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(providerGeneration));
        }

        hub_ = hub;
        providerGeneration_ = providerGeneration;
    }

    public ObservationProtocolReadResult<ObservationCursorWindow<ObservationDiagnosticEvent>>
        ReadDiagnostics(DiagnosticsReadParameters parameters)
    {
        ArgumentNullException.ThrowIfNull(parameters);
        if (!ValidateWindow(parameters.AfterSequence, parameters.MaxCount, out var failure))
        {
            return new(Value: null, failure);
        }

        if (!TryParseChannel(parameters.Channel, out var channel))
        {
            return Rejected<ObservationDiagnosticEvent>(
                DiagnosticsCapability,
                "Diagnostic channel must be 'debug', 'problem', or null.");
        }

        try
        {
            var window = hub_.ReadDiagnostics(
                parameters.AfterSequence,
                parameters.MaxCount,
                channel);
            return new(Project(window, MapDiagnostic), Failure: null);
        }
        catch (Exception error)
        {
            return Faulted<ObservationDiagnosticEvent>(DiagnosticsCapability, error);
        }
    }

    public ObservationProtocolReadResult<ObservationCursorWindow<ObservationLogEvent>>
        ReadLogs(LogsReadParameters parameters)
    {
        ArgumentNullException.ThrowIfNull(parameters);
        if (!ValidateWindow(parameters.AfterSequence, parameters.MaxCount, out var failure))
        {
            return new(Value: null, failure);
        }

        try
        {
            var window = hub_.ReadLogs(
                parameters.AfterSequence,
                parameters.MaxCount);
            return new(Project(window, MapLog), Failure: null);
        }
        catch (Exception error)
        {
            return Faulted<ObservationLogEvent>(LogsCapability, error);
        }
    }

    private static bool ValidateWindow(
        long afterSequence,
        int maxCount,
        out ObservationFailure? failure)
    {
        if (afterSequence >= 0
            && maxCount > 0
            && maxCount <= ObservationProtocolLimits.MaxPageSize)
        {
            failure = null;
            return true;
        }

        failure = new ObservationFailure(
            "observation.request.invalid",
            "protocol",
            $"Cursor must be non-negative and maxCount must be between 1 and {ObservationProtocolLimits.MaxPageSize}.",
            Retryable: false);
        return false;
    }

    private static bool TryParseChannel(
        string? value,
        out StudioDiagnosticChannel? channel)
    {
        channel = value switch
        {
            null => null,
            "debug" => StudioDiagnosticChannel.Debug,
            "problem" => StudioDiagnosticChannel.Problem,
            _ => null,
        };
        return value is null || channel is not null;
    }

    private static ObservationCursorWindow<TTarget> Project<TSource, TTarget>(
        StudioCursorWindow<TSource> window,
        Func<TSource, TTarget> map)
        where TSource : class
        where TTarget : class =>
        new(
            window.OldestAvailableSequence,
            window.NextCursor,
            window.TotalDropped,
            window.CursorExpired,
            window.Truncated,
            window.Items.Select(map).ToImmutableArray());

    private ObservationDiagnosticEvent MapDiagnostic(StudioDiagnosticRecord record) =>
        new(
            record.SequenceId,
            record.TimestampUtc,
            record.MonotonicTimestamp,
            Severity(record.Severity),
            Channel(record.Channel),
            record.Code,
            record.Category,
            MapContext(record.Context),
            record.Message,
            record.Remediation,
            MapAttributes(record.Attributes),
            record.Fingerprint,
            record.RepeatCount,
            record.WasTruncated);

    private ObservationLogEvent MapLog(StudioLogRecord record) =>
        new(
            record.SequenceId,
            record.TimestampUtc,
            record.MonotonicTimestamp,
            record.ManagedThreadId,
            Level(record.Level),
            record.Channel,
            MapContext(record.Context),
            record.MessageTemplate,
            record.RenderedMessage,
            MapAttributes(record.Attributes),
            record.WasTruncated);

    private ObservationRecordContext MapContext(StudioDiagnosticContext context)
    {
        if (!Guid.TryParseExact(context.Scope.Identity, "D", out var scopeId))
        {
            throw new InvalidOperationException(
                "Studio diagnostic scope identity is not a canonical typed GUID.");
        }

        return new ObservationRecordContext(
            Origin(context.Origin),
            context.Package,
            context.Component,
            new ObservationScopeReference(
                context.Scope.Kind,
                scopeId,
                context.Scope.Generation,
                providerGeneration_),
            context.OperationId,
            context.CorrelationId,
            context.ParentCorrelationId,
            Sensitivity(context.Sensitivity));
    }

    private static ImmutableArray<ObservationSafeAttribute> MapAttributes(
        ImmutableArray<StudioDiagnosticAttribute> attributes) =>
        attributes.IsDefaultOrEmpty
            ? []
            : attributes
                .Select(static attribute =>
                    new ObservationSafeAttribute(attribute.Name, attribute.Value))
                .ToImmutableArray();

    private static string Severity(StudioDiagnosticSeverity value) => value switch
    {
        StudioDiagnosticSeverity.Debug => "debug",
        StudioDiagnosticSeverity.Info => "info",
        StudioDiagnosticSeverity.Warning => "warning",
        StudioDiagnosticSeverity.Error => "error",
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static string Channel(StudioDiagnosticChannel value) => value switch
    {
        StudioDiagnosticChannel.Debug => "debug",
        StudioDiagnosticChannel.Problem => "problem",
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static string Level(StudioLogLevel value) => value switch
    {
        StudioLogLevel.Verbose => "verbose",
        StudioLogLevel.Debug => "debug",
        StudioLogLevel.Information => "information",
        StudioLogLevel.Warning => "warning",
        StudioLogLevel.Error => "error",
        StudioLogLevel.Fatal => "fatal",
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static string Origin(StudioRecordOrigin value) => value switch
    {
        StudioRecordOrigin.Managed => "managed",
        StudioRecordOrigin.Native => "native",
        StudioRecordOrigin.Framework => "framework",
        StudioRecordOrigin.Subprocess => "subprocess",
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static string Sensitivity(StudioDataSensitivity value) => value switch
    {
        StudioDataSensitivity.Public => "public",
        StudioDataSensitivity.ProjectPath => "projectPath",
        StudioDataSensitivity.Sensitive => "sensitive",
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static ObservationProtocolReadResult<ObservationCursorWindow<T>> Rejected<T>(
        string capability,
        string message)
        where T : class =>
        new(
            Value: null,
            new ObservationFailure(
                "observation.request.invalid",
                "protocol",
                message,
                Retryable: false,
                CapabilityId: capability));

    private static ObservationProtocolReadResult<ObservationCursorWindow<T>> Faulted<T>(
        string capability,
        Exception error)
        where T : class =>
        new(
            Value: null,
            new ObservationFailure(
                "observation.provider.faulted",
                "provider",
                $"{capability} projection failed: {error.GetType().Name}.",
                Retryable: true,
                Remediation: "Retry after refreshing the Studio session descriptor.",
                CapabilityId: capability));
}
