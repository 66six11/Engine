using System;
using System.Collections.Generic;
using Editor.Shell.Views.Docking;
using Xunit;

namespace Editor.Tests.Shell.Views.Docking;

public sealed class EditorDockWorkspaceViewTests
{
    [Fact]
    public void CompleteTabDragCore_runs_all_cleanup_and_preserves_failures()
    {
        var completionFailure = new InvalidOperationException("completion failure");
        var previewFailure = new InvalidOperationException("preview cleanup failure");
        var closeFailure = new InvalidOperationException("close failure");
        var events = new List<string>();

        var exception = Assert.Throws<AggregateException>(
            () => EditorDockWorkspaceView.CompleteTabDragCore(
                () =>
                {
                    events.Add("complete");
                    throw completionFailure;
                },
                _ => events.Add("show"),
                () => events.Add("hide-preview"),
                () =>
                {
                    events.Add("clear-preview");
                    throw previewFailure;
                },
                () => events.Add("clear-preview-workspace"),
                () =>
                {
                    events.Add("close-empty-floating-host");
                    throw closeFailure;
                }));

        Assert.Equal(
            [
                "complete",
                "hide-preview",
                "clear-preview",
                "clear-preview-workspace",
                "close-empty-floating-host",
            ],
            events);
        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(completionFailure, item),
            item => Assert.Same(previewFailure, item),
            item => Assert.Same(closeFailure, item));
    }
}
