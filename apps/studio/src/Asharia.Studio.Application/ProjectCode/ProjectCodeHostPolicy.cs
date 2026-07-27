using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Application.ProjectCode;

internal enum ProjectCodeHostKind
{
    Pinned,
}

internal enum ProjectCodeReplacementPolicy
{
    RestartRequired,
}

internal enum ProjectCodeHostPolicyReason
{
    ExternalBuildReloadEvidenceUnavailable,
}

internal sealed class ProjectCodeHostPolicyReceipt
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeHostPolicyReceipt(
        string policyId,
        ProjectCodeStagingCandidateReceipt candidate,
        ProjectCodeHostKind hostKind,
        ProjectCodeReplacementPolicy replacementPolicy,
        ProjectCodeHostPolicyReason reason)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(policyId);
        ArgumentNullException.ThrowIfNull(candidate);
        if (!IdentityPattern.IsMatch(policyId))
        {
            throw new ArgumentException(
                "Host policy identity must be one canonical SHA-256 identity.",
                nameof(policyId));
        }

        if (hostKind != ProjectCodeHostKind.Pinned)
        {
            throw new ArgumentOutOfRangeException(nameof(hostKind));
        }

        if (replacementPolicy != ProjectCodeReplacementPolicy.RestartRequired)
        {
            throw new ArgumentOutOfRangeException(nameof(replacementPolicy));
        }

        if (reason
            != ProjectCodeHostPolicyReason.ExternalBuildReloadEvidenceUnavailable)
        {
            throw new ArgumentOutOfRangeException(nameof(reason));
        }

        PolicyId = policyId;
        Candidate = candidate;
        HostKind = hostKind;
        ReplacementPolicy = replacementPolicy;
        Reason = reason;
    }

    public string PolicyId { get; }

    public ProjectCodeStagingCandidateReceipt Candidate { get; }

    public ProjectCodeHostKind HostKind { get; }

    public ProjectCodeReplacementPolicy ReplacementPolicy { get; }

    public ProjectCodeHostPolicyReason Reason { get; }
}

internal sealed record ProjectCodeHostPolicyDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeHostPolicySelectionResult
{
    private ProjectCodeHostPolicySelectionResult(
        ProjectCodeHostPolicyReceipt? receipt,
        IReadOnlyList<ProjectCodeHostPolicyDiagnostic> diagnostics)
    {
        Receipt = receipt;
        Diagnostics = diagnostics;
    }

    public ProjectCodeHostPolicyReceipt? Receipt { get; }

    public IReadOnlyList<ProjectCodeHostPolicyDiagnostic> Diagnostics { get; }

    public bool Succeeded => Receipt is not null && Diagnostics.Count == 0;

    internal static ProjectCodeHostPolicySelectionResult Success(
        ProjectCodeHostPolicyReceipt receipt)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        return new(receipt, []);
    }

    internal static ProjectCodeHostPolicySelectionResult Failure(
        IEnumerable<ProjectCodeHostPolicyDiagnostic> diagnostics)
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
                "Failed host policy selection requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
