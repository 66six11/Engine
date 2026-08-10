using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Windows.Graphics;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Windows.Graphics.DirectX.Direct3D11;
using Windows.Graphics.Imaging;

namespace Asharia.Studio.WindowsCapture.Tests;

internal sealed record DwmCompositedFrameObservation(
    long Sequence,
    TimeSpan CompositorRenderedTime,
    int ContentWidth,
    int ContentHeight,
    int SurfaceWidth,
    int SurfaceHeight,
    DirectXPixelFormat PixelFormat,
    DwmCompositedSentinelObservation Sentinel,
    DwmCompositedSentinelContinuity Continuity)
{
    public const string EvidenceKind = "wgc-dwm-composited-pixels";

    public bool PixelEvidenceAvailable => true;

    public bool PhysicalDisplayedEvidenceAvailable => false;

    public bool HasValidCaptureSurface =>
        ContentWidth > 0 &&
        ContentHeight > 0 &&
        ContentWidth <= SurfaceWidth &&
        ContentHeight <= SurfaceHeight &&
        PixelFormat == DirectXPixelFormat.B8G8R8A8UIntNormalized;

    public bool IsExact =>
        HasValidCaptureSurface &&
        Sentinel.IsExact &&
        Continuity.IsExact;

    public bool IsAllowedGrowGap =>
        HasValidCaptureSurface &&
        Sentinel.IsExact &&
        Continuity.IsAllowedGrowGap;

    public bool IsAcceptableForGrow => IsExact || IsAllowedGrowGap;
}

internal readonly record struct WgcDwmCompositedRecorderMetrics(
    int EnvelopeWidth,
    int EnvelopeHeight,
    int ReentrantFrameArrivals,
    int BackloggedFrames,
    int AnalyzerDrops)
{
    public bool HasNoDrops =>
        ReentrantFrameArrivals == 0 &&
        BackloggedFrames == 0 &&
        AnalyzerDrops == 0;
}

internal sealed class WgcDwmCompositedRecorder : IDisposable
{
    private static readonly TimeSpan kPollInterval = TimeSpan.FromMilliseconds(10);
    private const int kEnvelopeWidthAllowance = 1024;
    private const int kEnvelopeHeightAllowance = 512;
    private readonly object callbackLifecycleGate_ = new();
    private readonly object gate_ = new();
    private readonly List<DwmCompositedFrameObservation> observations_ = [];
    private readonly GraphicsCaptureItem item_;
    private readonly IDirect3DDevice device_;
    private readonly Direct3D11CaptureFramePool framePool_;
    private readonly GraphicsCaptureSession session_;
    private readonly SizeInt32 envelopeSize_;
    private DwmCompositedSentinelObservation? baseline_;
    private ExceptionDispatchInfo? failure_;
    private long sequence_;
    private int processing_;
    private int activeCallbacks_;
    private int reentrantFrameArrivals_;
    private int backloggedFrames_;
    private int analyzerDrops_;
    private bool started_;
    private bool disposed_;

    public WgcDwmCompositedRecorder(nint hwnd)
    {
        if (hwnd == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(hwnd));
        }

