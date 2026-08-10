using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Shell.Docking.Splitters;
using Xunit;

namespace Editor.Tests.Shell.Docking.Splitters;

public sealed class EditorDockSplitResizeCoordinatorTests
{
    [Fact]
    public async Task Newer_request_keeps_one_active_candidate_and_replaces_only_the_queued_successor()
    {
        var requests = new List<EditorDockSplitResizeRequest>();
        var tokens = new List<CancellationToken>();
        var cancellations = new List<EditorDockSplitResizeCancellation>();
        using var coordinator = new EditorDockSplitResizeCoordinator(
            async (request, token) =>
            {
                requests.Add(request);
                tokens.Add(token);
                if (request.Sequence == 1)
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(25), token);
                }
            },
            cancellations.Add);
        var first = CreateRequest(transactionId: 7, sequence: 1);
        var second = CreateRequest(transactionId: 7, sequence: 2);

        Assert.True(coordinator.RequestLatest(first));
        Assert.False(tokens[0].IsCancellationRequested);
        Assert.True(coordinator.RequestLatest(second));
        await coordinator.WhenIdleAsync();

        Assert.False(tokens[0].IsCancellationRequested);
        Assert.False(tokens[1].IsCancellationRequested);
        Assert.Empty(cancellations);
        Assert.False(coordinator.IsCurrent(second.TransactionId, second.Sequence));
        Assert.Equal([first, second], requests);
        Assert.Equal(
            new EditorDockSplitResizeCoordinatorMetrics(2, 2, 0, 0, 2, false, false),
            coordinator.CaptureMetrics());
    }

    [Fact]
    public async Task Latest_request_replaces_a_queued_successor_without_canceling_active_work()
    {
        var releaseActive = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var requests = new List<EditorDockSplitResizeRequest>();
        var cancellations = new List<EditorDockSplitResizeCancellation>();
        using var coordinator = new EditorDockSplitResizeCoordinator(
            async (request, token) =>
            {
                requests.Add(request);
                if (request.Sequence == 1)
                {
                    await releaseActive.Task.WaitAsync(token);
                }
            },
            cancellations.Add);
        var first = CreateRequest(transactionId: 9, sequence: 1);
        var superseded = CreateRequest(transactionId: 9, sequence: 2);
        var latest = CreateRequest(transactionId: 9, sequence: 3);

        Assert.True(coordinator.RequestLatest(first));
        Assert.True(coordinator.RequestLatest(superseded));
        Assert.True(coordinator.RequestLatest(latest));
        releaseActive.SetResult();
        await coordinator.WhenIdleAsync();

        Assert.Equal([first, latest], requests);
        var cancellation = Assert.Single(cancellations);
        Assert.Equal(superseded.Sequence, cancellation.LastSequence);
        Assert.Equal(
            new EditorDockSplitResizeCoordinatorMetrics(3, 2, 1, 0, 2, false, false),
            coordinator.CaptureMetrics());
    }

    [Fact]
    public async Task Older_request_cannot_replace_the_last_accepted_sequence()
    {
        var requestCount = 0;
        using var coordinator = new EditorDockSplitResizeCoordinator(
            (_, _) =>
            {
                requestCount++;
                return Task.CompletedTask;
            },
            _ => { });
        var current = CreateRequest(transactionId: 3, sequence: 2);
        var stale = CreateRequest(transactionId: 3, sequence: 1);

        Assert.True(coordinator.RequestLatest(current));
        await coordinator.WhenIdleAsync();
        Assert.False(coordinator.RequestLatest(stale));

        Assert.Equal(1, requestCount);
        Assert.False(coordinator.IsCurrent(current.TransactionId, current.Sequence));
    }

    [Fact]
    public async Task Cancellation_invalidates_only_the_matching_latest_request()
    {
        CancellationToken token = default;
        var cancellationCount = 0;
        using var coordinator = new EditorDockSplitResizeCoordinator(
            async (_, requestToken) =>
            {
                token = requestToken;
                await Task.Delay(Timeout.InfiniteTimeSpan, requestToken);
            },
            _ => cancellationCount++);
        var request = CreateRequest(transactionId: 11, sequence: 4);
        Assert.True(coordinator.RequestLatest(request));

        Assert.False(coordinator.Cancel(new EditorDockSplitResizeCancellation(10, 4, "split")));
        Assert.False(coordinator.Cancel(new EditorDockSplitResizeCancellation(11, 3, "split")));
        Assert.True(coordinator.Cancel(new EditorDockSplitResizeCancellation(11, 4, "split")));
        await coordinator.WhenIdleAsync();

        Assert.True(token.IsCancellationRequested);
        Assert.Equal(1, cancellationCount);
        Assert.False(coordinator.IsCurrent(request.TransactionId, request.Sequence));
        Assert.Equal(1UL, coordinator.CaptureMetrics().ActiveCancelledRequests);
    }

    [Fact]
    public void Synchronous_completion_is_not_reentered_or_canceled()
    {
        EditorDockSplitResizeCoordinator? coordinator = null;
        var completionCount = 0;
        var cancellationCount = 0;
        coordinator = new EditorDockSplitResizeCoordinator(
            (request, _) =>
            {
                Assert.True(coordinator!.TryComplete(request.TransactionId, request.Sequence));
                completionCount++;
                return Task.CompletedTask;
            },
            _ => cancellationCount++);
        using (coordinator)
        {
            var request = CreateRequest(transactionId: 13, sequence: 1);
            Assert.True(coordinator.RequestLatest(request));
            Assert.False(coordinator.IsCurrent(request.TransactionId, request.Sequence));
        }

        Assert.Equal(1, completionCount);
        Assert.Equal(0, cancellationCount);
    }

    private static EditorDockSplitResizeRequest CreateRequest(ulong transactionId, ulong sequence)
    {
        var snapshot = new EditorDockSplitResizeCommittedSnapshot(
            "split",
            Orientation.Horizontal,
            new GridLength(1d, GridUnitType.Star),
            new GridLength(1d, GridUnitType.Star),
            100d,
            100d,
            1d);
        var proposal = new EditorDockSplitResizeProposal(
            new GridLength(110d, GridUnitType.Star),
            new GridLength(90d, GridUnitType.Star),
            110d,
            90d,
            10d,
            -100d,
            100d);
        return new EditorDockSplitResizeRequest(
            transactionId,
            sequence,
            snapshot,
            proposal,
            CumulativeDelta: 10d,
            IsFinal: false);
    }
}
