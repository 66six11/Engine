using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewCompositionHostSourceTests
{
    [Fact]
    public void Host_creates_drawing_surface_visual_and_updates_size()
    {
        var source = LoadSource("Features", "SceneView", "Views", "SceneViewCompositionHost.cs");

        Assert.Contains("ElementComposition.GetElementVisual(this)", source, StringComparison.Ordinal);
        Assert.Contains("CreateDrawingSurface()", source, StringComparison.Ordinal);
        Assert.Contains("CreateSurfaceVisual()", source, StringComparison.Ordinal);
        Assert.Contains("visual_.Surface = surface_", source, StringComparison.Ordinal);
        Assert.Contains("ElementComposition.SetElementChildVisual(this, visual_)", source, StringComparison.Ordinal);
        Assert.Contains("visual_.Size = CurrentVisualSize()", source, StringComparison.Ordinal);
        Assert.Contains("CompositionDrawingSurface? Surface", source, StringComparison.Ordinal);
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
