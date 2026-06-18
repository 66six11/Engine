using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteViewModel : ViewModelBase
{
    private readonly IReadOnlyList<CommandPaletteItemViewModel> allItems_;
    private readonly Func<WorkbenchActionDescriptor, bool> executeAction_;
    private bool isOpen_;
    private string query_ = string.Empty;
    private IReadOnlyList<CommandPaletteItemViewModel> filteredItems_;
    private CommandPaletteItemViewModel? selectedItem_;
    private bool hasNoMatches_;

    public CommandPaletteViewModel(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        Func<WorkbenchActionDescriptor, bool> executeAction)
    {
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentNullException.ThrowIfNull(executeAction);

        executeAction_ = executeAction;
        allItems_ = actions.Select(action => new CommandPaletteItemViewModel(action)).ToArray();
        filteredItems_ = allItems_;
        selectedItem_ = filteredItems_.FirstOrDefault();
        hasNoMatches_ = filteredItems_.Count == 0;

        OpenCommand = new RelayCommand(Open);
        CloseCommand = new RelayCommand(Close);
        ExecuteSelectedCommand = new RelayCommand(ExecuteSelected);
    }

    public bool IsOpen
    {
        get => isOpen_;
        private set => SetProperty(ref isOpen_, value);
    }

    public string? Query
    {
        get => query_;
        set
        {
            if (SetProperty(ref query_, value ?? string.Empty))
            {
                RefreshFilteredItems();
            }
        }
    }

    public IReadOnlyList<CommandPaletteItemViewModel> FilteredItems
    {
        get => filteredItems_;
        private set => SetProperty(ref filteredItems_, value);
    }

    public CommandPaletteItemViewModel? SelectedItem
    {
        get => selectedItem_;
        set => SetProperty(ref selectedItem_, value);
    }

    public bool HasNoMatches
    {
        get => hasNoMatches_;
        private set => SetProperty(ref hasNoMatches_, value);
    }

    public IRelayCommand OpenCommand { get; }

    public IRelayCommand CloseCommand { get; }

    public IRelayCommand ExecuteSelectedCommand { get; }

    internal void Open()
    {
        Query = string.Empty;
        RefreshFilteredItems();
        SelectedItem = FilteredItems.FirstOrDefault();
        IsOpen = true;
    }

    internal void Close()
    {
        IsOpen = false;
    }

    private void ExecuteSelected()
    {
        if (SelectedItem is null)
        {
            return;
        }

        if (executeAction_(SelectedItem.Action))
        {
            Close();
        }
    }

    private void RefreshFilteredItems()
    {
        var query = query_.Trim();
        var filtered = string.IsNullOrEmpty(query)
            ? allItems_
            : allItems_.Where(item => MatchesQuery(item, query)).ToArray();

        FilteredItems = filtered;
        HasNoMatches = FilteredItems.Count == 0;
        if (SelectedItem is null || !FilteredItems.Contains(SelectedItem))
        {
            SelectedItem = FilteredItems.FirstOrDefault();
        }
    }

    private static bool MatchesQuery(CommandPaletteItemViewModel item, string query)
    {
        return item.Title.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Detail.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Id.Contains(query, StringComparison.OrdinalIgnoreCase);
    }
}
