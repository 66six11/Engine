using System;
using System.IO;
using System.Linq;
using Editor.Core.Models;
using Editor.Shell.Services;
using Editor.Shell.ViewModels;
using Editor.Shell.Views;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class EditorLifecycleViewHookTests
{
    [Fact]
    public void MainWindow_publish_helper_routes_to_view_model_lifecycle_service()
    {
        var lifecycleEvents = new EditorLifecycleEventService();
        var viewModel = new MainWindowViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            MainWindowViewModel.CreateWorkbenchActionRegistry(),
            savedLayout: null,
            lifecycleEvents: lifecycleEvents);

        var snapshot = MainWindow.PublishLifecycleEvent(
            viewModel,
            EditorLifecycleEventKind.ApplicationOpened,
            "main-window",
            "Opened");

        Assert.NotNull(snapshot);
        Assert.Equal(EditorLifecycleEventKind.ApplicationOpened, snapshot.Kind);
        Assert.Equal("main-window", snapshot.Source);
        Assert.Equal(snapshot, Assert.Single(lifecycleEvents.GetRecentEvents()));
    }

    [Fact]
    public void FloatingWindow_publish_helper_routes_to_view_model_lifecycle_service()
    {
        var lifecycleEvents = new EditorLifecycleEventService();
        var workspace = new EditorDockWorkspaceViewModel(
            MainWindowViewModel.CreatePanelRegistry(),
            lifecycleEvents);
        var viewModel = new EditorDockFloatingWindowViewModel(workspace, lifecycleEvents);

        var snapshot = EditorDockFloatingWindow.PublishLifecycleEvent(
            viewModel,
            EditorLifecycleEventKind.FloatingWindowOpened,
            "floating-window",
            "Opened");

        Assert.NotNull(snapshot);
        Assert.Equal(EditorLifecycleEventKind.FloatingWindowOpened, snapshot.Kind);
        Assert.Equal("floating-window", snapshot.Source);
        Assert.Equal(snapshot, Assert.Single(lifecycleEvents.GetRecentEvents()));
    }

    [Fact]
    public void MainWindow_source_contains_expected_lifecycle_publications()
    {
        var source = LoadSource("Shell", "Views", "MainWindow.axaml.cs");

        Assert.Contains("Closing += OnWindowClosing;", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationOpened", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosing", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.ApplicationClosed", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.HostActivated", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.HostDeactivated", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.WorkspaceRestored", source);
    }

    [Fact]
    public void FloatingWindow_source_contains_expected_lifecycle_publications()
    {
        var source = LoadSource("Shell", "Views", "EditorDockFloatingWindow.axaml.cs");

        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowOpened", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowClosed", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowActivated", source);
        Assert.Contains("PublishLifecycleEvent(EditorLifecycleEventKind.FloatingWindowDeactivated", source);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        var fullPathParts = new string[pathParts.Length + 1];
        fullPathParts[0] = root;
        Array.Copy(pathParts, 0, fullPathParts, 1, pathParts.Length);
        return File.ReadAllText(Path.Combine(fullPathParts));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Editor.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        directory = new DirectoryInfo(AppContext.BaseDirectory);
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
