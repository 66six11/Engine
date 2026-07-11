using System;
using System.Collections.Generic;

namespace Asharia.Editor.Diagnostics;

public interface IEditorDiagnosticService
{
    event EventHandler? DiagnosticsChanged;

    EditorDiagnosticRecord Publish(
        EditorDiagnosticSeverity severity,
        EditorDiagnosticChannel channel,
        string source,
        string category,
        string message);

    IReadOnlyList<EditorDiagnosticRecord> GetRecentDiagnostics();

    IReadOnlyList<EditorDiagnosticRecord> GetProblemDiagnostics();

    EditorDiagnosticRecord? GetLatestDiagnostic();
}
