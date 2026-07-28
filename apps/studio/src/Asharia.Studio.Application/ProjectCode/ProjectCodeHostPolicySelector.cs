using System;
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeHostPolicySelector
{
    private const string Schema =
        "com.asharia.project-code-host-policy-v1";
    private const string PinnedHostKind = "pinned";
    private const string RestartRequiredReplacement = "restart-required";
    private const string ExternalBuildReason =
        "external-build-reload-evidence-unavailable";

    public static async Task<ProjectCodeHostPolicySelectionResult> SelectAsync(
        ProjectCodeStagingCandidateReceipt candidate,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        cancellationToken.ThrowIfCancellationRequested();
        if (!await ProjectCodeStagingCandidateAdmitter
                .IsCandidateCurrentAsync(candidate, cancellationToken)
                .ConfigureAwait(false))
        {
            return ProjectCodeHostPolicySelectionResult.Failure(
                [new ProjectCodeHostPolicyDiagnostic(
                    "project-code.host-policy.candidate-not-current",
                    "candidate",
                    "Host policy selection requires one current staging candidate.")]);
        }

        return ProjectCodeHostPolicySelectionResult.Success(
            new ProjectCodeHostPolicyReceipt(
                ComputePolicyId(candidate.CandidateId),
                candidate,
                ProjectCodeHostKind.Pinned,
                ProjectCodeReplacementPolicy.RestartRequired,
                ProjectCodeHostPolicyReason
                    .ExternalBuildReloadEvidenceUnavailable));
    }

    public static async Task<bool> IsPolicyCurrentAsync(
        ProjectCodeHostPolicyReceipt policy,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(policy);
        cancellationToken.ThrowIfCancellationRequested();
        return policy.HostKind == ProjectCodeHostKind.Pinned
            && policy.ReplacementPolicy
                == ProjectCodeReplacementPolicy.RestartRequired
            && policy.Reason
                == ProjectCodeHostPolicyReason
                    .ExternalBuildReloadEvidenceUnavailable
            && string.Equals(
                policy.PolicyId,
                ComputePolicyId(policy.Candidate.CandidateId),
                StringComparison.Ordinal)
            && await ProjectCodeStagingCandidateAdmitter
                .IsCandidateCurrentAsync(policy.Candidate, cancellationToken)
                .ConfigureAwait(false);
    }

    private static string ComputePolicyId(string candidateId)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, candidateId);
        AppendString(hash, PinnedHostKind);
        AppendString(hash, RestartRequiredReplacement);
        AppendString(hash, ExternalBuildReason);
        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static void AppendString(
        IncrementalHash hash,
        string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }
}
