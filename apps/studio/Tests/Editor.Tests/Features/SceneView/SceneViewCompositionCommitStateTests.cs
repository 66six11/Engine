using Avalonia;
using Editor.Features.SceneView.Views;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionCommitStateTests
{
    [Fact]
    public void Failed_newer_attempt_rolls_back_to_last_successful_frame()
    {
        var state = new SceneViewCompositionCommitState();
        var successfulSize = new Size(320, 180);
        var successfulVersion = state.BeginAttempt();
        Assert.True(
            state.CompleteSuccessfulAttempt(
                successfulVersion,
                successfulSize));

        _ = state.BeginAttempt();
        var newestVersion = state.BeginAttempt();

        Assert.True(
            state.TryGetRollbackTarget(
                newestVersion,
                out var rollbackSize));
        Assert.Equal(successfulSize, rollbackSize);
    }

    [Fact]
    public void Attempted_size_does_not_become_successful_until_completion()
    {
        var state = new SceneViewCompositionCommitState();
        var attemptedSize = new Size(640, 360);
        var version = state.BeginAttempt();

        Assert.Null(state.LastSuccessfulFrameSizeDip);
        Assert.True(state.TryGetRollbackTarget(version, out var rollbackSize));
        Assert.Null(rollbackSize);

        Assert.True(
            state.CompleteSuccessfulAttempt(
                version,
                attemptedSize));
        Assert.Equal(attemptedSize, state.LastSuccessfulFrameSizeDip);
    }

    [Fact]
    public void Reset_invalidates_in_flight_completion()
    {
        var state = new SceneViewCompositionCommitState();
        var version = state.BeginAttempt();

        state.Reset();

        Assert.False(
            state.CompleteSuccessfulAttempt(
                version,
                new Size(800, 450)));
        Assert.Null(state.LastSuccessfulFrameSizeDip);
    }
}
