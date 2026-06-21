using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteViewModel : ViewModelBase
{
    private readonly IReadOnlyList<CommandPaletteItemViewModel> allCommandItems_;
    private readonly Func<string, WorkbenchCommandExecutionResult> executeCommand_;
    private bool isOpen_;
    private string query_ = string.Empty;
    private IReadOnlyList<CommandPaletteItemViewModel> filteredItems_;
    private CommandPaletteItemViewModel? selectedItem_;
    private bool hasNoMatches_;

    public CommandPaletteViewModel(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        Func<string, WorkbenchCommandExecutionResult> executeCommand)
    {
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentNullException.ThrowIfNull(executeCommand);

        executeCommand_ = executeCommand;
        allCommandItems_ = actions.Select(action => new CommandPaletteItemViewModel(action)).ToArray();
        filteredItems_ = CreateGroupedRows(allCommandItems_);
        selectedItem_ = SelectFirstCommand(filteredItems_);
        hasNoMatches_ = allCommandItems_.Count == 0;

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
        SelectedItem = SelectFirstCommand(FilteredItems);
        IsOpen = true;
    }

    internal void Close()
    {
        IsOpen = false;
    }

    private void ExecuteSelected()
    {
        if (SelectedItem is null || !SelectedItem.IsCommand || !SelectedItem.IsEnabled)
        {
            return;
        }

        var result = executeCommand_(SelectedItem.Id);
        if (result.Succeeded)
        {
            Close();
        }
    }

    private void RefreshFilteredItems()
    {
        var query = query_.Trim();
        var filteredCommands = string.IsNullOrEmpty(query)
            ? allCommandItems_
            : allCommandItems_.Where(item => MatchesQuery(item, query)).ToArray();

        FilteredItems = CreateGroupedRows(filteredCommands);
        HasNoMatches = filteredCommands.Count == 0;
        if (SelectedItem is null
            || !SelectedItem.IsCommand
            || !FilteredItems.Contains(SelectedItem))
        {
            SelectedItem = SelectFirstCommand(FilteredItems);
        }
    }

    private static IReadOnlyList<CommandPaletteItemViewModel> CreateGroupedRows(
        IReadOnlyList<CommandPaletteItemViewModel> commandItems)
    {
        var rows = new List<CommandPaletteItemViewModel>();
        var categories = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in commandItems)
        {
            if (categories.Add(item.Category))
            {
                rows.Add(CommandPaletteItemViewModel.CreateHeader(item.Category));
            }

            rows.Add(item);
        }

        return rows;
    }

    private static CommandPaletteItemViewModel? SelectFirstCommand(
        IReadOnlyList<CommandPaletteItemViewModel> items)
    {
        return items.FirstOrDefault(item => item.IsCommand && item.IsEnabled)
            ?? items.FirstOrDefault(item => item.IsCommand);
    }

    private static bool MatchesQuery(CommandPaletteItemViewModel item, string query)
    {
        return item.Title.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Detail.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Id.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.Category.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.SearchText.Contains(query, StringComparison.OrdinalIgnoreCase);
    }
}