        item_ = WgcInterop.CreateItemForWindow(hwnd);
        envelopeSize_ = new SizeInt32
        {
            Width = checked(item_.Size.Width + kEnvelopeWidthAllowance),
            Height = checked(item_.Size.Height + kEnvelopeHeightAllowance),
        };
        var device = WgcInterop.CreateDirect3DDevice();
        Direct3D11CaptureFramePool? framePool = null;
        GraphicsCaptureSession? session = null;
        try
        {
            framePool = Direct3D11CaptureFramePool.CreateFreeThreaded(
                device,
                DirectXPixelFormat.B8G8R8A8UIntNormalized,
                3,
                envelopeSize_);
            session = framePool.CreateCaptureSession(item_);
            session.IsCursorCaptureEnabled = false;
            if (OperatingSystem.IsWindowsVersionAtLeast(10, 0, 26100))
            {
                session.MinUpdateInterval = TimeSpan.FromMilliseconds(5);
            }
            framePool.FrameArrived += OnFrameArrived;

            device_ = device;
            framePool_ = framePool;
            session_ = session;
        }
        catch
        {
            session?.Dispose();
            framePool?.Dispose();
            device.Dispose();
            throw;
        }
    }

    public void Start()
    {
        lock (callbackLifecycleGate_)
        {
            ObjectDisposedException.ThrowIf(disposed_, this);
            if (started_)
            {
                throw new InvalidOperationException("The WGC recorder has already started.");
            }

            session_.StartCapture();
            started_ = true;
        }
    }

    public IReadOnlyList<DwmCompositedFrameObservation> Capture()
    {
        lock (gate_)
        {
            failure_?.Throw();
            return observations_.ToArray();
        }
    }

    public long LatestSequence
    {
        get
        {
            lock (gate_)
            {
                failure_?.Throw();
                return sequence_;
            }
        }
    }

    public WgcDwmCompositedRecorderMetrics CaptureMetrics()
    {
        lock (gate_)
        {
            failure_?.Throw();
            return new WgcDwmCompositedRecorderMetrics(
                envelopeSize_.Width,
                envelopeSize_.Height,
                reentrantFrameArrivals_,
                backloggedFrames_,
                analyzerDrops_);
        }
    }

    public async Task<DwmCompositedFrameObservation> WaitForExactFrameAsync(
        long afterSequence,
        Func<DwmCompositedFrameObservation, bool>? predicate,
        TimeSpan timeout)
    {
        using var deadline = new CancellationTokenSource(timeout);
        try
        {
            while (true)
            {
                lock (gate_)
                {
                    failure_?.Throw();
                    var match = observations_.FirstOrDefault(observation =>
                        observation.Sequence > afterSequence &&
                        observation.IsExact &&
                        (predicate is null || predicate(observation)));
                    if (match is not null)
                    {
                        return match;
                    }
                }

                await Task.Delay(kPollInterval, deadline.Token);
            }
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            string diagnostics;
            lock (gate_)
            {
                diagnostics = string.Join(
                    "; ",
                    observations_.TakeLast(5).Select(static observation =>
                        $"#{observation.Sequence} " +
                        $"content={observation.ContentWidth}x{observation.ContentHeight} " +
                        $"surface={observation.SurfaceWidth}x{observation.SurfaceHeight} " +
                        $"located={observation.Sentinel.Located} " +
                        $"blocks={observation.Sentinel.HasExactBlockSizes} " +
                        $"aligned={observation.Sentinel.HasAlignedCorners} " +
                        $"insets={observation.Sentinel.Insets} " +
                        $"leftTop={observation.Continuity.LeftTopInsetsMatch} " +
                        $"rightBottom={observation.Continuity.RightBottomInsetsMatch} " +
                        $"growGap={observation.Continuity.IsAllowedGrowGap}"));
                if (diagnostics.Length == 0)
                {
                    diagnostics = "no frames were analyzed";
                }
            }
            throw new TimeoutException(
                $"WGC did not produce an exact DWM-composited frame after sequence " +
                $"{afterSequence} within {timeout}: {diagnostics}.");
        }
    }

    public void Dispose()
    {
        lock (callbackLifecycleGate_)
        {
            if (disposed_)
            {
                return;
            }

            disposed_ = true;
            framePool_.FrameArrived -= OnFrameArrived;
        }

        session_.Dispose();
        lock (callbackLifecycleGate_)
        {
            while (activeCallbacks_ != 0)
            {
                Monitor.Wait(callbackLifecycleGate_);
            }
        }
        framePool_.Dispose();
        device_.Dispose();
    }

    private void OnFrameArrived(Direct3D11CaptureFramePool sender, object args)
    {
        if (!TryEnterCallback())
        {
            return;
        }

        try
        {
            if (Interlocked.Exchange(ref processing_, 1) != 0)
            {
                lock (gate_)
                {
                    reentrantFrameArrivals_++;
                    analyzerDrops_++;
                }
                return;
            }

            try
            {
                using var frame = sender.TryGetNextFrame();
                if (frame is null)
                {
                    return;
                }

                var contentSize = frame.ContentSize;
                if (contentSize.Width <= 0 ||
                    contentSize.Height <= 0 ||
                    contentSize.Width > envelopeSize_.Width ||
                    contentSize.Height > envelopeSize_.Height)
                {
                    throw new InvalidOperationException(
                        $"WGC ContentSize {contentSize.Width}x{contentSize.Height} escaped " +
                        $"the {envelopeSize_.Width}x{envelopeSize_.Height} capture envelope.");
                }

                var surface = frame.Surface.Description;
                var pixels = ReadDenseBgra(frame, contentSize.Width, contentSize.Height);
                var sentinel = DwmCompositedSentinelAnalyzer.Analyze(
                    pixels.Bytes,
                    pixels.Width,
                    pixels.Height,
                    pixels.Stride);
                DwmCompositedSentinelContinuity continuity;
                lock (gate_)
                {
                    baseline_ ??= sentinel.IsExact ? sentinel : null;
                    continuity = baseline_ is { } baseline
                        ? DwmCompositedSentinelAnalyzer.Compare(baseline, sentinel)
                        : default;
                    var observation = new DwmCompositedFrameObservation(
                        Sequence: ++sequence_,
                        CompositorRenderedTime: frame.SystemRelativeTime,
                        ContentWidth: contentSize.Width,
                        ContentHeight: contentSize.Height,
                        SurfaceWidth: surface.Width,
                        SurfaceHeight: surface.Height,
                        PixelFormat: surface.Format,
                        Sentinel: sentinel,
                        Continuity: continuity);
                    observations_.Add(observation);
                }
            }
            catch (Exception exception)
            {
                RecordFailure(exception);
            }
            finally
            {
                try
                {
                    if (!IsDisposing())
                    {
                        using var backlogged = sender.TryGetNextFrame();
                        if (backlogged is not null)
                        {
                            lock (gate_)
                            {
                                backloggedFrames_++;
                                analyzerDrops_++;
                            }
                        }
                    }
                }
                catch (Exception exception)
                {
                    RecordFailure(exception);
                }
                finally
                {
                    Volatile.Write(ref processing_, 0);
                }
            }
        }
        finally
        {
            ExitCallback();
        }
    }

    private bool TryEnterCallback()
    {
        lock (callbackLifecycleGate_)
        {
            if (disposed_)
            {
                return false;
            }

            activeCallbacks_++;
            return true;
        }
    }

    private void ExitCallback()
    {
        lock (callbackLifecycleGate_)
        {
            activeCallbacks_--;
            if (activeCallbacks_ == 0)
            {
                Monitor.PulseAll(callbackLifecycleGate_);
            }
        }
    }

    private bool IsDisposing()
    {
        lock (callbackLifecycleGate_)
        {
            return disposed_;
        }
    }

    private void RecordFailure(Exception exception)
    {
        lock (gate_)
        {
            analyzerDrops_++;
            failure_ ??= ExceptionDispatchInfo.Capture(exception);
        }
    }

    private static unsafe DenseBgraFrame ReadDenseBgra(
        Direct3D11CaptureFrame frame,
        int contentWidth,
        int contentHeight)
    {
        using var bitmap = SoftwareBitmap
            .CreateCopyFromSurfaceAsync(frame.Surface, BitmapAlphaMode.Premultiplied)
            .AsTask()
            .GetAwaiter()
            .GetResult();
        using var buffer = bitmap.LockBuffer(BitmapBufferAccessMode.Read);
        using var reference = buffer.CreateReference();
        if (!WinRT.ComWrappersSupport.TryUnwrapObject(reference, out var referenceObject))
        {
            throw new InvalidOperationException("Unable to unwrap IMemoryBufferReference.");
        }

        using var byteAccess = referenceObject.As(WgcInterop.MemoryBufferByteAccessId);
        var vtable = *(nint**)byteAccess.ThisPtr;
        var getBuffer = (delegate* unmanaged[Stdcall]<nint, byte**, uint*, int>)vtable[3];
        byte* source = null;
        uint capacity = 0;
        Marshal.ThrowExceptionForHR(getBuffer(byteAccess.ThisPtr, &source, &capacity));

        var plane = buffer.GetPlaneDescription(0);
        if (contentWidth > bitmap.PixelWidth || contentHeight > bitmap.PixelHeight)
        {
            throw new InvalidOperationException(
                "WGC ContentSize escaped its SoftwareBitmap surface.");
        }

        var rowBytes = checked(contentWidth * 4);
        var requiredSourceBytes = checked(
            (long)plane.StartIndex +
            ((long)(contentHeight - 1) * plane.Stride) +
            rowBytes);
        if (plane.StartIndex < 0 || plane.Stride < rowBytes || requiredSourceBytes > capacity)
        {
            throw new InvalidOperationException("SoftwareBitmap returned an invalid BGRA plane.");
        }

        var bytes = new byte[checked(rowBytes * contentHeight)];
        for (var row = 0; row < contentHeight; row++)
        {
            var sourceOffset = checked(plane.StartIndex + (row * plane.Stride));
            Marshal.Copy(
                (nint)(source + sourceOffset),
                bytes,
                row * rowBytes,
                rowBytes);
        }

        return new DenseBgraFrame(
            bytes,
            contentWidth,
            contentHeight,
            rowBytes);
    }

    private sealed record DenseBgraFrame(byte[] Bytes, int Width, int Height, int Stride);
}

