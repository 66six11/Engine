using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodeArtifactPublicationReceipt
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeArtifactPublicationReceipt(
        string publicationId,
        string absoluteRoot,
        ProjectCodeArtifactMetadataReport report,
        ProjectCodeArtifactFileEvidence manifest,
        ProjectCodeArtifactFileEvidence implementation,
        ProjectCodeArtifactFileEvidence referenceAssembly,
        ProjectCodeArtifactFileEvidence portablePdb,
        ProjectCodeArtifactFileEvidence dependencies)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(publicationId);
        ArgumentException.ThrowIfNullOrWhiteSpace(absoluteRoot);
        ArgumentNullException.ThrowIfNull(report);
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentNullException.ThrowIfNull(implementation);
        ArgumentNullException.ThrowIfNull(referenceAssembly);
        ArgumentNullException.ThrowIfNull(portablePdb);
        ArgumentNullException.ThrowIfNull(dependencies);
        if (!IdentityPattern.IsMatch(publicationId))
        {
            throw new ArgumentException(
                "Publication identity must be one canonical SHA-256 identity.",
                nameof(publicationId));
        }

        if (!Path.IsPathFullyQualified(absoluteRoot))
        {
            throw new ArgumentException(
                "Publication root must be fully qualified.",
                nameof(absoluteRoot));
        }

        var files = new[]
        {
            manifest,
            implementation,
            referenceAssembly,
            portablePdb,
            dependencies,
        };
        if (files
            .GroupBy(
                file => file.RelativePath,
                StringComparer.OrdinalIgnoreCase)
            .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Publication receipt requires five unique file paths.");
        }

        PublicationId = publicationId;
        AbsoluteRoot = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(absoluteRoot));
        Report = report;
        Manifest = manifest;
        Implementation = implementation;
        ReferenceAssembly = referenceAssembly;
        PortablePdb = portablePdb;
        Dependencies = dependencies;
    }

    public string PublicationId { get; }

    public string AbsoluteRoot { get; }

    public ProjectCodeArtifactMetadataReport Report { get; }

    public ProjectCodeArtifactFileEvidence Manifest { get; }

    public ProjectCodeArtifactFileEvidence Implementation { get; }

    public ProjectCodeArtifactFileEvidence ReferenceAssembly { get; }

    public ProjectCodeArtifactFileEvidence PortablePdb { get; }

    public ProjectCodeArtifactFileEvidence Dependencies { get; }

    public IReadOnlyList<ProjectCodeArtifactFileEvidence> Files =>
        [
            Manifest,
            Implementation,
            ReferenceAssembly,
            PortablePdb,
            Dependencies,
        ];
}

internal sealed record ProjectCodeArtifactPublicationDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeArtifactPublicationResult
{
    private ProjectCodeArtifactPublicationResult(
        ProjectCodeArtifactPublicationReceipt? receipt,
        IReadOnlyList<ProjectCodeArtifactPublicationDiagnostic> diagnostics)
    {
        Receipt = receipt;
        Diagnostics = diagnostics;
    }

    public ProjectCodeArtifactPublicationReceipt? Receipt { get; }

    public IReadOnlyList<ProjectCodeArtifactPublicationDiagnostic> Diagnostics
    {
        get;
    }

    public bool Succeeded => Receipt is not null && Diagnostics.Count == 0;

    internal static ProjectCodeArtifactPublicationResult Success(
        ProjectCodeArtifactPublicationReceipt receipt)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        return new(receipt, []);
    }

    internal static ProjectCodeArtifactPublicationResult Failure(
        IEnumerable<ProjectCodeArtifactPublicationDiagnostic> diagnostics)
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
                "Failed publication requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
