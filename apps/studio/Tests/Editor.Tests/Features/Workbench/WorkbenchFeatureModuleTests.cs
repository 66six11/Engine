using System.Linq;
using Editor.Core.Models;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Problems.ViewModels;
using Editor.Features.SceneView.ViewModels;
using Editor.Features.Workbench;
using Editor.Shell.Docking;
using Xunit;

namespace Editor.Tests.Features.Workbench;

public sealed class WorkbenchFeatureModuleTests
{
    [Fact]
    public void RegisterPanels_registers_stable_workbench_panel_descriptors()
    {
        var registry = new PanelRegistry();

        new WorkbenchFeatureModule().RegisterPanels(registry);

        var descriptors = registry.GetAll().ToArray();
        Assert.Collection(
            descriptors,
            descriptor => AssertDescriptor<SceneViewPanelViewModel>(
                descriptor,
                "scene-view",
                "Scene View",
                PanelKind.Document,
                DockArea.Center,
                "DOC",
                "custom viewport shell",
                "live"),
            descriptor => AssertDescriptor<HierarchyPanelViewModel>(
                descriptor,
                "hierarchy",
                "Hierarchy",
                PanelKind.Tool,
                DockArea.Left,
                "LEFT",
                "selection source",
                "tool"),
            descriptor => AssertDescriptor<InspectorPanelViewModel>(
                descriptor,
                "inspector",
                "Inspector",
                PanelKind.Tool,
                DockArea.Right,
                "RIGHT",
                "context target",
                "tool"),
            descriptor => AssertDescriptor<ConsolePanelViewModel>(
                descriptor,
                "console",
                "Console",
                PanelKind.Tool,
                DockArea.Bottom,
                "BOTTOM",
                "runtime log stream",
                "idle"),
            descriptor => AssertDescriptor<ProblemsPanelViewModel>(
                descriptor,
                "problems",
                "Problems",
                PanelKind.Tool,
                DockArea.Bottom,
                "BOTTOM",
                "validation queue",
                "0"));
    }

    private static void AssertDescriptor<TContent>(
        PanelDescriptor descriptor,
        string id,
        string title,
        PanelKind kind,
        DockArea area,
        string tag,
        string titleDetail,
        string statusText)
    {
        Assert.Equal(id, descriptor.Id);
        Assert.Equal(title, descriptor.Title);
        Assert.Equal(kind, descriptor.Kind);
        Assert.Equal(area, descriptor.DefaultArea);
        Assert.Equal($"Window/Panels/{title}", descriptor.MenuPath);
        Assert.Equal(DockContentCachePolicy.KeepAlive, descriptor.CachePolicy);
        Assert.Equal(tag, descriptor.Tag);
        Assert.Equal(titleDetail, descriptor.TitleDetail);
        Assert.Equal(statusText, descriptor.StatusText);
        Assert.IsType<TContent>(descriptor.CreateContent());
    }
}
