using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection.PortableExecutable;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodeArtifactFileEvidence
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeArtifactFileEvidence(
        string relativePath,
        long size,
        string sha256)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(relativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(sha256);
        if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(relativePath))
        {
            throw new ArgumentException(
                "Artifact evidence requires one portable relative path.",
                nameof(relativePath));
        }

        if (size < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(size));
        }

        if (!Sha256Pattern.IsMatch(sha256))
        {
            throw new ArgumentException(
                "Artifact evidence SHA-256 must use sixty-four lowercase hex digits.",
                nameof(sha256));
        }

        RelativePath = relativePath;
        Size = size;
        Sha256 = sha256;
    }

    public string RelativePath { get; }

    public long Size { get; }

    public string Sha256 { get; }
}

internal sealed record ProjectCodeInspectedAssembly
{
    public ProjectCodeInspectedAssembly(
        ProjectCodeArtifactFileEvidence file,
        string moduleName,
        Guid mvid,
        ProjectCodeAssemblyIdentity identity,
        IReadOnlyList<ProjectCodeAssemblyIdentity> references,
        CorFlags imageFlags,
        bool isReferenceAssembly)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentException.ThrowIfNullOrWhiteSpace(moduleName);
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(references);
        if (mvid == Guid.Empty)
        {
            throw new ArgumentException(
                "Inspected assembly requires one non-empty MVID.",
                nameof(mvid));
        }

        var referenceSnapshot = references.ToArray();
        if (referenceSnapshot.Any(reference => reference is null)
            || referenceSnapshot
                .GroupBy(
                    reference => reference.SimpleName,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Inspected assembly references require unique CLR simple names.",
                nameof(references));
        }

        File = file;
        ModuleName = moduleName;
        Mvid = mvid;
        Identity = identity;
        References = Array.AsReadOnly(referenceSnapshot);
        ImageFlags = imageFlags;
        IsReferenceAssembly = isReferenceAssembly;
    }

    public ProjectCodeArtifactFileEvidence File { get; }

    public string ModuleName { get; }

    public Guid Mvid { get; }

    public ProjectCodeAssemblyIdentity Identity { get; }

    public IReadOnlyList<ProjectCodeAssemblyIdentity> References { get; }

    public CorFlags ImageFlags { get; }

    public bool IsReferenceAssembly { get; }
}

internal sealed record ProjectCodePortablePdbMetadata
{
    public ProjectCodePortablePdbMetadata(
        ProjectCodeArtifactFileEvidence file,
        Guid contentId,
        uint stamp,
        IReadOnlyList<string> documents)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentNullException.ThrowIfNull(documents);
        if (contentId == Guid.Empty)
        {
            throw new ArgumentException(
                "Portable PDB requires one non-empty content id.",
                nameof(contentId));
        }

        var documentSnapshot = documents.ToArray();
        if (documentSnapshot.Length == 0
            || documentSnapshot.Any(string.IsNullOrWhiteSpace)
            || documentSnapshot
                .GroupBy(document => document, StringComparer.Ordinal)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Portable PDB documents must be non-empty and unique.",
                nameof(documents));
        }

        File = file;
        ContentId = contentId;
        Stamp = stamp;
        Documents = Array.AsReadOnly(documentSnapshot);
    }

    public ProjectCodeArtifactFileEvidence File { get; }

    public Guid ContentId { get; }

    public uint Stamp { get; }

    public IReadOnlyList<string> Documents { get; }
}

internal sealed record ProjectCodeDependencyMetadata
{
    public ProjectCodeDependencyMetadata(
        ProjectCodeArtifactFileEvidence file,
        string runtimeTarget,
        string library,
        string runtimeAsset)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentException.ThrowIfNullOrWhiteSpace(runtimeTarget);
        ArgumentException.ThrowIfNullOrWhiteSpace(library);
        ArgumentException.ThrowIfNullOrWhiteSpace(runtimeAsset);
        if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(runtimeAsset))
        {
            throw new ArgumentException(
                "Dependency runtime asset requires one portable relative path.",
                nameof(runtimeAsset));
        }

        File = file;
        RuntimeTarget = runtimeTarget;
        Library = library;
        RuntimeAsset = runtimeAsset;
    }

    public ProjectCodeArtifactFileEvidence File { get; }

    public string RuntimeTarget { get; }

    public string Library { get; }

    public string RuntimeAsset { get; }
}

