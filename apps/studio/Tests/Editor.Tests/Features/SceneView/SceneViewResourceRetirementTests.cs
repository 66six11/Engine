using System;
using System.Threading.Tasks;
using Editor.Features.SceneView.Interop;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewResourceRetirementTests
{
    [Fact]
    public async Task Consumer_failure_quarantines_without_producer_release()
    {
        var consumerAttempts = 0;
        var producerAttempts = 0;

        var result =
            await SceneViewResourceRetirement.RunAsync(
            () =>
            {
                consumerAttempts++;
                return Task.FromException(
                    new InvalidOperationException(
                        "consumer ownership was not released"));
            },
            () =>
            {
                producerAttempts++;
                return Task.CompletedTask;
            });

        Assert.False(result.Released);
        Assert.IsType<InvalidOperationException>(result.Failure);
        Assert.Equal(1, consumerAttempts);
        Assert.Equal(0, producerAttempts);
    }

    [Fact]
    public async Task Producer_failure_quarantines_after_one_consumer_release()
    {
        var consumerAttempts = 0;
        var producerAttempts = 0;

        var result =
            await SceneViewResourceRetirement.RunAsync(
            () =>
            {
                consumerAttempts++;
                return Task.CompletedTask;
            },
            () =>
            {
                producerAttempts++;
                return Task.FromException(
                    new InvalidOperationException(
                        "producer ownership was not released"));
            });

        Assert.False(result.Released);
        Assert.IsType<InvalidOperationException>(result.Failure);
        Assert.Equal(1, consumerAttempts);
        Assert.Equal(1, producerAttempts);
    }

    [Fact]
    public async Task Successful_retirement_releases_consumer_before_producer()
    {
        var steps = new System.Collections.Generic.List<string>();

        var result =
            await SceneViewResourceRetirement.RunAsync(
                () =>
                {
                    steps.Add("consumer");
                    return Task.CompletedTask;
                },
                () =>
                {
                    steps.Add("producer");
                    return Task.CompletedTask;
                });

        Assert.True(result.Released);
        Assert.Null(result.Failure);
        Assert.Equal(["consumer", "producer"], steps);
    }
}
