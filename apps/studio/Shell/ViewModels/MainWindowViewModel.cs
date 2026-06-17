using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using Avalonia;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Docking;
using Editor.Shell.Composition;

namespace Editor.Shell.ViewModels;

public class MainWindowViewModel : ViewModelBase
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;
    private Func<string, bool>? activateFloatingPanel_;

    public MainWindowViewModel()
        : this(CreatePanelRegistry(), EditorDockLayoutStore.TryLoad())
    {
    }

    internal MainWindowViewModel(
        IPanelRegistry panelRegistry,
        EditorDockLayoutSnapshot? savedLayout)
    {
        panelRegistry_ = panelRegistry;
        OpenPanelCommand = new RelayCommand<string?>(OpenPanel);
        PanelMenuItems = CreatePanelMenuItems(panelRegistry_.GetAll());

        DockWorkspace = new EditorDockWorkspaceViewModel(panelRegistry_);
        DockWorkspace.RestoreLayoutSnapshot(savedLayout);
        if (savedLayout?.FloatingWindows is { Count: > 0 } floatingWindows)
        {
            pendingFloatingWindowSnapshots_.AddRange(floatingWindows);
        }

        SaveLayoutCommand = new RelayCommand(SaveLayout);
        ResetLayoutCommand = new RelayCommand(ResetLayout);
    }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IRelayCommand SaveLayoutCommand { get; }

    public IRelayCommand ResetLayoutCommand { get; }

    public IRelayCommand<string?> OpenPanelCommand { get; }

    public IReadOnlyList<PanelMenuItemViewModel> PanelMenuItems { get; }

    public void SetFloatingWindowCallbacks(
        Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>> captureFloatingWindowSnapshots,
        Action closeFloatingWindows,
        Func<string, bool> activateFloatingPanel)
    {
        captureFloatingWindowSnapshots_ = captureFloatingWindowSnapshots;
        closeFloatingWindows_ = closeFloatingWindows;
        activateFloatingPanel_ = activateFloatingPanel;
    }

    public IReadOnlyList<EditorDockFloatingWindowRequest> ConsumeRestoredFloatingWindowRequests()
    {
        if (pendingFloatingWindowSnapshots_.Count == 0)
        {
            return [];
        }

        var requests = new List<EditorDockFloatingWindowRequest>();
        foreach (var snapshot in pendingFloatingWindowSnapshots_)
        {
            if (!EditorDockWorkspaceViewModel.TryCreateFloatingWorkspace(
                    panelRegistry_,
                    snapshot,
                    out var floatingWorkspace))
            {
                continue;
            }

            var window = new EditorDockFloatingWindowViewModel(floatingWorkspace);
            var bounds = new Rect(
                snapshot.X,
                snapshot.Y,
                Math.Max(240, snapshot.Width),
                Math.Max(180, snapshot.Height));
            requests.Add(new EditorDockFloatingWindowRequest(window, bounds));
        }

        pendingFloatingWindowSnapshots_.Clear();
        return requests;
    }

    private void OpenPanel(string? panelId)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            return;
        }

        if (DockWorkspace.ActivatePanel(panelId))
        {
            return;
        }

        if (activateFloatingPanel_?.Invoke(panelId) == true)
        {
            return;
        }

        DockWorkspace.OpenPanel(panelId);
    }

    private void SaveLayout()
    {
        var snapshot = DockWorkspace.CaptureLayoutSnapshot();
        if (captureFloatingWindowSnapshots_ is not null)
        {
            snapshot.FloatingWindows.AddRange(captureFloatingWindowSnapshots_());
        }

        EditorDockLayoutStore.TrySave(snapshot);
    }

    private void ResetLayout()
    {
        pendingFloatingWindowSnapshots_.Clear();
        closeFloatingWindows_?.Invoke();
        DockWorkspace.ResetLayout();
        EditorDockLayoutStore.TryDelete();
    }

    internal static IPanelRegistry CreatePanelRegistry()
    {
        var registry = new PanelRegistry();

        foreach (var module in EditorFeatureCatalog.CreateDefaultModules())
        {
            module.RegisterPanels(registry);
        }

        return registry;
    }

    private IReadOnlyList<PanelMenuItemViewModel> CreatePanelMenuItems(
        IReadOnlyList<PanelDescriptor> descriptors)
    {
        var items = new List<PanelMenuItemViewModel>();
        foreach (var descriptor in descriptors)
        {
            if (!descriptor.MenuPath.StartsWith("Window/Panels/", StringComparison.Ordinal))
            {
                continue;
            }

            items.Add(new PanelMenuItemViewModel(descriptor, OpenPanel));
        }

        return items;
    }
}
