using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodePinnedLoadImageBuilder
{
    private const int MaxFileBytes = 256 * 1024 * 1024;
    private const string Schema =
        "com.asharia.project-code-pinned-load-image-v1";

    public static async Task<ProjectCodePinnedLoadImageResult> BuildAsync(
        ProjectCodeHostPolicyReceipt policy,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(policy);
        cancellationToken.ThrowIfCancellationRequested();
        if (!await ProjectCodeHostPolicySelector
                .IsPolicyCurrentAsync(policy, cancellationToken)
                .ConfigureAwait(false))
        {
            return Failure(
                "project-code.pinned-load-image.policy-not-current",
                "policy",
                "Pinned load image requires one current host policy.");
        }

        var diagnostics = new List<ProjectCodePinnedLoadImageDiagnostic>();
        var publication = policy.Candidate.Publication;
        var implementation = await ReadVerifiedAsync(
                publication,
                publication.Implementation,
                "implementation",
                diagnostics,
                cancellationToken)
            .ConfigureAwait(false);
        var portablePdb = await ReadVerifiedAsync(
                publication,
                publication.PortablePdb,
                "portable-pdb",
                diagnostics,
                cancellationToken)
            .ConfigureAwait(false);
        if (implementation is null || portablePdb is null)
        {
            return ProjectCodePinnedLoadImageResult.Failure(diagnostics);
        }

        try
        {
            using var implementationStream = new MemoryStream(
                implementation,
                writable: false);
            if (HasModuleInitializer(implementationStream))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.pinned-load-image.module-initializer-unsupported",
                    "implementation",
                    "Pinned load image cannot contain a CLR module initializer."));
            }
        }
        catch (Exception error) when (
            error is BadImageFormatException
                or InvalidDataException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.pinned-load-image.assembly-invalid",
                "implementation",
                "Pinned load image assembly metadata is invalid."));
        }

        if (diagnostics.Count != 0)
        {
            return ProjectCodePinnedLoadImageResult.Failure(diagnostics);
        }

        if (!await ProjectCodeHostPolicySelector
                .IsPolicyCurrentAsync(policy, cancellationToken)
                .ConfigureAwait(false))
        {
            return Failure(
                "project-code.pinned-load-image.policy-changed",
                "policy",
                "Host policy changed while the pinned load image was captured.");
        }

        return ProjectCodePinnedLoadImageResult.Success(
            new ProjectCodePinnedLoadImageSnapshot(
                ComputeImageId(policy),
                policy,
                implementation,
                portablePdb));
    }

    public static async Task<bool> IsSnapshotCurrentAsync(
        ProjectCodePinnedLoadImageSnapshot snapshot,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            using var implementation =
                snapshot.OpenImplementationStream();
            return string.Equals(
                    snapshot.ImageId,
                    ComputeImageId(snapshot.Policy),
                    StringComparison.Ordinal)
                && snapshot.OwnedBytesMatchEvidence()
                && !HasModuleInitializer(implementation)
                && await ProjectCodeHostPolicySelector
                    .IsPolicyCurrentAsync(snapshot.Policy, cancellationToken)
                    .ConfigureAwait(false);
        }
        catch (Exception error) when (
            error is BadImageFormatException
                or InvalidDataException)
        {
            return false;
        }
    }

    private static async Task<byte[]?> ReadVerifiedAsync(
        ProjectCodeArtifactPublicationReceipt publication,
        ProjectCodeArtifactFileEvidence evidence,
        string location,
        ICollection<ProjectCodePinnedLoadImageDiagnostic> diagnostics,
        CancellationToken cancellationToken)
    {
        if (evidence.Size > MaxFileBytes || evidence.Size > int.MaxValue)
        {
            diagnostics.Add(Diagnostic(
                "project-code.pinned-load-image.file-budget-exceeded",
                location,
                "Pinned load image file exceeds the fixed read budget."));
            return null;
        }

        if (!TryResolveFile(publication.AbsoluteRoot, evidence, out var path))
        {
            diagnostics.Add(Diagnostic(
                "project-code.pinned-load-image.file-invalid",
                location,
                "Pinned load image file is missing or unsafe."));
            return null;
        }

        try
        {
            await using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                1024 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length != evidence.Size)
            {
                throw new InvalidDataException();
            }

            var bytes = new byte[checked((int)evidence.Size)];
            await stream.ReadExactlyAsync(
                bytes,
                cancellationToken).ConfigureAwait(false);
            if (await stream.ReadAsync(
                    new byte[1],
                    cancellationToken).ConfigureAwait(false) != 0
                || stream.Length != evidence.Size
                || !string.Equals(
                    Convert.ToHexString(
                        SHA256.HashData(bytes)).ToLowerInvariant(),
                    evidence.Sha256,
                    StringComparison.Ordinal))
            {
                throw new InvalidDataException();
            }

            return bytes;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception error) when (
            error is IOException
                or InvalidDataException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.pinned-load-image.file-changed",
                location,
                "Pinned load image file changed or became unreadable."));
            return null;
        }
    }

    private static bool HasModuleInitializer(Stream implementation)
    {
        using var peReader = new PEReader(
            implementation,
            PEStreamOptions.LeaveOpen);
        if (!peReader.HasMetadata
            || peReader.PEHeaders.CorHeader is null)
        {
            throw new BadImageFormatException();
        }

        var reader = peReader.GetMetadataReader(
            MetadataReaderOptions.None);
        foreach (var handle in reader.TypeDefinitions)
        {
            var type = reader.GetTypeDefinition(handle);
            if (!string.Equals(
                    reader.GetString(type.Name),
                    "<Module>",
                    StringComparison.Ordinal)
                || !string.IsNullOrEmpty(
                    reader.GetString(type.Namespace))
                || !type.BaseType.IsNil)
            {
                continue;
            }

            return type.GetMethods().Any(method =>
                string.Equals(
                    reader.GetString(
                        reader.GetMethodDefinition(method).Name),
                    ".cctor",
                    StringComparison.Ordinal));
        }

        throw new InvalidDataException();
    }

    private static bool TryResolveFile(
        string root,
        ProjectCodeArtifactFileEvidence evidence,
        out string path)
    {
        path = "";
        try
        {
            var relativePath = evidence.RelativePath.Replace(
                '/',
                Path.DirectorySeparatorChar);
            var candidate = Path.GetFullPath(
                Path.Combine(root, relativePath));
            var relative = Path.GetRelativePath(root, candidate);
            if (Path.IsPathRooted(relative)
                || relative == ".."
                || relative.StartsWith(
                    $"..{Path.DirectorySeparatorChar}",
                    StringComparison.Ordinal))
            {
                return false;
            }

            var current = root;
            foreach (var segment in relative.Split(
                         Path.DirectorySeparatorChar))
            {
                current = Path.Combine(current, segment);
                if ((File.GetAttributes(current)
                    & FileAttributes.ReparsePoint) != 0)
                {
                    return false;
                }
            }

            if (!File.Exists(candidate)
                || (File.GetAttributes(candidate)
                    & FileAttributes.Directory) != 0)
            {
                return false;
            }

            path = candidate;
            return true;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static string ComputeImageId(
        ProjectCodeHostPolicyReceipt policy)
    {
        var publication = policy.Candidate.Publication;
        using var hash = IncrementalHash.CreateHash(
            HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, policy.PolicyId);
        AppendEvidence(hash, "implementation", publication.Implementation);
        AppendEvidence(hash, "portable-pdb", publication.PortablePdb);
        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static void AppendEvidence(
        IncrementalHash hash,
        string role,
        ProjectCodeArtifactFileEvidence evidence)
    {
        AppendString(hash, role);
        Span<byte> size = stackalloc byte[sizeof(long)];
        BinaryPrimitives.WriteInt64LittleEndian(size, evidence.Size);
        hash.AppendData(size);
        AppendString(hash, evidence.Sha256);
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

    private static ProjectCodePinnedLoadImageResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodePinnedLoadImageResult.Failure(
            [Diagnostic(code, location, message)]);

    private static ProjectCodePinnedLoadImageDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);
}
