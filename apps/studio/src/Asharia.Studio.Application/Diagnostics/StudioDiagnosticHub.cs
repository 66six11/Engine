using System;
using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace Asharia.Studio.Application.Diagnostics;

public sealed class StudioDiagnosticHub : IStudioDiagnosticHub
{
    public const int DefaultDiagnosticCapacity = 2048;
    public const int DefaultLogCapacity = 8192;
    public const int DefaultReadLimit = 200;
    public const int MaxSubscriberCount = 64;

    private const int MaxCodeLength = 128;
    private const int MaxCategoryLength = 64;
    private const int MaxPackageLength = 128;
    private const int MaxComponentLength = 128;
    private const int MaxScopeKindLength = 32;
    private const int MaxScopeIdentityLength = 128;
    private const int MaxChannelLength = 64;
    private const int MaxMessageTemplateLength = 2048;
    private const int MaxMessageLength = 4096;
    private const int MaxRemediationLength = 1024;
    private const int MaxAttributeCount = 16;
    private const int MaxAttributeNameLength = 64;
    private const int MaxAttributeValueLength = 256;

    private readonly BoundedConcurrentRing<StudioDiagnosticRecord> diagnostics_;
    private readonly BoundedConcurrentRing<StudioLogRecord> logs_;
    private readonly object subscribersGate_ = new();
    private readonly Action?[] subscribers_ = new Action[MaxSubscriberCount];
    private long subscriberFailureCount_;
    private int logNotificationScheduled_;

    public StudioDiagnosticHub(
        int diagnosticCapacity = DefaultDiagnosticCapacity,
        int logCapacity = DefaultLogCapacity,
        StudioProcessIdentity? processIdentity = null)
    {
        diagnostics_ = new BoundedConcurrentRing<StudioDiagnosticRecord>(
            diagnosticCapacity,
            record => record.SequenceId);
        logs_ = new BoundedConcurrentRing<StudioLogRecord>(
            logCapacity,
            record => record.SequenceId);
        ProcessIdentity = processIdentity ?? StudioProcessIdentity.CreateNew();
    }

    public StudioProcessIdentity ProcessIdentity { get; }

    public int DiagnosticCapacity => diagnostics_.Capacity;

    public int LogCapacity => logs_.Capacity;

    public long SubscriberFailureCount =>
        Volatile.Read(ref subscriberFailureCount_);

