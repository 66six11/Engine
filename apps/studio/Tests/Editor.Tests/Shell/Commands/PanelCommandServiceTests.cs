using Editor.Core.Models;
using Editor.Core.Models.Panels;
using Editor.Shell.Commands;
using Editor.Shell.Docking.Panels;
using Editor.Shell.ViewModels.Docking;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class PanelCommandServiceTests
{
    [Fact]
    public void OpenOrFocusPanel_focuses_main_workspace_panel_before_external_panel()
    {
        var workspace = CreateWorkspace();
        var service = new PanelCommandService(workspace);
        var externalFocusCount = 0;
        service.SetExternalPanelCallbacks(
            _ =>
            {
                externalFocusCount++;
                return true;
            },
            _ => true);

        Assert.True(service.OpenOrFocusPanel("panel"));

        Assert.Equal(0, externalFocusCount);
        Assert.True(workspace.CenterWindow.Tabs[0].IsActive);
    }

    [Fact]
    public void OpenOrFocusPanel_focuses_external_panel_before_reopening_main_panel()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs[0];
        var service = new PanelCommandService(workspace);
        var externalFocusCount = 0;
        Assert.True(workspace.CloseTab(tab));
        service.SetExternalPanelCallbacks(
            panelId =>
            {
                externalFocusCount++;
                return panelId == "panel";
            },
            panelId => panelId == "panel");

        Assert.True(service.OpenOrFocusPanel("panel"));

        Assert.Equal(1, externalFocusCount);
        Assert.False(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void ClosePanel_closes_main_workspace_panel()
    {
        var workspace = CreateWorkspace();
        var service = new PanelCommandService(workspace);

        Assert.True(service.ClosePanel("panel"));

        Assert.False(workspace.ContainsPanel("panel"));
    }

    [Fact]
    public void ClosePanel_uses_external_callback_when_main_workspace_does_not_contain_panel()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs[0];
        var service = new PanelCommandService(workspace);
        var externalCloseCount = 0;
        Assert.True(workspace.CloseTab(tab));
        service.SetExternalPanelCallbacks(
            _ => false,
            _ => true,
            panelId =>
            {
                externalCloseCount++;
                return panelId == "panel";
            });

        Assert.True(service.ClosePanel("panel"));

        Assert.Equal(1, externalCloseCount);
    }

    [Fact]
    public void IsPanelOpen_includes_external_panel_state()
    {
        var workspace = CreateWorkspace();
        var tab = workspace.CenterWindow.Tabs[0];
        var service = new PanelCommandService(workspace);
        Assert.True(workspace.CloseTab(tab));
        service.SetExternalPanelCallbacks(
            _ => false,
            panelId => panelId == "panel");

        Assert.True(service.IsPanelOpen("panel"));
    }

    private static EditorDockWorkspaceViewModel CreateWorkspace()
    {
        var registry = new PanelRegistry();
        registry.Register(new PanelDescriptor(
            "panel",
            "Panel",
            PanelKind.Tool,
            DockArea.Center,
            "Window/Panels/Panel",
            DockContentCachePolicy.KeepAlive,
            () => new object()));
        return new EditorDockWorkspaceViewModel(registry);
    }
}
