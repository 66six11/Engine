using System;
using System.Collections.Generic;
using Editor.Core.CodeFirstUI;
using Editor.Core.Models;
using Editor.Shell.CodeFirstUI;
using Xunit;

namespace Editor.Tests.Shell.CodeFirstUI.Hosting;

public sealed class CodeFirstPanelHostViewModelTests
{
    [Fact]
    public void Attach_creates_enables_and_builds_current_tree()
    {
        var panel = new RecordingCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);

        host.OnPanelAttached(CreateLifecycleContext());

        Assert.Equal(["create:render.frameDebugger", "enable", "gui"], panel.Events);
        Assert.NotNull(host.CurrentTree);
        Assert.Equal("render.frameDebugger", host.CurrentTree.PanelId);
        Assert.Equal("render.frameDebugger/title", host.CurrentTree.Root.Children[0].Id.FullKeyPath);
    }

    [Fact]
    public void Frame_rebuilds_only_when_panel_requests_repaint()
    {
        var panel = new RecordingCodeFirstPanel
        {
            RequestRepaintOnFrame = false,
        };
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext());
        var initialBuildCount = panel.GuiBuildCount;

        host.OnEditorPanelFrame(CreateFrameContext(sequence: 1));

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);

        panel.RequestRepaintOnFrame = true;
        host.OnEditorPanelFrame(CreateFrameContext(sequence: 2));

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
    }

    [Fact]
    public void Detach_disables_and_dispose_destroys_panel_once()
    {
        var panel = new RecordingCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);

        host.OnPanelAttached(CreateLifecycleContext());
        host.OnPanelDetached(CreateLifecycleContext());
        host.Dispose();
        host.Dispose();

        Assert.Equal(
            ["create:render.frameDebugger", "enable", "gui", "disable", "destroy"],
            panel.Events);
    }

    [Fact]
    public void Frame_update_request_comes_from_panel()
    {
        var panel = new RecordingCodeFirstPanel
        {
            RequestedFrameUpdate = EditorPanelFrameUpdateRequest.Active(30),
        };
        var host = new CodeFirstPanelHostViewModel(panel);

        Assert.Equal(EditorPanelFrameUpdateMode.Active, host.FrameUpdateRequest.Mode);
        Assert.Equal(30, host.FrameUpdateRequest.TargetFramesPerSecond);
    }

    private static EditorPanelLifecycleContext CreateLifecycleContext()
    {
        return new EditorPanelLifecycleContext(
            "render.frameDebugger",
            "Frame Debugger",
            DockArea.Right,
            IsFloatingWorkspace: false);
    }

    private static EditorPanelFrameContext CreateFrameContext(long sequence)
    {
        return new EditorPanelFrameContext(
            CreateLifecycleContext(),
            DateTimeOffset.UnixEpoch.AddMilliseconds(sequence * 16),
            TimeSpan.FromMilliseconds(16),
            sequence);
    }

    private sealed class RecordingCodeFirstPanel : CodeFirstEditorPanel
    {
        public List<string> Events { get; } = [];

        public int GuiBuildCount { get; private set; }

        public bool RequestRepaintOnFrame { get; set; }

        public EditorPanelFrameUpdateRequest RequestedFrameUpdate { get; set; } =
            EditorPanelFrameUpdateRequest.Manual;

        public override EditorPanelFrameUpdateRequest FrameUpdateRequest => RequestedFrameUpdate;

        protected override void OnCreate(EditorPanelLifecycleContext context)
        {
            Events.Add($"create:{context.PanelId}");
        }

        protected override void OnEnable()
        {
            Events.Add("enable");
        }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            Events.Add("gui");
            gui.Label("title", "RenderGraph");
        }

        protected override void OnFrame(EditorPanelFrameContext context)
        {
            Events.Add($"frame:{context.Sequence}");
            if (RequestRepaintOnFrame)
            {
                context.RequestRepaint();
            }
        }

        protected override void OnDisable()
        {
            Events.Add("disable");
        }

        protected override void OnDestroy()
        {
            Events.Add("destroy");
        }
    }
}
