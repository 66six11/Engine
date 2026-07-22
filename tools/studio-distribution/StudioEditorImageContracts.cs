using System.Collections.ObjectModel;

namespace Asharia.Studio.Distribution;

public sealed record StudioEditorImageProductionRequest(
    DirectoryInfo PublishRoot,
    string EntryPoint,
    DirectoryInfo DotnetRoot,
    string SdkVersion,
    string HostFxrVersion,
    string HostRuntimeVersion,
    string ReferencePackVersion,
    FileInfo RuntimeContract,
    FileInfo EditorContract,
    DirectoryInfo OutputRoot,
    string EnvironmentId = "project-code-net10",
    string TargetFramework = "net10.0");

public sealed record StudioEditorImageFileBinding(
    string Path,
    string Role,
    string MediaType,
    long Size,
    string Sha256);

public sealed record StudioEditorImageProductionReceipt(
    string Root,
    string EntryPoint,
    IReadOnlyList<StudioEditorImageFileBinding> Files);

public sealed record StudioEditorImageProductionDiagnostic(
    string Code,
    string Location,
    string Message);

public sealed class StudioEditorImageProductionResult
{
    private StudioEditorImageProductionResult(
        StudioEditorImageProductionReceipt? receipt,
        IReadOnlyList<StudioEditorImageProductionDiagnostic> diagnostics)
    {
        Receipt = receipt;
        Diagnostics = diagnostics;
    }

    public StudioEditorImageProductionReceipt? Receipt { get; }

    public IReadOnlyList<StudioEditorImageProductionDiagnostic> Diagnostics { get; }

    public bool Succeeded => Receipt is not null && Diagnostics.Count == 0;

    internal static StudioEditorImageProductionResult Success(
        string root,
        string entryPoint,
        IEnumerable<StudioEditorImageFileBinding> files)
    {
        var snapshot = files.ToArray();
        return new StudioEditorImageProductionResult(
            new StudioEditorImageProductionReceipt(
                root,
                entryPoint,
                new ReadOnlyCollection<StudioEditorImageFileBinding>(snapshot)),
            Array.Empty<StudioEditorImageProductionDiagnostic>());
    }

    internal static StudioEditorImageProductionResult Failure(
        params StudioEditorImageProductionDiagnostic[] diagnostics) =>
        new(
            null,
            new ReadOnlyCollection<StudioEditorImageProductionDiagnostic>(diagnostics));
}
