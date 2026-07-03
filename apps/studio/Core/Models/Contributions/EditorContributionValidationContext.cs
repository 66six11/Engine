using System.Collections.Generic;

namespace Editor.Core.Models.Contributions;

public sealed record EditorContributionValidationContext(
    IReadOnlyCollection<string> RegisteredPanelIds,
    IReadOnlyCollection<string> RegisteredActionIds,
    IReadOnlyCollection<string> RegisteredDiagnosticSourceIds)
{
    public static EditorContributionValidationContext Empty { get; } = new(
        RegisteredPanelIds: [],
        RegisteredActionIds: [],
        RegisteredDiagnosticSourceIds: []);
}
