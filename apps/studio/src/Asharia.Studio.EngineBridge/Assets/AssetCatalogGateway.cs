using System;
using System.Buffers;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.EngineBridge.Assets.Abi;

namespace Asharia.Studio.EngineBridge.Assets;

public sealed class AssetCatalogGateway : IAssetCatalogGateway
{
    private const int MaximumPayloadCapacity = 16 * 1024 * 1024;
    private const int MaximumMessageCapacity = 64 * 1024;
    private const int MaximumResponseCapacity =
        MaximumPayloadCapacity + MaximumMessageCapacity;

    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    private readonly IAssetCatalogNativeApi nativeApi_;
    private readonly AssetCatalogSnapshotJsonParser parser_;
    private readonly ArrayPool<byte> responsePool_;
    private readonly SemaphoreSlim queryGate_ = new(initialCount: 1, maxCount: 1);

    public AssetCatalogGateway()
        : this(
            AssetCatalogNativeLibraryApi.Instance,
            TimeProvider.System,
            ArrayPool<byte>.Shared)
    {
    }

    internal AssetCatalogGateway(
        IAssetCatalogNativeApi nativeApi,
        TimeProvider timeProvider,
        ArrayPool<byte>? responsePool = null)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        ArgumentNullException.ThrowIfNull(timeProvider);
        nativeApi_ = nativeApi;
        parser_ = new AssetCatalogSnapshotJsonParser(timeProvider);
        responsePool_ = responsePool ?? ArrayPool<byte>.Shared;
    }

    public async ValueTask<AssetCatalogQueryResult> QueryAsync(
        AssetCatalogQueryScope scope,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scope);
        await queryGate_.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var result = await Task.Run(
                () => QueryCore(scope),
                CancellationToken.None).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            return result;
        }
        finally
        {
            queryGate_.Release();
        }
    }

    private unsafe AssetCatalogQueryResult QueryCore(AssetCatalogQueryScope scope)
    {
        if (!IsProjectFileWithinRoot(scope.ProjectRootPath, scope.ProjectFilePath))
        {
            return Failed(
                AssetCatalogQueryFailureKind.InvalidInput,
                "The asset catalog project file must be contained by the project root.");
        }

        try
        {
            var projectPath = StrictUtf8.GetBytes(scope.ProjectFilePath);
            var targetProfile = StrictUtf8.GetBytes(scope.TargetProfile);
            fixed (byte* project = projectPath)
            fixed (byte* target = targetProfile)
            {
                var request = AssetCatalogNativeQueryRequest.Current(
                    (nint)project,
                    (ulong)projectPath.Length,
                    (nint)target,
                    (ulong)targetProfile.Length,
                    productManifestPathUtf8: 0,
                    productManifestPathByteLength: 0,
                    AssetCatalogNativeLimits.StudioDefault);
                return Invoke(in request, scope);
            }
        }
        catch (EncoderFallbackException exception)
        {
            return Failed(AssetCatalogQueryFailureKind.InvalidInput, exception.Message);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            return Failed(
                AssetCatalogQueryFailureKind.NativeUnavailable,
                $"The canonical editor-content adapter is unavailable: {exception.Message}");
        }
    }

    private unsafe AssetCatalogQueryResult Invoke(
        in AssetCatalogNativeQueryRequest request,
        AssetCatalogQueryScope scope)
    {
        var response = responsePool_.Rent(MaximumResponseCapacity);
        try
        {
            AssetCatalogNativeResult nativeResult;
            AssetCatalogNativeStatus callStatus;
            fixed (byte* buffer = response)
            {
                callStatus = nativeApi_.Query(
                    in request,
                    (nint)buffer,
                    MaximumResponseCapacity,
                    out nativeResult,
                    AssetCatalogNativeResult.StructSize);
            }

            var validation = ValidateResult(
                callStatus,
                nativeResult,
                MaximumResponseCapacity);
            if (validation is not null)
            {
                return validation;
            }

            if (callStatus != AssetCatalogNativeStatus.Success)
            {
                string message;
                try
                {
                    message = Decode(response, nativeResult.MessageUtf8);
                }
                catch (Exception exception) when (
                    exception is ArgumentOutOfRangeException
                        or DecoderFallbackException)
                {
                    return InvalidResponse(
                        "The editor-content adapter returned an invalid failure message.");
                }
                return Failed(
                    MapFailure(callStatus),
                    string.IsNullOrWhiteSpace(message)
                        ? $"The asset catalog query failed with {callStatus}."
                        : message);
            }

            try
            {
                var payload = SliceMemory(response, nativeResult.PayloadJsonUtf8);
                var snapshot = parser_.Parse(payload);
                if (snapshot.ProjectId != scope.ProjectId
                    || !string.Equals(
                        snapshot.TargetProfile,
                        scope.TargetProfile,
                        StringComparison.Ordinal)
                    || !PathsEqual(snapshot.ProjectFile, scope.ProjectFilePath))
                {
                    return InvalidResponse(
                        "The editor-content adapter returned a snapshot for another query scope.");
                }
                return AssetCatalogQueryResult.Success(snapshot);
            }
            catch (Exception exception) when (
                exception is InvalidDataException
                    or ArgumentException
                    or OverflowException)
            {
                return InvalidResponse(exception.Message);
            }
        }
        finally
        {
            responsePool_.Return(response, clearArray: true);
        }
    }

    private static AssetCatalogQueryResult? ValidateResult(
        AssetCatalogNativeStatus callStatus,
        AssetCatalogNativeResult result,
        int responseCapacity)
    {
        if (!Enum.IsDefined(callStatus)
            || !Enum.IsDefined(result.OperationStatus)
            || result.Header.AbiVersion != AssetCatalogNativeAbi.Version
            || result.Header.StructSize != AssetCatalogNativeResult.StructSize
            || result.Reserved != 0)
        {
            return InvalidResponse(
                "The editor-content adapter returned an invalid ABI result.");
        }
        if (callStatus == AssetCatalogNativeStatus.BufferTooSmall
            || result.OperationStatus == AssetCatalogNativeStatus.BufferTooSmall)
        {
            return InvalidResponse(
                "The editor-content adapter exceeded the fixed response buffer.");
        }
        if (callStatus != result.OperationStatus
            || result.RequiredByteLength > (ulong)responseCapacity
            || result.RequiredByteLength > MaximumResponseCapacity
            || !IsExactPartition(result))
        {
            return InvalidResponse(
                "The editor-content adapter returned an inconsistent response layout.");
        }
        return HasExpectedPayloadShape(callStatus, result)
            ? null
            : callStatus == AssetCatalogNativeStatus.Success
                ? InvalidResponse(
                    "A successful asset catalog response must contain only JSON payload.")
                : InvalidResponse(
                    "A failed asset catalog response must contain only a message.");
    }

    private static bool HasExpectedPayloadShape(
        AssetCatalogNativeStatus operationStatus,
        AssetCatalogNativeResult result) =>
        operationStatus == AssetCatalogNativeStatus.Success
            ? result.PayloadJsonUtf8.ByteLength != 0
              && result.MessageUtf8.ByteLength == 0
            : result.PayloadJsonUtf8.ByteLength == 0
              && result.MessageUtf8.ByteLength != 0;

    private static bool IsExactPartition(AssetCatalogNativeResult result) =>
        result.PayloadJsonUtf8.ByteOffset == 0
        && result.PayloadJsonUtf8.ByteLength <= result.RequiredByteLength
        && result.MessageUtf8.ByteOffset == result.PayloadJsonUtf8.ByteLength
        && result.MessageUtf8.ByteOffset <= result.RequiredByteLength
        && result.MessageUtf8.ByteLength
            == result.RequiredByteLength - result.MessageUtf8.ByteOffset;

    private static ReadOnlyMemory<byte> SliceMemory(
        byte[] response,
        AssetCatalogNativeTextSpan span)
    {
        ValidateSpan(response, span);
        return response.AsMemory(
            checked((int)span.ByteOffset),
            checked((int)span.ByteLength));
    }

    private static ReadOnlySpan<byte> Slice(
        byte[] response,
        AssetCatalogNativeTextSpan span)
    {
        ValidateSpan(response, span);
        return response.AsSpan(
            checked((int)span.ByteOffset),
            checked((int)span.ByteLength));
    }

    private static void ValidateSpan(
        byte[] response,
        AssetCatalogNativeTextSpan span)
    {
        if (span.ByteOffset > (ulong)response.Length
            || span.ByteLength > (ulong)response.Length - span.ByteOffset)
        {
            throw new ArgumentOutOfRangeException(nameof(span));
        }
    }

    private static string Decode(
        byte[] response,
        AssetCatalogNativeTextSpan span) =>
        StrictUtf8.GetString(Slice(response, span));

    private static bool PathsEqual(string left, string right)
    {
        try
        {
            return string.Equals(
                Path.GetFullPath(left),
                Path.GetFullPath(right),
                OperatingSystem.IsWindows()
                    ? StringComparison.OrdinalIgnoreCase
                    : StringComparison.Ordinal);
        }
        catch (Exception exception) when (
            exception is ArgumentException
                or NotSupportedException
                or PathTooLongException)
        {
            return false;
        }
    }

    private static bool IsProjectFileWithinRoot(string projectRoot, string projectFile)
    {
        try
        {
            var root = Path.GetFullPath(projectRoot);
            var file = Path.GetFullPath(projectFile);
            var rootPrefix = Path.EndsInDirectorySeparator(root)
                ? root
                : root + Path.DirectorySeparatorChar;
            return file.StartsWith(
                rootPrefix,
                OperatingSystem.IsWindows()
                    ? StringComparison.OrdinalIgnoreCase
                    : StringComparison.Ordinal);
        }
        catch (Exception exception) when (
            exception is ArgumentException
                or NotSupportedException
                or PathTooLongException)
        {
            return false;
        }
    }

    private static AssetCatalogQueryFailureKind MapFailure(
        AssetCatalogNativeStatus status) => status switch
        {
            AssetCatalogNativeStatus.InvalidArgument or AssetCatalogNativeStatus.InvalidUtf8 =>
                AssetCatalogQueryFailureKind.InvalidInput,
            AssetCatalogNativeStatus.UnsupportedAbi =>
                AssetCatalogQueryFailureKind.NativeUnavailable,
            AssetCatalogNativeStatus.InvalidProject =>
                AssetCatalogQueryFailureKind.InvalidProject,
            AssetCatalogNativeStatus.IoFailure => AssetCatalogQueryFailureKind.IoFailure,
            AssetCatalogNativeStatus.LimitExceeded =>
                AssetCatalogQueryFailureKind.LimitExceeded,
            _ => AssetCatalogQueryFailureKind.InternalError,
        };

    private static AssetCatalogQueryResult InvalidResponse(string message) =>
        Failed(AssetCatalogQueryFailureKind.InvalidResponse, message);

    private static AssetCatalogQueryResult Failed(
        AssetCatalogQueryFailureKind kind,
        string message) =>
        AssetCatalogQueryResult.Failed(new AssetCatalogQueryFailure(kind, message));

    private static bool IsNativeBindingFailure(Exception exception) =>
        exception is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;
}
