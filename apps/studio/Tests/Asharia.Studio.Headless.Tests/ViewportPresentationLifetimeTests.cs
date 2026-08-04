using System;
using System.Threading.Tasks;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationLifetimeTests
{
    [Fact]
    public async Task Pause_blocks_new_frames_until_active_work_drains_and_scope_resumes()
    {
        var lifetime = new ViewportPresentationLifetime();
        var resumeCount = 0;
        lifetime.Resumed += (_, _) => resumeCount++;
        Assert.True(lifetime.TryBeginFrame(out var frame));

        var pausing = lifetime.PauseAndDrainAsync().AsTask();
        try
        {
            Assert.False(pausing.IsCompleted);
            var admittedWhilePausing = lifetime.TryBeginFrame(out var blockedFrame);
            blockedFrame?.Dispose();
            Assert.False(admittedWhilePausing);
        }
        finally
        {
            frame.Dispose();
        }

        await using (await pausing.WaitAsync(
                         TimeSpan.FromSeconds(2),
                         TestContext.Current.CancellationToken))
        {
            var admittedWhilePaused = lifetime.TryBeginFrame(out var blockedFrame);
            blockedFrame?.Dispose();
            Assert.False(admittedWhilePaused);
        }
        Assert.Equal(1, resumeCount);

        Assert.True(lifetime.TryBeginFrame(out var resumedFrame));
        resumedFrame.Dispose();
        await lifetime.StopAndDrainAsync();
    }

    [Fact]
    public async Task Stop_permanently_rejects_new_frame_admission()
    {
        var lifetime = new ViewportPresentationLifetime();
        Assert.True(lifetime.TryBeginFrame(out var frame));

        var stopping = lifetime.StopAndDrainAsync().AsTask();
        Assert.False(stopping.IsCompleted);
        frame.Dispose();
        await stopping;

        Assert.False(lifetime.TryBeginFrame(out _));
    }
}
