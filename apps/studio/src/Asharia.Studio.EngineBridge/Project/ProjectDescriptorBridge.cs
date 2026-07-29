using System;
using System.Text;
using Asharia.Studio.EngineBridge.Project.Abi;

namespace Asharia.Studio.EngineBridge.Project;

public sealed class ProjectDescriptorBridge
{
    private const int MaxProjectRootByteLength = 32 * 1024;
    private const int MaxProjectNameByteLength = 1024;
    private const int MaxProjectIdByteLength = 64;
    private const int MaxMessageByteLength = 64 * 1024;
    private const string OpenOperation = "project.open";
    private const string CreateOperation = "project.create-minimal";

    private static readonly UTF8Encoding StrictUtf8Encoding = new(
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

    public ProjectDescriptorSnapshot OpenProject(string projectRoot)
    {
        var rootUtf8 = EncodeRequired(
            projectRoot,
            nameof(projectRoot),
            MaxProjectRootByteLength);

        ProjectNativeResult result = default;
        var ownsResult = false;
        try
        {
            ProjectNativeStatus status;
            unsafe
            {
                fixed (byte* rootData = rootUtf8)
                {
                    var request = ProjectNativeOpenRequest.Current(
                        (nint)rootData,
                        (ulong)rootUtf8.Length);
                    status = nativeApi_.Open(in request, out result);
                    ownsResult = true;
                }
            }

            return ReadResult(OpenOperation, status, result);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(OpenOperation, exception);
        }
        finally
        {
            if (ownsResult)
            {
                nativeApi_.Release(result);
            }
        }
    }

    public ProjectDescriptorSnapshot CreateMinimalProject(
        string projectRoot,
        string projectName,
        Guid projectId)
    {
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Project id must not be empty.",
                nameof(projectId));
        }

        var rootUtf8 = EncodeRequired(
            projectRoot,
            nameof(projectRoot),
            MaxProjectRootByteLength);
        var nameUtf8 = EncodeRequired(
            projectName,
            nameof(projectName),
            MaxProjectNameByteLength);
        var projectIdUtf8 = StrictUtf8Encoding.GetBytes(
            projectId.ToString("D"));

        ProjectNativeResult result = default;
        var ownsResult = false;
        try
        {
            ProjectNativeStatus status;
            unsafe
            {
                fixed (byte* rootData = rootUtf8)
                fixed (byte* nameData = nameUtf8)
                fixed (byte* projectIdData = projectIdUtf8)
                {
                    var request = ProjectNativeCreateRequest.Current(
                        (nint)rootData,
                        (ulong)rootUtf8.Length,
                        (nint)nameData,
                        (ulong)nameUtf8.Length,
                        (nint)projectIdData,
                        (ulong)projectIdUtf8.Length);
                    status = nativeApi_.CreateMinimal(in request, out result);
                    ownsResult = true;
                }
            }

            return ReadResult(CreateOperation, status, result);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(CreateOperation, exception);
        }
        finally
        {
            if (ownsResult)
            {
                nativeApi_.Release(result);
            }
        }
    }

    private static ProjectDescriptorSnapshot ReadResult(
        string operation,
        ProjectNativeStatus returnedStatus,
        ProjectNativeResult result)
    {
        if (result.Header.AbiVersion != ProjectNativeAbi.Version
            || result.Header.StructSize < ProjectNativeResult.StructSize)
        {
            throw InvalidResult(
                operation,
                returnedStatus,
                "returned an unsupported result ABI header.");
        }

        if (result.Status != returnedStatus)
        {
            throw InvalidResult(
                operation,
                returnedStatus,
                $"returned status {returnedStatus} but result status {result.Status}.");
        }

        if (returnedStatus != ProjectNativeStatus.Success)
        {
            var nativeMessage = DecodeOptional(
                operation,
                result.MessageUtf8,
                result.MessageByteLength,
                MaxMessageByteLength,
                "message");
            throw new ProjectNativeCallException(
                operation,
                returnedStatus,
                string.IsNullOrWhiteSpace(nativeMessage)
                    ? $"Project native operation '{operation}' failed with status "
                        + $"{returnedStatus} ({(uint)returnedStatus})."
                    : nativeMessage);
        }

        var root = DecodeRequired(
            operation,
            result.ProjectRootUtf8,
            result.ProjectRootByteLength,
            MaxProjectRootByteLength,
            "project root");
        var name = DecodeRequired(
            operation,
            result.ProjectNameUtf8,
            result.ProjectNameByteLength,
            MaxProjectNameByteLength,
            "project name");
        var idText = DecodeRequired(
            operation,
            result.ProjectIdUtf8,
            result.ProjectIdByteLength,
            MaxProjectIdByteLength,
            "project id");
        if (!Guid.TryParseExact(idText, "D", out var projectId)
            || projectId == Guid.Empty)
        {
            throw InvalidResult(
                operation,
                returnedStatus,
                "returned an invalid project id.");
        }

        return new ProjectDescriptorSnapshot(root, name, projectId);
    }

    private static byte[] EncodeRequired(
        string value,
        string parameterName,
        int maxByteLength)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException(
                "Value must not be null or whitespace.",
                parameterName);
        }

        byte[] encoded;
        try
        {
            encoded = StrictUtf8Encoding.GetBytes(value);
        }
        catch (EncoderFallbackException exception)
        {
            throw new ArgumentException(
                "Value must contain valid Unicode text.",
                parameterName,
                exception);
        }

        if (encoded.Length > maxByteLength)
        {
            throw new ArgumentException(
                $"Value exceeds the {maxByteLength}-byte UTF-8 limit.",
                parameterName);
        }
        return encoded;
    }

    private static string DecodeRequired(
        string operation,
        nint data,
        ulong byteLength,
        int maxByteLength,
        string fieldName)
    {
        var text = DecodeOptional(
            operation,
            data,
            byteLength,
            maxByteLength,
            fieldName);
        if (string.IsNullOrWhiteSpace(text))
        {
            throw InvalidResult(
                operation,
                ProjectNativeStatus.Success,
                $"returned an empty {fieldName}.");
        }
        return text;
    }

    private static unsafe string DecodeOptional(
        string operation,
        nint data,
        ulong byteLength,
        int maxByteLength,
        string fieldName)
    {
        if (byteLength == 0)
        {
            return string.Empty;
        }
        if (data == 0 || byteLength > (ulong)maxByteLength)
        {
            throw InvalidResult(
                operation,
                null,
                $"returned an invalid {fieldName} buffer.");
        }

        try
        {
            return StrictUtf8Encoding.GetString(
                new ReadOnlySpan<byte>(
                    (void*)data,
                    checked((int)byteLength)));
        }
        catch (DecoderFallbackException exception)
        {
            throw InvalidResult(
                operation,
                null,
                $"returned malformed UTF-8 for {fieldName}.",
                exception);
        }
    }

    private static bool IsNativeBindingFailure(Exception exception)
    {
        return exception is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;
    }

    private static ProjectNativeCallException BindingFailure(
        string operation,
        Exception exception)
    {
        return new ProjectNativeCallException(
            operation,
            null,
            $"Project native operation '{operation}' is unavailable: "
                + exception.Message,
            exception);
    }

    private static ProjectNativeCallException InvalidResult(
        string operation,
        ProjectNativeStatus? status,
        string detail,
        Exception? innerException = null)
    {
        return new ProjectNativeCallException(
            operation,
            status,
            $"Project native operation '{operation}' produced an invalid result: "
                + detail,
            innerException);
    }
}
