using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Input;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Shell.Commands;
using Editor.Shell.Docking;
using Editor.Shell.Composition;
using Editor.Shell.Selection;
using Editor.Shell.Services;

namespace Editor.Shell.ViewModels;

public class MainWindowViewModel : ViewModelBase, IDisposable
{
    private readonly IPanelRegistry panelRegistry_;
    private readonly PanelCommandService panelCommandService_;
    private readonly WorkbenchShortcutRouter shortcutRouter_;
    private readonly IEditorBackgroundTaskService backgroundTasks_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private readonly List<EditorDockFloatingWindowSnapshot> pendingFloatingWindowSnapshots_ = [];
    private Func<IReadOnlyList<EditorDockFloatingWindowSnapshot>>? captureFloatingWindowSnapshots_;
    private Action? closeFloatingWindows_;
    private bool hasActiveBackgroundTasks_;
    private string activeBackgroundTaskTitle_ = string.Empty;
    private string activeBackgroundTaskMessage_ = string.Empty;
    private EditorCommandFeedbackSnapshot? lastCommandFeedback_;
    private EditorDiagnosticRecord? latestStatusDiagnostic_;
    private EditorCommandFeedbackSeverity? statusFeedbackSeverity_;
    private readonly IEditorDiagnosticService diagnostics_;
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
        var actionExecutor = new WorkbenchActionExecutor(
            commandHandlers);
        var commandRouter = new WorkbenchCommandFeedbackRouter(
            new WorkbenchCommandRouter(actionRegistry, actionExecutor),
            PublishCommandFeedback);
        panelCommandService_.PanelStateChanged += OnPanelCommandStateChanged;
        OpenPanelCommand = new RelayCommand<string?>(
            panelId => panelCommandService_.OpenOrFocusPanel(panelId));
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

    public EditorCommandFeedbackSnapshot? LastCommandFeedback
    {
        get => lastCommandFeedback_;
        private set
        {
            if (SetProperty(ref lastCommandFeedback_, value))
            {
                OnPropertyChanged(nameof(HasCommandFeedback));
                OnPropertyChanged(nameof(CommandFeedbackMessage));
                OnPropertyChanged(nameof(IsCommandFeedbackSuccess));
                OnPropertyChanged(nameof(IsCommandFeedbackWarning));
                OnPropertyChanged(nameof(IsCommandFeedbackError));
                OnPropertyChanged(nameof(IsCommandFeedbackInfo));
            }
        }
    }

    public bool HasCommandFeedback => latestStatusDiagnostic_ is not null || LastCommandFeedback is not null;

    public string CommandFeedbackMessage => latestStatusDiagnostic_?.Message ?? LastCommandFeedback?.Message ?? string.Empty;

    public bool IsCommandFeedbackSuccess =>
        CurrentCommandFeedbackSeverity == EditorCommandFeedbackSeverity.Success;

    public bool IsCommandFeedbackWarning =>
        CurrentCommandFeedbackSeverity == EditorCommandFeedbackSeverity.Warning;

    public bool IsCommandFeedbackError =>
        CurrentCommandFeedbackSeverity == EditorCommandFeedbackSeverity.Error;

    public bool IsCommandFeedbackInfo =>
        CurrentCommandFeedbackSeverity == EditorCommandFeedbackSeverity.Info;

    private EditorCommandFeedbackSeverity? CurrentCommandFeedbackSeverity =>
        statusFeedbackSeverity_ ?? LastCommandFeedback?.Severity;

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

    private bool OpenAboutDialogFromCommand()
    {
        _ = DialogHost.ShowAsync(EditorDialogRequest.Information(
            "About Studio",
            "Studio editor shell for VkEngine."));
        return true;
    }

    private void OnPanelCommandStateChanged(object? sender, EventArgs e)
    {
        RefreshPanelMenuOpenStates();
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

    private void PublishCommandFeedback(WorkbenchCommandExecutionResult result)
    {
        var feedback = EditorCommandFeedbackSnapshot.FromResult(result);
        LastCommandFeedback = feedback;
        diagnostics_.Publish(
            MapCommandFeedbackSeverity(feedback.Severity),
            EditorDiagnosticChannel.Debug,
            feedback.CommandId,
            "workbench",
            feedback.Message);
    }

    private void RefreshLatestDiagnostic()
    {
        var latestDiagnostic = diagnostics_.GetLatestDiagnostic();
        if (latestStatusDiagnostic_ == latestDiagnostic)
        {
            return;
        }

        latestStatusDiagnostic_ = latestDiagnostic;
        statusFeedbackSeverity_ = ResolveStatusFeedbackSeverity(latestDiagnostic);
        OnPropertyChanged(nameof(HasCommandFeedback));
        OnPropertyChanged(nameof(CommandFeedbackMessage));
        OnPropertyChanged(nameof(IsCommandFeedbackSuccess));
        OnPropertyChanged(nameof(IsCommandFeedbackWarning));
        OnPropertyChanged(nameof(IsCommandFeedbackError));
        OnPropertyChanged(nameof(IsCommandFeedbackInfo));
    }

    private EditorCommandFeedbackSeverity? ResolveStatusFeedbackSeverity(
        EditorDiagnosticRecord? diagnostic)
    {
        if (diagnostic is null)
        {
            return LastCommandFeedback?.Severity;
        }

        if (LastCommandFeedback is { } feedback
            && string.Equals(feedback.CommandId, diagnostic.Source, StringComparison.Ordinal)
            && string.Equals(feedback.Message, diagnostic.Message, StringComparison.Ordinal))
        {
            return feedback.Severity;
        }

        return diagnostic.Severity switch
        {
            EditorDiagnosticSeverity.Warning => EditorCommandFeedbackSeverity.Warning,
            EditorDiagnosticSeverity.Error => EditorCommandFeedbackSeverity.Error,
            _ => EditorCommandFeedbackSeverity.Info,
        };
    }

    private static EditorDiagnosticSeverity MapCommandFeedbackSeverity(EditorCommandFeedbackSeverity severity)
    {
        return severity switch
        {
            EditorCommandFeedbackSeverity.Warning => EditorDiagnosticSeverity.Warning,
            EditorCommandFeedbackSeverity.Error => EditorDiagnosticSeverity.Error,
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