    public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write)
    {
        ArgumentNullException.ThrowIfNull(write);
        var normalized = Normalize(write);
        var record = diagnostics_.Publish(sequence => new StudioDiagnosticRecord(
            sequence,
            TimeProvider.System.GetUtcNow(),
            TimeProvider.System.GetTimestamp(),
            normalized.Severity,
            normalized.Channel,
            normalized.Code,
            normalized.Category,
            normalized.Context,
            normalized.Message,
            normalized.Remediation,
            normalized.Attributes,
            CreateFingerprint(normalized),
            RepeatCount: 1,
            normalized.WasTruncated));
        NotifySubscribers();
        return record;
    }

    public StudioLogRecord PublishLog(StudioLogWrite write)
    {
        ArgumentNullException.ThrowIfNull(write);
        var normalized = Normalize(write);
        var record = logs_.Publish(sequence => new StudioLogRecord(
            sequence,
            TimeProvider.System.GetUtcNow(),
            TimeProvider.System.GetTimestamp(),
            Environment.CurrentManagedThreadId,
            normalized.Level,
            normalized.Channel,
            normalized.Context,
            normalized.MessageTemplate,
            normalized.RenderedMessage,
            normalized.Attributes,
            normalized.WasTruncated));
        ScheduleLogNotification();
        return record;
    }

    public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
        long afterSequence = 0,
        int maxCount = DefaultReadLimit,
        StudioDiagnosticChannel? channel = null) =>
        diagnostics_.Read(
            afterSequence,
            NormalizeReadLimit(maxCount, DiagnosticCapacity),
            channel is null
                ? null
                : record => record.Channel == channel.Value);

    public StudioCursorWindow<StudioLogRecord> ReadLogs(
        long afterSequence = 0,
        int maxCount = DefaultReadLimit) =>
        logs_.Read(
            afterSequence,
            NormalizeReadLimit(maxCount, LogCapacity));

    public StudioDiagnosticRecord? GetLatestDiagnostic() =>
        diagnostics_.GetLatest();

    public IDisposable Subscribe(Action invalidated)
    {
        ArgumentNullException.ThrowIfNull(invalidated);
        lock (subscribersGate_)
        {
            for (var index = 0; index < subscribers_.Length; index++)
            {
                if (subscribers_[index] is not null)
                {
                    continue;
                }

                subscribers_[index] = invalidated;
                return new Subscription(this, index, invalidated);
            }
        }

        throw new InvalidOperationException(
            $"Studio diagnostic subscriber capacity {MaxSubscriberCount} is exhausted.");
    }

    private void Unsubscribe(int index, Action invalidated)
    {
        lock (subscribersGate_)
        {
            if (ReferenceEquals(subscribers_[index], invalidated))
            {
                subscribers_[index] = null;
            }
        }
    }

    private void NotifySubscribers()
    {
        Action?[] snapshot;
        lock (subscribersGate_)
        {
            snapshot = (Action?[])subscribers_.Clone();
        }

        foreach (var subscriber in snapshot)
        {
            if (subscriber is null)
            {
                continue;
            }

            try
            {
                subscriber();
            }
            catch
            {
                Interlocked.Increment(ref subscriberFailureCount_);
            }
        }
    }

    private void ScheduleLogNotification()
    {
        if (Interlocked.CompareExchange(
                ref logNotificationScheduled_,
                1,
                0) != 0)
        {
            return;
        }

        ThreadPool.QueueUserWorkItem(
            static state => ((StudioDiagnosticHub)state!).DispatchLogNotifications(),
            this,
            preferLocal: false);
    }

    private void DispatchLogNotifications()
    {
        while (true)
        {
            var observedVersion = logs_.PublicationVersion;
            NotifySubscribers();
            Interlocked.Exchange(ref logNotificationScheduled_, 0);
            if (logs_.PublicationVersion == observedVersion
                || Interlocked.CompareExchange(
                    ref logNotificationScheduled_,
                    1,
                    0) != 0)
            {
                return;
            }
        }
    }

    private static int NormalizeReadLimit(int requested, int capacity)
    {
        if (requested <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(requested));
        }

        return Math.Min(requested, capacity);
    }

    private static NormalizedDiagnosticWrite Normalize(
        StudioDiagnosticWrite write)
    {
        var wasTruncated = false;
        var context = Normalize(write.Context, ref wasTruncated);
        var code = NormalizeRequired(
            write.Code,
            MaxCodeLength,
            nameof(write.Code),
            ref wasTruncated);
        var category = NormalizeRequired(
            write.Category,
            MaxCategoryLength,
            nameof(write.Category),
            ref wasTruncated);
        var message = NormalizeRequired(
            write.Message,
            MaxMessageLength,
            nameof(write.Message),
            ref wasTruncated);
        var remediation = NormalizeOptional(
            write.Remediation,
            MaxRemediationLength,
            ref wasTruncated);
        var attributes = NormalizeAttributes(write.Attributes, ref wasTruncated);
        return new NormalizedDiagnosticWrite(
            write.Severity,
            write.Channel,
            code,
            category,
            context,
            message,
            remediation,
            attributes,
            wasTruncated);
    }

    private static NormalizedLogWrite Normalize(StudioLogWrite write)
    {
        var wasTruncated = false;
        var context = Normalize(write.Context, ref wasTruncated);
        var channel = NormalizeRequired(
            write.Channel,
            MaxChannelLength,
            nameof(write.Channel),
            ref wasTruncated);
        var messageTemplate = NormalizeRequired(
            write.MessageTemplate,
            MaxMessageTemplateLength,
            nameof(write.MessageTemplate),
            ref wasTruncated);
        var renderedMessage = NormalizeRequired(
            write.RenderedMessage,
            MaxMessageLength,
            nameof(write.RenderedMessage),
            ref wasTruncated);
        var attributes = NormalizeAttributes(write.Attributes, ref wasTruncated);
        return new NormalizedLogWrite(
            write.Level,
            channel,
            context,
            messageTemplate,
            renderedMessage,
            attributes,
            wasTruncated);
    }

    private static StudioDiagnosticContext Normalize(
        StudioDiagnosticContext context,
        ref bool wasTruncated)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(context.Scope);
        if (context.Scope.Generation < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(context.Scope.Generation));
        }

        return context with
        {
            Package = NormalizeRequired(
                context.Package,
                MaxPackageLength,
                nameof(context.Package),
                ref wasTruncated),
            Component = NormalizeRequired(
                context.Component,
                MaxComponentLength,
                nameof(context.Component),
                ref wasTruncated),
            Scope = context.Scope with
            {
                Kind = NormalizeRequired(
                    context.Scope.Kind,
                    MaxScopeKindLength,
                    nameof(context.Scope.Kind),
                    ref wasTruncated),
                Identity = NormalizeRequired(
                    context.Scope.Identity,
                    MaxScopeIdentityLength,
                    nameof(context.Scope.Identity),
                    ref wasTruncated),
            },
        };
    }

    private static ImmutableArray<StudioDiagnosticAttribute> NormalizeAttributes(
        ImmutableArray<StudioDiagnosticAttribute> attributes,
        ref bool wasTruncated)
    {
        if (attributes.IsDefaultOrEmpty)
        {
            return [];
        }

        var count = Math.Min(attributes.Length, MaxAttributeCount);
        if (count != attributes.Length)
        {
            wasTruncated = true;
        }

        var builder = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(count);
        for (var index = 0; index < count; index++)
        {
            var attribute = attributes[index];
            builder.Add(new StudioDiagnosticAttribute(
                NormalizeRequired(
                    attribute.Name,
                    MaxAttributeNameLength,
                    nameof(attribute.Name),
                    ref wasTruncated),
                NormalizeRequired(
                    attribute.Value,
                    MaxAttributeValueLength,
                    nameof(attribute.Value),
                    ref wasTruncated)));
        }

        return builder.MoveToImmutable();
    }

    private static string NormalizeRequired(
        string value,
        int maxLength,
        string parameterName,
        ref bool wasTruncated)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value, parameterName);
        if (value.Length <= maxLength)
        {
            return value;
        }

        wasTruncated = true;
        return value[..maxLength];
    }

    private static string? NormalizeOptional(
        string? value,
        int maxLength,
        ref bool wasTruncated)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return null;
        }

        if (value.Length <= maxLength)
        {
            return value;
        }

        wasTruncated = true;
        return value[..maxLength];
    }

    private static string CreateFingerprint(NormalizedDiagnosticWrite write)
    {
        var input = new StringBuilder(
            write.Code.Length
            + write.Context.Component.Length
            + write.Context.Scope.Identity.Length
            + 32);
        input.Append(write.Code)
            .Append('|')
            .Append(write.Context.Component)
            .Append('|')
            .Append(write.Context.Scope.Kind)
            .Append('|')
            .Append(write.Context.Scope.Identity)
            .Append('|')
            .Append(write.Context.Scope.Generation);
        foreach (var attribute in write.Attributes)
        {
            input.Append('|')
                .Append(attribute.Name)
                .Append('=')
                .Append(attribute.Value);
        }

        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(input.ToString()));
        return Convert.ToHexString(hash);
    }

    private sealed class Subscription(
        StudioDiagnosticHub owner,
        int index,
        Action invalidated) : IDisposable
    {
        private StudioDiagnosticHub? owner_ = owner;

        public void Dispose()
        {
            Interlocked.Exchange(ref owner_, null)?
                .Unsubscribe(index, invalidated);
        }
    }

    private sealed record NormalizedDiagnosticWrite(
        StudioDiagnosticSeverity Severity,
        StudioDiagnosticChannel Channel,
        string Code,
        string Category,
        StudioDiagnosticContext Context,
        string Message,
        string? Remediation,
        ImmutableArray<StudioDiagnosticAttribute> Attributes,
        bool WasTruncated);

    private sealed record NormalizedLogWrite(
        StudioLogLevel Level,
        string Channel,
        StudioDiagnosticContext Context,
        string MessageTemplate,
        string RenderedMessage,
        ImmutableArray<StudioDiagnosticAttribute> Attributes,
        bool WasTruncated);
}
