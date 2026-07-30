using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;
using Editor.Core.Interop.Viewports.Adapters;
using Editor.Core.Interop.Viewports.Api;
using Editor.Core.Models.Viewports;
using Editor.Features.SceneView.Views;

namespace Editor.Features.SceneView.Interop;

internal sealed class SceneViewPresentationSession
{
    private const int MaximumOwnedSlots = 4;
    private const string ImageHandleType =
        KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle;
    private const string SemaphoreHandleType =
        KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle;

    private readonly ViewportNativeBridge bridge_;
    private readonly SceneViewCompositionHost host_;
    private readonly SceneViewPresentationState state_ = new();
    private readonly SemaphoreSlim producerGate_ = new(initialCount: 1, maxCount: 1);
    private readonly Dictionary<int, PresentSlot> activeSlots_ = [];
    private readonly Dictionary<int, PresentSlot> retiringSlots_ = [];
    private readonly HashSet<int> slotCreationReservations_ = [];
    private readonly HashSet<Task> frameTasks_ = [];
    private readonly HashSet<Task> retirementTasks_ = [];
    private PresentationConfiguration? configuration_;
    private IDisposable? shutdownRegistration_;
    private bool isAttached_;

    public SceneViewPresentationSession(
        ViewportNativeBridge bridge,
        SceneViewCompositionHost host)
    {
        ArgumentNullException.ThrowIfNull(bridge);
        ArgumentNullException.ThrowIfNull(host);
        bridge_ = bridge;
        host_ = host;
    }

    public void Attach()
    {
        EnsureUiThread();
        if (isAttached_)
        {
            return;
        }

        isAttached_ = true;
        state_.Attach();
        var registration =
            ViewportNativePresentDrain.RegisterShutdownParticipant(DetachAsync);
        if (isAttached_)
        {
            shutdownRegistration_ = registration;
        }
        else
        {
            registration.Dispose();
        }
    }

    public void Configure(
        ICompositionGpuInterop interop,
        ViewportCompositionCapabilitiesSnapshot compositionCapabilities,
        Action<ViewportNativePresentSnapshot> updatePresent,
        Action requestRetry)
    {
        EnsureUiThread();
        ArgumentNullException.ThrowIfNull(interop);
        ArgumentNullException.ThrowIfNull(compositionCapabilities);
        ArgumentNullException.ThrowIfNull(updatePresent);
        ArgumentNullException.ThrowIfNull(requestRetry);
        configuration_ =
            new PresentationConfiguration(
                interop,
                compositionCapabilities,
                updatePresent,
                requestRetry);
    }

    public void RequestFrame(SceneViewFrameObservation observation)
    {
        EnsureUiThread();
        ArgumentNullException.ThrowIfNull(observation);
        if (!isAttached_ || configuration_ is null)
        {
            return;
        }

        state_.Observe(observation);
        StartPendingRetirements();
        TrySchedulePendingFrame();
    }

    public void ResetConfiguration()
    {
        EnsureUiThread();
        configuration_ = null;
        foreach (var slotId in state_.Reset())
        {
            StartRetirement(slotId);
        }
    }

    public Task DetachAsync()
    {
        EnsureUiThread();
        var shutdownRegistration = shutdownRegistration_;
        shutdownRegistration_ = null;
        shutdownRegistration?.Dispose();
        if (isAttached_)
        {
            isAttached_ = false;
            configuration_ = null;
            foreach (var slotId in state_.Detach())
            {
                StartRetirement(slotId);
            }
        }

        return DrainAsync();
    }

    private void TrySchedulePendingFrame()
    {
        RemoveCompletedTasks();
        if (!isAttached_ ||
            configuration_ is not { } configuration ||
            !ViewportNativePresentDrain.CanBeginPresent ||
            !state_.TryBeginWork(
                OwnedSlotCount() < MaximumOwnedSlots,
                out var work))
        {
            return;
        }

        if (work.Kind == SceneViewPresentationWorkKind.CreateSlot &&
            !slotCreationReservations_.Add(work.SlotId))
        {
            throw new InvalidOperationException(
                "The Scene View slot creation is already reserved.");
        }

        var frameTask = ExecuteFrameGuardedAsync(work, configuration);
        frameTasks_.Add(frameTask);
        _ = ViewportNativePresentDrain.TrackAsync(
            ContinueAfterFrameAsync(frameTask));
    }

