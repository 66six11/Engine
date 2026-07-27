using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedLoadImageSnapshot
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);
    private readonly byte[] implementationBytes_;
    private readonly byte[] portablePdbBytes_;

    internal ProjectCodePinnedLoadImageSnapshot(
        string imageId,
        ProjectCodeHostPolicyReceipt policy,
        byte[] implementationBytes,
        byte[] portablePdbBytes)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(imageId);
        ArgumentNullException.ThrowIfNull(policy);
        ArgumentNullException.ThrowIfNull(implementationBytes);
        ArgumentNullException.ThrowIfNull(portablePdbBytes);
        if (!IdentityPattern.IsMatch(imageId))
        {
            throw new ArgumentException(
                "Pinned load image identity must be one canonical SHA-256 identity.",
                nameof(imageId));
        }

        if (policy.HostKind != ProjectCodeHostKind.Pinned
            || policy.ReplacementPolicy
                != ProjectCodeReplacementPolicy.RestartRequired
            || policy.Reason
                != ProjectCodeHostPolicyReason
                    .ExternalBuildReloadEvidenceUnavailable)
        {
            throw new ArgumentException(
                "Pinned load image requires the exact restart-required host policy.",
                nameof(policy));
        }

        var publication = policy.Candidate.Publication;
        if (!MatchesEvidence(implementationBytes, publication.Implementation)
            || !MatchesEvidence(portablePdbBytes, publication.PortablePdb))
        {
            throw new ArgumentException(
                "Pinned load image bytes do not match publication evidence.");
        }

        ImageId = imageId;
        Policy = policy;
        implementationBytes_ = implementationBytes;
        portablePdbBytes_ = portablePdbBytes;
    }

    public string ImageId { get; }

    public ProjectCodeHostPolicyReceipt Policy { get; }

    public long ImplementationSize => implementationBytes_.LongLength;

    public long PortablePdbSize => portablePdbBytes_.LongLength;

    public Stream OpenImplementationStream() =>
        new MemoryStream(
            implementationBytes_,
            0,
            implementationBytes_.Length,
            writable: false,
            publiclyVisible: false);

    public Stream OpenPortablePdbStream() =>
        new MemoryStream(
            portablePdbBytes_,
            0,
            portablePdbBytes_.Length,
            writable: false,
            publiclyVisible: false);

    internal bool OwnedBytesMatchEvidence()
    {
        var publication = Policy.Candidate.Publication;
        return MatchesEvidence(
                implementationBytes_,
                publication.Implementation)
            && MatchesEvidence(
                portablePdbBytes_,
                publication.PortablePdb);
    }

    private static bool MatchesEvidence(
        byte[] bytes,
        ProjectCodeArtifactFileEvidence evidence) =>
        bytes.LongLength == evidence.Size
        && string.Equals(
            Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(),
            evidence.Sha256,
            StringComparison.Ordinal);
}

internal sealed record ProjectCodePinnedLoadImageDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodePinnedLoadImageResult
{
    private ProjectCodePinnedLoadImageResult(
        ProjectCodePinnedLoadImageSnapshot? snapshot,
        IReadOnlyList<ProjectCodePinnedLoadImageDiagnostic> diagnostics)
    {
        Snapshot = snapshot;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedLoadImageSnapshot? Snapshot { get; }

    public IReadOnlyList<ProjectCodePinnedLoadImageDiagnostic> Diagnostics
    {
        get;
    }

    public bool Succeeded => Snapshot is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedLoadImageResult Success(
        ProjectCodePinnedLoadImageSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return new(snapshot, []);
    }

    internal static ProjectCodePinnedLoadImageResult Failure(
        IEnumerable<ProjectCodePinnedLoadImageDiagnostic> diagnostics)
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
                "Failed pinned load image snapshot requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
