using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Editor.Shell.Docking;
using Editor.Shell.Composition;
using Editor.Shell.Selection;

namespace Editor.Shell.ViewModels;

public class MainWindowViewModel : ViewModelBase
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly PanelCommandService panelCommandService_;
    private readonly WorkbenchShortcutRouter shortcutRouter_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;

    public MainWindowViewModel()
        : this(new EditorSelectionService(), EditorDockLayoutStore.TryLoad())
    {
    }

    private MainWindowViewModel(
        IEditorSelectionService selectionService,
        EditorDockLayoutSnapshot? savedLayout)
        : this(
            CreatePanelRegistry(selectionService),
            CreateWorkbenchActionRegistry(selectionService),
            savedLayout,
            selectionService)
    {
    }

    internal MainWindowViewModel(
        IPanelRegistry panelRegistry,
        IWorkbenchActionRegistry actionRegistry,
        EditorDockLayoutSnapshot? savedLayout,
        IEditorSelectionService? selectionService = null)
    {
        SelectionService = selectionService ?? new EditorSelectionService();
        panelRegistry_ = panelRegistry;

        DockWorkspace = new EditorDockWorkspaceViewModel(panelRegistry_);
        panelCommandService_ = new PanelCommandService(DockWorkspace);
        var actionExecutor = new WorkbenchActionExecutor(panelCommandService_, OpenCommandPaletteFromCommand);
        var commandRouter = new WorkbenchCommandRouter(actionRegistry, actionExecutor);
        panelCommandService_.PanelStateChanged += OnPanelCommandStateChanged;
        OpenPanelCommand = new RelayCommand<string?>(
            panelId => panelCommandService_.OpenOrFocusPanel(panelId));
        var actions = actionRegistry.GetAll();
        CommandPalette = new CommandPaletteViewModel(actions, commandRouter.Execute);
        shortcutRouter_ = WorkbenchShortcutRouter.FromActions(actions, commandRouter);
        ToolsMenuItems = CreateCommandMenuItems(actions, "Tools/", commandRouter);
        PanelMenuItems = CreatePanelMenuItems(actions, commandRouter);
        DockWorkspace.RestoreLayoutSnapshot(savedLayout);
        if (savedLayout?.FloatingWindows is { Count: > 0 } floatingWindows)
        {
            pendingFloatingWindowSnapshots_.AddRange(floatingWindows);
        }
        RefreshPanelMenuOpenStates();

        SaveLayoutCommand = new RelayCommand(SaveLayout);
        ResetLayoutCommand = new RelayCommand(ResetLayout);
    }

    public IEditorSelectionService SelectionService { get; }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IRelayCommand SaveLayoutCommand { get; }

    public IRelayCommand ResetLayoutCommand { get; }

    public IRelayCommand<string?> OpenPanelCommand { get; }

    public CommandPaletteViewModel CommandPalette { get; }

    public IReadOnlyList<WorkbenchMenuItemViewModel> ToolsMenuItems { get; }

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

    internal WorkbenchCommandExecutionResult? ExecuteShortcut(
        Key key,
        KeyModifiers keyModifiers,
        bool isTextInputFocused)
    {
        return shortcutRouter_.TryExecute(key, keyModifiers, isTextInputFocused);
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

    private bool OpenCommandPaletteFromCommand()
    {
        if (!CommandPalette.OpenCommand.CanExecute(null))
        {
            return false;
        }

        CommandPalette.OpenCommand.Execute(null);
        return true;
    }

    private void OnPanelCommandStateChanged(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
    }

    internal static IPanelRegistry CreatePanelRegistry(IEditorSelectionService? selectionService = null)
    {
        selectionService ??= new EditorSelectionService();
        var registry = new PanelRegistry();

        foreach (var module in EditorFeatureCatalog.CreateDefaultModules(selectionService))
        {
            module.RegisterPanels(registry);
        }

        return registry;
    }

    internal static IWorkbenchActionRegistry CreateWorkbenchActionRegistry(IEditorSelectionService? selectionService = null)
    {
        selectionService ??= new EditorSelectionService();
        var registry = new WorkbenchActionRegistry();

        foreach (var module in EditorFeatureCatalog.CreateDefaultModules(selectionService))
        {
            module.RegisterActions(registry);
        }

        return registry;
    }

    private IReadOnlyList<PanelMenuItemViewModel> CreatePanelMenuItems(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        IWorkbenchCommandRouter commandRouter)
    {
        var items = new List<PanelMenuItemViewModel>();
        foreach (var action in actions)
        {
            if (action.Kind != WorkbenchActionKind.OpenPanel
                || !action.MenuPath.StartsWith("Window/Panels/", StringComparison.Ordinal))
            {
                continue;
            }

            items.Add(new PanelMenuItemViewModel(
                action,
                commandRouter.Execute));
        }

        return items;
    }

    private static IReadOnlyList<WorkbenchMenuItemViewModel> CreateCommandMenuItems(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        string menuPathPrefix,
        IWorkbenchCommandRouter commandRouter)
    {
        var items = new List<WorkbenchMenuItemViewModel>();
        foreach (var action in actions)
        {
            if (!action.MenuPath.StartsWith(menuPathPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            items.Add(new WorkbenchMenuItemViewModel(
                action,
                commandRouter.Execute));
        }

        return items;
    }
}