internal static class WgcInterop
{
    internal static readonly Guid MemoryBufferByteAccessId =
        new("5B0D3235-4DBA-4D44-865E-8F1D0E4FD04D");

    private const uint D3D11CreateDeviceBgraSupport = 0x20;
    private const uint D3D11SdkVersion = 7;
    private static readonly Guid GraphicsCaptureItemInteropId =
        new("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356");
    private static readonly Guid GraphicsCaptureItemId =
        new("79C3F95B-31F7-4EC2-A464-632EF5D30760");
    private static readonly Guid DxgiDeviceId =
        new("54EC77FA-1377-44E6-8C32-88FD5F44C84C");

    public static unsafe GraphicsCaptureItem CreateItemForWindow(nint hwnd)
    {
        using var factory = WinRT.ActivationFactory.Get(
            "Windows.Graphics.Capture.GraphicsCaptureItem");
        using var interop = factory.As(GraphicsCaptureItemInteropId);
        var vtable = *(nint**)interop.ThisPtr;
        var createForWindow =
            (delegate* unmanaged[Stdcall]<nint, nint, Guid*, nint*, int>)vtable[3];
        var itemId = GraphicsCaptureItemId;
        nint itemAbi = 0;
        Marshal.ThrowExceptionForHR(
            createForWindow(interop.ThisPtr, hwnd, &itemId, &itemAbi));
        try
        {
            return WinRT.MarshalInterface<GraphicsCaptureItem>.FromAbi(itemAbi);
        }
        finally
        {
            Marshal.Release(itemAbi);
        }
    }

