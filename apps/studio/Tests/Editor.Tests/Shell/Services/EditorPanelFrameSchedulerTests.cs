using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Shell.Services;
using Xunit;

namespace Editor.Tests.Shell.Services;

public sealed class EditorPanelFrameSchedulerTests
{
    [Fact]
    public void Manual_panel_is_not_ticked_automatically()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var sink = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Manual);

        scheduler.AttachPanel(CreateContext("manual"), sink);
        var frames = scheduler.Tick(DateTimeOffset.UnixEpoch);

        Assert.Empty(frames);
        Assert.Empty(sink.Frames);
    }

    [Fact]
    public void Visible_panel_ticks_only_while_attached()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var sink = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Visible());
        var context = CreateContext("visible");

        scheduler.AttachPanel(context, sink);
        scheduler.Tick(DateTimeOffset.UnixEpoch);
        scheduler.DetachPanel(context);
        scheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(16));

        var frame = Assert.Single(sink.Frames);
        Assert.Equal("visible", frame.Panel.PanelId);
    }

    [Fact]
    public void Active_panel_pauses_when_deactivated()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var sink = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Active());
        var context = CreateContext("active");

        scheduler.AttachPanel(context, sink);
        scheduler.Tick(DateTimeOffset.UnixEpoch);
        scheduler.ActivatePanel(context);
        scheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(16));
        scheduler.DeactivatePanel(context);
        scheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(32));
        scheduler.ActivatePanel(context);
        scheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(48));

        Assert.Equal(2, sink.Frames.Count);
        Assert.All(sink.Frames, frame => Assert.Equal("active", frame.Panel.PanelId));
    }

    [Fact]
    public void Target_fps_throttles_tick_frequency()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var sink = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Visible(targetFramesPerSecond: 2));
        var context = CreateContext("throttled");

        scheduler.AttachPanel(context, sink);
        scheduler.Tick(DateTimeOffset.UnixEpoch);
        scheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(250));
        scheduler.Tick(DateTimeOffset.UnixEpoch.AddMilliseconds(500));

        Assert.Equal(2, sink.Frames.Count);
        Assert.Equal(DateTimeOffset.UnixEpoch, sink.Frames[0].NowUtc);
        Assert.Equal(DateTimeOffset.UnixEpoch.AddMilliseconds(500), sink.Frames[1].NowUtc);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Frame_update_request_rejects_invalid_target_fps(double targetFramesPerSecond)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => EditorPanelFrameUpdateRequest.Visible(targetFramesPerSecond));
    }

    [Fact]
    public void Frame_update_request_rejects_unknown_mode()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new EditorPanelFrameUpdateRequest((EditorPanelFrameUpdateMode)42));
    }

    [Fact]
    public void Repaint_request_is_recorded_without_forcing_renderer_work()
    {
        var scheduler = new EditorPanelFrameScheduler();
        var sink = new RecordingFrameUpdateSink(EditorPanelFrameUpdateRequest.Visible())
        {
            RequestRepaintOnFrame = true,
        };
        var context = CreateContext("repaint");

        scheduler.AttachPanel(context, sink);
        var frames = scheduler.Tick(DateTimeOffset.UnixEpoch);

        var frame = Assert.Single(frames);
        Assert.True(frame.IsRepaintRequested);
        Assert.Equal(frame, Assert.Single(sink.Frames));
    }

    private static EditorPanelLifecycleContext CreateContext(string panelId)
    {
        return new EditorPanelLifecycleContext(
            panelId,
            "Panel",
            EditorDockArea.Center,
            IsFloatingWorkspace: false);
    }

    private sealed class RecordingFrameUpdateSink(
        EditorPanelFrameUpdateRequest frameUpdateRequest) : IEditorPanelFrameUpdateSink
    {
        public List<EditorPanelFrameContext> Frames { get; } = [];

        public bool RequestRepaintOnFrame { get; init; }

        public EditorPanelFrameUpdateRequest FrameUpdateRequest { get; } = frameUpdateRequest;

        public void OnEditorPanelFrame(EditorPanelFrameContext context)
        {
            if (RequestRepaintOnFrame)
            {
                context.RequestRepaint();
            }

            Frames.Add(context);
        }
    }
}
