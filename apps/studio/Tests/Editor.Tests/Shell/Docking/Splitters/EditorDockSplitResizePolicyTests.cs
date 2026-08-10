using System;
using Avalonia.Controls;
using Editor.Shell.Docking.Splitters;
using Xunit;

namespace Editor.Tests.Shell.Docking.Splitters;

public sealed class EditorDockSplitResizePolicyTests
{
    [Fact]
    public void Star_pair_preserves_total_and_uses_pixel_equivalent_star_weights()
    {
        var resolved = EditorDockSplitResizePolicy.TryResolve(
            CreateInput(
                new GridLength(1d, GridUnitType.Star),
                firstActual: 120d,
                new GridLength(2d, GridUnitType.Star),
                secondActual: 180d,
                requestedDelta: 25d),
            out var proposal);

        Assert.True(resolved);
        Assert.True(proposal.FirstLength.IsStar);
        Assert.True(proposal.SecondLength.IsStar);
        Assert.Equal(145d, proposal.FirstLength.Value);
        Assert.Equal(155d, proposal.SecondLength.Value);
        Assert.Equal(300d, proposal.FirstActualLength + proposal.SecondActualLength);
    }

    [Fact]
    public void First_fixed_definition_is_resized_and_second_length_is_preserved()
    {
        var secondLength = new GridLength(1d, GridUnitType.Star);
        var resolved = EditorDockSplitResizePolicy.TryResolve(
            CreateInput(
                new GridLength(100d),
                firstActual: 100d,
                secondLength,
                secondActual: 200d,
                requestedDelta: 30d),
            out var proposal);

        Assert.True(resolved);
        Assert.True(proposal.FirstLength.IsAbsolute);
        Assert.Equal(130d, proposal.FirstLength.Value);
        Assert.Equal(secondLength, proposal.SecondLength);
        Assert.Equal(170d, proposal.SecondActualLength);
    }

    [Fact]
    public void Second_fixed_definition_is_resized_when_first_is_star()
    {
        var firstLength = new GridLength(1d, GridUnitType.Star);
        var resolved = EditorDockSplitResizePolicy.TryResolve(
            CreateInput(
                firstLength,
                firstActual: 100d,
                new GridLength(200d),
                secondActual: 200d,
                requestedDelta: 30d),
            out var proposal);

        Assert.True(resolved);
        Assert.Equal(firstLength, proposal.FirstLength);
        Assert.True(proposal.SecondLength.IsAbsolute);
        Assert.Equal(170d, proposal.SecondLength.Value);
    }

    [Theory]
    [InlineData(50d, 10d)]
    [InlineData(-50d, -10d)]
    public void Delta_is_clamped_by_both_definitions(double requestedDelta, double expectedDelta)
    {
        var input = new EditorDockSplitResizePolicyInput(
            new EditorDockSplitResizeDefinition(new GridLength(100d), 100d, 80d, 120d),
            new EditorDockSplitResizeDefinition(
                new GridLength(100d, GridUnitType.Star),
                100d,
                90d,
                110d),
            OriginalCombinedActualLength: 200d,
            RequestedDelta: requestedDelta,
            UseLayoutRounding: false,
            LayoutScale: 1d);

        Assert.True(EditorDockSplitResizePolicy.TryResolve(input, out var proposal));
        Assert.Equal(expectedDelta, proposal.AppliedDelta);
        Assert.Equal(-10d, proposal.MinimumDelta);
        Assert.Equal(10d, proposal.MaximumDelta);
    }

    [Fact]
    public void Delta_uses_Avalonia_layout_rounding_at_render_scale()
    {
        var input = CreateInput(
            new GridLength(100d),
            firstActual: 100d,
            new GridLength(100d, GridUnitType.Star),
            secondActual: 100d,
            requestedDelta: 0.4d,
            useLayoutRounding: true,
            layoutScale: 1.5d);

        Assert.True(EditorDockSplitResizePolicy.TryResolve(input, out var proposal));
        Assert.Equal(2d / 3d, proposal.AppliedDelta, precision: 10);
    }

    [Fact]
    public void Star_pair_rejects_total_drift_larger_than_one_physical_pixel()
    {
        var input = new EditorDockSplitResizePolicyInput(
            new EditorDockSplitResizeDefinition(
                new GridLength(1d, GridUnitType.Star),
                100d,
                0d,
                double.PositiveInfinity),
            new EditorDockSplitResizeDefinition(
                new GridLength(1d, GridUnitType.Star),
                102d,
                0d,
                double.PositiveInfinity),
            OriginalCombinedActualLength: 200d,
            RequestedDelta: 1d,
            UseLayoutRounding: true,
            LayoutScale: 1d);

        Assert.False(EditorDockSplitResizePolicy.TryResolve(input, out _));
    }

    [Theory]
    [InlineData(double.NaN, 1d)]
    [InlineData(1d, 0d)]
    [InlineData(1d, double.PositiveInfinity)]
    public void Non_finite_delta_or_invalid_scale_is_rejected(double delta, double layoutScale)
    {
        var input = CreateInput(
            new GridLength(100d),
            firstActual: 100d,
            new GridLength(1d, GridUnitType.Star),
            secondActual: 100d,
            requestedDelta: delta,
            layoutScale: layoutScale);

        Assert.False(EditorDockSplitResizePolicy.TryResolve(input, out _));
    }

    [Fact]
    public void Cumulative_delta_is_always_resolved_from_the_drag_start_snapshot()
    {
        var origin = CreateInput(
            new GridLength(1d, GridUnitType.Star),
            firstActual: 400d,
            new GridLength(1d, GridUnitType.Star),
            secondActual: 400d,
            requestedDelta: 10d);
        Assert.True(EditorDockSplitResizePolicy.TryResolve(origin, out var firstProposal));
        Assert.Equal(410d, firstProposal.FirstActualLength);

        var nextFromSameOrigin = origin with { RequestedDelta = 20d };
        Assert.True(EditorDockSplitResizePolicy.TryResolve(nextFromSameOrigin, out var nextProposal));
        Assert.Equal(420d, nextProposal.FirstActualLength);
    }

    private static EditorDockSplitResizePolicyInput CreateInput(
        GridLength firstLength,
        double firstActual,
        GridLength secondLength,
        double secondActual,
        double requestedDelta,
        bool useLayoutRounding = false,
        double layoutScale = 1d)
    {
        return new EditorDockSplitResizePolicyInput(
            new EditorDockSplitResizeDefinition(
                firstLength,
                firstActual,
                0d,
                double.PositiveInfinity),
            new EditorDockSplitResizeDefinition(
                secondLength,
                secondActual,
                0d,
                double.PositiveInfinity),
            firstActual + secondActual,
            requestedDelta,
            useLayoutRounding,
            layoutScale);
    }
}
