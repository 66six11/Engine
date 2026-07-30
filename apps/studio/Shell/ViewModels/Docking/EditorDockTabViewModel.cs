using System;
using Asharia.Editor.Panels;
using Asharia.Studio.Application.Panels;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Shell.Lifecycle;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockTabViewModel : ViewModelBase
{
    private bool isActive_;
    private bool isDragSource_;
    private EditorDockArea area_;
    private IDisposable? panelInstanceRelease_;
    private EditorPanelFrameScheduler? panelFrameScheduler_;
    private bool isPanelAttached_;
    private bool isPanelActive_;
    private bool isPanelShown_;
    private bool isFloatingWorkspace_;
    private EditorPanelLayoutContext? lastPanelLayout_;

    public EditorDockTabViewModel(
        string id,
        string title,
        string tag,
        string titleDetail,
        string statusText,
        PanelKind kind,
        EditorDockArea area,
        object content,
        string? iconKey = null,
        IDisposable? panelInstanceRelease = null,
        EditorPanelFrameScheduler? panelFrameScheduler = null)
    {
        Id = id;
        Title = title;
        Tag = tag;
        TitleDetail = titleDetail;
        StatusText = statusText;
        Kind = kind;
        area_ = area;
        Content = content;
        panelInstanceRelease_ = panelInstanceRelease;
        panelFrameScheduler_ = panelFrameScheduler;
        IconKey = iconKey;
    }

    public string Id { get; }

    public string Title { get; }

    public string Tag { get; }

    public string TitleDetail { get; }

    public string StatusText { get; }

    public string? IconKey { get; }

    public bool HasIcon => !string.IsNullOrWhiteSpace(IconKey);

    public PanelKind Kind { get; }

    public object Content { get; }

    public EditorDockArea Area
    {
        get => area_;
        set
        {
            if (SetProperty(ref area_, value))
            {
                lastPanelLayout_ = null;
                UpdatePanelFrameSchedulerContext();
            }
        }
    }

    public bool IsActive
    {
        get => isActive_;
        set => SetProperty(ref isActive_, value);
    }

    public bool IsDragSource
    {
        get => isDragSource_;
        private set => SetProperty(ref isDragSource_, value);
    }

    internal void SetDragSourceState(bool isDragSource)
    {
        IsDragSource = isDragSource;
    }

    internal void ReleasePanelInstance()
    {
        var exceptions = new CallbackExceptionBatch();
        ReleasePanelInstance(exceptions);
        exceptions.ThrowIfAny();
    }

    internal void ReleasePanelInstance(CallbackExceptionBatch exceptions)
    {
        var release = panelInstanceRelease_;
        panelInstanceRelease_ = null;
        DetachPanelInstance(exceptions);
        if (release is not null)
        {
            exceptions.Capture(release.Dispose);
        }
    }

    internal void AttachPanelInstance(bool isFloatingWorkspace)
    {
        var exceptions = new CallbackExceptionBatch();
        AttachPanelInstance(isFloatingWorkspace, exceptions);
        exceptions.ThrowIfAny();
    }

    internal void AttachPanelInstance(
        bool isFloatingWorkspace,
        CallbackExceptionBatch exceptions)
    {
        isFloatingWorkspace_ = isFloatingWorkspace;
        if (isPanelAttached_)
        {
            return;
        }

        isPanelAttached_ = true;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            exceptions.Capture(() => lifecycleSink.OnPanelAttached(context));
        }

        if (Content is IEditorPanelFrameUpdateSink frameUpdateSink)
        {
            exceptions.Capture(
                () => panelFrameScheduler_?.AttachPanel(context, frameUpdateSink));
        }
    }

    internal void SetPanelLifecycleHostKind(bool isFloatingWorkspace)
    {
        isFloatingWorkspace_ = isFloatingWorkspace;
        lastPanelLayout_ = null;
        UpdatePanelFrameSchedulerContext();
    }

    internal void SetPanelFrameScheduler(EditorPanelFrameScheduler? panelFrameScheduler)
    {
        if (ReferenceEquals(panelFrameScheduler_, panelFrameScheduler))
        {
            return;
        }

        var wasActive = isPanelActive_;
        var wasShown = isPanelShown_;
        var context = CreateLifecycleContext();
        if (isPanelAttached_ && Content is IEditorPanelFrameUpdateSink frameUpdateSink)
        {
            panelFrameScheduler_?.DetachPanel(context);
            panelFrameScheduler_ = panelFrameScheduler;
            panelFrameScheduler_?.AttachPanel(context, frameUpdateSink);
            if (wasShown)
            {
                panelFrameScheduler_?.ShowPanel(context);
            }

            if (wasActive)
            {
                panelFrameScheduler_?.ActivatePanel(context);
            }

            return;
        }

        panelFrameScheduler_ = panelFrameScheduler;
    }

    internal void ShowPanelInstance()
    {
        var exceptions = new CallbackExceptionBatch();
        ShowPanelInstance(exceptions);
        exceptions.ThrowIfAny();
    }

    internal void ShowPanelInstance(CallbackExceptionBatch exceptions)
    {
        if (!isPanelAttached_ || isPanelShown_)
        {
            return;
        }

        isPanelShown_ = true;
        lastPanelLayout_ = null;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelVisibilitySink visibilitySink)
        {
            exceptions.Capture(() => visibilitySink.OnPanelShown(context));
        }

        exceptions.Capture(() => panelFrameScheduler_?.ShowPanel(context));
    }

    internal void HidePanelInstance()
    {
        var exceptions = new CallbackExceptionBatch();
        HidePanelInstance(exceptions);
        exceptions.ThrowIfAny();
    }

    internal void HidePanelInstance(CallbackExceptionBatch exceptions)
    {
        if (!isPanelShown_)
        {
            return;
        }

        DeactivatePanelInstance(exceptions);
        isPanelShown_ = false;
        lastPanelLayout_ = null;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelVisibilitySink visibilitySink)
        {
            exceptions.Capture(() => visibilitySink.OnPanelHidden(context));
        }

        exceptions.Capture(() => panelFrameScheduler_?.HidePanel(context));
    }

    internal void ActivatePanelInstance()
    {
        var exceptions = new CallbackExceptionBatch();
        ActivatePanelInstance(exceptions);
        exceptions.ThrowIfAny();
    }

    internal void ActivatePanelInstance(CallbackExceptionBatch exceptions)
    {
        if (!isPanelAttached_ || isPanelActive_)
        {
            return;
        }

        ShowPanelInstance(exceptions);
        isPanelActive_ = true;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            exceptions.Capture(() => lifecycleSink.OnPanelActivated(context));
        }

        exceptions.Capture(() => panelFrameScheduler_?.ActivatePanel(context));
    }

    internal void DeactivatePanelInstance()
    {
        var exceptions = new CallbackExceptionBatch();
        DeactivatePanelInstance(exceptions);
        exceptions.ThrowIfAny();
    }

    internal void DeactivatePanelInstance(CallbackExceptionBatch exceptions)
    {
        if (!isPanelActive_)
        {
            return;
        }

        isPanelActive_ = false;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            exceptions.Capture(() => lifecycleSink.OnPanelDeactivated(context));
        }

        exceptions.Capture(() => panelFrameScheduler_?.DeactivatePanel(context));
    }

    internal void UpdatePanelLayout(
        double logicalWidth,
        double logicalHeight,
        double renderScale)
    {
        if (!isPanelAttached_ || !isPanelShown_)
        {
            return;
        }

        var layout = new EditorPanelLayoutContext(
            CreateLifecycleContext(),
            logicalWidth,
            logicalHeight,
            renderScale);
        if (layout == lastPanelLayout_)
        {
            return;
        }

        lastPanelLayout_ = layout;
        if (Content is IEditorPanelLayoutSink layoutSink)
        {
            layoutSink.OnPanelLayoutChanged(layout);
        }
    }

    private void DetachPanelInstance(CallbackExceptionBatch exceptions)
    {
        if (!isPanelAttached_)
        {
            return;
        }

        HidePanelInstance(exceptions);
        isPanelAttached_ = false;
        lastPanelLayout_ = null;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            exceptions.Capture(() => lifecycleSink.OnPanelDetached(context));
        }

        exceptions.Capture(() => panelFrameScheduler_?.DetachPanel(context));
    }

    private void UpdatePanelFrameSchedulerContext()
    {
        if (isPanelAttached_)
        {
            panelFrameScheduler_?.UpdatePanel(CreateLifecycleContext());
        }
    }

    private EditorPanelLifecycleContext CreateLifecycleContext()
    {
        return new EditorPanelLifecycleContext(
            Id,
            Title,
            Area,
            isFloatingWorkspace_);
    }

}
