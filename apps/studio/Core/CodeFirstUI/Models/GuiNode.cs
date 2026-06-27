using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI;

public sealed record GuiNode(
    GuiNodeId Id,
    GuiNodeKind Kind,
    string? Label,
    IReadOnlyList<GuiNode> Children);
