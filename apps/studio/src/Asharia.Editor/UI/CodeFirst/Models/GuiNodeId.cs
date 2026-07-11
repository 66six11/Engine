
namespace Asharia.Editor.UI.CodeFirst.Models;

public sealed record GuiNodeId(
    string PanelId,
    string KeyPath,
    GuiNodeKind Kind)
{
    public string FullKeyPath => string.IsNullOrWhiteSpace(KeyPath)
        ? PanelId
        : $"{PanelId}/{KeyPath}";
}