    private async Task<bool> ExecuteFrameGuardedAsync(
        SceneViewPresentationWork work,
        PresentationConfiguration configuration)
    {
        try
        {
            return await ExecuteFrameAsync(work, configuration);
        }
        catch (OperationCanceledException)
            when (work.NativeStartAdmission.WasCanceled)
        {
            state_.AbortWork(work.SlotId);
            StartPendingRetirements();
            return true;
        }
        catch (Exception ex)
        {
            state_.AbortWork(work.SlotId);
            StartPendingRetirements();
            if (state_.IsCurrent(work.Request))
            {
                configuration.UpdatePresent(
                    CreateLocalSnapshot(
                        work.Request,
                        ViewportNativePresentStatus.RenderFailed,
                        CreateExceptionMessage(
                            "Scene View native frame production failed",
                            ex)));
            }

            return false;
        }
        finally
        {
            if (work.Kind == SceneViewPresentationWorkKind.CreateSlot)
            {
                slotCreationReservations_.Remove(work.SlotId);
            }
        }
    }

    private async Task<bool> ExecuteFrameAsync(
        SceneViewPresentationWork work,
        PresentationConfiguration configuration)
    {
        PresentSlot? slot;
        if (work.Kind == SceneViewPresentationWorkKind.CreateSlot)
        {
            slot = await CreateSlotAsync(work, configuration);
            if (slot is null)
            {
                return !state_.IsCurrent(work.Request);
            }
        }
        else
        {
            slot = activeSlots_[work.SlotId];
            var renderResult =
                await RunNativeAsync(
                    () =>
                    {
                        var packet = slot.Packet;
                        var status =
                            bridge_.RenderPresentSlot(
                                ref packet,
                                work.Request.PixelExtent,
                                work.Request.HasScene,
                            work.Request.SceneRevision);
                        return new NativeRenderResult(status, packet);
                    },
                    work.NativeStartAdmission);
            slot.Packet = renderResult.Packet;
            if (!ViewportNativeStatus.IsSuccess(renderResult.Status))
            {
                var canRetry = renderResult.Status == ViewportNativeStatus.Unavailable;
                state_.CompleteFrame(
                    work.SlotId,
                    canReuse: canRetry,
                    warmCurrentGeneration: false);
                StartPendingRetirements();
                if (canRetry)
                {
                    if (state_.IsCurrent(work.Request))
                    {
                        state_.RequeueIfCurrent(work.Request);
                        configuration.RequestRetry();
                        return false;
                    }

                    return true;
                }

                StartPendingRetirements();
                UpdatePresentIfCurrent(
                    work.Request,
                    configuration,
                    slot.Packet.ToSnapshot(
                        work.Request.ViewportId,
                        work.Request.PixelExtent,
                        MapPresentStatus(renderResult.Status),
                        "Native viewport present slot could not render the frame."));
                return true;
            }
        }

        if (!state_.IsCurrent(work.Request))
        {
            state_.CompleteFrame(
                work.SlotId,
                canReuse: false,
                warmCurrentGeneration: false);
            StartPendingRetirements();
            return true;
        }

        bool committed;
        try
        {
            committed =
                await host_.TryCommitFrameAsync(
                    work.Request.DisplaySizeDip,
                    () => state_.IsCurrent(work.Request),
                    surface =>
                        surface.UpdateWithSemaphoresAsync(
                            slot.Image!,
                            slot.WaitSemaphore!,
                            slot.SignalSemaphore!));
        }
        catch (Exception ex)
        {
            state_.CompleteFrame(
                work.SlotId,
                canReuse: false,
                warmCurrentGeneration: false);
            state_.RequeueIfCurrent(work.Request);
            StartPendingRetirements();
            UpdatePresentIfCurrent(
                work.Request,
                configuration,
                slot.Packet.ToSnapshot(
                    work.Request.ViewportId,
                    work.Request.PixelExtent,
                    ViewportNativePresentStatus.ImportFailed,
                    CreateExceptionMessage(
                        "Scene View composition update failed",
                        ex)));
            return false;
        }

        state_.CompleteFrame(
            work.SlotId,
            canReuse: committed,
            warmCurrentGeneration: committed);
        StartPendingRetirements();
        if (!committed)
        {
            return true;
        }

        UpdatePresentIfCurrent(
            work.Request,
            configuration,
            slot.Packet.ToSnapshot(
                work.Request.ViewportId,
                work.Request.PixelExtent,
                ViewportNativePresentStatus.Success,
                "Presented the current Scene View frame."));
        return true;
    }

