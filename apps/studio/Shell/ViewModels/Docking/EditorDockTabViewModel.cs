using System;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Models.Panels;
using Editor.Shell.Services;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockTabViewModel : ViewModelBase
{
    private bool isActive_;
    private bool isDragSource_;
    private DockArea area_;
    private IDisposable? panelInstanceRelease_;
    private EditorPanelFrameScheduler? panelFrameScheduler_;
    private bool isPanelAttached_;
    private bool isPanelActive_;
    private bool isFloatingWorkspace_;

    public EditorDockTabViewModel(
        string id,
        string title,
        string tag,
        string titleDetail,
        string statusText,
        PanelKind kind,
        DockArea area,
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

    public DockArea Area
    {
        get => area_;
        set
        {
            if (SetProperty(ref area_, value))
            {
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
        var release = panelInstanceRelease_;
        panelInstanceRelease_ = null;
        DetachPanelInstance();
        release?.Dispose();
    }

    internal void AttachPanelInstance(bool isFloatingWorkspace)
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
            lifecycleSink.OnPanelAttached(context);
        }

        if (Content is IEditorPanelFrameUpdateSink frameUpdateSink)
        {
            panelFrameScheduler_?.AttachPanel(context, frameUpdateSink);
        }
    }

    internal void SetPanelLifecycleHostKind(bool isFloatingWorkspace)
    {
        isFloatingWorkspace_ = isFloatingWorkspace;
        UpdatePanelFrameSchedulerContext();
    }

    internal void SetPanelFrameScheduler(EditorPanelFrameScheduler? panelFrameScheduler)
    {
        if (ReferenceEquals(panelFrameScheduler_, panelFrameScheduler))
        {
            return;
        }

        var wasActive = isPanelActive_;
        var context = CreateLifecycleContext();
        if (isPanelAttached_ && Content is IEditorPanelFrameUpdateSink frameUpdateSink)
        {
            panelFrameScheduler_?.DetachPanel(context);
            panelFrameScheduler_ = panelFrameScheduler;
            panelFrameScheduler_?.AttachPanel(context, frameUpdateSink);
            if (wasActive)
            {
                panelFrameScheduler_?.ActivatePanel(context);
            }

            return;
        }

        panelFrameScheduler_ = panelFrameScheduler;
    }

    internal void ActivatePanelInstance()
    {
        if (!isPanelAttached_ || isPanelActive_)
        {
            return;
        }

        isPanelActive_ = true;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelActivated(context);
        }

        panelFrameScheduler_?.ActivatePanel(context);
    }

    internal void DeactivatePanelInstance()
    {
        if (!isPanelActive_)
        {
            return;
        }

        isPanelActive_ = false;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelDeactivated(context);
        }

        panelFrameScheduler_?.DeactivatePanel(context);
    }

    private void DetachPanelInstance()
    {
        if (!isPanelAttached_)
        {
            return;
        }

        DeactivatePanelInstance();
        isPanelAttached_ = false;
        var context = CreateLifecycleContext();
        if (Content is IEditorPanelLifecycleSink lifecycleSink)
        {
            lifecycleSink.OnPanelDetached(context);
        }

        panelFrameScheduler_?.DetachPanel(context);
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
