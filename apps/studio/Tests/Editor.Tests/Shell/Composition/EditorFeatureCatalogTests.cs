using System.Linq;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorFeatureCatalogTests
{
    [Fact]
    public void CreateDefaultModules_registers_default_workbench_panels()
    {
        var composition = new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules()).Compose();

        Assert.Equal(
            ["scene-view", "hierarchy", "inspector", "console", "problems"],
            composition.PanelRegistry.GetAll().Select(descriptor => descriptor.Id));
    }

    [Fact]
    public void CreateDefaultModules_registers_default_workbench_actions()
    {
        var composition = new EditorExtensionHost(EditorFeatureCatalog.CreateDefaultModules()).Compose();

        Assert.Equal(
            [
                "workbench.commandPalette.open",
                "workbench.about.open",
                "workbench.panel.scene-view",
                "workbench.panel.hierarchy",
                "workbench.panel.inspector",
                "workbench.panel.console",
                "workbench.panel.problems",
            ],
            composition.ActionRegistry.GetAll().Select(action => action.Id));
    }
}
