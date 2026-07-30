using System.Threading.Tasks;
using Editor.Features.SceneView.Views;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewSurfaceUpdateGateTests
{
    [Fact]
    public async Task Waiting_update_starts_only_after_the_previous_presentation_is_accepted()
    {
        var gate = new SceneViewSurfaceUpdateGate();
        var firstUpdateStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirstUpdate = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var firstAccepted = false;
        var secondUpdateObservedFirstAcceptance = false;

        var first =
            gate.RunAsync(
                () => true,
                async () =>
                {
                    firstUpdateStarted.SetResult();
                    await releaseFirstUpdate.Task;
                    return true;
                },
                () =>
                {
                    firstAccepted = true;
                    return true;
                });
        await firstUpdateStarted.Task;

        var second =
            gate.RunAsync(
                () => true,
                () =>
                {
                    secondUpdateObservedFirstAcceptance = firstAccepted;
                    return Task.FromResult(true);
                },
                () => true);

        Assert.False(second.IsCompleted);
        releaseFirstUpdate.SetResult();

        Assert.True(await first);
        Assert.True(await second);
        Assert.True(secondUpdateObservedFirstAcceptance);
    }

    [Fact]
    public async Task Detach_during_an_update_rejects_it_and_skips_the_waiting_update()
    {
        var gate = new SceneViewSurfaceUpdateGate();
        var updateStarted = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseUpdate = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var canPresent = true;
        var acceptCalls = 0;
        var waitingUpdateCalls = 0;

        var active =
            gate.RunAsync(
                () => canPresent,
                async () =>
                {
                    updateStarted.SetResult();
                    await releaseUpdate.Task;
                    return true;
                },
                () =>
                {
                    acceptCalls++;
                    return true;
                });
        await updateStarted.Task;

        var waiting =
            gate.RunAsync(
                () => canPresent,
                () =>
                {
                    waitingUpdateCalls++;
                    return Task.FromResult(true);
                },
                () =>
                {
                    acceptCalls++;
                    return true;
                });

        canPresent = false;
        releaseUpdate.SetResult();

        Assert.False(await active);
        Assert.False(await waiting);
        Assert.Equal(0, acceptCalls);
        Assert.Equal(0, waitingUpdateCalls);
    }
}
