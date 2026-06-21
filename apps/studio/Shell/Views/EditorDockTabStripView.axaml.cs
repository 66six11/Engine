using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockTabStripView : UserControl
{
    private readonly List<IDisposable> scrollSubscriptions_ = [];
    private EditorDockWindowViewModel? window_;
    private bool isAttachedToVisualTree_;
    private bool isLayoutRefreshQueued_;
    private bool isWindowSubscribed_;

    public EditorDockTabStripView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    internal double HorizontalOffset => DockTabStripScrollViewer.Offset.X;

    internal double ExtentWidth => DockTabStripScrollViewer.Extent.Width;

    internal double ViewportWidth => DockTabStripScrollViewer.Viewport.Width;

    internal bool TryGetContentOrigin(Visual relativeTo, out Point origin)
    {
        var viewportOrigin = DockTabStripScrollViewer.TranslatePoint(new Point(0, 0), relativeTo);
        var contentOrigin = DockTabStripItems.TranslatePoint(new Point(0, 0), relativeTo);
        if (viewportOrigin is null || contentOrigin is null)
        {
            origin = default;
            return false;
        }

        origin = new Point(
            EditorDockTabStripScrollController.CalculateContentOriginX(
                viewportOrigin.Value.X,
                HorizontalOffset),
            contentOrigin.Value.Y);
        return true;
    }

    internal bool AutoScrollNearHorizontalEdge(Point pointer, Visual relativeTo)
    {
        var viewportPointer = relativeTo.TranslatePoint(pointer, DockTabStripScrollViewer);
        if (viewportPointer is null)
        {
            return false;
        }

        var nextOffset = EditorDockTabStripScrollController.CalculateAutoScrollOffset(
            viewportPointer.Value.X,
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth);
        return SetHorizontalOffset(nextOffset);
    }

    internal void BringTabIntoView(EditorDockTabViewModel tab)
    {
        if (!TryGetTabBoundsInContent(tab, out var bounds))
        {
            return;
        }

        var nextOffset = EditorDockTabStripScrollController.CalculateOffsetToReveal(
            bounds,
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth);
        SetHorizontalOffset(nextOffset);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttachedToVisualTree_ = true;
        AttachScrollSubscriptions();
        SetWindow(DataContext as EditorDockWindowViewModel);
        BringActiveTabIntoView();
        QueueLayoutRefresh();
        UpdateOverflowAffordances();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttachedToVisualTree_ = false;
        DetachLayoutRefresh();
        DetachWindow();
        DetachScrollSubscriptions();
        base.OnDetachedFromVisualTree(e);
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        SetWindow(DataContext as EditorDockWindowViewModel);
        BringActiveTabIntoView();
        QueueLayoutRefresh();
        UpdateOverflowAffordances();
    }

    private void SetWindow(EditorDockWindowViewModel? window)
    {
        if (ReferenceEquals(window_, window))
        {
            AttachWindow();
            return;
        }

        DetachWindow();
        window_ = window;
        AttachWindow();
    }

    private void AttachWindow()
    {
        if (!isAttachedToVisualTree_ || window_ is null || isWindowSubscribed_)
        {
            return;
        }

        window_.PropertyChanged += OnWindowPropertyChanged;
        window_.TabStripItems.CollectionChanged += OnTabStripItemsChanged;
        isWindowSubscribed_ = true;
    }

    private void DetachWindow()
    {
        if (window_ is null || !isWindowSubscribed_)
        {
            return;
        }

        window_.PropertyChanged -= OnWindowPropertyChanged;
        window_.TabStripItems.CollectionChanged -= OnTabStripItemsChanged;
        isWindowSubscribed_ = false;
    }

    private void OnWindowPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(EditorDockWindowViewModel.ActiveTab))
        {
            return;
        }

        BringActiveTabIntoView();
        QueueLayoutRefresh();
    }

    private void OnTabStripItemsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        BringActiveTabIntoView();
        QueueLayoutRefresh();
        UpdateOverflowAffordances();
    }

    private void BringActiveTabIntoView()
    {
        if (window_?.ActiveTab is { } activeTab)
        {
            BringTabIntoView(activeTab);
        }
    }

    private bool TryGetTabBoundsInContent(EditorDockTabViewModel tab, out Rect bounds)
    {
        foreach (var host in DockTabStripItems.GetVisualDescendants().OfType<EditorDockTabItemView>())
        {
            if (!host.IsVisible
                || host.DataContext is not EditorDockTabStripItemViewModel item
                || !ReferenceEquals(item.Tab, tab)
                || host.TranslatePoint(new Point(0, 0), DockTabStripItems) is not { } origin
                || host.Bounds.Width <= 0
                || host.Bounds.Height <= 0)
            {
                continue;
            }

            bounds = new Rect(origin, host.Bounds.Size);
            return true;
        }

        bounds = default;
        return false;
    }

    private bool SetHorizontalOffset(double requestedOffset)
    {
        var nextOffset = EditorDockTabStripScrollController.ClampOffset(
            requestedOffset,
            ExtentWidth,
            ViewportWidth);
        if (Math.Abs(nextOffset - HorizontalOffset) < 0.5)
        {
            return false;
        }

        DockTabStripScrollViewer.Offset = new Vector(nextOffset, DockTabStripScrollViewer.Offset.Y);
        UpdateOverflowAffordances();
        return true;
    }

    private void QueueLayoutRefresh()
    {
        if (!isAttachedToVisualTree_ || isLayoutRefreshQueued_)
        {
            return;
        }

        isLayoutRefreshQueued_ = true;
        DockTabStripItems.LayoutUpdated += OnDockTabStripItemsLayoutUpdated;
    }

    private void DetachLayoutRefresh()
    {
        if (!isLayoutRefreshQueued_)
        {
            return;
        }

        DockTabStripItems.LayoutUpdated -= OnDockTabStripItemsLayoutUpdated;
        isLayoutRefreshQueued_ = false;
    }

    private void OnDockTabStripItemsLayoutUpdated(object? sender, EventArgs e)
    {
        DetachLayoutRefresh();
        BringActiveTabIntoView();
        UpdateOverflowAffordances();
    }

    private void AttachScrollSubscriptions()
    {
        if (scrollSubscriptions_.Count > 0)
        {
            return;
        }

        scrollSubscriptions_.Add(DockTabStripScrollViewer
            .GetObservable(ScrollViewer.OffsetProperty)
            .Subscribe(new ActionObserver<Vector>(_ => UpdateOverflowAffordances())));
        scrollSubscriptions_.Add(DockTabStripScrollViewer
            .GetObservable(ScrollViewer.ExtentProperty)
            .Subscribe(new ActionObserver<Size>(_ => OnScrollGeometryChanged())));
        scrollSubscriptions_.Add(DockTabStripScrollViewer
            .GetObservable(ScrollViewer.ViewportProperty)
            .Subscribe(new ActionObserver<Size>(_ => OnScrollGeometryChanged())));
    }

    private void OnScrollGeometryChanged()
    {
        BringActiveTabIntoView();
        QueueLayoutRefresh();
        UpdateOverflowAffordances();
    }

    private void DetachScrollSubscriptions()
    {
        foreach (var subscription in scrollSubscriptions_)
        {
            subscription.Dispose();
        }

        scrollSubscriptions_.Clear();
    }

    private void UpdateOverflowAffordances()
    {
        var maxOffset = EditorDockTabStripScrollController.ClampOffset(
            double.MaxValue,
            ExtentWidth,
            ViewportWidth);
        var offset = EditorDockTabStripScrollController.ClampOffset(
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth);
        LeftOverflowAffordance.IsVisible = maxOffset > 0.5 && offset > 0.5;
        RightOverflowAffordance.IsVisible = maxOffset - offset > 0.5;
    }

    private sealed class ActionObserver<T> : IObserver<T>
    {
        private readonly Action<T> onNext_;

        public ActionObserver(Action<T> onNext)
        {
            onNext_ = onNext;
        }

        public void OnCompleted()
        {
        }

        public void OnError(Exception error)
        {
        }

        public void OnNext(T value)
        {
            onNext_(value);
        }
    }
}
