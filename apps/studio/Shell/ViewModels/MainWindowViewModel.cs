using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using Avalonia;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Docking;

namespace Editor.Shell.ViewModels;

public class MainWindowViewModel : ViewModelBase
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;
    private Func<string, bool>? activateFloatingPanel_;

    public MainWindowViewModel()
    {
        panelRegistry_ = CreatePanelRegistry();

        var savedLayout = EditorDockLayoutStore.TryLoad();
        DockWorkspace = new EditorDockWorkspaceViewModel(panelRegistry_);
        DockWorkspace.RestoreLayoutSnapshot(savedLayout);
        if (savedLayout?.FloatingWindows is { Count: > 0 } floatingWindows)
        {
            pendingFloatingWindowSnapshots_.AddRange(floatingWindows);
        }

        SaveLayoutCommand = new RelayCommand(SaveLayout);
        ResetLayoutCommand = new RelayCommand(ResetLayout);
        OpenPanelCommand = new RelayCommand<string?>(OpenPanel);
    }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IRelayCommand SaveLayoutCommand { get; }

    public IRelayCommand ResetLayoutCommand { get; }

    public IRelayCommand<string?> OpenPanelCommand { get; }

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
