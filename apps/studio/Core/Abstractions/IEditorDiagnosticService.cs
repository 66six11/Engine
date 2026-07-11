using System;
using System.Collections.Generic;
using Asharia.Editor.Diagnostics;
using Editor.Core.Models.Diagnostics;

namespace Editor.Core.Abstractions;

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
