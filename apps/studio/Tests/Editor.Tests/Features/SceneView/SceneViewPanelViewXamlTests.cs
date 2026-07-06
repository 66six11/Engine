using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPanelViewXamlTests
{
    [Fact]
    public void Scene_view_contains_composition_host_surface()
    {
        var xaml = LoadSource("Features", "SceneView", "Views", "SceneViewPanelView.axaml");

        Assert.Contains("xmlns:views=\"using:Editor.Features.SceneView.Views\"", xaml, StringComparison.Ordinal);
        Assert.Contains("<views:SceneViewCompositionHost", xaml, StringComparison.Ordinal);
        Assert.Contains("x:Name=\"CompositionHost\"", xaml, StringComparison.Ordinal);
        Assert.Contains("Focusable=\"True\"", xaml, StringComparison.Ordinal);
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
