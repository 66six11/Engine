using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedAssemblyLoader
{
    private const string Schema =
        "com.asharia.project-code-pinned-assembly-host-v1";
    private readonly SemaphoreSlim gate_ = new(1, 1);
    private readonly Func<
        AssemblyLoadContext,
        Stream,
        Stream,
        Assembly> loadAssembly_;
    private readonly Dictionary<Guid, Reservation> reservations_ = [];

    public ProjectCodePinnedAssemblyLoader()
        : this(static (context, implementation, portablePdb) =>
            context.LoadFromStream(implementation, portablePdb))
    {
    }

    internal ProjectCodePinnedAssemblyLoader(
        Func<AssemblyLoadContext, Stream, Stream, Assembly> loadAssembly)
    {
        ArgumentNullException.ThrowIfNull(loadAssembly);
        loadAssembly_ = loadAssembly;
    }

    public async Task<ProjectCodePinnedAssemblyLoadResult> LoadAsync(
        ProjectCodePinnedLoadImageSnapshot image,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(image);
        await gate_.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var projectId =
                image.Policy.Candidate.Publication.Report.ProjectId;
            if (reservations_.TryGetValue(
                    projectId,
                    out var reservation))
            {
                if (reservation.Host is not null
                    && string.Equals(
                        reservation.ImageId,
                        image.ImageId,
                        StringComparison.Ordinal))
                {
                    return ProjectCodePinnedAssemblyLoadResult.Success(
                        reservation.Host);
                }

                return Failure(
                    reservation.Host is null
                        ? "project-code.pinned-assembly-load.previous-attempt-failed"
                        : "project-code.pinned-assembly-load.restart-required",
                    "project",
                    reservation.Host is null
                        ? "A previous pinned load attempt requires a process restart."
                        : "A different pinned project image requires a process restart.");
            }

            if (!await ProjectCodePinnedLoadImageBuilder
                    .IsSnapshotCurrentAsync(image, cancellationToken)
                    .ConfigureAwait(false))
            {
                return Failure(
                    "project-code.pinned-assembly-load.image-not-current",
                    "image",
                    "Pinned assembly load requires one current load image.");
            }

            if (!DefaultEditorContractMatches(image))
            {
                return Failure(
                    "project-code.pinned-assembly-load.host-contract-mismatch",
                    "host",
                    "The process Editor contract does not match the load image.");
            }

            cancellationToken.ThrowIfCancellationRequested();
            var hostId = ComputeHostId(image);
            var loadContext = new PinnedLoadContext(hostId);
            reservation = new Reservation(image.ImageId, loadContext);
            reservations_.Add(projectId, reservation);
            try
            {
                using var implementation =
                    image.OpenImplementationStream();
                using var portablePdb = image.OpenPortablePdbStream();
                var assembly = loadAssembly_(
                    loadContext,
                    implementation,
                    portablePdb);
                var host = new ProjectCodePinnedAssemblyHost(
                    hostId,
                    image,
                    loadContext,
                    assembly);
                reservation.Complete(host);
                return ProjectCodePinnedAssemblyLoadResult.Success(host);
            }
            catch (Exception error) when (
                error is ArgumentException
                    or BadImageFormatException
                    or FileLoadException
                    or FileNotFoundException
                    or InvalidDataException
                    or NotSupportedException)
            {
                return Failure(
                    "project-code.pinned-assembly-load.failed-restart-required",
                    "image",
                    "Pinned assembly load failed after residency began; restart is required.");
            }
        }
        finally
        {
            gate_.Release();
        }
    }

    private static bool DefaultEditorContractMatches(
        ProjectCodePinnedLoadImageSnapshot image)
    {
        var assembly = typeof(EditorModule).Assembly;
        return ReferenceEquals(
                AssemblyLoadContext.GetLoadContext(assembly),
                AssemblyLoadContext.Default)
            && ProjectCodePinnedAssemblyHost.HasBindingIdentity(
                assembly.GetName(),
                image.Policy.Candidate.Publication.Report
                    .EditorContractIdentity);
    }

    private static string ComputeHostId(
        ProjectCodePinnedLoadImageSnapshot image)
    {
        using var hash = IncrementalHash.CreateHash(
            HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, image.ImageId);
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

    private static ProjectCodePinnedAssemblyLoadResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodePinnedAssemblyLoadResult.Failure(
            [new(code, location, message)]);

    private sealed class PinnedLoadContext(string name)
        : AssemblyLoadContext(name, isCollectible: false)
    {
        protected override Assembly? Load(AssemblyName assemblyName) => null;
    }

    private sealed class Reservation(
        string imageId,
        AssemblyLoadContext loadContext)
    {
        public string ImageId { get; } = imageId;

        public AssemblyLoadContext LoadContext { get; } = loadContext;

        public ProjectCodePinnedAssemblyHost? Host { get; private set; }

        public void Complete(ProjectCodePinnedAssemblyHost host)
        {
            ArgumentNullException.ThrowIfNull(host);
            if (Host is not null)
            {
                throw new InvalidOperationException(
                    "Pinned assembly reservation is already complete.");
            }

            Host = host;
        }
    }
}
