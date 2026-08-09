using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Platform;
using Avalonia.Threading;

namespace Editor.Shell.Views.Docking;

internal sealed class EditorDockPresentationOuterLayoutCommit(
    Action apply,
    Action rollback,
    Action? accept = null,
    Func<bool>? isCurrent = null)
{
    public Action Apply { get; } = apply ?? throw new ArgumentNullException(nameof(apply));

    public Action Rollback { get; } = rollback ?? throw new ArgumentNullException(nameof(rollback));

    public Action Accept { get; } = accept ?? (static () => { });

    public Func<bool> IsCurrent { get; } = isCurrent ?? (static () => true);
}

/// <summary>
/// Converts interactive Win32 sizing into latest-wins presentation proposals. USER32 keeps the
/// last accepted HWND rectangle until the exact Scene candidate is ready; the accepted rectangle,
/// Avalonia layout, and viewport surface are then advanced by the same UI transaction.
/// </summary>
internal sealed partial class EditorDockWin32PresentationResizeAdapter : IDisposable
{
    private const uint kWindowMessageSizing = 0x0214;
    private const uint kWindowMessageCancelMode = 0x001F;
    private const uint kWindowMessageWindowPositionChanging = 0x0046;
    private const uint kWindowMessageKeyDown = 0x0100;
    private const uint kWindowMessageDpiChanged = 0x02E0;
    private const uint kWindowMessageEnterSizeMove = 0x0231;
    private const uint kWindowMessageExitSizeMove = 0x0232;
    private const int kVirtualKeyEscape = 0x1B;
    private const uint kSetWindowPositionNoZOrder = 0x0004;
    private const uint kSetWindowPositionNoActivate = 0x0010;
    private const uint kSetWindowPositionNoSize = 0x0001;
    private const uint kSetWindowPositionNoMove = 0x0002;
    private readonly EditorDockPresentationLayoutHost host_;
    private readonly TopLevel topLevel_;
    private readonly nint windowHandle_;
    private readonly Win32Properties.CustomWndProcHookCallback hook_;
    private readonly Action drainLatestProposal_;
    private bool hasAcceptedRect_;
    private bool hasLatestProposal_;
    private bool hasObservedProposal_;
    private bool isCancelRestorationGuardActive_;
    private bool isDpiCancellationPending_;
    private bool isProposalDrainPosted_;
    private bool strictSizingEnabled_;
    private bool isApplyingWindowRect_;
    private bool isDisposed_;
    private bool isSizingInteractionActive_;
    private bool isSizingInteractionClosing_;
    private ulong nextSizingEpoch_;
    private ulong sizingEpoch_;
    private NativeRect acceptedRect_;
    private NativeRect lastProposedRect_;
    private ProjectionSnapshot projection_;

    private EditorDockWin32PresentationResizeAdapter(
        EditorDockPresentationLayoutHost host,
        TopLevel topLevel,
        nint windowHandle)
    {
        host_ = host;
        topLevel_ = topLevel;
        windowHandle_ = windowHandle;
        hook_ = OnWindowMessage;
        drainLatestProposal_ = DrainLatestProposal;
        Win32Properties.AddWndProcHookCallback(topLevel_, hook_);
    }

    public static EditorDockWin32PresentationResizeAdapter? TryAttach(
        EditorDockPresentationLayoutHost host)
    {
        ArgumentNullException.ThrowIfNull(host);
        if (!OperatingSystem.IsWindows() ||
            TopLevel.GetTopLevel(host) is not { } topLevel ||
            topLevel.TryGetPlatformHandle() is not { } platformHandle ||
            !string.Equals(platformHandle.HandleDescriptor, "HWND", StringComparison.Ordinal))
        {
            return null;
        }

        return new EditorDockWin32PresentationResizeAdapter(
            host,
            topLevel,
            platformHandle.Handle);
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        sizingEpoch_ = checked(++nextSizingEpoch_);
        hasLatestProposal_ = false;
        hasObservedProposal_ = false;
        isProposalDrainPosted_ = false;
        strictSizingEnabled_ = false;
        isSizingInteractionActive_ = false;
        isSizingInteractionClosing_ = false;
        isCancelRestorationGuardActive_ = false;
        isDpiCancellationPending_ = false;
        Win32Properties.RemoveWndProcHookCallback(topLevel_, hook_);
    }

