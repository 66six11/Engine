using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Shell.Views.Windowing;

public sealed class MainWindowXamlTests
{
    [Fact]
    public void Project_shell_uses_compiled_bindings_for_real_session_states()
    {
        var xaml = LoadMainWindowXaml();

        Assert.Contains("x:DataType=\"vm:StudioShellViewModel\"", xaml);
        Assert.Contains("IsVisible=\"{Binding IsStarting}\"", xaml);
        Assert.Contains("IsVisible=\"{Binding IsWorkspaceVisible}\"", xaml);
        Assert.Contains("IsVisible=\"{Binding HasNoProject}\"", xaml);
        Assert.Contains("IsVisible=\"{Binding HasProject}\"", xaml);
        Assert.Contains("Text=\"{Binding StartingStateText}\"", xaml);
        Assert.Contains("Text=\"{Binding ProjectStateText}\"", xaml);
        Assert.Contains("Command=\"{Binding CreateProjectCommand}\"", xaml);
        Assert.Contains("Command=\"{Binding OpenProjectCommand}\"", xaml);
        Assert.Contains("Command=\"{Binding CreateEntityCommand}\"", xaml);
        Assert.Contains("Command=\"{Binding SaveSceneCommand}\"", xaml);
        Assert.Contains("Text=\"{Binding DocumentStateText}\"", xaml);
        Assert.Contains("ItemsSource=\"{Binding SceneEntities}\"", xaml);
        Assert.Contains("StudioHierarchyPanel", xaml, StringComparison.Ordinal);
        Assert.Contains("StudioInspectorPanel", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("Dock", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("ProjectLaunch", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("MainWindowViewModel", xaml, StringComparison.Ordinal);
        Assert.Contains("Background=\"#0B0F14\"", xaml, StringComparison.Ordinal);
        Assert.Contains("Foreground=\"#E6EDF3\"", xaml, StringComparison.Ordinal);
        Assert.Contains("Background=\"#10161D\"", xaml, StringComparison.Ordinal);
        Assert.Contains("BorderBrush=\"#1F2933\"", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("DynamicResource", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("StaticResource", xaml, StringComparison.Ordinal);
    }

    [Fact]
    public void Hard_cut_shell_declares_stable_accessibility_metadata()
    {
        var xaml = LoadMainWindowXaml();

        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellWindow\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellStartingState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellNoProjectState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellNoDocumentState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellActiveProjectState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioHierarchyPanel\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioInspectorPanel\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"New project name\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Create project\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Open project\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Studio startup state\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Project state: No Project\"", xaml);
        Assert.Contains("AutomationProperties.Name=\"Document state: No Document\"", xaml);
        Assert.Contains("AutomationProperties.ControlTypeOverride=\"StatusBar\"", xaml);
        Assert.Contains("AutomationProperties.ControlTypeOverride=\"Group\"", xaml);
        Assert.Contains("AutomationProperties.LiveSetting=\"Polite\"", xaml);
    }

    private static string LoadMainWindowXaml()
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(
            root,
            "Shell",
            "Views",
            "Windowing",
            "MainWindow.axaml"));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Asharia.Studio.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Asharia.Studio.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Asharia.Studio.sln.");
    }
}
