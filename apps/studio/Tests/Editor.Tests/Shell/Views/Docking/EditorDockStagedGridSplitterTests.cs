using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.Views.Docking;
using Xunit;

namespace Editor.Tests.Shell.Views.Docking;

public sealed class EditorDockStagedGridSplitterTests
{
    [Fact]
    public void Real_dock_factory_creates_the_staged_preview_splitter()
    {
        var split = CreateSplit();

        var splitter = EditorDockSplitNodeView.CreateSplitter(
            split,
            GridResizeDirection.Columns,
            "vertical");

        var staged = Assert.IsType<EditorDockStagedGridSplitter>(splitter);
        Assert.True(staged.ShowsPreview);
        Assert.Equal(GridResizeBehavior.PreviousAndNext, staged.ResizeBehavior);
        Assert.Equal(GridResizeDirection.Columns, staged.ResizeDirection);
        Assert.Same(split, staged.DataContext);
        Assert.Equal(split.Id, staged.Tag);
        Assert.Contains("owned-dock-layout-splitter", staged.Classes);
    }

    [Fact]
    public void Completion_restores_committed_layout_before_publishing_final_request()
    {
        var events = new List<string>();

        EditorDockStagedGridSplitter.CompleteDragCore(
            () => events.Add("base-complete"),
            () => events.Add("restore-committed"),
            () => events.Add("publish-final"));

        Assert.Equal(
            ["base-complete", "restore-committed", "publish-final"],
            events);
    }

    [Fact]
    public void Completion_restores_committed_layout_when_default_completion_fails()
    {
        var events = new List<string>();
        var failure = new InvalidOperationException("default completion failed");

        var exception = Assert.Throws<InvalidOperationException>(
            () => EditorDockStagedGridSplitter.CompleteDragCore(
                () =>
                {
                    events.Add("base-complete");
                    throw failure;
                },
                () => events.Add("restore-committed"),
                () => events.Add("publish-final")));

        Assert.Same(failure, exception);
        Assert.Equal(["base-complete", "restore-committed"], events);
    }

    [Fact]
    public void Cancellation_restores_layout_and_cancels_pending_even_when_base_fails()
    {
        var events = new List<string>();

        Assert.Throws<InvalidOperationException>(
            () => EditorDockStagedGridSplitter.CancelDragCore(
                () =>
                {
                    events.Add("base-cancel");
                    throw new InvalidOperationException("base cancellation failed");
                },
                () => events.Add("restore-committed"),
                () => events.Add("cancel-pending")));

        Assert.Equal(
            ["base-cancel", "restore-committed", "cancel-pending"],
            events);
    }

    [Fact]
    public void Drag_delta_vector_replaces_previous_value_because_Avalonia_reports_from_drag_start()
    {
        var cumulative = EditorDockStagedGridSplitter.ResolveCumulativeDelta(0d, 2d);
        cumulative = EditorDockStagedGridSplitter.ResolveCumulativeDelta(0d, 5d);

        Assert.Equal(5d, cumulative);
    }

    [Fact]
    public void Drag_delta_vector_is_offset_by_an_intermediate_committed_layout()
    {
        var cumulative = EditorDockStagedGridSplitter.ResolveCumulativeDelta(
            committedLayoutDelta: 5d,
            dragDeltaVector: 3d);

        Assert.Equal(8d, cumulative);
    }

    [Fact]
    public void Zero_origin_delta_is_still_published_after_an_intermediate_commit()
    {
        var committed = new Editor.Shell.Docking.Splitters.EditorDockSplitResizeCommittedSnapshot(
            "split",
            Orientation.Horizontal,
            new GridLength(760d),
            new GridLength(1d, GridUnitType.Star),
            760d,
            515d,
            1d);
        var returnToOrigin = new Editor.Shell.Docking.Splitters.EditorDockSplitResizeProposal(
            new GridLength(640d),
            new GridLength(1d, GridUnitType.Star),
            640d,
            635d,
            0d,
            -640d,
            635d);

        Assert.True(EditorDockStagedGridSplitter.ShouldPublishProposal(
            committed,
            returnToOrigin));
        Assert.False(EditorDockStagedGridSplitter.ShouldPublishProposal(
            committed,
            returnToOrigin with
            {
                FirstActualLength = committed.FirstActualLength,
                SecondActualLength = committed.SecondActualLength,
            }));
    }

    [Fact]
    public void No_op_against_the_committed_front_is_queued_behind_active_work()
    {
        var committed = new Editor.Shell.Docking.Splitters.EditorDockSplitResizeCommittedSnapshot(
            "split",
            Orientation.Horizontal,
            new GridLength(640d),
            new GridLength(1d, GridUnitType.Star),
            640d,
            635d,
            1d);
        var returnToCommitted = new Editor.Shell.Docking.Splitters.EditorDockSplitResizeProposal(
            committed.FirstLength,
            committed.SecondLength,
            committed.FirstActualLength,
            committed.SecondActualLength,
            0d,
            -640d,
            635d);

        Assert.False(EditorDockStagedGridSplitter.ShouldQueueProposal(
            committed,
            returnToCommitted,
            hasActive: false,
            hasQueued: false));
        Assert.True(EditorDockStagedGridSplitter.ShouldQueueProposal(
            committed,
            returnToCommitted,
            hasActive: true,
            hasQueued: false));
        Assert.True(EditorDockStagedGridSplitter.ShouldQueueProposal(
            committed,
            returnToCommitted,
            hasActive: false,
            hasQueued: true));
    }

    private static EditorDockSplitNodeViewModel CreateSplit()
    {
        var firstWindow = new EditorDockWindowViewModel(
            "first-window",
            "First",
            EditorDockArea.Left,
            "first");
        var secondWindow = new EditorDockWindowViewModel(
            "second-window",
            "Second",
            EditorDockArea.Center,
            "second");
        return new EditorDockSplitNodeViewModel(
            "split",
            Orientation.Horizontal,
            new EditorDockWindowNodeViewModel("first-node", firstWindow),
            new EditorDockWindowNodeViewModel("second-node", secondWindow),
            new GridLength(1d, GridUnitType.Star),
            new GridLength(1d, GridUnitType.Star));
    }
}
