using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class CommandPaletteItemViewModel
{
    internal CommandPaletteItemViewModel(WorkbenchActionDescriptor action)
    {
        Action = action;
        Id = action.Id;
        Title = action.Title;
        Detail = action.MenuPath;
        IconKey = action.IconKey;
    }

    internal WorkbenchActionDescriptor Action { get; }

    public string Id { get; }

    public string Title { get; }

    public string Detail { get; }

    public string? IconKey { get; }
}
