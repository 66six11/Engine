using System.Collections.ObjectModel;

namespace Asharia.Studio.Distribution;

public sealed record StudioEditorHostProfileProductionRequest(
    DirectoryInfo OutputRoot);

public sealed record StudioEditorHostProfileBinding(
    string Path,
    string HostKind,
    string TargetPlatform,
    long Size,
    string Sha256);

public sealed record StudioEditorHostProfileProductionReceipt(
    string Root,
    StudioEditorHostProfileBinding Profile);

public sealed record StudioEditorHostProfileProductionDiagnostic(
    string Code,
    string Location,
    string Message);

public sealed class StudioEditorHostProfileProductionResult
{
    private StudioEditorHostProfileProductionResult(
        StudioEditorHostProfileProductionReceipt? receipt,
        IReadOnlyList<StudioEditorHostProfileProductionDiagnostic> diagnostics)
    {
        Receipt = receipt;
        Diagnostics = diagnostics;
    }

    public StudioEditorHostProfileProductionReceipt? Receipt { get; }

    public IReadOnlyList<StudioEditorHostProfileProductionDiagnostic> Diagnostics { get; }

    public bool Succeeded => Receipt is not null && Diagnostics.Count == 0;

    internal static StudioEditorHostProfileProductionResult Success(
        string root,
        StudioEditorHostProfileBinding profile) =>
        new(
            new StudioEditorHostProfileProductionReceipt(root, profile),
            Array.Empty<StudioEditorHostProfileProductionDiagnostic>());

    internal static StudioEditorHostProfileProductionResult Failure(
        params StudioEditorHostProfileProductionDiagnostic[] diagnostics) =>
        new(
            null,
            new ReadOnlyCollection<StudioEditorHostProfileProductionDiagnostic>(
                diagnostics));
}
