using System;
using System.Buffers;
using System.Collections.Generic;

namespace Asharia.Studio.WindowsCapture.Tests;

internal readonly record struct Bgra32(byte Blue, byte Green, byte Red, byte Alpha = 255);

internal readonly record struct PixelRectangle(int X, int Y, int Width, int Height)
{
    public int Right => checked(X + Width);

    public int Bottom => checked(Y + Height);
}

internal readonly record struct DwmCompositedSentinelLayout(
    PixelRectangle TopLeft,
    PixelRectangle TopRight,
    PixelRectangle BottomLeft,
    PixelRectangle BottomRight)
{
    public PixelRectangle SceneBounds => new(
        Math.Min(TopLeft.X, BottomLeft.X),
        Math.Min(TopLeft.Y, TopRight.Y),
        Math.Max(TopRight.Right, BottomRight.Right) -
            Math.Min(TopLeft.X, BottomLeft.X),
        Math.Max(BottomLeft.Bottom, BottomRight.Bottom) -
            Math.Min(TopLeft.Y, TopRight.Y));
}

internal readonly record struct DwmCompositedSceneInsets(
    int Left,
    int Top,
    int Right,
    int Bottom)
{
    public static DwmCompositedSceneInsets From(
        DwmCompositedSentinelLayout layout,
        int frameWidth,
        int frameHeight)
    {
        var scene = layout.SceneBounds;
        return new DwmCompositedSceneInsets(
            scene.X,
            scene.Y,
            checked(frameWidth - scene.Right),
            checked(frameHeight - scene.Bottom));
    }
}

internal readonly record struct DwmCompositedSentinelObservation(
    bool Located,
    bool IsBlank,
    bool HasExactBlockSizes,
    bool HasAlignedCorners,
    DwmCompositedSentinelLayout Layout,
    DwmCompositedSceneInsets Insets)
{
    public bool IsExact =>
        Located &&
        !IsBlank &&
        HasExactBlockSizes &&
        HasAlignedCorners;
}

internal readonly record struct DwmCompositedSentinelContinuity(
    bool CurrentIsExact,
    bool LeftTopInsetsMatch,
    bool RightBottomInsetsMatch,
    bool RightBottomInsetsDoNotDecrease,
    int RightGapPixels,
    int BottomGapPixels)
{
    public bool IsExact =>
        CurrentIsExact &&
        LeftTopInsetsMatch &&
        RightBottomInsetsMatch;

    public bool IsAllowedGrowGap =>
        CurrentIsExact &&
        LeftTopInsetsMatch &&
        !RightBottomInsetsMatch &&
        RightBottomInsetsDoNotDecrease;

    public bool IsAcceptableForGrow => IsExact || IsAllowedGrowGap;
}

internal static class DwmCompositedSentinelAnalyzer
{
    internal static readonly Bgra32 TopLeft = new(255, 0, 255);
    internal static readonly Bgra32 TopRight = new(0, 255, 0);
    internal static readonly Bgra32 BottomLeft = new(255, 255, 0);
    internal static readonly Bgra32 BottomRight = new(0, 255, 255);

    private const int kExpectedBlockEdge = 24;
    private const int kBlockEdgeTolerance = 2;
    private const int kAlignmentTolerance = 2;
    private const int kMinimumCandidateEdge = 6;
    private const int kMinimumViewportSpan = 32;
    private const double kMinimumComponentDensity = 0.75;
    private const byte kColorTolerance = 12;

    public static DwmCompositedSentinelObservation Analyze(
        ReadOnlySpan<byte> bgra,
        int width,
        int height,
        int stride)
    {
        ValidateFrame(bgra, width, height, stride);
        var isBlank = IsBlank(bgra, width, height, stride);
        if (isBlank || !TryLocate(bgra, width, height, stride, out var layout))
        {
            return new DwmCompositedSentinelObservation(
                Located: false,
                IsBlank: isBlank,
                HasExactBlockSizes: false,
                HasAlignedCorners: false,
                Layout: default,
                Insets: default);
        }

        return new DwmCompositedSentinelObservation(
            Located: true,
            IsBlank: false,
            HasExactBlockSizes: HasExactBlockSizes(layout),
            HasAlignedCorners: HasAlignedCorners(layout),
            Layout: layout,
            Insets: DwmCompositedSceneInsets.From(layout, width, height));
    }

    public static DwmCompositedSentinelContinuity Compare(
        DwmCompositedSentinelObservation baseline,
        DwmCompositedSentinelObservation current,
        int insetTolerance = 2)
    {
        if (insetTolerance < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(insetTolerance));
        }

