using System;
using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Dialogs;

namespace Editor.Shell.Views.Dialogs;

public partial class EditorDialogHostView : UserControl
{
    private EditorDialogHostViewModel? viewModel_;

    public EditorDialogHostView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    internal static bool TryCancelDialog(object? dataContext)
    {
        return dataContext is EditorDialogHostViewModel viewModel
            && viewModel.TryCancel();
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (viewModel_ is not null)
        {
            viewModel_.PropertyChanged -= OnViewModelPropertyChanged;
        }

        viewModel_ = DataContext as EditorDialogHostViewModel;
        if (viewModel_ is not null)
        {
            viewModel_.PropertyChanged += OnViewModelPropertyChanged;
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(EditorDialogHostViewModel.IsOpen)
            && viewModel_?.IsOpen == true)
        {
            Dispatcher.UIThread.Post(
                () => Focus(),
                DispatcherPriority.Input);
        }
    }

    private void OnDialogHostKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape && TryCancelDialog(DataContext))
        {
            e.Handled = true;
        }
    }
}
