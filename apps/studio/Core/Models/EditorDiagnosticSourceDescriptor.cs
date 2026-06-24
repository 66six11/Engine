namespace Editor.Core.Models;

public sealed record EditorDiagnosticSourceDescriptor(
    string Id,
    string Title,
    EditorDiagnosticChannel DefaultChannel,
    EditorContributionSourceKind SourceKind);
