using System;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationExceptionSafetyTests
{
    [Fact]
    public void ApplyStrong_restores_every_visible_property_when_a_later_setter_fails()
    {
        var surface = "old-surface";
        var size = 100;
        var opacity = 0.5f;
        var steps = new ViewportPresentationVisualMutationStep[]
        {
            new(
                () => surface = "candidate-surface",
                () => surface = "old-surface"),
            new(
                () =>
                {
                    size = 200;
                    throw new InvalidOperationException("Injected second-property failure.");
                },
                () => size = 100),
            new(
                () => opacity = 1,
                () => opacity = 0.5f),
        };

        Assert.Throws<InvalidOperationException>(() =>
            ViewportPresentationVisualMutation.ApplyStrong(steps));

        Assert.Equal("old-surface", surface);
        Assert.Equal(100, size);
        Assert.Equal(0.5f, opacity);
    }

    [Fact]
    public void ApplyStrong_reports_ambiguous_when_a_restore_setter_fails()
    {
        var surface = "old-surface";
        var steps = new ViewportPresentationVisualMutationStep[]
        {
            new(
                () => surface = "candidate-surface",
                () => throw new InvalidOperationException("Injected rollback failure.")),
            new(
                () => throw new InvalidOperationException("Injected publication failure."),
                () => { }),
        };

        var exception = Assert.Throws<
            ViewportPresentationVisualMutationAmbiguousException>(() =>
                ViewportPresentationVisualMutation.ApplyStrong(steps));

        Assert.Equal("candidate-surface", surface);
        Assert.Single(exception.RestorationFailures);
    }

    [Fact]
    public async System.Threading.Tasks.Task Process_quarantine_retains_each_resource_once_and_stop_returns_receipt()
    {
        var registry = new ViewportPresentationProcessQuarantineRegistry();
        var operation = new object();
        var stream = new object();
        var surface = new object();
        var first = registry.TransferPublished(
            "endpoint-a",
            operation,
            [stream],
            [surface],
            "test ambiguity");
        var duplicate = registry.TransferPublished(
            "endpoint-a",
            operation,
            [stream],
            [surface],
            "test ambiguity");

        Assert.Equal(1, first.AcceptedOperationCount);
        Assert.Equal(1, first.AcceptedStreamCount);
        Assert.Equal(1, first.AcceptedSurfaceCount);
        Assert.Equal(0, duplicate.AcceptedOperationCount);
        Assert.Equal(0, duplicate.AcceptedStreamCount);
        Assert.Equal(0, duplicate.AcceptedSurfaceCount);

        var lifetime = new ViewportPresentationLifetime(registry);
        var receipt = await lifetime.StopAndDrainWithQuarantineReceiptAsync();
        Assert.Equal(1, receipt.OperationCount);
        Assert.Equal(1, receipt.StreamCount);
        Assert.Equal(1, receipt.SurfaceCount);
        Assert.True(receipt.RetainedUntilProcessExit);
        Assert.Equal(receipt, lifetime.LastQuarantineDrainReceipt);
    }
}
