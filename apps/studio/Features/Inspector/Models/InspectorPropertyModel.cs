namespace Editor.Features.Inspector.Models;

public sealed record InspectorPropertyModel(
    string Name,
    string Value,
    InspectorPropertyValueKind ValueKind = InspectorPropertyValueKind.Text);
