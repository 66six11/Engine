using System;
using System.Threading;
using Asharia.Studio.EngineBridge.Scene.Abi;

namespace Asharia.Studio.EngineBridge.Scene;

public sealed class SceneWorld : IDisposable
{
    private const string CreateOperation = "scene.world.create";
    private const string DestroyOperation = "scene.world.destroy";

    private readonly ISceneNativeApi nativeApi_;
    private readonly Thread ownerThread_;
    private nint handle_;

    private SceneWorld(
        ISceneNativeApi nativeApi,
        nint handle,
        Thread ownerThread)
    {
        nativeApi_ = nativeApi;
        handle_ = handle;
        ownerThread_ = ownerThread;
    }

    public bool IsOpen => handle_ != 0;

    public static SceneWorld Create()
    {
        return Create(SceneNativeLibraryApi.Instance);
    }

    internal static SceneWorld Create(ISceneNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);

        var request = SceneNativeWorldCreateRequest.Current;
        nint handle = 0;
        SceneNativeStatus status;
        try
        {
            status = nativeApi.CreateWorld(in request, out handle);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(CreateOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(CreateOperation, status);
        }

        if (handle == 0)
        {
            throw new SceneNativeCallException(
                CreateOperation,
                SceneNativeStatus.Success,
                $"Scene native operation '{CreateOperation}' reported success "
                + "but returned a null World handle.");
        }

        return new SceneWorld(nativeApi, handle, Thread.CurrentThread);
    }

    public void Dispose()
    {
        if (handle_ == 0)
        {
            return;
        }

        if (!ReferenceEquals(Thread.CurrentThread, ownerThread_))
        {
            throw new InvalidOperationException(
                "Scene World must be disposed on the thread that created it. "
                + $"Owner managed thread id is {ownerThread_.ManagedThreadId}; "
                + $"current managed thread id is {Environment.CurrentManagedThreadId}.");
        }

        SceneNativeStatus status;
        try
        {
            status = nativeApi_.DestroyWorld(handle_);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            throw BindingFailure(DestroyOperation, exception);
        }

        if (status != SceneNativeStatus.Success)
        {
            throw StatusFailure(DestroyOperation, status);
        }

        handle_ = 0;
    }

    private static SceneNativeCallException BindingFailure(
        string operation,
        Exception exception)
    {
        return new SceneNativeCallException(
            operation,
            null,
            $"Scene native operation '{operation}' is unavailable: {exception.Message}",
            exception);
    }

    private static bool IsNativeBindingFailure(Exception exception)
    {
        return exception is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;
    }

    private static SceneNativeCallException StatusFailure(
        string operation,
        SceneNativeStatus status)
    {
        return new SceneNativeCallException(
            operation,
            status,
            $"Scene native operation '{operation}' failed with status "
            + $"{status} ({(uint)status}).");
    }
}