    private nint OnWindowMessage(
        nint windowHandle,
        uint message,
        nint wParam,
        nint lParam,
        ref bool handled)
    {
        if (isDisposed_ || windowHandle != windowHandle_)
        {
            return 0;
        }

        if (isApplyingWindowRect_)
        {
            // SetWindowPos can synchronously dispatch WM_DPICHANGED. Avalonia must still process
            // that message, but the strict commit prepared for the old projection cannot publish.
            if (ShouldDeferDpiCancellation(
                    message,
                    isApplyingWindowRect_,
                    strictSizingEnabled_,
                    isSizingInteractionActive_,
                    isSizingInteractionClosing_))
            {
                isDpiCancellationPending_ = true;
            }
            return 0;
        }

        try
        {
            Dispatcher.UIThread.VerifyAccess();
            if (message == kWindowMessageEnterSizeMove)
            {
                BeginSizingInteraction();
                return 0;
            }
            if (message == kWindowMessageExitSizeMove)
            {
                EndSizingInteraction();
                return 0;
            }
            if (message == kWindowMessageWindowPositionChanging &&
                isCancelRestorationGuardActive_ && hasAcceptedRect_ && lParam != 0)
            {
                var position = Marshal.PtrToStructure<NativeWindowPosition>(lParam);
                position.X = acceptedRect_.Left;
                position.Y = acceptedRect_.Top;
                position.Width = acceptedRect_.Width;
                position.Height = acceptedRect_.Height;
                position.Flags &= ~(kSetWindowPositionNoSize | kSetWindowPositionNoMove);
                Marshal.StructureToPtr(position, lParam, fDeleteOld: false);
                // Consume exactly the modal-loop restoration caused by Escape/WM_CANCELMODE.
                // Keeping this guard armed would also suppress later Snap, maximize or
                // programmatic Window changes that intentionally use the fallback path.
                isCancelRestorationGuardActive_ = false;
                return 0;
            }
            if (ShouldHandleCancellationMessage(
                    message,
                    wParam,
                    strictSizingEnabled_,
                    isSizingInteractionActive_,
                    isSizingInteractionClosing_))
            {
                CancelSizingInteraction(guardNativeRestoration:
                    ShouldArmNativeRestorationGuard(
                        message,
                        wParam,
                        strictSizingEnabled_,
                        isSizingInteractionActive_,
                        isSizingInteractionClosing_));
                return 0;
            }
            if (message != kWindowMessageSizing || lParam == 0 ||
                !isSizingInteractionActive_ || sizingEpoch_ == 0 ||
                !strictSizingEnabled_ || !hasAcceptedRect_)
            {
                return 0;
            }

            // The native callback only copies the transient RECT, restores the last exact
            // rectangle and posts one coalesced UI drain. Projection, visual-tree discovery and
            // transaction allocation happen outside the hot WM_SIZING path.
            lastProposedRect_ = Marshal.PtrToStructure<NativeRect>(lParam);
            hasLatestProposal_ = true;
            hasObservedProposal_ = true;
            Marshal.StructureToPtr(acceptedRect_, lParam, fDeleteOld: false);
            handled = true;
            PostProposalDrainIfNeeded();
            return 1;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Win32 viewport sizing proposal failed: {0}", exception);
            if (message == kWindowMessageSizing && lParam != 0 &&
                strictSizingEnabled_ && hasAcceptedRect_)
            {
                // Once a visible exact Scene entered strict sizing, failure must keep the last
                // exact HWND rather than release one blank/cropped fallback frame.
                try
                {
                    Marshal.StructureToPtr(acceptedRect_, lParam, fDeleteOld: false);
                    handled = true;
                    return 1;
                }
                catch (Exception failClosedException)
                {
                    Trace.TraceError(
                        "Win32 viewport sizing could not restore the accepted RECT: {0}",
                        failClosedException);
                }
            }
            return 0;
        }
    }

    private void BeginSizingInteraction()
    {
        sizingEpoch_ = checked(++nextSizingEpoch_);
        isSizingInteractionActive_ = true;
        isSizingInteractionClosing_ = false;
        isCancelRestorationGuardActive_ = false;
        isDpiCancellationPending_ = false;
        hasLatestProposal_ = false;
        hasObservedProposal_ = false;
        strictSizingEnabled_ = false;
        hasAcceptedRect_ = false;
        if (!host_.CanStartPrecommittedWindowResize() ||
            !TryCaptureProjection(out projection_))
        {
            return;
        }

        acceptedRect_ = projection_.WindowRect;
        hasAcceptedRect_ = true;
        strictSizingEnabled_ = true;
    }

