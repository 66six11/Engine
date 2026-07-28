namespace Asharia.Editor.Projects;

public sealed record ProjectOpenSessionDiagnosticSnapshot(
    string Code,
    string ManifestPath,
    string Pointer,
    string Message);
