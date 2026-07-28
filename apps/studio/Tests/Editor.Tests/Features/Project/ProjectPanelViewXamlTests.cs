using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Features.Project;

public sealed class ProjectPanelViewXamlTests
{
    [Fact]
    public void Project_panel_uses_compiled_bindings_and_explains_disabled_actions()
    {
        var xaml = LoadSource(
            "Features",
            "Project",
            "Views",
            "ProjectPanelView.axaml");

        Assert.Contains("x:DataType=\"vm:ProjectPanelViewModel\"", xaml);
        Assert.Contains("IsEnabled=\"{Binding CanSearch}\"", xaml);
        Assert.Contains("IsEnabled=\"{Binding CanOpenProject}\"", xaml);
        Assert.Contains("ToolTip.Tip=\"{Binding UnavailableReason}\"", xaml);
        Assert.Contains("Text=\"{Binding EmptyStateTitle}\"", xaml);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            var editorSolution = Path.Combine(directory.FullName, "Editor.sln");
            if (File.Exists(editorSolution))
            {
                return File.ReadAllText(Path.Combine(
                    [directory.FullName, .. pathParts]));
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
