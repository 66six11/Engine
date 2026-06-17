using System.Linq;
using Editor.Shell.Composition;
using Editor.Shell.Commands;
using Editor.Shell.Docking;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorFeatureCatalogTests
{
    [Fact]
    public void CreateDefaultModules_registers_default_workbench_panels()
    {
        var registry = new PanelRegistry();

        foreach (var module in EditorFeatureCatalog.CreateDefaultModules())
        {
            module.RegisterPanels(registry);
        }

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            registry.GetAll().Select(descriptor => descriptor.Id));
    }

    [Fact]
    public void CreateDefaultModules_registers_default_workbench_actions()
    {
        var registry = new WorkbenchActionRegistry();

        foreach (var module in EditorFeatureCatalog.CreateDefaultModules())
        {
            module.RegisterActions(registry);
        }

        Assert.Equal(
            [
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
                "workbench.panel.inspector",
                "workbench.panel.console",
                "workbench.panel.problems",
            ],
            registry.GetAll().Select(action => action.Id));
    }
}
