using System;
using Asharia.Studio.Application.Diagnostics;

namespace Asharia.Studio.Observe.Tests;

internal sealed class ContradictoryCursorHub : IStudioDiagnosticHub
{
    private readonly StudioDiagnosticHub inner_ = new(
        diagnosticCapacity: 2,
        logCapacity: 2);

    public StudioProcessIdentity ProcessIdentity => inner_.ProcessIdentity;

    public int DiagnosticCapacity => inner_.DiagnosticCapacity;

    public int LogCapacity => inner_.LogCapacity;

    public long SubscriberFailureCount => inner_.SubscriberFailureCount;

    public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
        inner_.PublishDiagnostic(write);

    public StudioLogRecord PublishLog(StudioLogWrite write) =>
        inner_.PublishLog(write);

    public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
        long afterSequence = 0,
        int maxCount = StudioDiagnosticHub.DefaultReadLimit,
        StudioDiagnosticChannel? channel = null) =>
        Contradictory<StudioDiagnosticRecord>();

    public StudioCursorWindow<StudioLogRecord> ReadLogs(
        long afterSequence = 0,
        int maxCount = StudioDiagnosticHub.DefaultReadLimit) =>
        Contradictory<StudioLogRecord>();

    public StudioDiagnosticRecord? GetLatestDiagnostic() =>
        inner_.GetLatestDiagnostic();

    public IDisposable Subscribe(Action invalidated) =>
        inner_.Subscribe(invalidated);

    private static StudioCursorWindow<T> Contradictory<T>()
        where T : class =>
        new(
            OldestAvailableSequence: 10,
            NextCursor: 0,
            TotalDropped: 9,
            CursorExpired: false,
            Truncated: false,
            Items: []);
}
