using System;
using System.Collections.Immutable;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Observe.Client;
using Xunit;

namespace Asharia.Studio.Observe.Tests;

public sealed class StudioObservationClientCursorTests
{
    [Fact]
    public void Typed_cursor_rejects_each_cross_field_contradiction()
    {
        var invalidWindows = new[]
        {
            Window(oldest: 0, next: 0, dropped: 0, expired: false),
            Window(oldest: 10, next: 0, dropped: 9, expired: false),
            Window(oldest: 10, next: 0, dropped: 0, expired: true),
            Window(oldest: 10, next: 10, dropped: 9, expired: true, [new CursorItem(9)]),
            Window(oldest: 1, next: 0, dropped: 0, expired: true),
        };

        foreach (var window in invalidWindows)
        {
            var result = Validate(window, requestedAfterSequence: 0);

            Assert.False(result.Succeeded);
            Assert.Equal("observation.client.invalid-cursor", result.Failure!.Code);
        }
    }

    [Fact]
    public void Typed_cursor_accepts_expired_retained_data_and_a_future_cursor()
    {
        var expired = Validate(
            Window(
                oldest: 10,
                next: 12,
                dropped: 9,
                expired: true,
                [new CursorItem(11), new CursorItem(12)]),
            requestedAfterSequence: 0);
        var future = Validate(
            Window(oldest: 1, next: 100, dropped: 0, expired: false),
            requestedAfterSequence: 100);

        Assert.True(expired.Succeeded);
        Assert.True(future.Succeeded);
    }

    private static StudioObservationOperationResult<ObservationCursorWindow<CursorItem>>
        Validate(
            ObservationCursorWindow<CursorItem> window,
            long requestedAfterSequence)
    {
        var outcome = window.CursorExpired || window.Truncated
            ? ObservationOutcome.Partial
            : ObservationOutcome.Complete;
        return StudioObservationConnection.ValidateCursor(
            new StudioObservationOperationResult<ObservationCursorWindow<CursorItem>>(
                new ObservationResponse<ObservationCursorWindow<CursorItem>>(
                    ObservationProtocolVersion.Current,
                    new ObservationRequestId(Guid.NewGuid()),
                    new StudioInstanceId(Guid.NewGuid()),
                    EndpointGeneration: 1,
                    outcome,
                    window),
                Failure: null),
            requestedAfterSequence,
            requestedMaxCount: 4,
            static item => item.Sequence);
    }

    private static ObservationCursorWindow<CursorItem> Window(
        long oldest,
        long next,
        long dropped,
        bool expired,
        ImmutableArray<CursorItem> items = default) =>
        new(
            oldest,
            next,
            dropped,
            expired,
            Truncated: false,
            items.IsDefault ? [] : items);

    private sealed record CursorItem(long Sequence);
}
