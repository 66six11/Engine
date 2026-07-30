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
        Assert.Contains("SceneViewPresentationSession", source, StringComparison.Ordinal);
        Assert.Contains("QueryCompositionCompatibility(", source, StringComparison.Ordinal);
        Assert.Contains("GetSceneRenderState()", source, StringComparison.Ordinal);
        Assert.Contains("scene.HasScene", source, StringComparison.Ordinal);
        Assert.Contains("scene.Revision", source, StringComparison.Ordinal);
        Assert.Contains("RequestFrame(", source, StringComparison.Ordinal);
        Assert.Contains("TryGetCompositionGpuInterop", source, StringComparison.Ordinal);
        Assert.Contains("UpdateNativePresent(", source, StringComparison.Ordinal);
        Assert.Contains("RenderRequested", source, StringComparison.Ordinal);
        Assert.Contains("OnSceneViewRenderRequested", source, StringComparison.Ordinal);
        Assert.Contains("QueueNativeFrameRetry", source, StringComparison.Ordinal);
        Assert.Contains("isRetryQueued_", source, StringComparison.Ordinal);
        Assert.Contains("RequestCompositionUpdate(", source, StringComparison.Ordinal);
        Assert.Contains("CompleteQueuedFrameRetry(queueSequence)", source, StringComparison.Ordinal);
        Assert.Contains("RequestNativeFrame()", source, StringComparison.Ordinal);
        Assert.Contains("TopLevel.GetTopLevel", source, StringComparison.Ordinal);
        Assert.Contains("RenderScaling", source, StringComparison.Ordinal);
        Assert.Contains("CompositionHost.Bounds.Size", source, StringComparison.Ordinal);
        Assert.Contains("CompositionHost.SizeChanged += OnCompositionHostSizeChanged", source, StringComparison.Ordinal);
        Assert.Contains("Task.Run(", source, StringComparison.Ordinal);
        Assert.Contains("await precedingDetach", source, StringComparison.Ordinal);
        Assert.Contains("ReleaseCompositionResourcesAsync(", source, StringComparison.Ordinal);
        Assert.Contains("ViewportNativePresentDrain.TrackAsync(detachTask_)", source, StringComparison.Ordinal);
        Assert.Contains("SetFrameSourceViewModel(DataContext as SceneViewPanelViewModel)", source, StringComparison.Ordinal);
        Assert.Contains("isAttached_ = true", source, StringComparison.Ordinal);
        Assert.Contains("OnDataContextChanged", source, StringComparison.Ordinal);
        Assert.Contains("BeginCapabilityProbe();", source, StringComparison.Ordinal);
        Assert.Contains("private void OnCompositionHostSizeChanged(", source, StringComparison.Ordinal);
        Assert.DoesNotContain("change.Property != BoundsProperty", source, StringComparison.Ordinal);
        Assert.Contains("PresentationSetupState.Configured", source, StringComparison.Ordinal);
        Assert.Contains("PresentationSetupState.WaitingForFrameExtent", source, StringComparison.Ordinal);
        Assert.Contains("BeginPresentationConfiguration();", source, StringComparison.Ordinal);
        Assert.DoesNotContain("EditorPanelFrameContext", source, StringComparison.Ordinal);
        Assert.DoesNotContain("pendingPresent_", source, StringComparison.Ordinal);
        Assert.DoesNotContain("NativePresent?.Status", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WidthPixels /", source, StringComparison.Ordinal);
        Assert.DoesNotContain("HeightPixels /", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".Wait()", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".Result", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Thread.Sleep", source, StringComparison.Ordinal);
    }

    [Fact]
    public void Layout_size_and_scene_changes_observe_immediately_while_retry_uses_composition_cadence()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml.cs");
        var resizeHandler =
            source[
                source.IndexOf(
                    "private void OnCompositionHostSizeChanged(",
                    StringComparison.Ordinal)..source.IndexOf(
                    "private void OnTopLevelScalingChanged(",
                    StringComparison.Ordinal)];
        var sceneHandler =
            source[
                source.IndexOf(
                    "private void OnSceneViewRenderRequested(",
                    StringComparison.Ordinal)..source.IndexOf(
                    "private void OnCompositionHostSizeChanged(",
                    StringComparison.Ordinal)];
        var presentationChange =
            source[
                source.IndexOf(
                    "private void RequestFrameForPresentationChange()",
                    StringComparison.Ordinal)..source.IndexOf(
                    "private enum PresentationSetupState",
                    StringComparison.Ordinal)];

        Assert.Contains("RequestFrameForPresentationChange();", resizeHandler, StringComparison.Ordinal);
        Assert.Contains("RequestNativeFrame();", sceneHandler, StringComparison.Ordinal);
        Assert.DoesNotContain("QueueNativeFrameRetry", resizeHandler, StringComparison.Ordinal);
        Assert.DoesNotContain("QueueNativeFrameRetry", sceneHandler, StringComparison.Ordinal);
        Assert.Contains("RequestNativeFrame();", presentationChange, StringComparison.Ordinal);
        Assert.DoesNotContain("QueueNativeFrameRetry", presentationChange, StringComparison.Ordinal);
        Assert.Contains("configuration.RequestRetry()", LoadSource(
            "Features",
            "SceneView",
            "Interop",
            "SceneViewPresentationSession.cs"), StringComparison.Ordinal);
        Assert.Contains("RequestCompositionUpdate(", source, StringComparison.Ordinal);
    }

    [Fact]
    public void Scene_view_guards_async_native_probe_and_frame_request_exceptions()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml.cs");

        Assert.DoesNotContain("async void ProbeCompositionCapabilities()", source, StringComparison.Ordinal);
        Assert.Contains("_ = ProbeCompositionCapabilitiesAsync(probeSequence)", source, StringComparison.Ordinal);
        Assert.Contains("private async Task ProbeCompositionCapabilitiesAsync(ulong probeSequence)", source, StringComparison.Ordinal);
        Assert.Contains("catch (Exception ex)", source, StringComparison.Ordinal);
        Assert.Contains("CreateLocalCompositionSnapshot(", source, StringComparison.Ordinal);
        Assert.Contains("ViewportNativePresentStatus.RenderFailed", source, StringComparison.Ordinal);
    }

    [Fact]
    public void Scene_view_invalidates_pixels_when_top_level_scaling_changes()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml.cs");

        Assert.Contains("presentationTopLevel_.ScalingChanged += OnTopLevelScalingChanged", source, StringComparison.Ordinal);
        Assert.Contains("presentationTopLevel_.ScalingChanged -= OnTopLevelScalingChanged", source, StringComparison.Ordinal);
        Assert.Contains("private void OnTopLevelScalingChanged(object? sender, EventArgs e)", source, StringComparison.Ordinal);
        Assert.Contains("RequestFrameForPresentationChange();", source, StringComparison.Ordinal);
        Assert.Contains("topLevel.RenderScaling", source, StringComparison.Ordinal);
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
