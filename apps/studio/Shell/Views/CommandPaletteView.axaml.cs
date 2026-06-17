using System;
using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class CommandPaletteView : UserControl
{
    private CommandPaletteViewModel? viewModel_;

    public CommandPaletteView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (viewModel_ is not null)
        {
            viewModel_.PropertyChanged -= OnViewModelPropertyChanged;
        }

        viewModel_ = DataContext as CommandPaletteViewModel;
        if (viewModel_ is not null)
        {
            viewModel_.PropertyChanged += OnViewModelPropertyChanged;
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(CommandPaletteViewModel.IsOpen)
            && viewModel_?.IsOpen == true)
        {
            Dispatcher.UIThread.Post(
                () =>
                {
                    CommandPaletteSearchBox.Focus();
                    CommandPaletteSearchBox.SelectAll();
                },
                DispatcherPriority.Input);
        }
    }

    private void OnCommandPaletteKeyDown(object? sender, KeyEventArgs e)
    {
        if (viewModel_ is null)
        {
            return;
        }

        if (e.Key == Key.Escape)
        {
            viewModel_.CloseCommand.Execute(null);
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Enter)
        {
            viewModel_.ExecuteSelectedCommand.Execute(null);
            e.Handled = true;
        }
    }

    private void OnResultsDoubleTapped(object? sender, TappedEventArgs e)
    {
        viewModel_?.ExecuteSelectedCommand.Execute(null);
        e.Handled = true;
    }
}
