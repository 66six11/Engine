using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPanelViewSourceTests
{
    [Fact]
    public void Scene_view_wires_native_present_through_composition_host_without_blocking()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml.cs");

        Assert.Contains("ViewportNativeBridge", source, StringComparison.Ordinal);
        Assert.Contains("SceneViewCompositionPresenter", source, StringComparison.Ordinal);
        Assert.Contains("SceneViewNativeViewportLifecycle", source, StringComparison.Ordinal);
        Assert.Contains("QueryCompositionCompatibility(", source, StringComparison.Ordinal);
        Assert.Contains("AcquirePresentPacket(", source, StringComparison.Ordinal);
        Assert.Contains("PresentAsync(", source, StringComparison.Ordinal);
        Assert.Contains("CompositionHost.Surface", source, StringComparison.Ordinal);
        Assert.Contains("TryGetCompositionGpuInterop", source, StringComparison.Ordinal);
        Assert.Contains("UpdateNativePresent(", source, StringComparison.Ordinal);
        Assert.Contains("viewportLifecycle_", source, StringComparison.Ordinal);
        Assert.Contains("TryBeginPresent(", source, StringComparison.Ordinal);
        Assert.Contains("FrameRequested", source, StringComparison.Ordinal);
        Assert.Contains("OnSceneViewFrameRequested", source, StringComparison.Ordinal);
        Assert.Contains("TryPresentNativeFrameFromCurrentStateAsync", source, StringComparison.Ordinal);
        Assert.Contains("context.RequestRepaint()", source, StringComparison.Ordinal);
        Assert.Contains("TopLevel.GetTopLevel", source, StringComparison.Ordinal);
        Assert.Contains("RenderScaling", source, StringComparison.Ordinal);
        Assert.DoesNotContain("pendingPresent_", source, StringComparison.Ordinal);
        Assert.DoesNotContain("CanStartPresent()", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".Wait()", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".Result", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Thread.Sleep", source, StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
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
