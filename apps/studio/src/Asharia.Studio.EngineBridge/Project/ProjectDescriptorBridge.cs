using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.EngineBridge.Project.Abi;

namespace Asharia.Studio.EngineBridge.Project;

public sealed class ProjectDescriptorBridge : IProjectDescriptorGateway
{
    private const int InitialResponseCapacity = 64 * 1024;
    private const int MaximumResponseCapacity = 128 * 1024;
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    private readonly IProjectNativeApi nativeApi_;

    public ProjectDescriptorBridge()
        : this(ProjectNativeLibraryApi.Instance)
    {
    }

    internal ProjectDescriptorBridge(IProjectNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        nativeApi_ = nativeApi;
    }

    public ValueTask<ProjectDescriptorOperationResult> CreateMinimalProjectAsync(
        string parentDirectory,
        string projectName,
        Guid projectId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(parentDirectory);
        ArgumentException.ThrowIfNullOrWhiteSpace(projectName);
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project id must not be empty.",
                nameof(projectId));
        }

        return new ValueTask<ProjectDescriptorOperationResult>(Task.Run(
            () => CreateCore(parentDirectory, projectName, projectId),
            cancellationToken));
    }

    public ValueTask<ProjectDescriptorOperationResult> OpenProjectAsync(
        string projectPath,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectPath);
        return new ValueTask<ProjectDescriptorOperationResult>(Task.Run(
            () => OpenCore(projectPath),
            cancellationToken));
    }

    private unsafe ProjectDescriptorOperationResult CreateCore(
        string parentDirectory,
        string projectName,
        Guid projectId)
    {
        try
        {
            var parentBytes = StrictUtf8.GetBytes(parentDirectory);
            var nameBytes = StrictUtf8.GetBytes(projectName);
            var idBytes = StrictUtf8.GetBytes(projectId.ToString("D"));
            fixed (byte* parent = parentBytes)
            fixed (byte* name = nameBytes)
            fixed (byte* id = idBytes)
            {
                var request = ProjectNativeCreateRequest.Current(
                    (nint)parent,
                    (ulong)parentBytes.Length,
                    (nint)name,
                    (ulong)nameBytes.Length,
                    (nint)id,
                    (ulong)idBytes.Length);
                return Invoke((nint response, ulong capacity, out ProjectNativeResult result) =>
                    nativeApi_.CreateMinimal(
                        in request,
                        response,
                        capacity,
                        out result,
                        ProjectNativeResult.StructSize));
            }
        }
        catch (EncoderFallbackException exception)
        {
            return InvalidInput(exception.Message);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            return NativeUnavailable(exception);
        }
    }

    private unsafe ProjectDescriptorOperationResult OpenCore(string projectPath)
    {
        try
        {
            var pathBytes = StrictUtf8.GetBytes(projectPath);
            fixed (byte* path = pathBytes)
            {
                var request = ProjectNativeOpenRequest.Current(
                    (nint)path,
                    (ulong)pathBytes.Length);
                return Invoke((nint response, ulong capacity, out ProjectNativeResult result) =>
                    nativeApi_.Open(
                        in request,
                        response,
                        capacity,
                        out result,
                        ProjectNativeResult.StructSize));
            }
        }
        catch (EncoderFallbackException exception)
        {
            return InvalidInput(exception.Message);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            return NativeUnavailable(exception);
        }
    }

    private unsafe ProjectDescriptorOperationResult Invoke(ProjectNativeCall call)
    {
        var response = new byte[InitialResponseCapacity];
        while (true)
        {
            ProjectNativeResult nativeResult;
            ProjectNativeStatus returnedStatus;
            fixed (byte* responsePointer = response)
            {
                returnedStatus = call(
                    (nint)responsePointer,
                    (ulong)response.Length,
                    out nativeResult);
            }

            if (nativeResult.Header.AbiVersion != ProjectNativeAbi.Version
                || nativeResult.Header.StructSize != ProjectNativeResult.StructSize
                || nativeResult.Status != returnedStatus)
            {
                return InvalidNativeResult(
                    "The project adapter returned inconsistent ABI metadata.");
            }

            if (returnedStatus == ProjectNativeStatus.BufferTooSmall)
            {
                if (nativeResult.RequiredByteLength <= (ulong)response.Length
                    || nativeResult.RequiredByteLength > MaximumResponseCapacity)
                {
                    return InvalidNativeResult(
                        "The project adapter returned an invalid response size.");
                }

                response = new byte[checked((int)nativeResult.RequiredByteLength)];
                continue;
            }

            if (nativeResult.RequiredByteLength > (ulong)response.Length)
            {
                return InvalidNativeResult(
                    "The project adapter response exceeds its caller-owned buffer.");
            }

            string root;
            string name;
            string idText;
            string message;
            try
            {
                root = Decode(response, nativeResult.ProjectRootUtf8);
                name = Decode(response, nativeResult.ProjectNameUtf8);
                idText = Decode(response, nativeResult.ProjectIdUtf8);
                message = Decode(response, nativeResult.MessageUtf8);
            }
            catch (Exception exception) when (
                exception is ArgumentOutOfRangeException
                    or DecoderFallbackException)
            {
                return InvalidNativeResult(
                    "The project adapter returned an invalid UTF-8 response.");
            }

            if (returnedStatus != ProjectNativeStatus.Success)
            {
                return ProjectDescriptorOperationResult.Failed(
                    new ProjectDescriptorFailure(
                        MapFailure(returnedStatus),
                        string.IsNullOrWhiteSpace(message)
                            ? $"Project adapter failed with {returnedStatus}."
                            : message));
            }

            if (string.IsNullOrWhiteSpace(root)
                || string.IsNullOrWhiteSpace(name)
                || !Guid.TryParseExact(idText, "D", out var projectId)
                || projectId == Guid.Empty
                || !string.IsNullOrEmpty(message))
            {
                return InvalidNativeResult(
                    "The project adapter returned an incomplete success response.");
            }

            return ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(root, name, projectId));
        }
    }

    private static string Decode(
        byte[] response,
        ProjectNativeTextSpan span)
    {
        if (span.ByteOffset > (ulong)response.Length
            || span.ByteLength > (ulong)response.Length - span.ByteOffset)
        {
            throw new ArgumentOutOfRangeException(nameof(span));
        }

        return span.ByteLength == 0
            ? string.Empty
            : StrictUtf8.GetString(
                response,
                checked((int)span.ByteOffset),
                checked((int)span.ByteLength));
    }

    private static ProjectDescriptorFailureKind MapFailure(
        ProjectNativeStatus status) => status switch
    {
        ProjectNativeStatus.InvalidArgument or ProjectNativeStatus.InvalidUtf8 =>
            ProjectDescriptorFailureKind.InvalidInput,
        ProjectNativeStatus.AlreadyExists => ProjectDescriptorFailureKind.AlreadyExists,
        ProjectNativeStatus.Busy => ProjectDescriptorFailureKind.Busy,
        ProjectNativeStatus.InvalidProject => ProjectDescriptorFailureKind.InvalidProject,
        ProjectNativeStatus.IoFailure => ProjectDescriptorFailureKind.IoFailure,
        ProjectNativeStatus.UnsupportedAbi => ProjectDescriptorFailureKind.NativeUnavailable,
        _ => ProjectDescriptorFailureKind.InternalError,
    };

    private static ProjectDescriptorOperationResult InvalidInput(string message) =>
        ProjectDescriptorOperationResult.Failed(
            new ProjectDescriptorFailure(
                ProjectDescriptorFailureKind.InvalidInput,
                message));

    private static ProjectDescriptorOperationResult InvalidNativeResult(string message) =>
        ProjectDescriptorOperationResult.Failed(
            new ProjectDescriptorFailure(
                ProjectDescriptorFailureKind.InternalError,
                message));

    private static ProjectDescriptorOperationResult NativeUnavailable(Exception exception) =>
        ProjectDescriptorOperationResult.Failed(
            new ProjectDescriptorFailure(
                ProjectDescriptorFailureKind.NativeUnavailable,
                $"The canonical project adapter is unavailable: {exception.Message}"));

    private static bool IsNativeBindingFailure(Exception exception) =>
        exception is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;

    private delegate ProjectNativeStatus ProjectNativeCall(
        nint responseUtf8,
        ulong responseCapacity,
        out ProjectNativeResult result);
}
