using System;
using Xunit;

namespace Asharia.Studio.WindowsCapture.Tests;

public sealed class DwmCompositedSentinelAnalyzerTests
{
    [Fact]
    public void Analyze_locates_exact_native_scene_corner_blocks()
    {
        var frame = CreateSentinelFrame(
            width: 160,
            height: 120,
            sceneX: 8,
            sceneY: 6,
            sceneWidth: 144,
            sceneHeight: 108,
            sentinelEdge: 24);

        var observation = Analyze(frame, 160, 120);

        Assert.True(observation.IsExact);
        Assert.Equal(new DwmCompositedSceneInsets(8, 6, 8, 6), observation.Insets);
    }

    [Fact]
    public void Analyze_rejects_stretched_sentinel_blocks()
    {
        var frame = CreateSentinelFrame(
            width: 200,
            height: 160,
            sceneX: 8,
            sceneY: 6,
            sceneWidth: 184,
            sceneHeight: 148,
            sentinelEdge: 36);

        var observation = Analyze(frame, 200, 160);

        Assert.True(observation.Located);
        Assert.False(observation.HasExactBlockSizes);
        Assert.False(observation.IsExact);
    }

    [Fact]
    public void Compare_allows_only_a_right_bottom_transitional_grow_gap()
    {
        var baseline = Analyze(
            CreateSentinelFrame(160, 120, 8, 6, 144, 108, 24),
            160,
            120);
        var grownWithStaleScene = Analyze(
            CreateSentinelFrame(184, 136, 8, 6, 144, 108, 24),
            184,
            136);

        var continuity = DwmCompositedSentinelAnalyzer.Compare(
            baseline,
            grownWithStaleScene);

        Assert.True(grownWithStaleScene.IsExact);
        Assert.True(continuity.LeftTopInsetsMatch);
        Assert.False(continuity.RightBottomInsetsMatch);
        Assert.True(continuity.RightBottomInsetsDoNotDecrease);
        Assert.Equal(24, continuity.RightGapPixels);
        Assert.Equal(16, continuity.BottomGapPixels);
        Assert.True(continuity.IsAllowedGrowGap);
        Assert.True(continuity.IsAcceptableForGrow);
        Assert.False(continuity.IsExact);
    }

    [Fact]
    public void Compare_rejects_a_shrink_frame_that_reduces_right_bottom_insets()
    {
        var baseline = Analyze(
            CreateSentinelFrame(160, 120, 8, 6, 144, 108, 24),
            160,
            120);
        var shrunkAroundStaleScene = Analyze(
            CreateSentinelFrame(156, 116, 8, 6, 144, 108, 24),
            156,
            116);

        var continuity = DwmCompositedSentinelAnalyzer.Compare(
            baseline,
            shrunkAroundStaleScene);

        Assert.True(shrunkAroundStaleScene.IsExact);
        Assert.True(continuity.LeftTopInsetsMatch);
        Assert.False(continuity.RightBottomInsetsMatch);
        Assert.False(continuity.RightBottomInsetsDoNotDecrease);
        Assert.Equal(0, continuity.RightGapPixels);
        Assert.Equal(0, continuity.BottomGapPixels);
        Assert.False(continuity.IsAllowedGrowGap);
        Assert.False(continuity.IsAcceptableForGrow);
        Assert.False(continuity.IsExact);
    }

    [Fact]
    public void Compare_rejects_a_grow_gap_when_either_trailing_inset_decreases_beyond_tolerance()
    {
        var baseline = Analyze(
            CreateSentinelFrame(160, 120, 8, 6, 144, 108, 24),
            160,
            120);
        var mixedDirectionInsets = Analyze(
            CreateSentinelFrame(184, 116, 8, 6, 144, 108, 24),
            184,
            116);

        var continuity = DwmCompositedSentinelAnalyzer.Compare(
            baseline,
            mixedDirectionInsets);

        Assert.True(continuity.LeftTopInsetsMatch);
        Assert.False(continuity.RightBottomInsetsMatch);
        Assert.False(continuity.RightBottomInsetsDoNotDecrease);
        Assert.False(continuity.IsAllowedGrowGap);
        Assert.False(continuity.IsAcceptableForGrow);
    }

    [Fact]
    public void Compare_tolerates_one_pixel_of_corner_measurement_jitter_during_a_grow_gap()
    {
        var baseline = Analyze(
            CreateSentinelFrame(160, 120, 8, 6, 144, 108, 24),
            160,
            120);
        var roundedCornerJitter = Analyze(
            CreateSentinelFrame(184, 119, 8, 6, 144, 108, 24),
            184,
            119);

        var continuity = DwmCompositedSentinelAnalyzer.Compare(
            baseline,
            roundedCornerJitter);

        Assert.True(continuity.RightBottomInsetsDoNotDecrease);
        Assert.True(continuity.IsAllowedGrowGap);
        Assert.True(continuity.IsAcceptableForGrow);
    }