        var observationsAreExact = baseline.IsExact && current.IsExact;
        var leftTopInsetsMatch =
            observationsAreExact &&
            Within(baseline.Insets.Left, current.Insets.Left, insetTolerance) &&
            Within(baseline.Insets.Top, current.Insets.Top, insetTolerance);
        var rightBottomInsetsMatch =
            observationsAreExact &&
            Within(baseline.Insets.Right, current.Insets.Right, insetTolerance) &&
            Within(baseline.Insets.Bottom, current.Insets.Bottom, insetTolerance);
        var rightBottomInsetsDoNotDecrease =
            observationsAreExact &&
            current.Insets.Right + insetTolerance >= baseline.Insets.Right &&
            current.Insets.Bottom + insetTolerance >= baseline.Insets.Bottom;

        return new DwmCompositedSentinelContinuity(
            CurrentIsExact: current.IsExact,
            LeftTopInsetsMatch: leftTopInsetsMatch,
            RightBottomInsetsMatch: rightBottomInsetsMatch,
            RightBottomInsetsDoNotDecrease: rightBottomInsetsDoNotDecrease,
            RightGapPixels: observationsAreExact
                ? Math.Max(0, current.Insets.Right - baseline.Insets.Right)
                : 0,
            BottomGapPixels: observationsAreExact
                ? Math.Max(0, current.Insets.Bottom - baseline.Insets.Bottom)
                : 0);
    }

    private static bool TryLocate(
        ReadOnlySpan<byte> bgra,
        int width,
        int height,
        int stride,
        out DwmCompositedSentinelLayout layout)
    {
        var components = FindColorComponents(bgra, width, height, stride);
        var topLeft = components[0];
        var topRight = components[1];
        var bottomLeft = components[2];
        var bottomRight = components[3];

        foreach (var first in topLeft)
        {
            foreach (var second in topRight)
            {
                if (second.X - first.X < kMinimumViewportSpan ||
                    !Within(first.Y, second.Y, kAlignmentTolerance))
                {
                    continue;
                }

                foreach (var third in bottomLeft)
                {
                    if (third.Y - first.Y < kMinimumViewportSpan ||
                        !Within(first.X, third.X, kAlignmentTolerance))
                    {
                        continue;
                    }

                    foreach (var fourth in bottomRight)
                    {
                        if (!Within(second.Right, fourth.Right, kAlignmentTolerance) ||
                            !Within(third.Bottom, fourth.Bottom, kAlignmentTolerance))
                        {
                            continue;
                        }

                        layout = new DwmCompositedSentinelLayout(
                            first,
                            second,
                            third,
                            fourth);
                        return true;
                    }
                }
            }
        }

        layout = default;
        return false;
    }

    private static IReadOnlyList<PixelRectangle>[] FindColorComponents(
        ReadOnlySpan<byte> bgra,
        int width,
        int height,
        int stride)
    {
        var pixelCount = checked(width * height);
        var classifications = ArrayPool<byte>.Shared.Rent(pixelCount);
        var visited = ArrayPool<byte>.Shared.Rent(pixelCount);
        var queue = ArrayPool<int>.Shared.Rent(pixelCount);
        var rectangles = new List<PixelRectangle>[]
        {
            [],
            [],
            [],
            [],
        };
        try
        {
            classifications.AsSpan(0, pixelCount).Clear();
            visited.AsSpan(0, pixelCount).Clear();
            for (var y = 0; y < height; y++)
            {
                for (var x = 0; x < width; x++)
                {
                    classifications[(y * width) + x] = Classify(
                        bgra,
                        stride,
                        x,
                        y);
                }
            }

            for (var origin = 0; origin < pixelCount; origin++)
            {
                var classification = classifications[origin];
                if (classification == 0 || visited[origin] != 0)
                {
                    continue;
                }

                var head = 0;
                var tail = 0;
                queue[tail++] = origin;
                visited[origin] = 1;
                var minimumX = origin % width;
                var maximumX = minimumX;
                var minimumY = origin / width;
                var maximumY = minimumY;
                var componentPixels = 0;
                while (head < tail)
                {
                    var pixel = queue[head++];
                    var x = pixel % width;
                    var y = pixel / width;
                    componentPixels++;
                    minimumX = Math.Min(minimumX, x);
                    maximumX = Math.Max(maximumX, x);
                    minimumY = Math.Min(minimumY, y);
                    maximumY = Math.Max(maximumY, y);

                    Enqueue(pixel - 1, x > 0);
                    Enqueue(pixel + 1, x + 1 < width);
                    Enqueue(pixel - width, y > 0);
                    Enqueue(pixel + width, y + 1 < height);
                }

                var rectangleWidth = checked(maximumX - minimumX + 1);
                var rectangleHeight = checked(maximumY - minimumY + 1);
                var area = checked(rectangleWidth * rectangleHeight);
                if (rectangleWidth < kMinimumCandidateEdge ||
                    rectangleHeight < kMinimumCandidateEdge ||
                    (double)componentPixels / area < kMinimumComponentDensity)
                {
                    continue;
                }

                rectangles[classification - 1].Add(new PixelRectangle(
                    minimumX,
                    minimumY,
                    rectangleWidth,
                    rectangleHeight));

                void Enqueue(int candidate, bool isInBounds)
                {
                    if (!isInBounds ||
                        visited[candidate] != 0 ||
                        classifications[candidate] != classification)
                    {
                        return;
                    }

                    visited[candidate] = 1;
                    queue[tail++] = candidate;
                }
            }

            return rectangles;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(classifications);
            ArrayPool<byte>.Shared.Return(visited);
            ArrayPool<int>.Shared.Return(queue);
        }
    }

    private static byte Classify(
        ReadOnlySpan<byte> bgra,
        int stride,
        int x,
        int y) =>
        Matches(bgra, stride, x, y, TopLeft) ? (byte)1 :
        Matches(bgra, stride, x, y, TopRight) ? (byte)2 :
        Matches(bgra, stride, x, y, BottomLeft) ? (byte)3 :
        Matches(bgra, stride, x, y, BottomRight) ? (byte)4 :
        (byte)0;

    private static bool HasExactBlockSizes(DwmCompositedSentinelLayout layout) =>
        HasExactBlockSize(layout.TopLeft) &&
        HasExactBlockSize(layout.TopRight) &&
        HasExactBlockSize(layout.BottomLeft) &&
        HasExactBlockSize(layout.BottomRight);

    private static bool HasExactBlockSize(PixelRectangle rectangle) =>
        Within(rectangle.Width, kExpectedBlockEdge, kBlockEdgeTolerance) &&
        Within(rectangle.Height, kExpectedBlockEdge, kBlockEdgeTolerance);

    private static bool HasAlignedCorners(DwmCompositedSentinelLayout layout) =>
        Within(layout.TopLeft.X, layout.BottomLeft.X, kAlignmentTolerance) &&
        Within(layout.TopLeft.Y, layout.TopRight.Y, kAlignmentTolerance) &&
        Within(layout.TopRight.Right, layout.BottomRight.Right, kAlignmentTolerance) &&
        Within(layout.BottomLeft.Bottom, layout.BottomRight.Bottom, kAlignmentTolerance);

    private static bool IsBlank(
        ReadOnlySpan<byte> bgra,
        int width,
        int height,
        int stride)
    {
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var offset = (y * stride) + (x * 4);
                if (bgra[offset] > 8 || bgra[offset + 1] > 8 || bgra[offset + 2] > 8)
                {
                    return false;
                }
            }
        }

        return true;
    }

    private static bool Matches(
        ReadOnlySpan<byte> bgra,
        int stride,
        int x,
        int y,
        Bgra32 expected)
    {
        var offset = (y * stride) + (x * 4);
        return WithinTolerance(bgra[offset], expected.Blue) &&
               WithinTolerance(bgra[offset + 1], expected.Green) &&
               WithinTolerance(bgra[offset + 2], expected.Red);
    }

    private static bool WithinTolerance(byte actual, byte expected) =>
        Math.Abs(actual - expected) <= kColorTolerance;

    private static bool Within(int first, int second, int tolerance) =>
        Math.Abs(first - second) <= tolerance;

    private static void ValidateFrame(
        ReadOnlySpan<byte> bgra,
        int width,
        int height,
        int stride)
    {
        if (width < kMinimumCandidateEdge || height < kMinimumCandidateEdge)
        {
            throw new ArgumentOutOfRangeException(
                nameof(width),
                $"A sentinel frame must be at least {kMinimumCandidateEdge}x" +
                $"{kMinimumCandidateEdge} pixels.");
        }
        if (stride < checked(width * 4))
        {
            throw new ArgumentOutOfRangeException(nameof(stride));
        }
        if (bgra.Length < checked(stride * height))
        {
            throw new ArgumentException("The BGRA buffer is smaller than its declared frame.", nameof(bgra));
        }
    }
}
