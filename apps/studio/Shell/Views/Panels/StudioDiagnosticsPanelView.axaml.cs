using System;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.Diagnostics;

namespace Editor.Shell.Views.Panels;

public partial class StudioDiagnosticsPanelView : UserControl
{
    private StudioDiagnosticsPanelViewModel? viewModel_;
    private bool isAttached_;

    public StudioDiagnosticsPanelView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttached_ = true;
        UpdateSubscription();
        QueueConsoleFollow();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached_ = false;
        ReplaceViewModel(null);
        base.OnDetachedFromVisualTree(e);
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (isAttached_)
        {
            UpdateSubscription();
        }
    }

    private void UpdateSubscription() =>
        ReplaceViewModel(DataContext as StudioDiagnosticsPanelViewModel);

    private void ReplaceViewModel(StudioDiagnosticsPanelViewModel? viewModel)
    {
        if (ReferenceEquals(viewModel_, viewModel))
        {
            return;
        }

        if (viewModel_ is not null)
        {
            viewModel_.Console.PropertyChanged -= OnConsolePropertyChanged;
        }

        viewModel_ = viewModel;
        if (viewModel is not null)
        {
            viewModel.Console.PropertyChanged += OnConsolePropertyChanged;
        }
    }

    private void OnConsolePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(
                e.PropertyName,
                nameof(StudioConsoleProjectionViewModel.Rows),
                StringComparison.Ordinal)
            || string.Equals(
                e.PropertyName,
                nameof(StudioConsoleProjectionViewModel.IsPaused),
                StringComparison.Ordinal)
            || string.Equals(
                e.PropertyName,
                nameof(StudioConsoleProjectionViewModel.FollowTail),
                StringComparison.Ordinal))
        {
            QueueConsoleFollow();
        }
    }

    private void QueueConsoleFollow()
    {
        Dispatcher.UIThread.Post(FollowLatestConsoleRecord, DispatcherPriority.Background);
    }

    private void FollowLatestConsoleRecord()
    {
        if (!isAttached_
            || viewModel_ is not { Console: { IsPaused: false, FollowTail: true } } viewModel
            || viewModel.Console.Rows.Count == 0)
        {
            return;
        }

        ConsoleList.ScrollIntoView(viewModel.Console.Rows[^1]);
    }
}