    [Fact]
    public void Compare_rejects_scene_inset_drift()
    {
        var baseline = Analyze(
            CreateSentinelFrame(160, 120, 8, 6, 144, 108, 24),
            160,
            120);
        var shifted = Analyze(
            CreateSentinelFrame(160, 120, 12, 6, 140, 108, 24),
            160,
            120);

        var continuity = DwmCompositedSentinelAnalyzer.Compare(
            baseline,
            shifted,
            insetTolerance: 2);

        Assert.True(shifted.IsExact);
        Assert.False(continuity.LeftTopInsetsMatch);
        Assert.True(continuity.RightBottomInsetsMatch);
        Assert.False(continuity.IsAllowedGrowGap);
        Assert.False(continuity.IsAcceptableForGrow);
        Assert.False(continuity.IsExact);
    }

    [Fact]
    public void Analyze_rejects_a_partial_corner_set()
    {
        const int width = 160;
        const int height = 120;
        const int stride = width * 4;
        var pixels = CreateFrame(width, height, new Bgra32(32, 32, 32));
        Fill(pixels, stride, 8, 6, 24, DwmCompositedSentinelAnalyzer.TopLeft);
        Fill(pixels, stride, 128, 6, 24, DwmCompositedSentinelAnalyzer.TopRight);
        Fill(pixels, stride, 8, 90, 24, DwmCompositedSentinelAnalyzer.BottomLeft);

        var observation = Analyze(pixels, width, height);

        Assert.False(observation.Located);
        Assert.False(observation.IsBlank);
    }

    [Fact]
    public void Analyze_reports_an_all_black_frame_as_blank()
    {
        const int width = 32;
        const int height = 32;
        var pixels = new byte[width * height * 4];

        var observation = Analyze(pixels, width, height);

        Assert.False(observation.Located);
        Assert.True(observation.IsBlank);
    }

    [Fact]
    public void Analyze_rejects_an_undersized_stride()
    {
        var pixels = new byte[32 * 32 * 4];

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            DwmCompositedSentinelAnalyzer.Analyze(pixels, 32, 32, 31 * 4));
    }

    private static DwmCompositedSentinelObservation Analyze(
        byte[] pixels,
        int width,
        int height) =>
        DwmCompositedSentinelAnalyzer.Analyze(pixels, width, height, width * 4);

    private static byte[] CreateSentinelFrame(
        int width,
        int height,
        int sceneX,
        int sceneY,
        int sceneWidth,
        int sceneHeight,
        int sentinelEdge)
    {
        var stride = width * 4;
        var pixels = CreateFrame(width, height, new Bgra32(32, 32, 32));
        Fill(
            pixels,
            stride,
            sceneX,
            sceneY,
            sentinelEdge,
            DwmCompositedSentinelAnalyzer.TopLeft);
        Fill(
            pixels,
            stride,
            sceneX + sceneWidth - sentinelEdge,
            sceneY,
            sentinelEdge,
            DwmCompositedSentinelAnalyzer.TopRight);
        Fill(
            pixels,
            stride,
            sceneX,
            sceneY + sceneHeight - sentinelEdge,
            sentinelEdge,
            DwmCompositedSentinelAnalyzer.BottomLeft);
        Fill(
            pixels,
            stride,
            sceneX + sceneWidth - sentinelEdge,
            sceneY + sceneHeight - sentinelEdge,
            sentinelEdge,
            DwmCompositedSentinelAnalyzer.BottomRight);
        return pixels;
    }

    private static byte[] CreateFrame(int width, int height, Bgra32 color)
    {
        var stride = width * 4;
        var pixels = new byte[stride * height];
        Fill(pixels, stride, 0, 0, width, height, color);
        return pixels;
    }

    private static void Fill(
        byte[] pixels,
        int stride,
        int x,
        int y,
        int edge,
        Bgra32 color) =>
        Fill(pixels, stride, x, y, edge, edge, color);

    private static void Fill(
        byte[] pixels,
        int stride,
        int x,
        int y,
        int width,
        int height,
        Bgra32 color)
    {
        for (var row = y; row < y + height; row++)
        {
            for (var column = x; column < x + width; column++)
            {
                var offset = (row * stride) + (column * 4);
                pixels[offset] = color.Blue;
                pixels[offset + 1] = color.Green;
                pixels[offset + 2] = color.Red;
                pixels[offset + 3] = color.Alpha;
            }
        }
    }
}
