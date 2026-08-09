using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public readonly record struct ViewportPresentationQuarantineTransferReceipt(
    long TransferSequence,
    string EndpointId,
    int AcceptedOperationCount,
    int AcceptedStreamCount,
    int AcceptedSurfaceCount,
    int AcceptedFrameCount,
    long TransferredAtTimestamp,
    string Reason);

public readonly record struct ViewportPresentationQuarantineDrainReceipt(
    long ObservedAtTimestamp,
    long TransferCount,
    int OperationCount,
    int StreamCount,
    int SurfaceCount,
    int FrameCount,
    bool RetainedUntilProcessExit);

/// <summary>
/// Process-lifetime owner for resources whose compositor/native ownership is ambiguous. It never
/// guesses by disposing them during ordinary endpoint teardown; the operating system reclaims them
/// only after the process and Vulkan runtime have exited.
/// </summary>
internal sealed class ViewportPresentationProcessQuarantineRegistry
{
    private readonly object gate_ = new();
    private readonly HashSet<object> operations_ = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<object> streams_ = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<object> surfaces_ = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<object> frames_ = new(ReferenceEqualityComparer.Instance);
    private long transferCount_;

    public ViewportPresentationQuarantineTransferReceipt TransferPublished(
        string endpointId,
        object operation,
        IReadOnlyList<object> streams,
        IReadOnlyList<object> surfaces,
        string reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(endpointId);
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentNullException.ThrowIfNull(streams);
        ArgumentNullException.ThrowIfNull(surfaces);
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);

        lock (gate_)
        {
            var acceptedOperations = operations_.Add(operation) ? 1 : 0;
            var acceptedStreams = AddUnique(streams_, streams);
            var acceptedSurfaces = AddUnique(surfaces_, surfaces);
            var sequence = ++transferCount_;
            return new ViewportPresentationQuarantineTransferReceipt(
                sequence,
                endpointId,
                acceptedOperations,
                acceptedStreams,
                acceptedSurfaces,
                AcceptedFrameCount: 0,
                Stopwatch.GetTimestamp(),
                reason);
        }
    }

    public ViewportPresentationQuarantineTransferReceipt TransferFrame(
        string endpointId,
        object frame,
        string reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(endpointId);
        ArgumentNullException.ThrowIfNull(frame);
        ArgumentException.ThrowIfNullOrWhiteSpace(reason);

        lock (gate_)
        {
            var accepted = frames_.Add(frame) ? 1 : 0;
            var sequence = ++transferCount_;
            return new ViewportPresentationQuarantineTransferReceipt(
                sequence,
                endpointId,
                AcceptedOperationCount: 0,
                AcceptedStreamCount: 0,
                AcceptedSurfaceCount: 0,
                AcceptedFrameCount: accepted,
                Stopwatch.GetTimestamp(),
                reason);
        }
    }

    public ViewportPresentationQuarantineDrainReceipt CaptureDrainReceipt()
    {
        lock (gate_)
        {
            return new ViewportPresentationQuarantineDrainReceipt(
                Stopwatch.GetTimestamp(),
                transferCount_,
                operations_.Count,
                streams_.Count,
                surfaces_.Count,
                frames_.Count,
                RetainedUntilProcessExit: true);
        }
    }

    private static int AddUnique(
        HashSet<object> destination,
        IReadOnlyList<object> resources)
    {
        var accepted = 0;
        foreach (var resource in resources)
        {
            ArgumentNullException.ThrowIfNull(resource);
            accepted += destination.Add(resource) ? 1 : 0;
        }
        return accepted;
    }
}

internal static class ViewportPresentationProcessQuarantine
{
    internal static ViewportPresentationProcessQuarantineRegistry Registry { get; } = new();
}
