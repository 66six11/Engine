using Editor.Core.Models.Contributions;

namespace Editor.Core.Models.Diagnostics;

public sealed record EditorDiagnosticSourceDescriptor(
    string Id,
    string Title,
    EditorDiagnosticChannel DefaultChannel,
    EditorContributionSourceKind SourceKind);