internal sealed record ProjectCodeArtifactMetadataReport
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeArtifactMetadataReport(
        string reportId,
        string rawOutputId,
        Guid projectId,
        string workspaceId,
        string credentialId,
        string sdkVersion,
        string targetFramework,
        string assemblyName,
        ProjectCodeInspectedAssembly implementation,
        ProjectCodeInspectedAssembly referenceAssembly,
        ProjectCodePortablePdbMetadata portablePdb,
        ProjectCodeDependencyMetadata dependencies)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reportId);
        ArgumentException.ThrowIfNullOrWhiteSpace(rawOutputId);
        ArgumentException.ThrowIfNullOrWhiteSpace(workspaceId);
        ArgumentException.ThrowIfNullOrWhiteSpace(credentialId);
        ArgumentException.ThrowIfNullOrWhiteSpace(sdkVersion);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetFramework);
        ArgumentException.ThrowIfNullOrWhiteSpace(assemblyName);
        ArgumentNullException.ThrowIfNull(implementation);
        ArgumentNullException.ThrowIfNull(referenceAssembly);
        ArgumentNullException.ThrowIfNull(portablePdb);
        ArgumentNullException.ThrowIfNull(dependencies);
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Artifact report requires one non-empty project id.",
                nameof(projectId));
        }

        if (!IdentityPattern.IsMatch(reportId)
            || !IdentityPattern.IsMatch(rawOutputId))
        {
            throw new ArgumentException(
                "Artifact report identities must be canonical SHA-256 identities.");
        }

        var files = new[]
        {
            implementation.File,
            referenceAssembly.File,
            portablePdb.File,
            dependencies.File,
        };
        if (files
            .GroupBy(
                file => file.RelativePath,
                StringComparer.OrdinalIgnoreCase)
            .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Artifact report requires four unique file paths.");
        }

        ReportId = reportId;
        RawOutputId = rawOutputId;
        ProjectId = projectId;
        WorkspaceId = workspaceId;
        CredentialId = credentialId;
        SdkVersion = sdkVersion;
        TargetFramework = targetFramework;
        AssemblyName = assemblyName;
        Implementation = implementation;
        ReferenceAssembly = referenceAssembly;
        PortablePdb = portablePdb;
        Dependencies = dependencies;
    }

    public string ReportId { get; }

    public string RawOutputId { get; }

    public Guid ProjectId { get; }

    public string WorkspaceId { get; }

    public string CredentialId { get; }

    public string SdkVersion { get; }

    public string TargetFramework { get; }

    public string AssemblyName { get; }

    public ProjectCodeInspectedAssembly Implementation { get; }

    public ProjectCodeInspectedAssembly ReferenceAssembly { get; }

    public ProjectCodePortablePdbMetadata PortablePdb { get; }

    public ProjectCodeDependencyMetadata Dependencies { get; }
}

internal sealed record ProjectCodeArtifactInspectionDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeArtifactInspectionResult
{
    private ProjectCodeArtifactInspectionResult(
        ProjectCodeArtifactMetadataReport? report,
        IReadOnlyList<ProjectCodeArtifactInspectionDiagnostic> diagnostics)
    {
        Report = report;
        Diagnostics = diagnostics;
    }

    public ProjectCodeArtifactMetadataReport? Report { get; }

    public IReadOnlyList<ProjectCodeArtifactInspectionDiagnostic> Diagnostics { get; }

    public bool Succeeded => Report is not null && Diagnostics.Count == 0;

    internal static ProjectCodeArtifactInspectionResult Success(
        ProjectCodeArtifactMetadataReport report)
    {
        ArgumentNullException.ThrowIfNull(report);
        return new(report, []);
    }

    internal static ProjectCodeArtifactInspectionResult Failure(
        IEnumerable<ProjectCodeArtifactInspectionDiagnostic> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        var snapshot = diagnostics
            .Distinct()
            .OrderBy(item => item.Location, StringComparer.Ordinal)
            .ThenBy(item => item.Code, StringComparer.Ordinal)
            .ThenBy(item => item.Message, StringComparer.Ordinal)
            .ToArray();
        if (snapshot.Length == 0)
        {
            throw new ArgumentException(
                "Failed artifact inspection requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
