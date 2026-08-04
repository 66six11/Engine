using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.Docking.TabStrips;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Views.Docking;

public partial class EditorDockTabStripView : UserControl
{
    private const double OverflowHoverScrollStep = 9.0;
    private static readonly TimeSpan OverflowHoverScrollInterval = TimeSpan.FromMilliseconds(16);
    private readonly List<IDisposable> scrollSubscriptions_ = [];
    private readonly DispatcherTimer overflowHoverScrollTimer_;
    private EditorDockWindowViewModel? window_;
    private OverflowHoverScrollDirection overflowHoverScrollDirection_;
    private bool isAttachedToVisualTree_;
    private bool isLayoutRefreshQueued_;
    private bool isWindowSubscribed_;

    public EditorDockTabStripView()
    {
        InitializeComponent();
        overflowHoverScrollTimer_ = new DispatcherTimer
        {
            Interval = OverflowHoverScrollInterval,
        };
        overflowHoverScrollTimer_.Tick += OnOverflowHoverScrollTimerTick;
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

    internal bool TryGetViewportBounds(Visual relativeTo, out Rect bounds)
    {
        var origin = DockTabStripScrollViewer.TranslatePoint(new Point(0, 0), relativeTo);
        if (origin is null
            || DockTabStripScrollViewer.Bounds.Width <= 0
            || DockTabStripScrollViewer.Bounds.Height <= 0)
        {
            bounds = default;
            return false;
        }

        bounds = new Rect(origin.Value, DockTabStripScrollViewer.Bounds.Size);
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
        StopOverflowHoverScroll();
        DetachLayoutRefresh();
        DetachWindow();
        DetachScrollSubscriptions();
        base.OnDetachedFromVisualTree(e);
    }

    private void OnLeftOverflowAffordancePointerEntered(object? sender, PointerEventArgs e)
    {
        StartOverflowHoverScroll(OverflowHoverScrollDirection.Left);
    }

    private void OnRightOverflowAffordancePointerEntered(object? sender, PointerEventArgs e)
    {
        StartOverflowHoverScroll(OverflowHoverScrollDirection.Right);
    }

    private void OnOverflowAffordancePointerExited(object? sender, PointerEventArgs e)
    {
        StopOverflowHoverScroll();
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

    private void StartOverflowHoverScroll(OverflowHoverScrollDirection direction)
    {
        if (!CanOverflowHoverScroll(direction))
        {
            StopOverflowHoverScroll();
            return;
        }

        overflowHoverScrollDirection_ = direction;
        if (!ScrollOverflowHoverOnce())
        {
            StopOverflowHoverScroll();
            return;
        }

        if (!CanOverflowHoverScroll(direction))
        {
            StopOverflowHoverScroll();
            return;
        }

        if (!overflowHoverScrollTimer_.IsEnabled)
        {
            overflowHoverScrollTimer_.Start();
        }
    }

    private void StopOverflowHoverScroll()
    {
        overflowHoverScrollDirection_ = OverflowHoverScrollDirection.None;
        if (overflowHoverScrollTimer_.IsEnabled)
        {
            overflowHoverScrollTimer_.Stop();
        }
    }

    private void OnOverflowHoverScrollTimerTick(object? sender, EventArgs e)
    {
        if (!ScrollOverflowHoverOnce())
        {
            StopOverflowHoverScroll();
        }
    }

    private bool ScrollOverflowHoverOnce()
    {
        if (!CanOverflowHoverScroll(overflowHoverScrollDirection_))
        {
            return false;
        }

        var pointerX = overflowHoverScrollDirection_ == OverflowHoverScrollDirection.Left
            ? 0
            : ViewportWidth;
        var nextOffset = EditorDockTabStripScrollController.CalculateAutoScrollOffset(
            pointerX,
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth,
            step: OverflowHoverScrollStep);
        return SetHorizontalOffset(nextOffset);
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
        var canScrollLeft = CanOverflowHoverScroll(OverflowHoverScrollDirection.Left);
        var canScrollRight = CanOverflowHoverScroll(OverflowHoverScrollDirection.Right);
        LeftOverflowAffordance.IsVisible = canScrollLeft;
        RightOverflowAffordance.IsVisible = canScrollRight;
        if (overflowHoverScrollDirection_ != OverflowHoverScrollDirection.None
            && !CanOverflowHoverScroll(overflowHoverScrollDirection_))
        {
            StopOverflowHoverScroll();
        }
    }

    private bool CanOverflowHoverScroll(OverflowHoverScrollDirection direction)
    {
        var maxOffset = EditorDockTabStripScrollController.ClampOffset(
            double.MaxValue,
            ExtentWidth,
            ViewportWidth);
        var offset = EditorDockTabStripScrollController.ClampOffset(
            HorizontalOffset,
            ExtentWidth,
            ViewportWidth);
        return direction switch
        {
            OverflowHoverScrollDirection.Left => maxOffset > 0.5 && offset > 0.5,
            OverflowHoverScrollDirection.Right => maxOffset - offset > 0.5,
            _ => false,
        };
    }

    private enum OverflowHoverScrollDirection
    {
        None,
        Left,
        Right,
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
