using System;
using System.Buffers.Binary;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeStagingCandidateAdmitter
{
    private const string Schema =
        "com.asharia.project-code-staging-candidate-v1";

    public static async Task<ProjectCodeStagingCandidateAdmissionResult>
        AdmitAsync(
            ProjectCodeArtifactPublicationReceipt publication,
            CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(publication);
        cancellationToken.ThrowIfCancellationRequested();

        var indexResult = await ProjectCodeModuleIndexer
            .IndexAsync(publication, cancellationToken)
            .ConfigureAwait(false);
        if (!indexResult.Succeeded)
        {
            return ProjectCodeStagingCandidateAdmissionResult.Failure(
                indexResult.Diagnostics.Select(diagnostic =>
                    new ProjectCodeStagingCandidateDiagnostic(
                        diagnostic.Code,
                        diagnostic.Location,
                        diagnostic.Message)));
        }

        var moduleIndex = indexResult.Report!;
        if (moduleIndex.Entries.Count == 0)
        {
            return Failure(
                "project-code.staging-candidate.modules-empty",
                "module-index",
                "Staging candidate requires at least one declared Editor module.");
        }

        var candidate = new ProjectCodeStagingCandidateReceipt(
            ComputeCandidateId(
                publication.PublicationId,
                moduleIndex.IndexId),
            publication,
            moduleIndex);
        if (!await ProjectCodeArtifactPublisher
                .IsPublicationCurrentAsync(publication, cancellationToken)
                .ConfigureAwait(false))
        {
            return Failure(
                "project-code.staging-candidate.publication-changed",
                "publication",
                "Artifact publication changed while the staging candidate was admitted.");
        }

        return ProjectCodeStagingCandidateAdmissionResult.Success(candidate);
    }

    public static async Task<bool> IsCandidateCurrentAsync(
        ProjectCodeStagingCandidateReceipt candidate,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(candidate);
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(
                candidate.CandidateId,
                ComputeCandidateId(
                    candidate.Publication.PublicationId,
                    candidate.ModuleIndex.IndexId),
                StringComparison.Ordinal))
        {
            return false;
        }

        var currentIndex = await ProjectCodeModuleIndexer
            .IndexAsync(candidate.Publication, cancellationToken)
            .ConfigureAwait(false);
        return currentIndex.Succeeded
            && currentIndex.Report!.Entries.Count != 0
            && HasSameIndex(candidate.ModuleIndex, currentIndex.Report);
    }

    private static bool HasSameIndex(
        ProjectCodeModuleIndexReport left,
        ProjectCodeModuleIndexReport right) =>
        string.Equals(left.IndexId, right.IndexId, StringComparison.Ordinal)
        && string.Equals(
            left.PublicationId,
            right.PublicationId,
            StringComparison.Ordinal)
        && left.ProjectId == right.ProjectId
        && string.Equals(
            left.AssemblyName,
            right.AssemblyName,
            StringComparison.Ordinal)
        && left.Entries.SequenceEqual(right.Entries);

    private static string ComputeCandidateId(
        string publicationId,
        string indexId)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, publicationId);
        AppendString(hash, indexId);
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

    private static ProjectCodeStagingCandidateAdmissionResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodeStagingCandidateAdmissionResult.Failure(
            [new ProjectCodeStagingCandidateDiagnostic(
                code,
                location,
                message)]);
}