    public static IDirect3DDevice CreateDirect3DDevice()
    {
        var result = D3D11CreateDevice(
            0,
            D3DDriverType.Hardware,
            0,
            D3D11CreateDeviceBgraSupport,
            0,
            0,
            D3D11SdkVersion,
            out var d3dDevice,
            out _,
            out var immediateContext);
        if (result < 0)
        {
            result = D3D11CreateDevice(
                0,
                D3DDriverType.Warp,
                0,
                D3D11CreateDeviceBgraSupport,
                0,
                0,
                D3D11SdkVersion,
                out d3dDevice,
                out _,
                out immediateContext);
        }

        Marshal.ThrowExceptionForHR(result);
        try
        {
            var dxgiId = DxgiDeviceId;
            result = Marshal.QueryInterface(d3dDevice, in dxgiId, out var dxgiDevice);
            Marshal.ThrowExceptionForHR(result);
            try
            {
                result = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, out var inspectable);
                Marshal.ThrowExceptionForHR(result);
                try
                {
                    return WinRT.MarshalInterface<IDirect3DDevice>.FromAbi(inspectable);
                }
                finally
                {
                    Marshal.Release(inspectable);
                }
            }
            finally
            {
                Marshal.Release(dxgiDevice);
            }
        }
        finally
        {
            Marshal.Release(immediateContext);
            Marshal.Release(d3dDevice);
        }
    }

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int D3D11CreateDevice(
        nint adapter,
        D3DDriverType driverType,
        nint software,
        uint flags,
        nint featureLevels,
        uint featureLevelCount,
        uint sdkVersion,
        out nint device,
        out uint featureLevel,
        out nint immediateContext);

    [DllImport("d3d11.dll", ExactSpelling = true)]
    private static extern int CreateDirect3D11DeviceFromDXGIDevice(
        nint dxgiDevice,
        out nint graphicsDevice);

    private enum D3DDriverType : uint
    {
        Hardware = 1,
        Warp = 5,
    }
}
