using Asharia.Editor.Diagnostics;

namespace Editor.Core.Models.Diagnostics;

public sealed record EditorDiagnosticRecord(
    long SequenceId,
    EditorDiagnosticSeverity Severity,
    EditorDiagnosticChannel Channel,
    string Source,
    string Category,
    string Message);
