using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace Asharia.Studio.Application.Diagnostics;

public sealed class StudioDiagnosticHub : IStudioDiagnosticHub
{
    public const int DefaultDiagnosticCapacity = 2048;
    public const int DefaultLogCapacity = 8192;
    public const long DefaultDiagnosticByteCapacity = 8L * 1024 * 1024;
    public const long DefaultLogByteCapacity = 32L * 1024 * 1024;
    public const int DefaultReadLimit = 200;
    public const int MaxSubscriberCount = 64;
    public const int DefaultActiveProblemCapacity = 1024;
    public const long DefaultActiveProblemByteCapacity = 4L * 1024 * 1024;

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
    private readonly object activeProblemsGate_ = new();
    private readonly Dictionary<StudioProblemId, StudioDiagnosticRecord> activeProblems_ = [];
    private readonly SubscriberSet diagnosticSubscribers_ = new();
    private readonly SubscriberSet logSubscribers_ = new();
    private readonly int activeProblemCapacity_;
    private readonly long activeProblemByteCapacity_;
    private long activeProblemVersion_;
    private long activeProblemDroppedCount_;
    private long activeProblemResidentBytes_;
    private long diagnosticCommitVersion_;
    private int diagnosticNotificationScheduled_;
    private int logNotificationScheduled_;

    public StudioDiagnosticHub(
        int diagnosticCapacity = DefaultDiagnosticCapacity,
        int logCapacity = DefaultLogCapacity,
        StudioProcessIdentity? processIdentity = null,
        long diagnosticByteCapacity = DefaultDiagnosticByteCapacity,
        long logByteCapacity = DefaultLogByteCapacity,
        int activeProblemCapacity = DefaultActiveProblemCapacity,
        long activeProblemByteCapacity = DefaultActiveProblemByteCapacity)
    {
        if (activeProblemCapacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(activeProblemCapacity));
        }

