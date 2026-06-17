using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using Avalonia;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Editor.Shell.Docking;
using Editor.Shell.Composition;

namespace Editor.Shell.ViewModels;

public class MainWindowViewModel : ViewModelBase
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly PanelCommandService panelCommandService_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;

    public MainWindowViewModel()
        : this(CreatePanelRegistry(), EditorDockLayoutStore.TryLoad())
    {
    }

    internal MainWindowViewModel(
        IPanelRegistry panelRegistry,
        EditorDockLayoutSnapshot? savedLayout)
    {
        panelRegistry_ = panelRegistry;

        DockWorkspace = new EditorDockWorkspaceViewModel(panelRegistry_);
        panelCommandService_ = new PanelCommandService(DockWorkspace);
        panelCommandService_.PanelStateChanged += OnPanelCommandStateChanged;
        OpenPanelCommand = new RelayCommand<string?>(
            panelId => panelCommandService_.OpenOrFocusPanel(panelId));
        PanelMenuItems = CreatePanelMenuItems(panelRegistry_.GetAll());
        DockWorkspace.RestoreLayoutSnapshot(savedLayout);
        if (savedLayout?.FloatingWindows is { Count: > 0 } floatingWindows)
        {
            pendingFloatingWindowSnapshots_.AddRange(floatingWindows);
        }
        RefreshPanelMenuOpenStates();

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
        Func<string, bool> activateFloatingPanel,
        Func<string, bool> isFloatingPanelOpen,
        Func<string, bool>? closeFloatingPanel = null)
    {
        captureFloatingWindowSnapshots_ = captureFloatingWindowSnapshots;
        closeFloatingWindows_ = closeFloatingWindows;
        panelCommandService_.SetExternalPanelCallbacks(
            activateFloatingPanel,
            isFloatingPanelOpen,
            closeFloatingPanel);
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

    public void RefreshPanelMenuOpenStates()
    {
        foreach (var item in PanelMenuItems)
        {
            item.SetOpenState(panelCommandService_.IsPanelOpen(item.PanelId));
        }
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

    private void OnPanelCommandStateChanged(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
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

            items.Add(new PanelMenuItemViewModel(
                descriptor,
                panelId => panelCommandService_.OpenOrFocusPanel(panelId)));
        }

        return items;
    }
}
