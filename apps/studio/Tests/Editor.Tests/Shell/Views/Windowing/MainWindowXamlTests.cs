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
        Assert.Contains("Command=\"{Binding CreateMeshEntityCommand}\"", xaml);
        Assert.Contains("Content=\"+ Mesh\"", xaml);
        Assert.Contains("Command=\"{Binding SaveSceneCommand}\"", xaml);
        Assert.Contains("x:Name=\"UndoSceneButton\"", xaml);
        Assert.Contains("Command=\"{Binding UndoSceneCommand}\"", xaml);
        Assert.Contains("Content=\"{Binding UndoSceneLabel}\"", xaml);
        Assert.Contains("x:Name=\"RedoSceneButton\"", xaml);
        Assert.Contains("Command=\"{Binding RedoSceneCommand}\"", xaml);
        Assert.Contains("Content=\"{Binding RedoSceneLabel}\"", xaml);
        Assert.Contains("Text=\"{Binding DocumentStateText}\"", xaml);
        Assert.Contains("DataContext=\"{Binding DockWorkspace}\"", xaml);
        Assert.Contains("EditorDockWorkspaceView", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("ProjectLaunch", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("MainWindowViewModel", xaml, StringComparison.Ordinal);
        Assert.Contains("DynamicResource EditorBrushBase00", xaml, StringComparison.Ordinal);
        Assert.Contains("x:Name=\"StudioMainMenu\"", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain(
            "AutomationProperties.AutomationId=\"StudioMainMenu\"",
            xaml,
            StringComparison.Ordinal);
        Assert.Contains("Items are projected from StudioShellViewModel.ActionCatalog", xaml);
        Assert.DoesNotContain("Header=\"_File\"", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"_Edit\"", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"_Scene\"", xaml, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"_Window\"", xaml, StringComparison.Ordinal);

        var hierarchyXaml = LoadPanelXaml("StudioHierarchyPanelView.axaml");
        Assert.Contains("x:DataType=\"vm:StudioHierarchyPanelViewModel\"", hierarchyXaml);
        Assert.Contains("ItemsSource=\"{Binding VisibleRows}\"", hierarchyXaml);
        Assert.Contains("SelectedItem=\"{Binding SelectedRow", hierarchyXaml);
        Assert.Contains("{Binding FilterText", hierarchyXaml);
        Assert.Contains("VirtualizingStackPanel", hierarchyXaml);
        Assert.DoesNotContain("Shell.SceneEntities", hierarchyXaml);
        Assert.DoesNotContain("Shell.SelectedEntity", hierarchyXaml);
        Assert.Contains(
            "ContextRequested=\"OnHierarchyContextRequested\"",
            hierarchyXaml);

        var inspectorXaml = LoadPanelXaml("StudioInspectorPanelView.axaml");
        Assert.Contains("x:DataType=\"vm:StudioInspectorPanelViewModel\"", inspectorXaml);
        Assert.Contains("Text=\"{Binding Shell.InspectorName}\"", inspectorXaml);
        Assert.Contains(
            "commands:StudioActionButton.ActionId=\"{x:Static actions:StudioShellActionIds.ApplyEntityName}\"",
            inspectorXaml);
        Assert.Contains(
            "commands:StudioActionButton.ActionId=\"{x:Static actions:StudioShellActionIds.ApplyEntityTransform}\"",
            inspectorXaml);
        Assert.Contains("Shell.RotationDegreesX", inspectorXaml, StringComparison.Ordinal);
        Assert.Contains("Shell.RotationDegreesY", inspectorXaml, StringComparison.Ordinal);
        Assert.Contains("Shell.RotationDegreesZ", inspectorXaml, StringComparison.Ordinal);
        Assert.Contains("Rotation degrees (X / Y / Z · YXZ)", inspectorXaml, StringComparison.Ordinal);
        Assert.Contains("InspectorOperationMessage", inspectorXaml, StringComparison.Ordinal);
        Assert.DoesNotContain("Shell.RotationW", inspectorXaml, StringComparison.Ordinal);

        var scenePanelXaml = LoadPanelXaml("StudioScenePanelView.axaml");
        Assert.Contains(
            "commands:StudioActionButton.ActionId=\"{x:Static actions:StudioShellActionIds.CreateMeshEntity}\"",
            scenePanelXaml);
        Assert.Contains("IsChecked=\"{Binding IsWireframe, Mode=TwoWay}\"", scenePanelXaml);
    }

    [Fact]
    public void Docked_shell_declares_stable_accessibility_metadata()
    {
        var xaml = LoadMainWindowXaml();

        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellWindow\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellStartingState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellNoProjectState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellNoDocumentState\"", xaml);
        Assert.Contains("AutomationProperties.AutomationId=\"StudioShellActiveProjectState\"", xaml);
        Assert.Contains(
            "AutomationProperties.AutomationId=\"StudioHierarchyPanel\"",
            LoadPanelXaml("StudioHierarchyPanelView.axaml"));
        Assert.Contains(
            "AutomationProperties.AutomationId=\"StudioInspectorPanel\"",
            LoadPanelXaml("StudioInspectorPanelView.axaml"));
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

    private static string LoadPanelXaml(string fileName)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(
            root,
            "Shell",
            "Views",
            "Panels",
            fileName));
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
