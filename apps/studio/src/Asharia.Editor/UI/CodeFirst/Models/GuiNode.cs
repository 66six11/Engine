using System.Collections.Generic;

namespace Asharia.Editor.UI.CodeFirst.Models;

public sealed record GuiNode(
    GuiNodeId Id,
    GuiNodeKind Kind,
    string? Label,
    GuiNodePayload Payload,
    IReadOnlyList<GuiNode> Children);
