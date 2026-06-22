using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteItemViewModel
{
    internal CommandPaletteItemViewModel(WorkbenchActionDescriptor action)
    {
        Id = action.Id;
        Title = action.Title;
        Detail = action.MenuPath;
        Category = action.Category;
        IconKey = action.IconKey;
        DefaultShortcut = action.DefaultShortcut ?? string.Empty;
        IsEnabled = action.IsEnabled;
        DisabledReason = action.DisabledReason ?? string.Empty;
        SearchText = action.SearchText ?? string.Empty;
        RowOpacity = action.IsEnabled ? 1.0 : 0.55;
        IsCommand = true;
        IsHeader = false;
    }

    private CommandPaletteItemViewModel(string category)
    {
        Id = $"header:{category}";
        Title = category;
        Detail = string.Empty;
        Category = category;
        IconKey = null;
        DefaultShortcut = string.Empty;
        IsEnabled = false;
        DisabledReason = string.Empty;
        SearchText = string.Empty;
        RowOpacity = 1.0;
        IsCommand = false;
        IsHeader = true;
    }

    public string Id { get; }

    public string Title { get; }

    public string Detail { get; }

    public string Category { get; }

    public string? IconKey { get; }

    public string DefaultShortcut { get; }

    public bool HasDefaultShortcut => !string.IsNullOrWhiteSpace(DefaultShortcut);

    public bool IsEnabled { get; }

    public string DisabledReason { get; }

    public bool HasDisabledReason => !string.IsNullOrWhiteSpace(DisabledReason);

    public string SearchText { get; }

    public double RowOpacity { get; }

    public bool IsCommand { get; }

    public bool IsHeader { get; }

    internal static CommandPaletteItemViewModel CreateHeader(string category)
    {
        return new CommandPaletteItemViewModel(category);
    }
}
