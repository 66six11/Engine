using System.Linq;
using Editor.Shell.Composition;
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
}
