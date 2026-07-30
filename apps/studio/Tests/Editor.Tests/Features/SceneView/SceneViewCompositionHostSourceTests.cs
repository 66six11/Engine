using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionHostSourceTests
{
    [Fact]
    public void Host_serializes_surface_updates_and_keeps_the_visual_fitted_to_current_bounds()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewCompositionHost.cs");

        Assert.Contains("ElementComposition.GetElementVisual(this)", source, StringComparison.Ordinal);
        Assert.Contains("CreateDrawingSurface()", source, StringComparison.Ordinal);
        Assert.Contains("CreateSurfaceVisual()", source, StringComparison.Ordinal);
        Assert.Contains("visual_.Surface = surface_", source, StringComparison.Ordinal);
        Assert.Contains("ElementComposition.SetElementChildVisual(this, visual_)", source, StringComparison.Ordinal);
        Assert.Contains("CompositionDrawingSurface? Surface", source, StringComparison.Ordinal);
        Assert.Contains("TryCommitFrameAsync(", source, StringComparison.Ordinal);
        Assert.Contains("SceneViewSurfaceUpdateGate surfaceUpdateGate_", source, StringComparison.Ordinal);
        Assert.Contains("surfaceUpdateGate_.RunAsync(", source, StringComparison.Ordinal);
        Assert.Contains("RequestCompositionUpdate(", source, StringComparison.Ordinal);
        Assert.Contains("updateTask = updateSurface(surface)", source, StringComparison.Ordinal);
        Assert.Contains("ApplyFramePlacement(visual)", source, StringComparison.Ordinal);
        Assert.Contains("visual.Size = ToVector(Bounds.Size)", source, StringComparison.Ordinal);
        Assert.Contains("visual.Offset = Vector3.Zero", source, StringComparison.Ordinal);
        Assert.Contains("change.Property == BoundsProperty", source, StringComparison.Ordinal);
        Assert.Equal(
            1,
            source.Split(
                "RequestCompositionUpdate(",
                StringSplitOptions.None).Length - 1);
        Assert.DoesNotContain("Bounds.Size.Equals(frameSizeDip)", source, StringComparison.Ordinal);
        Assert.DoesNotContain("isPlacementUpdateQueued_", source, StringComparison.Ordinal);
        Assert.DoesNotContain("(Bounds.Width -", source, StringComparison.Ordinal);
        Assert.DoesNotContain("(Bounds.Height -", source, StringComparison.Ordinal);
        Assert.Contains("ReleaseCompositionResourcesAsync(", source, StringComparison.Ordinal);
        Assert.Contains("DisposeSurfaceAfterAsync(", source, StringComparison.Ordinal);
        Assert.DoesNotContain("SceneViewCompositionCommitState", source, StringComparison.Ordinal);
        Assert.DoesNotContain("TryGetRollbackTarget(", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WidthPixels /", source, StringComparison.Ordinal);
        Assert.DoesNotContain("HeightPixels /", source, StringComparison.Ordinal);

        Assert.DoesNotContain("SemaphoreSlim", source, StringComparison.Ordinal);
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
