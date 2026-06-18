using System.Collections.Generic;

namespace Editor.Features.Inspector.Models;

public sealed record InspectorSectionModel(
    string Title,
    IReadOnlyList<InspectorPropertyModel> Properties);