    private async Task<PresentSlot?> CreateSlotAsync(
        SceneViewPresentationWork work,
        PresentationConfiguration configuration)
    {
        var packet =
            await RunNativeAsync(
                () =>
                    bridge_.CreatePresentSlot(
                        configuration.CompositionCapabilities,
                        work.Request.PixelExtent,
                        work.Request.HasScene,
                        work.Request.SceneRevision),
                work.NativeStartAdmission);
        if (!ViewportNativeStatus.IsSuccess(packet.Status))
        {
            var snapshot =
                await RunNativeAsync(
                    () =>
                        bridge_.SnapshotAndReleasePresentPacket(
                            packet,
                            work.Request.ViewportId,
                            work.Request.PixelExtent));
            state_.AbandonSlotCreation(work.SlotId);
            if (packet.Status == ViewportNativeStatus.Unavailable &&
                state_.IsCurrent(work.Request))
            {
                state_.RequeueIfCurrent(work.Request);
                configuration.RequestRetry();
            }
            else
            {
                UpdatePresentIfCurrent(work.Request, configuration, snapshot);
            }

            return null;
        }

        state_.CompleteSlotCreation(work.SlotId);
        var slot = new PresentSlot(packet);
        slotCreationReservations_.Remove(work.SlotId);
        activeSlots_.Add(work.SlotId, slot);
        if (!state_.IsCurrent(work.Request))
        {
            state_.CompleteFrame(
                work.SlotId,
                canReuse: false,
                warmCurrentGeneration: false);
            StartPendingRetirements();
            return null;
        }

        try
        {
            EnsureUiThread();
            slot.Image =
                configuration.Interop.ImportImage(
                    new PlatformHandle(packet.ImageHandle, ImageHandleType),
                    packet.CreateAvaloniaImageProperties());
            slot.WaitSemaphore =
                configuration.Interop.ImportSemaphore(
                    new PlatformHandle(
                        packet.WaitSemaphoreHandle,
                        SemaphoreHandleType));
            slot.SignalSemaphore =
                configuration.Interop.ImportSemaphore(
                    new PlatformHandle(
                        packet.SignalSemaphoreHandle,
                        SemaphoreHandleType));
            return slot;
        }
        catch (Exception ex)
        {
            state_.CompleteFrame(
                work.SlotId,
                canReuse: false,
                warmCurrentGeneration: false);
            state_.RequeueIfCurrent(work.Request);
            StartPendingRetirements();
            UpdatePresentIfCurrent(
                work.Request,
                configuration,
                packet.ToSnapshot(
                    work.Request.ViewportId,
                    work.Request.PixelExtent,
                    ViewportNativePresentStatus.ImportFailed,
                    CreateExceptionMessage(
                        "Scene View external image import failed",
                        ex)));
            return null;
        }
    }

    private async Task ContinueAfterFrameAsync(Task<bool> frameTask)
    {
        var shouldRetry = false;
        try
        {
            shouldRetry = await frameTask;
        }
        catch
        {
            // ExecuteFrameGuardedAsync reports the failure on the view path.
        }
        finally
        {
            frameTasks_.Remove(frameTask);
            StartPendingRetirements();
            if (shouldRetry)
            {
                TrySchedulePendingFrame();
            }
        }
    }

    private void StartPendingRetirements()
    {
        foreach (var slotId in state_.CollectRetirements())
        {
            StartRetirement(slotId);
        }
    }

    private void StartRetirement(int slotId)
    {
        if (!activeSlots_.Remove(slotId, out var slot))
        {
            return;
        }

        retiringSlots_.Add(slotId, slot);
        var retirementTask = RetireSlotAsync(slotId, slot);
        retirementTasks_.Add(retirementTask);
        _ = ViewportNativePresentDrain.TrackAsync(
            ContinueAfterRetirementAsync(retirementTask));
    }

    private async Task RetireSlotAsync(int slotId, PresentSlot slot)
    {
        var result =
            await SceneViewResourceRetirement.RunAsync(
                () => DisposeImportedObjectsAsync(slot),
                () => RunNativeAsync(
                    () => bridge_.ReleasePresentPacket(slot.Packet)));
        if (!result.Released)
        {
            SceneViewResourceQuarantine.Retain(
                slot.Image,
                slot.WaitSemaphore,
                slot.SignalSemaphore,
                slot.Packet,
                result.Failure!);
        }

        retiringSlots_.Remove(slotId);
    }

    private async Task ContinueAfterRetirementAsync(Task retirementTask)
    {
        try
        {
            await retirementTask;
        }
        catch
        {
            // Drain continues so other slots can release independently.
        }
        finally
        {
            retirementTasks_.Remove(retirementTask);
            TrySchedulePendingFrame();
        }
    }

    private async Task DrainAsync()
    {
        while (true)
        {
            RemoveCompletedTasks();
            var activeTasks = new List<Task>(frameTasks_.Count + retirementTasks_.Count);
            activeTasks.AddRange(frameTasks_);
            activeTasks.AddRange(retirementTasks_);
            if (activeTasks.Count == 0)
            {
                return;
            }

            try
            {
                await Task.WhenAll(activeTasks);
            }
            catch
            {
                // Each operation owns its error and still completes resource retirement.
            }

            StartPendingRetirements();
        }
    }

