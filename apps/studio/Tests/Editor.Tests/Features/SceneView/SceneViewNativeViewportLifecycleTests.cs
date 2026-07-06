using System.Threading.Tasks;
using Editor.Features.SceneView.Interop;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewNativeViewportLifecycleTests
{
    [Fact]
    public void Try_begin_present_rejects_reentry_until_pending_present_completes()
    {
        var lifecycle = new SceneViewNativeViewportLifecycle();
        var pendingPresent = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var rejectedFactories = 0;

        Assert.True(lifecycle.TryBeginPresent(() => pendingPresent.Task));
        Assert.False(
            lifecycle.TryBeginPresent(
                () =>
                {
                    rejectedFactories++;
                    return Task.CompletedTask;
                }));
        Assert.Equal(0, rejectedFactories);

        pendingPresent.SetResult();

        Assert.True(lifecycle.TryBeginPresent(() => Task.CompletedTask));
    }

    [Fact]
    public void Try_begin_present_rejects_null_present_task()
    {
        var lifecycle = new SceneViewNativeViewportLifecycle();

        Assert.Throws<System.ArgumentNullException>(() => lifecycle.TryBeginPresent(null!));
    }
}