    internal static bool ShouldHandleCancellationMessage(
        uint message,
        nint wParam,
        bool strictSizingEnabled,
        bool isSizingInteractionActive,
        bool isSizingInteractionClosing) =>
        strictSizingEnabled &&
        (isSizingInteractionActive || isSizingInteractionClosing) &&
        (message == kWindowMessageCancelMode ||
         message == kWindowMessageDpiChanged ||
         message == kWindowMessageKeyDown && wParam == kVirtualKeyEscape);

    internal static bool ShouldArmNativeRestorationGuard(
        uint message,
        nint wParam,
        bool strictSizingEnabled,
        bool isSizingInteractionActive,
        bool isSizingInteractionClosing) =>
        // Only the live USER32 modal sizing loop can issue the native restoration that needs
        // interception. A transaction may remain in the closing state after WM_EXITSIZEMOVE,
        // but arming the guard there would consume the next unrelated Snap/maximize/programmatic
        // WINDOWPOS change.
        isSizingInteractionActive &&
        message != kWindowMessageDpiChanged &&
        ShouldHandleCancellationMessage(
            message,
            wParam,
            strictSizingEnabled,
            isSizingInteractionActive,
            isSizingInteractionClosing);

    internal static bool ShouldDeferDpiCancellation(
        uint message,
        bool isApplyingWindowRect,
        bool strictSizingEnabled,
        bool isSizingInteractionActive,
        bool isSizingInteractionClosing) =>
        isApplyingWindowRect &&
        message == kWindowMessageDpiChanged &&
        ShouldHandleCancellationMessage(
            message,
            0,
            strictSizingEnabled,
            isSizingInteractionActive,
            isSizingInteractionClosing);

    private void EndSizingInteraction()
    {
        if (!isSizingInteractionActive_)
        {
            isCancelRestorationGuardActive_ = false;
            return;
        }

        isSizingInteractionActive_ = false;
        isSizingInteractionClosing_ = strictSizingEnabled_ && hasObservedProposal_;
        if (!isSizingInteractionClosing_)
        {
            sizingEpoch_ = checked(++nextSizingEpoch_);
        }
        PostProposalDrainIfNeeded();
    }

    private void CancelSizingInteraction(bool guardNativeRestoration = false)
    {
        sizingEpoch_ = checked(++nextSizingEpoch_);
        isSizingInteractionActive_ = false;
        isSizingInteractionClosing_ = false;
        hasLatestProposal_ = false;
        hasObservedProposal_ = false;
        strictSizingEnabled_ = false;
        isDpiCancellationPending_ = false;
        isCancelRestorationGuardActive_ = guardNativeRestoration && hasAcceptedRect_;
    }

    private bool ConsumePendingDpiCancellation()
    {
        var pending = isDpiCancellationPending_;
        isDpiCancellationPending_ = false;
        return pending;
    }

    private void PostProposalDrainIfNeeded()
    {
        if (isProposalDrainPosted_ || !hasLatestProposal_ || isDisposed_)
        {
            return;
        }

        isProposalDrainPosted_ = true;
        // The HWND has not changed, so admission does not need to wait for layout settlement.
        // Start production before the next Render-priority composition turn; publication still
        // occurs through the transaction coordinator at the exact committed layout.
        Dispatcher.UIThread.Post(drainLatestProposal_, DispatcherPriority.Send);
    }

    private void DrainLatestProposal()
    {
        try
        {
            Dispatcher.UIThread.VerifyAccess();
            isProposalDrainPosted_ = false;
            if (isDisposed_ || !strictSizingEnabled_ || !hasLatestProposal_ ||
                sizingEpoch_ == 0)
            {
                return;
            }

            var epoch = sizingEpoch_;
            var proposedRect = lastProposedRect_;
            hasLatestProposal_ = false;
            if (!TryCalculateWorkspaceTarget(projection_, proposedRect, out var targetSize))
            {
                throw new InvalidOperationException(
                    "The proposed Window rectangle could not be projected to the workspace.");
            }

            var windowCommit = new WindowRectCommit(this, epoch, proposedRect, projection_);
            if (!host_.TryQueuePrecommittedWindowResize(
                    targetSize,
                    new EditorDockPresentationOuterLayoutCommit(
                        windowCommit.Apply,
                        windowCommit.Rollback,
                        windowCommit.Accept,
                        windowCommit.IsCurrent)))
            {
                throw new InvalidOperationException(
                    "The exact Window presentation proposal was rejected.");
            }
        }
        catch (Exception exception)
        {
            isProposalDrainPosted_ = false;
            Trace.TraceError("Win32 viewport sizing drain failed: {0}", exception);
            if (isSizingInteractionClosing_)
            {
                CancelSizingInteraction();
            }
        }
    }

