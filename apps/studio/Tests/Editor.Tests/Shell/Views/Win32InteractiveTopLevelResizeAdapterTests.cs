#if ASHARIA_STUDIO_WINDOWS
using Asharia.Studio.Presentation.Avalonia.Windows.Windowing;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class Win32InteractiveTopLevelResizeAdapterTests
{
    [Fact]
    public void Escape_only_cancels_an_active_strict_sizing_interaction()
    {
        const uint windowMessageKeyDown = 0x0100;
        const int virtualKeyEscape = 0x1B;

        Assert.False(Win32InteractiveTopLevelResizeAdapter.ShouldHandleCancellationMessage(
            windowMessageKeyDown,
            virtualKeyEscape,
            strictSizingEnabled: true,
            isSizingInteractionActive: false));
        Assert.True(Win32InteractiveTopLevelResizeAdapter.ShouldHandleCancellationMessage(
            windowMessageKeyDown,
            virtualKeyEscape,
            strictSizingEnabled: true,
            isSizingInteractionActive: true));
        Assert.False(Win32InteractiveTopLevelResizeAdapter.ShouldHandleCancellationMessage(
            windowMessageKeyDown,
            virtualKeyEscape,
            strictSizingEnabled: false,
            isSizingInteractionActive: true));
    }

    [Fact]
    public void Ordinary_escape_does_not_arm_a_stale_native_restoration_guard()
    {
        const uint windowMessageKeyDown = 0x0100;
        const uint windowMessageDpiChanged = 0x02E0;
        const int virtualKeyEscape = 0x1B;

        Assert.False(Win32InteractiveTopLevelResizeAdapter.ShouldArmNativeRestorationGuard(
            windowMessageKeyDown,
            virtualKeyEscape,
            strictSizingEnabled: true,
            isSizingInteractionActive: false));
        Assert.True(Win32InteractiveTopLevelResizeAdapter.ShouldArmNativeRestorationGuard(
            windowMessageKeyDown,
            virtualKeyEscape,
            strictSizingEnabled: true,
            isSizingInteractionActive: true));
        Assert.False(Win32InteractiveTopLevelResizeAdapter.ShouldArmNativeRestorationGuard(
            message: windowMessageDpiChanged,
            wParam: 0,
            strictSizingEnabled: true,
            isSizingInteractionActive: true));
    }

    [Theory]
    [InlineData(true, true, true, true)]
    [InlineData(false, true, true, false)]
    [InlineData(true, false, true, false)]
    [InlineData(true, true, false, false)]
    public void Nested_dpi_change_is_deferred_only_for_an_applying_strict_transaction(
        bool isApplyingWindowRect,
        bool strictSizingEnabled,
        bool isSizingInteractionActive,
        bool expected)
    {
        const uint windowMessageDpiChanged = 0x02E0;

        Assert.Equal(
            expected,
            Win32InteractiveTopLevelResizeAdapter.ShouldDeferDpiCancellation(
                windowMessageDpiChanged,
                isApplyingWindowRect,
                strictSizingEnabled,
                isSizingInteractionActive));
    }
}
#endif
