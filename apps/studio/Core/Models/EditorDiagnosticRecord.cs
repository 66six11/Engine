namespace Editor.Core.Models;

public sealed record EditorDiagnosticRecord(
    long SequenceId,
    EditorDiagnosticSeverity Severity,
    EditorDiagnosticChannel Channel,
    string Source,
    string Category,
    string Message);
