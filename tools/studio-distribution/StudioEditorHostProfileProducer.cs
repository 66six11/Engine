using System.Security.Cryptography;

namespace Asharia.Studio.Distribution;

public static class StudioEditorHostProfileProducer
{
    private static readonly StringComparison FileSystemPathComparison =
        OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;

    public static StudioEditorHostProfileProductionResult Produce(
        StudioEditorHostProfileProductionRequest request)
    {
        string? stagingRoot = null;
        var committed = false;
        try
        {
            var outputRoot = ResolveOutputRoot(request);
            var outputParent = Path.GetDirectoryName(outputRoot)
                ?? throw new InvalidDataException(
                    "The Editor Host Profile output root has no parent directory.");
            var payload = StudioEditorHostProfilePayload.ReadExactBytes();
            var expectedSha256 = Hash(payload);

            stagingRoot = Path.Combine(
                outputParent,
                $".editor-host-profile-staging-{Guid.NewGuid():N}");
            Directory.CreateDirectory(stagingRoot);
            var stagedProfile = ResolveProfilePath(stagingRoot);
            Directory.CreateDirectory(Path.GetDirectoryName(stagedProfile)!);
            using (var output = new FileStream(
                stagedProfile,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.None))
            {
                output.Write(payload);
            }

            VerifyClosedRoot(stagingRoot, payload, expectedSha256);
            Directory.Move(stagingRoot, outputRoot);
            committed = true;
            VerifyClosedRoot(outputRoot, payload, expectedSha256);

            return StudioEditorHostProfileProductionResult.Success(
                outputRoot,
                new StudioEditorHostProfileBinding(
                    StudioEditorHostProfilePayload.RelativePath,
                    StudioEditorHostProfilePayload.HostKind,
                    StudioEditorHostProfilePayload.TargetPlatform,
                    payload.LongLength,
                    expectedSha256));
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or InvalidDataException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            if (!committed && stagingRoot is not null && Directory.Exists(stagingRoot))
            {
                try
                {
                    Directory.Delete(stagingRoot, recursive: true);
                }
                catch (Exception cleanupError) when (
                    cleanupError is IOException
                        or UnauthorizedAccessException)
                {
                    return StudioEditorHostProfileProductionResult.Failure(
                        Diagnostic(error.Message),
                        new StudioEditorHostProfileProductionDiagnostic(
                            "distribution.editor-host-profile.cleanup-failed",
                            stagingRoot,
                            "Could not remove the owned staging directory."));
                }
            }

            return StudioEditorHostProfileProductionResult.Failure(
                Diagnostic(error.Message));
        }
    }

    private static string ResolveOutputRoot(
        StudioEditorHostProfileProductionRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.OutputRoot);
        var suppliedPath = request.OutputRoot.FullName;
        if (!Path.IsPathFullyQualified(suppliedPath))
        {
            throw new InvalidDataException(
                "The Editor Host Profile output root must be absolute.");
        }

        var outputRoot = StudioDistributionPath.NormalizePublicAbsolutePath(
            suppliedPath);
        if (Directory.Exists(outputRoot) || File.Exists(outputRoot))
        {
            throw new InvalidDataException(
                "The Editor Host Profile output root must not already exist.");
        }

        var parent = Path.GetDirectoryName(outputRoot);
        if (string.IsNullOrEmpty(parent)
            || !Directory.Exists(parent)
            || HasReparsePointInPath(parent))
        {
            throw new InvalidDataException(
                "The Editor Host Profile output parent must be an existing non-reparse directory.");
        }

        return outputRoot;
    }

    private static void VerifyClosedRoot(
        string root,
        byte[] expectedBytes,
        string expectedSha256)
    {
        if (!Directory.Exists(root) || HasReparsePointInPath(root))
        {
            throw new InvalidDataException(
                "The staged Editor Host Profile root is not a regular directory tree.");
        }

        var expectedDirectories = new HashSet<string>(StringComparer.Ordinal)
        {
            "profiles",
            "profiles/editor",
        };
        var actualDirectories = Directory
            .EnumerateDirectories(root, "*", SearchOption.AllDirectories)
            .Select(path => PortableRelativePath(root, path))
            .ToHashSet(StringComparer.Ordinal);
        if (!actualDirectories.SetEquals(expectedDirectories))
        {
            throw new InvalidDataException(
                "The Editor Host Profile root contains an unexpected directory layout.");
        }

        var files = Directory
            .EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .ToArray();
        if (files.Length != 1
            || PortableRelativePath(root, files[0])
                != StudioEditorHostProfilePayload.RelativePath
            || (File.GetAttributes(files[0]) & FileAttributes.ReparsePoint) != 0)
        {
            throw new InvalidDataException(
                "The Editor Host Profile root must contain exactly the canonical profile file.");
        }

        var actualBytes = File.ReadAllBytes(files[0]);
        if (!actualBytes.AsSpan().SequenceEqual(expectedBytes)
            || Hash(actualBytes) != expectedSha256)
        {
            throw new InvalidDataException(
                "The staged Editor Host Profile bytes differ from the production policy.");
        }
    }

    private static string ResolveProfilePath(string root) =>
        Path.Combine(
            root,
            StudioEditorHostProfilePayload.RelativePath.Replace(
                '/',
                Path.DirectorySeparatorChar));

    private static string PortableRelativePath(string root, string path) =>
        Path.GetRelativePath(root, path)
            .Replace(Path.DirectorySeparatorChar, '/');

    private static string Hash(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    private static bool HasReparsePointInPath(string path)
    {
        var current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current))
        {
            if ((File.Exists(current) || Directory.Exists(current))
                && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
            {
                return true;
            }

            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrEmpty(parent)
                || string.Equals(parent, current, FileSystemPathComparison))
            {
                return false;
            }

            current = parent;
        }

        return false;
    }

    private static StudioEditorHostProfileProductionDiagnostic Diagnostic(
        string message) =>
        new(
            "distribution.editor-host-profile.production-failed",
            "outputRoot",
            message);
}
