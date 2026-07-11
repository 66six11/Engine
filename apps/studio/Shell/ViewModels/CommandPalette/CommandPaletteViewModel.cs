using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Commands;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Models.Workbench;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.CommandPalette;

public sealed class CommandPaletteViewModel : ViewModelBase
{
    private const int MaxRecentCommands = 5;

    private readonly IReadOnlyList<CommandPaletteItemViewModel> allCommandItems_;
    private readonly Func<string, EditorCommandExecutionResult> executeCommand_;
    private readonly List<string> recentCommandIds_ = [];
    private bool isOpen_;
    private string query_ = string.Empty;
    private IReadOnlyList<CommandPaletteItemViewModel> filteredItems_;
    private CommandPaletteItemViewModel? selectedItem_;
    private bool hasNoMatches_;
    private string lastResultMessage_ = string.Empty;

    public CommandPaletteViewModel(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        Func<string, EditorCommandExecutionResult> executeCommand)
    {
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentNullException.ThrowIfNull(executeCommand);

        executeCommand_ = executeCommand;
        allCommandItems_ = actions.Select(action => new CommandPaletteItemViewModel(action)).ToArray();
        filteredItems_ = CreateDisplayRows(allCommandItems_, includeRecent: true);
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

    public string LastResultMessage
    {
        get => lastResultMessage_;
        private set
        {
            if (SetProperty(ref lastResultMessage_, value))
            {
                OnPropertyChanged(nameof(HasLastResultMessage));
            }
        }
    }

    public bool HasLastResultMessage => !string.IsNullOrWhiteSpace(LastResultMessage);

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
            RecordRecentCommand(SelectedItem.Id);
            LastResultMessage = string.Empty;
            Close();
            return;
        }

        LastResultMessage = string.IsNullOrWhiteSpace(result.Message)
            ? $"Command '{SelectedItem.Id}' did not complete."
            : result.Message;
    }

    private void RefreshFilteredItems()
    {
        var query = query_.Trim();
        var filteredCommands = string.IsNullOrEmpty(query)
            ? allCommandItems_
            : allCommandItems_.Where(item => MatchesQuery(item, query)).ToArray();

        FilteredItems = CreateDisplayRows(filteredCommands, string.IsNullOrEmpty(query));
        HasNoMatches = filteredCommands.Count == 0;
        if (SelectedItem is null
            || !SelectedItem.IsCommand
            || !FilteredItems.Contains(SelectedItem))
        {
            SelectedItem = SelectFirstCommand(FilteredItems);
        }
    }

    private void RecordRecentCommand(string commandId)
    {
        recentCommandIds_.Remove(commandId);
        recentCommandIds_.Insert(0, commandId);
        if (recentCommandIds_.Count > MaxRecentCommands)
        {
            recentCommandIds_.RemoveRange(MaxRecentCommands, recentCommandIds_.Count - MaxRecentCommands);
        }
    }

    private IReadOnlyList<CommandPaletteItemViewModel> CreateDisplayRows(
        IReadOnlyList<CommandPaletteItemViewModel> filteredCommands,
        bool includeRecent)
    {
        if (!includeRecent || recentCommandIds_.Count == 0)
        {
            return CreateGroupedRows(allCommandItems_, filteredCommands);
        }

        var rows = new List<CommandPaletteItemViewModel>();
        var recentItems = recentCommandIds_
            .Select(commandId => allCommandItems_.FirstOrDefault(item => item.Id == commandId))
            .Where(item => item is not null)
            .Cast<CommandPaletteItemViewModel>()
            .ToArray();
        if (recentItems.Length > 0)
        {
            rows.Add(CommandPaletteItemViewModel.CreateHeader("Recent"));
            rows.AddRange(recentItems);
        }

        var recentIds = new HashSet<string>(recentItems.Select(item => item.Id), StringComparer.Ordinal);
        var visibleNonRecentCommands = filteredCommands
            .Where(item => !recentIds.Contains(item.Id))
            .ToArray();
        rows.AddRange(CreateGroupedRows(allCommandItems_, visibleNonRecentCommands));
        return rows;
    }

    private static IReadOnlyList<CommandPaletteItemViewModel> CreateGroupedRows(
        IReadOnlyList<CommandPaletteItemViewModel> registeredCommandItems,
        IReadOnlyList<CommandPaletteItemViewModel> visibleCommandItems)
    {
        var rows = new List<CommandPaletteItemViewModel>();
        var categoryOrder = new List<string>();
        var categories = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in registeredCommandItems)
        {
            if (categories.Add(item.Category))
            {
                categoryOrder.Add(item.Category);
            }
        }

        var commandsByCategory = new Dictionary<string, List<CommandPaletteItemViewModel>>(StringComparer.Ordinal);
        foreach (var item in visibleCommandItems)
        {
            if (!commandsByCategory.TryGetValue(item.Category, out var categoryCommands))
            {
                categoryCommands = [];
                commandsByCategory.Add(item.Category, categoryCommands);
            }

            categoryCommands.Add(item);
        }

        foreach (var category in categoryOrder)
        {
            if (!commandsByCategory.TryGetValue(category, out var categoryCommands))
            {
                continue;
            }

            rows.Add(CommandPaletteItemViewModel.CreateHeader(category));
            rows.AddRange(categoryCommands);
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
            || item.DefaultShortcut.Contains(query, StringComparison.OrdinalIgnoreCase)
            || item.SearchText.Contains(query, StringComparison.OrdinalIgnoreCase);
    }
}
