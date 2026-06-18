using System.Collections.Generic;

namespace Editor.Features.Inspector.Models;

public sealed record InspectorDocumentModel(
    string Title,
    string Subtitle,
    int SelectionCount,
    IReadOnlyList<InspectorSectionModel> Sections)
{
    public bool IsMultiSelection => SelectionCount > 1;
}