        if (activeProblemByteCapacity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(activeProblemByteCapacity));
        }

        diagnostics_ = new BoundedConcurrentRing<StudioDiagnosticRecord>(
            diagnosticCapacity,
            diagnosticByteCapacity,
            record => record.SequenceId,
            EstimateResidentBytes);
        logs_ = new BoundedConcurrentRing<StudioLogRecord>(
            logCapacity,
            logByteCapacity,
            record => record.SequenceId,
            EstimateResidentBytes);
        activeProblemCapacity_ = activeProblemCapacity;
        activeProblemByteCapacity_ = activeProblemByteCapacity;
        ProcessIdentity = processIdentity ?? StudioProcessIdentity.CreateNew();
    }

    public StudioProcessIdentity ProcessIdentity { get; }

    public int DiagnosticCapacity => diagnostics_.Capacity;

    public int LogCapacity => logs_.Capacity;

    public long SubscriberFailureCount =>
        DiagnosticSubscriberFailureCount + LogSubscriberFailureCount;

    public StudioDiagnosticBufferState DiagnosticBufferState =>
        ToPublicState(diagnostics_.GetState());

    public StudioDiagnosticBufferState LogBufferState =>
        ToPublicState(logs_.GetState());

    public long DiagnosticSubscriberFailureCount =>
        diagnosticSubscribers_.FailureCount;

    public long LogSubscriberFailureCount =>
        logSubscribers_.FailureCount;

    public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write)
    {
        ArgumentNullException.ThrowIfNull(write);
        var normalized = Normalize(write);
        StudioDiagnosticRecord Publish() => diagnostics_.Publish(sequence => new StudioDiagnosticRecord(
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
            normalized.WasTruncated,
            normalized.ProblemId,
            normalized.ProblemTransition));
        StudioDiagnosticRecord record;
        if (normalized.ProblemTransition is null)
        {
            record = Publish();
        }
        else
        {
            lock (activeProblemsGate_)
            {
                record = Publish();
                ApplyProblemTransition(record);
            }
        }

        Interlocked.Increment(ref diagnosticCommitVersion_);
        ScheduleDiagnosticNotification();
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

    public StudioActiveProblemSnapshot ReadActiveProblems()
    {
        lock (activeProblemsGate_)
        {
            return new StudioActiveProblemSnapshot(
                activeProblemVersion_,
                activeProblemCapacity_,
                activeProblemByteCapacity_,
                activeProblems_.Count,
                activeProblemResidentBytes_,
                activeProblemDroppedCount_,
                activeProblemDroppedCount_ > 0,
                activeProblems_.Values
                    .OrderBy(record => record.SequenceId)
                    .ToImmutableArray());
        }
    }

    public IDisposable SubscribeDiagnostics(Action invalidated) =>
        diagnosticSubscribers_.Subscribe(invalidated);

    public IDisposable SubscribeLogs(Action invalidated) =>
        logSubscribers_.Subscribe(invalidated);

    private void ScheduleDiagnosticNotification()
    {
        if (Interlocked.CompareExchange(
                ref diagnosticNotificationScheduled_,
                1,
                0) != 0)
        {
            return;
        }

        ThreadPool.QueueUserWorkItem(
            static state => ((StudioDiagnosticHub)state!).DispatchDiagnosticNotifications(),
            this,
            preferLocal: false);
    }

    private void DispatchDiagnosticNotifications()
    {
        while (true)
        {
            var observedVersion = Volatile.Read(ref diagnosticCommitVersion_);
            diagnosticSubscribers_.Notify();
            Interlocked.Exchange(ref diagnosticNotificationScheduled_, 0);
            if (Volatile.Read(ref diagnosticCommitVersion_) == observedVersion
                || Interlocked.CompareExchange(
                    ref diagnosticNotificationScheduled_,
                    1,
                    0) != 0)
            {
                return;
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
            logSubscribers_.Notify();
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

    private static StudioDiagnosticBufferState ToPublicState(
        BoundedConcurrentRingState state) =>
        new(
            state.CountCapacity,
            state.PayloadByteCapacity,
            state.ResidentCount,
            state.EstimatedResidentPayloadBytes,
            state.TotalDropped);

    private void ApplyProblemTransition(StudioDiagnosticRecord record)
    {
        var problemId = record.ProblemId!.Value;
        activeProblemVersion_++;
        if (record.ProblemTransition == StudioProblemTransition.Active)
        {
            activeProblems_.TryGetValue(problemId, out var replaced);
            var replacedBytes = replaced is null
                ? 0
                : EstimateResidentBytes(replaced);
            var recordBytes = EstimateResidentBytes(record);
            var replacementBytes = activeProblemResidentBytes_
                - replacedBytes
                + recordBytes;
            if ((replaced is not null
                    || activeProblems_.Count < activeProblemCapacity_)
                && replacementBytes <= activeProblemByteCapacity_)
            {
                activeProblems_[problemId] = record;
                activeProblemResidentBytes_ = replacementBytes;
            }
            else
            {
                activeProblemDroppedCount_++;
            }

            return;
        }

        if (activeProblems_.Remove(problemId, out var removed))
        {
            activeProblemResidentBytes_ -= EstimateResidentBytes(removed);
        }
    }

    private static long EstimateResidentBytes(StudioDiagnosticRecord record)
    {
        var bytes = 0L;
        bytes += Utf8Bytes(record.Code);
        bytes += Utf8Bytes(record.Category);
        bytes += EstimateResidentBytes(record.Context);
        bytes += Utf8Bytes(record.Message);
        bytes += Utf8Bytes(record.Remediation);
        bytes += Utf8Bytes(record.Fingerprint);
        bytes += Utf8Bytes(record.ProblemId?.Value);
        bytes += EstimateResidentBytes(record.Attributes);
        return bytes;
    }

    private static long EstimateResidentBytes(StudioLogRecord record)
    {
        var bytes = 0L;
        bytes += Utf8Bytes(record.Channel);
        bytes += EstimateResidentBytes(record.Context);
        bytes += Utf8Bytes(record.MessageTemplate);
        bytes += Utf8Bytes(record.RenderedMessage);
        bytes += EstimateResidentBytes(record.Attributes);
        return bytes;
    }

    private static long EstimateResidentBytes(StudioDiagnosticContext context)
    {
        var bytes = 0L;
        bytes += Utf8Bytes(context.Package);
        bytes += Utf8Bytes(context.Component);
        bytes += Utf8Bytes(context.Scope.Kind);
        bytes += Utf8Bytes(context.Scope.Identity);
        return bytes;
    }

    private static long EstimateResidentBytes(
        ImmutableArray<StudioDiagnosticAttribute> attributes)
    {
        var bytes = 0L;
        foreach (var attribute in attributes)
        {
            bytes += Utf8Bytes(attribute.Name);
            bytes += Utf8Bytes(attribute.Value);
        }

        return bytes;
    }

    private static int Utf8Bytes(string? value) =>
        value is null ? 0 : Encoding.UTF8.GetByteCount(value);

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
        var problem = NormalizeProblem(write);
        return new NormalizedDiagnosticWrite(
            write.Severity,
            write.Channel,
            code,
            category,
            context,
            message,
            remediation,
            attributes,
            wasTruncated,
            problem.Id,
            problem.Transition);
    }

    private static (
        StudioProblemId? Id,
        StudioProblemTransition? Transition) NormalizeProblem(
        StudioDiagnosticWrite write)
    {
        if (write.ProblemId.HasValue != write.ProblemTransition.HasValue)
        {
            throw new ArgumentException(
                "Problem identity and transition must either both be present or both be absent.",
                nameof(write));
        }

        if (write.ProblemId is null)
        {
            return (null, null);
        }

        var problemId = write.ProblemId.Value;
        var transition = write.ProblemTransition!.Value;

        if (write.Channel != StudioDiagnosticChannel.Problem)
        {
            throw new ArgumentException(
                "Problem transitions require the problem diagnostic channel.",
                nameof(write));
        }

        if (!Enum.IsDefined(transition))
        {
            throw new ArgumentOutOfRangeException(
                nameof(write),
                transition,
                "Problem transition is not defined.");
        }

        if (string.IsNullOrWhiteSpace(problemId.Value))
        {
            throw new ArgumentException(
                "Problem identity cannot be empty.",
                nameof(write));
        }

        return (problemId, transition);
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

    private sealed class SubscriberSet
    {
        private readonly object gate_ = new();
        private readonly Action?[] subscribers_ = new Action[MaxSubscriberCount];
        private long failureCount_;

        public long FailureCount => Volatile.Read(ref failureCount_);

        public IDisposable Subscribe(Action invalidated)
        {
            ArgumentNullException.ThrowIfNull(invalidated);
            lock (gate_)
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

        public void Notify()
        {
            Action?[] snapshot;
            lock (gate_)
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
                    Interlocked.Increment(ref failureCount_);
                }
            }
        }

        private void Unsubscribe(int index, Action invalidated)
        {
            lock (gate_)
            {
                if (ReferenceEquals(subscribers_[index], invalidated))
                {
                    subscribers_[index] = null;
                }
            }
        }

        private sealed class Subscription(
            SubscriberSet owner,
            int index,
            Action invalidated) : IDisposable
        {
            private SubscriberSet? owner_ = owner;

            public void Dispose()
            {
                Interlocked.Exchange(ref owner_, null)?
                    .Unsubscribe(index, invalidated);
            }
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
        bool WasTruncated,
        StudioProblemId? ProblemId,
        StudioProblemTransition? ProblemTransition);

    private sealed record NormalizedLogWrite(
        StudioLogLevel Level,
        string Channel,
        StudioDiagnosticContext Context,
        string MessageTemplate,
        string RenderedMessage,
        ImmutableArray<StudioDiagnosticAttribute> Attributes,
        bool WasTruncated);
}
