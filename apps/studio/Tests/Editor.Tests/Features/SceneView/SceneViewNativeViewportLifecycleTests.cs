using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Editor.Features.SceneView.Interop;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewNativeViewportLifecycleTests
{
    [Fact]
    public async Task Try_begin_present_rejects_reentry_until_pending_present_completes()
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
        await WaitUntilAsync(() => lifecycle.CanBeginPresent);

        Assert.True(lifecycle.TryBeginPresent(() => Task.CompletedTask));
    }

    [Fact]
    public void Try_begin_present_rejects_null_present_task()
    {
        var lifecycle = new SceneViewNativeViewportLifecycle();

        Assert.Throws<System.ArgumentNullException>(() => lifecycle.TryBeginPresent(null!));
    }

    [Fact]
    public void Scene_view_native_lifecycle_tracks_present_drain_for_app_shutdown()
    {
        var source = LoadSource("Features", "SceneView", "Interop", "SceneViewNativeViewportLifecycle.cs");

        Assert.Contains("ViewportNativePresentDrain.CanBeginPresent", source);
        Assert.Contains("ViewportNativePresentDrain.TrackAsync", source);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        for (var attempt = 0; attempt < 100; attempt++)
        {
            if (condition())
            {
                return;
            }

            await Task.Delay(10);
        }

        Assert.True(condition());
    }

    private static string FindRepositoryRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
