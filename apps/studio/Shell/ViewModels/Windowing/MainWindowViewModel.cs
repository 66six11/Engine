using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using Asharia.Editor.Commands;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Lifecycle;
using Asharia.Editor.Selection;
using Asharia.Editor.Tasks;
using Avalonia;
using Avalonia.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models.Workbench;
using Editor.Core.Services;
using Editor.Shell.Commands;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Layout;
using Asharia.Studio.Application.Lifecycle;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Tasks;
using Editor.Shell.Services;
using Editor.Shell.ViewModels.CommandPalette;
using Editor.Shell.ViewModels.Dialogs;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.ViewModels.Menus;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Windowing;

public class MainWindowViewModel : ViewModelBase, IDisposable
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly PanelCommandService panelCommandService_;
    private readonly WorkbenchShortcutRouter shortcutRouter_;
    private readonly IEditorBackgroundTaskService backgroundTasks_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private readonly IEditorDiagnosticService diagnostics_;
    private readonly RelayCommand openStatusMessageTargetCommand_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;
    private bool hasActiveBackgroundTasks_;
    private string activeBackgroundTaskTitle_ = string.Empty;
    private string activeBackgroundTaskMessage_ = string.Empty;
    private EditorStatusMessageSnapshot? lastStatusMessage_;
    private EditorDiagnosticRecord? latestStatusDiagnostic_;
    private EditorStatusMessageSeverity? statusMessageSeverity_;
    private bool isDisposed_;

    public MainWindowViewModel()
        : this(EditorDockLayoutStore.TryLoad())
    {
    }

    private MainWindowViewModel(EditorDockLayoutSnapshot? savedLayout)
        : this(CreateDefaultViewModelArguments(savedLayout))
    {
    }

    private MainWindowViewModel(MainWindowViewModelArguments arguments)
        : this(
            arguments.Composition.PanelRegistry,
            arguments.Composition.ActionRegistry,
            arguments.SavedLayout,
            arguments.SelectionService,
            diagnostics: arguments.Diagnostics)
    {
    }

    internal MainWindowViewModel(
        IPanelRegistry panelRegistry,
        IWorkbenchActionRegistry actionRegistry,
        EditorDockLayoutSnapshot? savedLayout,
        IEditorSelectionService? selectionService = null,
        IEditorBackgroundTaskService? backgroundTasks = null,
        IEditorUiDispatcher? uiDispatcher = null,
        IEditorLifecycleEventService? lifecycleEvents = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        SelectionService = selectionService ?? new EditorSelectionService();
        panelRegistry_ = panelRegistry;
        backgroundTasks_ = backgroundTasks ?? new EditorBackgroundTaskService();
        uiDispatcher_ = uiDispatcher ?? new AvaloniaEditorUiDispatcher();
        diagnostics_ = diagnostics ?? new EditorDiagnosticService();
        diagnostics_.DiagnosticsChanged += OnDiagnosticsChanged;
        RefreshLatestDiagnostic();
        backgroundTasks_.TasksChanged += OnBackgroundTasksChanged;
        RefreshBackgroundTaskSummary();

        LifecycleEvents = lifecycleEvents ?? new EditorLifecycleEventService();
        DockWorkspace = new EditorDockWorkspaceViewModel(panelRegistry_, LifecycleEvents);
        panelCommandService_ = new PanelCommandService(DockWorkspace);
        DialogHost = new EditorDialogHostViewModel();
        var actions = actionRegistry.GetAll();
        var commandHandlers = WorkbenchCommandHandlerRegistry.CreateBuiltIn(
            actions,
            panelCommandService_,
            OpenCommandPaletteFromCommand,
            OpenAboutDialogFromCommand);
        var actionExecutor = new WorkbenchActionExecutor(commandHandlers);
        var commandRouter = new WorkbenchCommandStatusMessageRouter(
            new WorkbenchCommandRouter(actionRegistry, actionExecutor),
            PublishCommandStatusMessage);
        panelCommandService_.PanelStateChanged += OnPanelCommandStateChanged;
        OpenPanelCommand = new RelayCommand<string?>(
            panelId => panelCommandService_.OpenOrFocusPanel(panelId));
        openStatusMessageTargetCommand_ = new RelayCommand(
            OpenStatusMessageTarget,
            () => CanOpenStatusMessageTarget);
        CommandPalette = new CommandPaletteViewModel(actions, commandRouter.Execute);
        shortcutRouter_ = WorkbenchShortcutRouter.FromActions(actions, commandRouter);
        ToolsMenuItems = CreateCommandMenuItems(actions, "Tools/", commandRouter);
        HelpMenuItems = CreateCommandMenuItems(actions, "Help/", commandRouter);
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

    public IEditorLifecycleEventService LifecycleEvents { get; }

    public EditorDockWorkspaceViewModel DockWorkspace { get; }

    public IRelayCommand SaveLayoutCommand { get; }

    public IRelayCommand ResetLayoutCommand { get; }

    public IRelayCommand<string?> OpenPanelCommand { get; }

    public IRelayCommand OpenStatusMessageTargetCommand => openStatusMessageTargetCommand_;

    public CommandPaletteViewModel CommandPalette { get; }

    public EditorDialogHostViewModel DialogHost { get; }

    public IReadOnlyList<WorkbenchMenuItemViewModel> ToolsMenuItems { get; }

    public IReadOnlyList<WorkbenchMenuItemViewModel> HelpMenuItems { get; }

    public IReadOnlyList<PanelMenuItemViewModel> PanelMenuItems { get; }

    public bool HasActiveBackgroundTasks
    {
        get => hasActiveBackgroundTasks_;
        private set => SetProperty(ref hasActiveBackgroundTasks_, value);
    }

    public string ActiveBackgroundTaskTitle
    {
        get => activeBackgroundTaskTitle_;
        private set => SetProperty(ref activeBackgroundTaskTitle_, value);
    }

    public string ActiveBackgroundTaskMessage
    {
        get => activeBackgroundTaskMessage_;
        private set => SetProperty(ref activeBackgroundTaskMessage_, value);
    }

    public EditorStatusMessageSnapshot? LastStatusMessage
    {
        get => lastStatusMessage_;
        private set
        {
            if (SetProperty(ref lastStatusMessage_, value))
            {
                OnStatusMessageProjectionChanged();
            }
        }
    }

    public bool HasStatusMessage => latestStatusDiagnostic_ is not null || LastStatusMessage is not null;

    public string StatusMessageText =>
        latestStatusDiagnostic_?.Message ?? LastStatusMessage?.Message ?? string.Empty;

    public bool IsStatusMessageDebug =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Debug;

    public bool IsStatusMessageInfo =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Info;

    public bool IsStatusMessageSuccess =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Success;

    public bool IsStatusMessageWarning =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Warning;

    public bool IsStatusMessageError =>
        CurrentStatusMessageSeverity == EditorStatusMessageSeverity.Error;

    public bool CanOpenStatusMessageTarget =>
        panelCommandService_.CanOpenOrFocusPanel(LastStatusMessage?.TargetPanelId);

    private EditorStatusMessageSeverity? CurrentStatusMessageSeverity =>
        statusMessageSeverity_ ?? LastStatusMessage?.Severity;

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
                    LifecycleEvents,
                    out var floatingWorkspace))
            {
                continue;
            }

            var window = new EditorDockFloatingWindowViewModel(floatingWorkspace, LifecycleEvents);
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

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        closeFloatingWindows_?.Invoke();
        backgroundTasks_.TasksChanged -= OnBackgroundTasksChanged;
        diagnostics_.DiagnosticsChanged -= OnDiagnosticsChanged;
        panelCommandService_.PanelStateChanged -= OnPanelCommandStateChanged;
        DockWorkspace.Dispose();
    }

    internal EditorCommandExecutionResult? ExecuteShortcut(
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

    private bool OpenAboutDialogFromCommand()
    {
        _ = DialogHost.ShowAsync(StudioDialogRequests.About());
        return true;
    }

    private void OnPanelCommandStateChanged(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
        OnPropertyChanged(nameof(CanOpenStatusMessageTarget));
        openStatusMessageTargetCommand_.NotifyCanExecuteChanged();
    }

    private void OnBackgroundTasksChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshBackgroundTaskSummary();
            return;
        }

        uiDispatcher_.Post(RefreshBackgroundTaskSummary);
    }

    private void OnDiagnosticsChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshLatestDiagnostic();
            return;
        }

        uiDispatcher_.Post(RefreshLatestDiagnostic);
    }

    private void RefreshBackgroundTaskSummary()
    {
        var activeBackgroundTasks = backgroundTasks_.GetActiveSnapshots();
        if (activeBackgroundTasks.Count == 0)
        {
            HasActiveBackgroundTasks = false;
            ActiveBackgroundTaskTitle = string.Empty;
            ActiveBackgroundTaskMessage = string.Empty;
            return;
        }

        var activeBackgroundTask = activeBackgroundTasks[0];
        HasActiveBackgroundTasks = true;
        ActiveBackgroundTaskTitle = activeBackgroundTask.Title;
        ActiveBackgroundTaskMessage = activeBackgroundTask.Message ?? string.Empty;
    }

    internal void PublishStatusMessage(EditorStatusMessageSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        LastStatusMessage = snapshot;
    }

    private void PublishCommandStatusMessage(EditorCommandExecutionResult result)
    {
        var statusMessage = EditorStatusMessageSnapshot.FromCommandResult(result);
        LastStatusMessage = statusMessage;
        diagnostics_.Publish(
            MapStatusMessageSeverity(statusMessage.Severity),
            EditorDiagnosticChannel.Debug,
            result.CommandId,
            "workbench",
            statusMessage.Message);
    }

    private void OpenStatusMessageTarget()
    {
        var targetPanelId = LastStatusMessage?.TargetPanelId;
        if (!panelCommandService_.CanOpenOrFocusPanel(targetPanelId))
        {
            return;
        }

        panelCommandService_.OpenOrFocusPanel(targetPanelId);
    }

    private void RefreshLatestDiagnostic()
    {
        var latestDiagnostic = diagnostics_.GetLatestDiagnostic();
        if (latestStatusDiagnostic_ == latestDiagnostic)
        {
            return;
        }

        latestStatusDiagnostic_ = latestDiagnostic;
        statusMessageSeverity_ = ResolveStatusMessageSeverity(latestDiagnostic);
        OnStatusMessageProjectionChanged();
    }

    private void OnStatusMessageProjectionChanged()
    {
        OnPropertyChanged(nameof(HasStatusMessage));
        OnPropertyChanged(nameof(StatusMessageText));
        OnPropertyChanged(nameof(IsStatusMessageDebug));
        OnPropertyChanged(nameof(IsStatusMessageInfo));
        OnPropertyChanged(nameof(IsStatusMessageSuccess));
        OnPropertyChanged(nameof(IsStatusMessageWarning));
        OnPropertyChanged(nameof(IsStatusMessageError));
        OnPropertyChanged(nameof(CanOpenStatusMessageTarget));
        openStatusMessageTargetCommand_.NotifyCanExecuteChanged();
    }

    private EditorStatusMessageSeverity? ResolveStatusMessageSeverity(
        EditorDiagnosticRecord? diagnostic)
    {
        if (diagnostic is null)
        {
            return LastStatusMessage?.Severity;
        }

        if (LastStatusMessage is { } statusMessage
            && string.Equals(statusMessage.Message, diagnostic.Message, StringComparison.Ordinal))
        {
            return statusMessage.Severity;
        }

        return diagnostic.Severity switch
        {
            EditorDiagnosticSeverity.Debug => EditorStatusMessageSeverity.Debug,
            EditorDiagnosticSeverity.Warning => EditorStatusMessageSeverity.Warning,
            EditorDiagnosticSeverity.Error => EditorStatusMessageSeverity.Error,
            _ => EditorStatusMessageSeverity.Info,
        };
    }

    private static EditorDiagnosticSeverity MapStatusMessageSeverity(EditorStatusMessageSeverity severity)
    {
        return severity switch
        {
            EditorStatusMessageSeverity.Debug => EditorDiagnosticSeverity.Debug,
            EditorStatusMessageSeverity.Warning => EditorDiagnosticSeverity.Warning,
            EditorStatusMessageSeverity.Error => EditorDiagnosticSeverity.Error,
            _ => EditorDiagnosticSeverity.Info,
        };
    }

    internal static IPanelRegistry CreatePanelRegistry(IEditorSelectionService? selectionService = null)
    {
        return CreateDefaultComposition(selectionService).PanelRegistry;
    }

    internal static IWorkbenchActionRegistry CreateWorkbenchActionRegistry(IEditorSelectionService? selectionService = null)
    {
        return CreateDefaultComposition(selectionService).ActionRegistry;
    }

    internal static EditorExtensionComposition CreateDefaultComposition(
        IEditorSelectionService? selectionService = null,
        IEditorDiagnosticService? diagnostics = null)
    {
        return StudioCompositionRoot.CreateDefaultComposition(selectionService, diagnostics);
    }

    private static MainWindowViewModelArguments CreateDefaultViewModelArguments(
        EditorDockLayoutSnapshot? savedLayout)
    {
        var selectionService = new EditorSelectionService();
        var diagnostics = new EditorDiagnosticService();
        return new MainWindowViewModelArguments(
            StudioCompositionRoot.CreateDefaultComposition(selectionService, diagnostics),
            savedLayout,
            selectionService,
            diagnostics);
    }

    private sealed record MainWindowViewModelArguments(
        EditorExtensionComposition Composition,
        EditorDockLayoutSnapshot? SavedLayout,
        IEditorSelectionService SelectionService,
        IEditorDiagnosticService Diagnostics);

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
