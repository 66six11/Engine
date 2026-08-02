using System;
using System.IO;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Transport;

[SupportedOSPlatform("windows")]
internal sealed class CurrentUserManifestStore
{
    private const int FileBufferBytes = 4096;

    private readonly string rootDirectory_;
    private readonly SecurityIdentifier currentUser_;

    public CurrentUserManifestStore(
        string rootDirectory,
        StudioInstanceId studioInstanceId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rootDirectory);
        if (studioInstanceId.Value == Guid.Empty)
        {
            throw new ArgumentException(
                "Studio instance identity must be non-empty.",
                nameof(studioInstanceId));
        }

        rootDirectory_ = Path.GetFullPath(rootDirectory);
        using var identity = WindowsIdentity.GetCurrent();
        currentUser_ = identity.User
            ?? throw new InvalidOperationException(
                "Current Windows identity does not expose a user SID.");
        ManifestPath = Path.Combine(
            rootDirectory_,
            $"{studioInstanceId.Value:D}.json");
        if (!string.Equals(
                Path.GetDirectoryName(Path.GetFullPath(ManifestPath)),
                rootDirectory_,
                StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                "Development session manifest escaped its configured root.");
        }
    }

    public string ManifestPath { get; }

    public static string DefaultRootDirectory()
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

    public async ValueTask PublishAsync(
        DevelopmentSessionManifest manifest,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        if (manifest.StudioInstanceId.Value.ToString("D")
            != Path.GetFileNameWithoutExtension(ManifestPath))
        {
            throw new InvalidOperationException(
                "Manifest identity does not match its owner-scoped filename.");
        }

        cancellationToken.ThrowIfCancellationRequested();
        EnsureProtectedDirectory();
        var payload = ObservationProtocolJson.WriteSessionManifest(manifest);
        var temporaryPath = Path.Combine(
            rootDirectory_,
            $".{Path.GetFileName(ManifestPath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            await using (var stream = FileSystemAclExtensions.Create(
                new FileInfo(temporaryPath),
                FileMode.CreateNew,
                FileSystemRights.Write,
                FileShare.None,
                FileBufferBytes,
                FileOptions.Asynchronous | FileOptions.WriteThrough,
                CreateFileSecurity()))
            {
                await stream.WriteAsync(payload, cancellationToken)
                    .ConfigureAwait(false);
                await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
                stream.Flush(flushToDisk: true);
            }

            cancellationToken.ThrowIfCancellationRequested();
            if (File.Exists(ManifestPath))
            {
                File.Replace(
                    temporaryPath,
                    ManifestPath,
                    destinationBackupFileName: null,
                    ignoreMetadataErrors: true);
            }
            else
            {
                File.Move(temporaryPath, ManifestPath);
            }

            FileSystemAclExtensions.SetAccessControl(
                new FileInfo(ManifestPath),
                CreateFileSecurity());
        }
        finally
        {
            if (File.Exists(temporaryPath))
            {
                File.Delete(temporaryPath);
            }
        }
    }

    public void Remove()
    {
        if (!File.Exists(ManifestPath))
        {
            return;
        }

        File.Delete(ManifestPath);
    }

    private void EnsureProtectedDirectory()
    {
        var security = CreateDirectorySecurity();
        var directory = FileSystemAclExtensions.CreateDirectory(
            security,
            rootDirectory_);
        if ((directory.Attributes & FileAttributes.ReparsePoint) != 0)
        {
            throw new IOException(
                "Development session manifest root cannot be a reparse point.");
        }

        FileSystemAclExtensions.SetAccessControl(directory, security);
    }

    private DirectorySecurity CreateDirectorySecurity()
    {
        var security = new DirectorySecurity();
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        security.AddAccessRule(new FileSystemAccessRule(
            currentUser_,
            FileSystemRights.FullControl,
            InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
            PropagationFlags.None,
            AccessControlType.Allow));
        return security;
    }

    private FileSecurity CreateFileSecurity()
    {
        var security = new FileSecurity();
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        security.AddAccessRule(new FileSystemAccessRule(
            currentUser_,
            FileSystemRights.FullControl,
            AccessControlType.Allow));
        return security;
    }
}