    private bool TryCaptureProjection(out ProjectionSnapshot projection)
    {
        projection = default;
        var scaling = topLevel_.RenderScaling;
        if (!GetWindowRect(windowHandle_, out var windowRect) ||
            !GetClientRect(windowHandle_, out var clientRect) ||
            !double.IsFinite(scaling) || scaling <= 0)
        {
            return false;
        }

        projection = new ProjectionSnapshot(
            windowRect,
            clientRect.Width,
            clientRect.Height,
            topLevel_.ClientSize.Width - host_.Bounds.Width,
            topLevel_.ClientSize.Height - host_.Bounds.Height,
            scaling);
        return true;
    }

    private static bool TryCalculateWorkspaceTarget(
        ProjectionSnapshot projection,
        NativeRect proposedRect,
        out Size targetSize)
    {
        return TryProjectWorkspaceTarget(
            new Size(projection.WindowRect.Width, projection.WindowRect.Height),
            new Size(projection.ClientWidth, projection.ClientHeight),
            new Size(proposedRect.Width, proposedRect.Height),
            new Size(projection.FixedLogicalWidth, projection.FixedLogicalHeight),
            projection.Scaling,
            out targetSize);
    }

    internal static bool TryProjectWorkspaceTarget(
        Size currentOuterPixels,
        Size currentClientPixels,
        Size proposedOuterPixels,
        Size fixedLogicalInsets,
        double scaling,
        out Size targetSize)
    {
        targetSize = default;
        if (!double.IsFinite(scaling) || scaling <= 0)
        {
            return false;
        }

        var targetClientWidth = proposedOuterPixels.Width -
            (currentOuterPixels.Width - currentClientPixels.Width);
        var targetClientHeight = proposedOuterPixels.Height -
            (currentOuterPixels.Height - currentClientPixels.Height);
        if (targetClientWidth <= 0 || targetClientHeight <= 0)
        {
            return false;
        }

        var targetWidth = (targetClientWidth / scaling) - fixedLogicalInsets.Width;
        var targetHeight = (targetClientHeight / scaling) - fixedLogicalInsets.Height;
        if (!double.IsFinite(targetWidth) || !double.IsFinite(targetHeight) ||
            targetWidth <= 0 || targetHeight <= 0)
        {
            return false;
        }

        targetSize = new Size(targetWidth, targetHeight);
        return true;
    }

    private void ApplyWindowRect(NativeRect targetRect)
    {
        if (isDisposed_)
        {
            throw new ObjectDisposedException(nameof(EditorDockWin32PresentationResizeAdapter));
        }

        isApplyingWindowRect_ = true;
        try
        {
            if (!SetWindowPos(
                    windowHandle_,
                    0,
                    targetRect.Left,
                    targetRect.Top,
                    targetRect.Width,
                    targetRect.Height,
                    kSetWindowPositionNoZOrder | kSetWindowPositionNoActivate))
            {
                throw new Win32Exception(Marshal.GetLastPInvokeError());
            }

            if (!GetWindowRect(windowHandle_, out var actualRect))
            {
                throw new Win32Exception(Marshal.GetLastPInvokeError());
            }
            if (actualRect.Width != targetRect.Width || actualRect.Height != targetRect.Height)
            {
                throw new InvalidOperationException(
                    $"USER32 committed {actualRect.Width}x{actualRect.Height} instead of " +
                    $"{targetRect.Width}x{targetRect.Height}.");
            }

            topLevel_.UpdateLayout();
        }
        finally
        {
            isApplyingWindowRect_ = false;
        }
    }

    private void ValidateCommit(ulong epoch, NativeRect targetRect)
    {
        if (isDisposed_ || epoch == 0 || epoch != sizingEpoch_ ||
            !isSizingInteractionActive_ && !isSizingInteractionClosing_)
        {
            throw new OperationCanceledException(
                "The interactive Window resize proposal is no longer current.");
        }
        if (!hasAcceptedRect_ || !GetWindowRect(windowHandle_, out var currentRect) ||
            currentRect != acceptedRect_)
        {
            throw new OperationCanceledException(
                "The predecessor Window rectangle changed during exact resize preparation.");
        }
    }

