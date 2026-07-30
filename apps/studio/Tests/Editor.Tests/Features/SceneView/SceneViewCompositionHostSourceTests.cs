using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionHostSourceTests
{
    [Fact]
    public void Host_commits_exact_frame_geometry_atomically_and_centers_old_frame()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewCompositionHost.cs");

        Assert.Contains("ElementComposition.GetElementVisual(this)", source, StringComparison.Ordinal);
        Assert.Contains("CreateDrawingSurface()", source, StringComparison.Ordinal);
        Assert.Contains("CreateSurfaceVisual()", source, StringComparison.Ordinal);
        Assert.Contains("visual_.Surface = surface_", source, StringComparison.Ordinal);
        Assert.Contains("ElementComposition.SetElementChildVisual(this, visual_)", source, StringComparison.Ordinal);
        Assert.Contains("CompositionDrawingSurface? Surface", source, StringComparison.Ordinal);
        Assert.Contains("TryCommitFrameAsync(", source, StringComparison.Ordinal);
        Assert.Contains("surfaceUpdateGate_.WaitAsync()", source, StringComparison.Ordinal);
        Assert.Contains("RequestCompositionUpdate(", source, StringComparison.Ordinal);
        Assert.Contains("updateTask = updateSurface(surface)", source, StringComparison.Ordinal);
        Assert.Contains("ApplyFramePlacement(visual, frameSizeDip)", source, StringComparison.Ordinal);
        Assert.Contains("visual.Size = ToVector(size)", source, StringComparison.Ordinal);
        Assert.Contains("change.Property == BoundsProperty", source, StringComparison.Ordinal);
        Assert.Contains("isPlacementUpdateQueued_", source, StringComparison.Ordinal);
        Assert.Contains("(Bounds.Width - size.Width) / 2d", source, StringComparison.Ordinal);
        Assert.Contains("(Bounds.Height - size.Height) / 2d", source, StringComparison.Ordinal);
        Assert.Equal(
            2,
            source.Split(
                "!isCurrent() || !Bounds.Size.Equals(frameSizeDip)",
                StringSplitOptions.None).Length - 1);
        Assert.Contains("ReleaseCompositionResourcesAsync(", source, StringComparison.Ordinal);
        Assert.Contains("DisposeSurfaceAfterAsync(", source, StringComparison.Ordinal);
        Assert.Contains("CompleteSuccessfulAttempt(", source, StringComparison.Ordinal);
        Assert.Contains("TryGetRollbackTarget(", source, StringComparison.Ordinal);
        Assert.Contains("LastSuccessfulFrameSizeDip", source, StringComparison.Ordinal);
        Assert.DoesNotContain("WidthPixels /", source, StringComparison.Ordinal);
        Assert.DoesNotContain("HeightPixels /", source, StringComparison.Ordinal);
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
