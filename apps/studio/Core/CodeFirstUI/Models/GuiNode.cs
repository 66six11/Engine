using System.Collections.Generic;

namespace Editor.Core.CodeFirstUI.Models;

public sealed record GuiNode(
    GuiNodeId Id,
    GuiNodeKind Kind,
    string? Label,
    GuiNodePayload Payload,
    IReadOnlyList<GuiNode> Children);