    private void ValidateProjection(ProjectionSnapshot projection)
    {
        var scaling = topLevel_.RenderScaling;
        var fixedLogicalWidth = topLevel_.ClientSize.Width - host_.Bounds.Width;
        var fixedLogicalHeight = topLevel_.ClientSize.Height - host_.Bounds.Height;
        if (Math.Abs(scaling - projection.Scaling) > double.Epsilon ||
            Math.Abs(fixedLogicalWidth - projection.FixedLogicalWidth) >
                LayoutHelper.LayoutEpsilon ||
            Math.Abs(fixedLogicalHeight - projection.FixedLogicalHeight) >
                LayoutHelper.LayoutEpsilon)
        {
            throw new OperationCanceledException(
                "The Window DPI or chrome/layout epoch changed during exact resize preparation.");
        }
    }

    private void AcceptCommit(ulong epoch, NativeRect targetRect)
    {
        if (epoch == sizingEpoch_)
        {
            acceptedRect_ = GetWindowRect(windowHandle_, out var actualRect)
                ? actualRect
                : targetRect;
            hasAcceptedRect_ = true;
            if (TryCaptureProjection(out var acceptedProjection))
            {
                projection_ = acceptedProjection;
            }
        }
        if (epoch == sizingEpoch_ && isSizingInteractionClosing_ &&
            targetRect == lastProposedRect_)
        {
            sizingEpoch_ = checked(++nextSizingEpoch_);
            isSizingInteractionClosing_ = false;
            hasObservedProposal_ = false;
            strictSizingEnabled_ = false;
        }
    }

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetWindowRect(nint windowHandle, out NativeRect rectangle);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetClientRect(nint windowHandle, out NativeRect rectangle);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetWindowPos(
        nint windowHandle,
        nint insertAfter,
        int x,
        int y,
        int width,
        int height,
        uint flags);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;

        public int Width => checked(Right - Left);

        public int Height => checked(Bottom - Top);

        public static bool operator ==(NativeRect left, NativeRect right) =>
            left.Left == right.Left && left.Top == right.Top &&
            left.Right == right.Right && left.Bottom == right.Bottom;

        public static bool operator !=(NativeRect left, NativeRect right) => !(left == right);

        public override bool Equals(object? obj) => obj is NativeRect other && this == other;

        public override int GetHashCode() => HashCode.Combine(Left, Top, Right, Bottom);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeWindowPosition
    {
        public nint WindowHandle;
        public nint InsertAfter;
        public int X;
        public int Y;
        public int Width;
        public int Height;
        public uint Flags;
    }

    private readonly record struct ProjectionSnapshot(
        NativeRect WindowRect,
        int ClientWidth,
        int ClientHeight,
        double FixedLogicalWidth,
        double FixedLogicalHeight,
        double Scaling);

    private sealed class WindowRectCommit(
        EditorDockWin32PresentationResizeAdapter owner,
        ulong epoch,
        NativeRect targetRect,
        ProjectionSnapshot projection)
    {
        private NativeRect rollbackRect_;
        private bool hasRollbackRect_;

        public void Apply()
        {
            try
            {
                owner.ValidateCommit(epoch, targetRect);
                owner.ValidateProjection(projection);
                if (!GetWindowRect(owner.windowHandle_, out rollbackRect_))
                {
                    throw new Win32Exception(Marshal.GetLastPInvokeError());
                }

                hasRollbackRect_ = true;
                owner.ApplyWindowRect(targetRect);
                if (owner.ConsumePendingDpiCancellation())
                {
                    throw new OperationCanceledException(
                        "The Window DPI changed while committing the exact resize rectangle.");
                }
                // SetWindowPos can move a border across monitors and synchronously change DPI
                // while the hook is suppressed for our own commit. Revalidate before any viewport
                // surface is published; a changed epoch falls back instead of looping strict
                // proposals against stale physical extents.
                owner.ValidateProjection(projection);
            }
            catch (OperationCanceledException)
            {
                owner.CancelSizingInteraction();
                throw;
            }
            catch
            {
                if (owner.ConsumePendingDpiCancellation())
                {
                    owner.CancelSizingInteraction();
                }
                throw;
            }
        }

        public void Rollback()
        {
            if (hasRollbackRect_ && !owner.isDisposed_)
            {
                owner.ApplyWindowRect(rollbackRect_);
            }
        }

        public void Accept() => owner.AcceptCommit(epoch, targetRect);

        public bool IsCurrent()
        {
            try
            {
                owner.ValidateCommit(epoch, targetRect);
                return true;
            }
            catch (OperationCanceledException)
            {
                return false;
            }
        }
    }
}
