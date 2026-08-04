using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Views.Docking;

public sealed class EditorDockPanelContentHost : ContentControl
{
    public static readonly StyledProperty<EditorDockTabViewModel?> PanelProperty =
        AvaloniaProperty.Register<EditorDockPanelContentHost, EditorDockTabViewModel?>(
            nameof(Panel));

    private readonly EditorDockPanelLayoutNotificationQueue layoutNotificationQueue_;
    private bool isAttachedToVisualTree_;
    private TopLevel? topLevel_;

    public EditorDockPanelContentHost()
    {
        layoutNotificationQueue_ = new EditorDockPanelLayoutNotificationQueue(
            action => Dispatcher.UIThread.Post(action, DispatcherPriority.Loaded),
            PublishCurrentLayout);
    }

    public EditorDockTabViewModel? Panel
    {
        get => GetValue(PanelProperty);
        set => SetValue(PanelProperty, value);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttachedToVisualTree_ = true;
        topLevel_ = TopLevel.GetTopLevel(this);
        if (topLevel_ is not null)
        {
            topLevel_.ScalingChanged += OnTopLevelScalingChanged;
        }

        QueueLayoutNotification();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttachedToVisualTree_ = false;
        layoutNotificationQueue_.Cancel();
        if (topLevel_ is not null)
        {
            topLevel_.ScalingChanged -= OnTopLevelScalingChanged;
            topLevel_ = null;
        }

        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == PanelProperty)
        {
            Content = Panel?.Content;
            InvalidateArrange();
            QueueLayoutNotification();
        }
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        var arrangedSize = base.ArrangeOverride(finalSize);
        QueueLayoutNotification();
        return arrangedSize;
    }

    private void OnTopLevelScalingChanged(object? sender, EventArgs e)
    {
        QueueLayoutNotification();
    }

    private void QueueLayoutNotification()
    {
        if (!isAttachedToVisualTree_)
        {
            return;
        }

        layoutNotificationQueue_.Request();
    }

    private void PublishCurrentLayout()
    {
        if (!isAttachedToVisualTree_)
        {
            return;
        }

        var logicalSize = Bounds.Size;
        Panel?.UpdatePanelLayout(
            logicalSize.Width,
            logicalSize.Height,
            topLevel_?.RenderScaling ?? 1d);
    }
}

internal sealed class EditorDockPanelLayoutNotificationQueue
{
    private readonly Action<Action> post_;
    private readonly Action publish_;
    private readonly Action<Exception> reportFailure_;
    private long nextSequence_;
    private long queuedSequence_;

    internal EditorDockPanelLayoutNotificationQueue(
        Action<Action> post,
        Action publish,
        Action<Exception>? reportFailure = null)
    {
        ArgumentNullException.ThrowIfNull(post);
        ArgumentNullException.ThrowIfNull(publish);

        post_ = post;
        publish_ = publish;
        reportFailure_ = reportFailure ?? ReportFailure;
    }

    internal void Request()
    {
        if (queuedSequence_ != 0)
        {
            return;
        }

        var sequence = ++nextSequence_;
        queuedSequence_ = sequence;
        post_(() => Complete(sequence));
    }

    internal void Cancel()
    {
        queuedSequence_ = 0;
        ++nextSequence_;
    }

    private void Complete(long sequence)
    {
        if (queuedSequence_ != sequence)
        {
            return;
        }

        queuedSequence_ = 0;
        try
        {
            publish_();
        }
        catch (Exception exception)
        {
            reportFailure_(exception);
        }
    }

    private static void ReportFailure(Exception exception) =>
        Trace.TraceError(
            "Dock panel layout notification failed: {0}",
            exception);
}
