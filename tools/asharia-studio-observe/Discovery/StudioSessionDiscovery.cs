using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.Observe.Discovery;

internal sealed record StudioDiscoveryIssue(
    string ManifestName,
    ObservationFailure Failure);

internal sealed record StudioSessionDiscoveryResult(
    ImmutableArray<DevelopmentSessionManifest> Sessions,
    ImmutableArray<StudioDiscoveryIssue> Issues);

internal sealed record StudioSessionResolution(
    DevelopmentSessionManifest? Manifest,
    ObservationFailure? Failure);

[SupportedOSPlatform("windows")]
internal sealed class StudioSessionDiscovery
{
    internal const int MaxSessionManifests = 64;

    private const int FileBufferBytes = 4096;

    private readonly string rootDirectory_;
    private readonly SecurityIdentifier currentUser_;

    internal StudioSessionDiscovery()
        : this(DefaultRootDirectory())
    {
    }

    internal StudioSessionDiscovery(string rootDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rootDirectory);
        rootDirectory_ = Path.GetFullPath(rootDirectory);
        using var identity = WindowsIdentity.GetCurrent();
        currentUser_ = identity.User
            ?? throw new InvalidOperationException(
                "Current Windows identity does not expose a user SID.");
    }

    internal async ValueTask<StudioSessionDiscoveryResult> ListAsync(
        CancellationToken cancellationToken)
    {
        if (!Directory.Exists(rootDirectory_))
        {
            return new StudioSessionDiscoveryResult([], []);
        }

        var rootFailure = ValidateRoot();
        if (rootFailure is not null)
        {
            return new StudioSessionDiscoveryResult(
                [],
                [new StudioDiscoveryIssue("development-sessions", rootFailure)]);
        }

        var paths = new List<string>(MaxSessionManifests + 1);
        try
        {
            foreach (var path in Directory.EnumerateFiles(
                rootDirectory_,
                "*.json",
                SearchOption.TopDirectoryOnly))
            {
                paths.Add(path);
                if (paths.Count > MaxSessionManifests)
                {
                    break;
                }
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return new StudioSessionDiscoveryResult(
                [],
                [new StudioDiscoveryIssue(
                    "development-sessions",
                    Failure(
                        "observation.discovery.unavailable",
                        "unavailable",
                        "Current-user Studio discovery directory is unavailable."))]);
        }

        paths.Sort(StringComparer.Ordinal);
        var sessions = ImmutableArray.CreateBuilder<DevelopmentSessionManifest>(
            Math.Min(paths.Count, MaxSessionManifests));
        var issues = ImmutableArray.CreateBuilder<StudioDiscoveryIssue>();
        if (paths.Count > MaxSessionManifests)
        {
            issues.Add(new StudioDiscoveryIssue(
                "development-sessions",
                Failure(
                    "observation.discovery.too-many",
                    "protocol",
                    $"Discovery contains more than {MaxSessionManifests} session manifests.")));
            paths.RemoveAt(paths.Count - 1);
        }

        foreach (var path in paths)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var resolution = await ReadAndValidateAsync(path, cancellationToken)
                .ConfigureAwait(false);
            if (resolution.Manifest is not null)
            {
                sessions.Add(resolution.Manifest);
            }
            else
            {
                issues.Add(new StudioDiscoveryIssue(
                    Path.GetFileName(path),
                    resolution.Failure ?? Failure(
                        "observation.discovery.invalid",
                        "protocol",
                        "Discovery manifest was rejected.")));
            }
        }

        return new StudioSessionDiscoveryResult(
            sessions.ToImmutable(),
            issues.ToImmutable());
    }

    internal ValueTask<StudioSessionResolution> ResolveAsync(
        StudioInstanceId studioInstanceId,
        CancellationToken cancellationToken)
    {
        if (studioInstanceId.Value == Guid.Empty)
        {
            throw new ArgumentException(
                "Studio instance identity must be non-empty.",
                nameof(studioInstanceId));
        }

        if (!Directory.Exists(rootDirectory_))
        {
            return ValueTask.FromResult(Rejected(
                "observation.discovery.not-found",
                "stale",
                "Studio discovery directory was not found."));
        }

        var rootFailure = ValidateRoot();
        if (rootFailure is not null)
        {
            return ValueTask.FromResult(new StudioSessionResolution(
                Manifest: null,
                rootFailure));
        }

        var path = Path.Combine(
            rootDirectory_,
            $"{studioInstanceId.Value:D}.json");
        return ReadAndValidateAsync(path, cancellationToken);
    }

    internal static string DefaultRootDirectory()
    {
        var localApplicationData = Environment.GetFolderPath(
            Environment.SpecialFolder.LocalApplicationData,
            Environment.SpecialFolderOption.DoNotVerify);
        if (string.IsNullOrWhiteSpace(localApplicationData))
        {
            throw new DirectoryNotFoundException(
                "Current user local application data directory is unavailable.");
        }

        return Path.Combine(
            localApplicationData,
            "Asharia",
            "Studio",
            "development-sessions");
    }

    private async ValueTask<StudioSessionResolution> ReadAndValidateAsync(
        string path,
        CancellationToken cancellationToken)
    {
        FileInfo file;
        try
        {
            file = new FileInfo(path);
            if (!file.Exists)
            {
                return MissingManifest();
            }

            if ((file.Attributes & FileAttributes.ReparsePoint) != 0)
            {
                return Rejected(
                    "observation.discovery.invalid-acl",
                    "security",
                    "Studio discovery manifest cannot be a reparse point.");
            }
        }
        catch (Exception error) when (
            error is FileNotFoundException or DirectoryNotFoundException)
        {
            return MissingManifest();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return ManifestUnavailable();
        }

        var aclFailure = ValidateAcl(file);
        if (aclFailure is not null)
        {
            return new StudioSessionResolution(Manifest: null, aclFailure);
        }

        byte[] payload;
        try
        {
            await using var stream = new FileStream(
                file.FullName,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read | FileShare.Delete,
                FileBufferBytes,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length <= 0
                || stream.Length > ObservationProtocolLimits.MaxSessionManifestBytes)
            {
                return Rejected(
                    "observation.manifest.too-large",
                    "protocol",
                    "Studio discovery manifest size is outside the typed protocol bound.");
            }

            payload = new byte[(int)stream.Length];
            await stream.ReadExactlyAsync(payload, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception error) when (
            error is FileNotFoundException or DirectoryNotFoundException)
        {
            return MissingManifest();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return ManifestUnavailable();
        }

        var parsed = ObservationProtocolJson.ReadSessionManifest(payload);
        if (!parsed.Succeeded)
        {
            return new StudioSessionResolution(Manifest: null, parsed.Failure);
        }

        var manifest = parsed.Value!;
        if (!string.Equals(
                Path.GetFileNameWithoutExtension(file.Name),
                manifest.StudioInstanceId.Value.ToString("D"),
                StringComparison.OrdinalIgnoreCase))
        {
            return Rejected(
                "observation.discovery.identity-mismatch",
                "stale",
                "Studio discovery filename does not match its typed instance identity.");
        }

        var processFailure = ValidateProcessIdentity(manifest);
        return processFailure is null
            ? new StudioSessionResolution(manifest, Failure: null)
            : new StudioSessionResolution(Manifest: null, processFailure);
    }

    private ObservationFailure? ValidateAcl(FileSystemInfo entry)
    {
        FileSystemSecurity security;
        try
        {
            security = entry switch
            {
                DirectoryInfo directory =>
                    FileSystemAclExtensions.GetAccessControl(directory),
                FileInfo file => FileSystemAclExtensions.GetAccessControl(file),
                _ => throw new ArgumentOutOfRangeException(nameof(entry)),
            };
        }
        catch (Exception error) when (
            error is FileNotFoundException or DirectoryNotFoundException)
        {
            return Failure(
                "observation.discovery.not-found",
                "stale",
                "Studio discovery entry was removed while its ACL was being verified.");
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return Failure(
                "observation.discovery.invalid-acl",
                "security",
                "Studio discovery ACL could not be verified.");
        }

        var rules = security
            .GetAccessRules(
                includeExplicit: true,
                includeInherited: true,
                typeof(SecurityIdentifier))
            .Cast<FileSystemAccessRule>()
            .ToArray();
        if (!security.AreAccessRulesProtected
            || security.GetOwner(typeof(SecurityIdentifier)) is not SecurityIdentifier owner
            || owner != currentUser_
            || rules.Length != 1
            || rules[0].IsInherited
            || rules[0].AccessControlType != AccessControlType.Allow
            || rules[0].IdentityReference != currentUser_
            || (rules[0].FileSystemRights & FileSystemRights.FullControl)
                != FileSystemRights.FullControl)
        {
            return Failure(
                "observation.discovery.invalid-acl",
                "security",
                "Studio discovery ACL is not protected for exactly the current user.");
        }

        return null;
    }

    private ObservationFailure? ValidateRoot()
    {
        try
        {
            var root = new DirectoryInfo(rootDirectory_);
            if ((root.Attributes & FileAttributes.ReparsePoint) != 0)
            {
                return Failure(
                    "observation.discovery.invalid-acl",
                    "security",
                    "Studio discovery directory cannot be a reparse point.");
            }

            return ValidateAcl(root);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return Failure(
                "observation.discovery.unavailable",
                "unavailable",
                "Studio discovery directory could not be verified.");
        }
    }

    private static ObservationFailure? ValidateProcessIdentity(
        DevelopmentSessionManifest manifest)
    {
        try
        {
            using var process = Process.GetProcessById(manifest.ProcessId);
            if (process.HasExited)
            {
                return Stale();
            }

            var processStartTimeUtc = new DateTimeOffset(
                process.StartTime.ToUniversalTime(),
                TimeSpan.Zero);
            return processStartTimeUtc == manifest.ProcessStartTimeUtc
                ? null
                : Stale();
        }
        catch (ArgumentException)
        {
            return Stale();
        }
        catch (InvalidOperationException)
        {
            return Stale();
        }
        catch (System.ComponentModel.Win32Exception)
        {
            return Failure(
                "observation.discovery.process-unavailable",
                "unavailable",
                "Studio process identity could not be verified.");
        }
    }

    private static StudioSessionResolution Rejected(
        string code,
        string category,
        string message) =>
        new(Manifest: null, Failure(code, category, message));

    private static StudioSessionResolution MissingManifest() =>
        Rejected(
            "observation.discovery.not-found",
            "stale",
            "Studio discovery manifest was not found.");

    private static StudioSessionResolution ManifestUnavailable() =>
        Rejected(
            "observation.discovery.unavailable",
            "unavailable",
            "Studio discovery manifest could not be read.");

    private static ObservationFailure Stale() =>
        Failure(
            "observation.discovery.stale",
            "stale",
            "Studio discovery manifest refers to a process that is no longer current.");

    private static ObservationFailure Failure(
        string code,
        string category,
        string message) =>
        new(code, category, message, Retryable: false);
}
