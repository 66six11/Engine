using Dock.Model.Controls;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Docking;

namespace Editor.Shell.ViewModels;

public partial class MainWindowViewModel : ViewModelBase
{
    public MainWindowViewModel()
    {
        var panelRegistry = CreatePanelRegistry();

        DockFactory = new EditorDockFactory(panelRegistry);
        DockLayout = DockFactory.CreateLayout();
        DockFactory.InitLayout(DockLayout);
    }

    public EditorDockFactory DockFactory { get; }

    public IRootDock DockLayout { get; }

    private static IPanelRegistry CreatePanelRegistry()
    {
        var registry = new PanelRegistry();

        RegisterPlaceholder(registry, "scene-view", "Scene View", PanelKind.Document, DockArea.Center);
        RegisterPlaceholder(registry, "hierarchy", "Hierarchy", PanelKind.Tool, DockArea.Left);
        RegisterPlaceholder(registry, "inspector", "Inspector", PanelKind.Tool, DockArea.Right);
        RegisterPlaceholder(registry, "console", "Console", PanelKind.Tool, DockArea.Bottom);
        RegisterPlaceholder(registry, "problems", "Problems", PanelKind.Tool, DockArea.Bottom);

        return registry;
    }

    private static void RegisterPlaceholder(
        IPanelRegistry registry,
        string id,
        string title,
        PanelKind kind,
        DockArea defaultArea)
    {
        registry.Register(new PanelDescriptor(
            id,
            title,
            kind,
            defaultArea,
            $"Window/Panels/{title}",
            DockContentCachePolicy.KeepAlive,
            () => new PanelPlaceholderViewModel(title)));
    }
}
