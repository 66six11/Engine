using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodeStagingCandidateReceipt
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeStagingCandidateReceipt(
        string candidateId,
        ProjectCodeArtifactPublicationReceipt publication,
        ProjectCodeModuleIndexReport moduleIndex)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(candidateId);
        ArgumentNullException.ThrowIfNull(publication);
        ArgumentNullException.ThrowIfNull(moduleIndex);
        if (!IdentityPattern.IsMatch(candidateId))
        {
            throw new ArgumentException(
                "Staging candidate identity must be one canonical SHA-256 identity.",
                nameof(candidateId));
        }

        if (moduleIndex.Entries.Count == 0)
        {
            throw new ArgumentException(
                "Staging candidate requires at least one declared Editor module.",
                nameof(moduleIndex));
        }

        if (!string.Equals(
                publication.PublicationId,
                moduleIndex.PublicationId,
                StringComparison.Ordinal)
            || publication.Report.ProjectId != moduleIndex.ProjectId
            || !string.Equals(
                publication.Report.AssemblyName,
                moduleIndex.AssemblyName,
                StringComparison.Ordinal))
        {
            throw new ArgumentException(
                "Staging candidate publication and module index identities differ.",
                nameof(moduleIndex));
        }

        CandidateId = candidateId;
        Publication = publication;
        ModuleIndex = moduleIndex;
    }

    public string CandidateId { get; }

    public ProjectCodeArtifactPublicationReceipt Publication { get; }

    public ProjectCodeModuleIndexReport ModuleIndex { get; }
}

internal sealed record ProjectCodeStagingCandidateDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeStagingCandidateAdmissionResult
{
    private ProjectCodeStagingCandidateAdmissionResult(
        ProjectCodeStagingCandidateReceipt? receipt,
        IReadOnlyList<ProjectCodeStagingCandidateDiagnostic> diagnostics)
    {
        Receipt = receipt;
        Diagnostics = diagnostics;
    }

    public ProjectCodeStagingCandidateReceipt? Receipt { get; }

    public IReadOnlyList<ProjectCodeStagingCandidateDiagnostic> Diagnostics
    {
        get;
    }

    public bool Succeeded => Receipt is not null && Diagnostics.Count == 0;

    internal static ProjectCodeStagingCandidateAdmissionResult Success(
        ProjectCodeStagingCandidateReceipt receipt)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        return new(receipt, []);
    }

    internal static ProjectCodeStagingCandidateAdmissionResult Failure(
        IEnumerable<ProjectCodeStagingCandidateDiagnostic> diagnostics)
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
                "Failed staging candidate admission requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
