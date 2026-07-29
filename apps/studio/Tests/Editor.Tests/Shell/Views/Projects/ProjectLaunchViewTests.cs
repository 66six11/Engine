using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Editor.Shell.ViewModels.Projects;
using Editor.Shell.Views.Projects;
using Xunit;

namespace Editor.Tests.Shell.Views.Projects;

public sealed class ProjectLaunchViewTests
{
    [Fact]
    public void Project_launch_surface_uses_compiled_bindings_without_a_disabled_action()
    {
        var xaml = LoadSource(
            "Shell",
            "Views",
            "Projects",
            "ProjectLaunchView.axaml");

        Assert.Contains("x:DataType=\"vm:ProjectLaunchViewModel\"", xaml);
        Assert.Contains("Text=\"{Binding ProjectCandidateDisplayName}\"", xaml);
        Assert.Contains("Text=\"{Binding StateTitle}\"", xaml);
        Assert.Contains("Text=\"{Binding NextStepText}\"", xaml);
        Assert.Contains("Text=\"{Binding PrimaryDiagnosticManifestPath}\"", xaml);
        Assert.Contains("Text=\"{Binding PrimaryDiagnosticPointer}\"", xaml);
        Assert.Contains("MaxHeight=\"180\"", xaml);
        Assert.DoesNotContain("<Button", xaml);
    }

    [Fact]
    public void Project_launch_surface_bounds_long_diagnostics()
    {
        var source = new ProjectOpenSessionSnapshotSource(
            new ProjectOpenSessionSnapshot(
                ProjectOpenSessionState.PendingBuild,
                ProjectOpenNextAction.BuildProjectHost,
                project: null,
                [
                    new ProjectOpenSessionDiagnosticSnapshot(
                        "project.host.missing",
                        "project/asharia.project.json",
                        "/projectCode",
                        new string('x', 1000)),
                ]));
        using var viewModel = new ProjectLaunchViewModel(source);
        var view = new ProjectLaunchView
        {
            DataContext = viewModel,
        };

        view.Measure(new Size(720, 120));
        view.Arrange(new Rect(0, 0, 720, 120));

        var scrollViewer = view.FindControl<ScrollViewer>("ProjectLaunchScrollViewer");
        Assert.NotNull(scrollViewer);
        Assert.Equal(new Size(720, 120), view.Bounds.Size);
        Assert.True(scrollViewer.Bounds.Width <= 720);
        Assert.True(scrollViewer.Bounds.Height <= 120);
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
