using Asharia.Studio.Application.Actions;
using Avalonia.Input;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class StudioActionShortcutRouterTests
{
    [Theory]
    [InlineData(KeyModifiers.Control)]
    [InlineData(KeyModifiers.Meta)]
    public void Exactly_one_platform_primary_modifier_maps_to_control(
        KeyModifiers input)
    {
        Assert.True(StudioActionShortcutRouter.TryNormalizePrimaryModifiers(
            input,
            out var normalized));
        Assert.Equal(StudioShortcutModifiers.Control, normalized);
    }

    [Theory]
    [InlineData(KeyModifiers.None)]
    [InlineData(KeyModifiers.Alt)]
    [InlineData(KeyModifiers.Control | KeyModifiers.Meta)]
    [InlineData(KeyModifiers.Control | KeyModifiers.Alt)]
    public void Ambiguous_or_alt_modifier_combinations_fail_closed(KeyModifiers input)
    {
        Assert.False(StudioActionShortcutRouter.TryNormalizePrimaryModifiers(
            input,
            out var normalized));
        Assert.Equal(StudioShortcutModifiers.None, normalized);
    }

    [Theory]
    [InlineData(KeyModifiers.Control | KeyModifiers.Shift)]
    [InlineData(KeyModifiers.Meta | KeyModifiers.Shift)]
    public void Shift_is_preserved_with_one_platform_primary_modifier(
        KeyModifiers input)
    {
        Assert.True(StudioActionShortcutRouter.TryNormalizePrimaryModifiers(
            input,
            out var normalized));
        Assert.Equal(
            StudioShortcutModifiers.Control | StudioShortcutModifiers.Shift,
            normalized);
    }
}