    private async Task<T> RunNativeAsync<T>(Func<T> operation)
    {
        ArgumentNullException.ThrowIfNull(operation);
        await producerGate_.WaitAsync().ConfigureAwait(false);
        try
        {
            return await Task.Run(operation).ConfigureAwait(false);
        }
        finally
        {
            producerGate_.Release();
        }
    }

    private async Task<T> RunNativeAsync<T>(
        Func<T> operation,
        SceneViewNativeStartAdmission nativeStartAdmission)
    {
        ArgumentNullException.ThrowIfNull(operation);
        await producerGate_
            .WaitAsync(nativeStartAdmission.CancellationToken)
            .ConfigureAwait(false);
        try
        {
            if (!nativeStartAdmission.TryBegin())
            {
                throw new OperationCanceledException(
                    nativeStartAdmission.CancellationToken);
            }

            return await Task.Run(operation).ConfigureAwait(false);
        }
        finally
        {
            producerGate_.Release();
        }
    }

    private async Task RunNativeAsync(Action operation)
    {
        ArgumentNullException.ThrowIfNull(operation);
        await producerGate_.WaitAsync().ConfigureAwait(false);
        try
        {
            await Task.Run(operation).ConfigureAwait(false);
        }
        finally
        {
            producerGate_.Release();
        }
    }

    private static async Task DisposeImportedObjectsAsync(PresentSlot slot)
    {
        EnsureUiThread();
        if (slot.Image is { } image)
        {
            await image.DisposeAsync();
            slot.Image = null;
        }

        if (slot.WaitSemaphore is { } waitSemaphore)
        {
            await waitSemaphore.DisposeAsync();
            slot.WaitSemaphore = null;
        }

        if (slot.SignalSemaphore is { } signalSemaphore)
        {
            await signalSemaphore.DisposeAsync();
            slot.SignalSemaphore = null;
        }
    }

    private void UpdatePresentIfCurrent(
        SceneViewFrameRequest request,
        PresentationConfiguration configuration,
        ViewportNativePresentSnapshot snapshot)
    {
        if (state_.IsCurrent(request))
        {
            configuration.UpdatePresent(snapshot);
        }
    }

    private void RemoveCompletedTasks()
    {
        frameTasks_.RemoveWhere(static task => task.IsCompleted);
        retirementTasks_.RemoveWhere(static task => task.IsCompleted);
    }

    private int OwnedSlotCount()
    {
        return activeSlots_.Count +
               retiringSlots_.Count +
               slotCreationReservations_.Count +
               SceneViewResourceQuarantine.Count;
    }

    private static ViewportNativePresentSnapshot CreateLocalSnapshot(
        SceneViewFrameRequest request,
        ViewportNativePresentStatus status,
        string message)
    {
        return new ViewportNativePresentSnapshot(
            request.ViewportId,
            request.PixelExtent,
            actualExtent: null,
            formatName: "Unknown",
            colorSpace: "Unknown",
            frameIndex: 0UL,
            status,
            message,
            DateTimeOffset.UtcNow);
    }

    private static string CreateExceptionMessage(string prefix, Exception ex)
    {
        return string.IsNullOrWhiteSpace(ex.Message)
            ? $"{prefix}."
            : $"{prefix}: {ex.Message}";
    }

    private static ViewportNativePresentStatus MapPresentStatus(uint status)
    {
        return status switch
        {
            ViewportNativeStatus.DeviceLost => ViewportNativePresentStatus.DeviceLost,
            ViewportNativeStatus.RenderFailed => ViewportNativePresentStatus.RenderFailed,
            ViewportNativeStatus.UnsupportedAbi => ViewportNativePresentStatus.UnsupportedAbi,
            _ => ViewportNativePresentStatus.RenderProducerUnavailable,
        };
    }

    private static void EnsureUiThread()
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            throw new InvalidOperationException(
                "Scene View composition resources must be accessed on the UI dispatcher.");
        }
    }

    private sealed record PresentationConfiguration(
        ICompositionGpuInterop Interop,
        ViewportCompositionCapabilitiesSnapshot CompositionCapabilities,
        Action<ViewportNativePresentSnapshot> UpdatePresent,
        Action RequestRetry);

    private sealed class PresentSlot
    {
        public PresentSlot(ViewportNativePresentPacket packet)
        {
            Packet = packet;
        }

        public ViewportNativePresentPacket Packet;

        public ICompositionImportedGpuImage? Image { get; set; }

        public ICompositionImportedGpuSemaphore? WaitSemaphore { get; set; }

        public ICompositionImportedGpuSemaphore? SignalSemaphore { get; set; }
    }

    private readonly record struct NativeRenderResult(
        uint Status,
        ViewportNativePresentPacket Packet);
}
