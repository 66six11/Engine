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

    [Fact]
    public void Select_list_item_updates_state_and_rebuilds_tree()
    {
        var panel = new ListDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));

        host.SelectListItem(
            new GuiNodeId("ui.style", "layout/catalog/sections", GuiNodeKind.List),
            "buttons");

        Assert.Equal("buttons", panel.SelectedSectionId);
        Assert.True(host.StateStore.TryGetSelectedItem(
            new GuiNodeId("ui.style", "layout/catalog/sections", GuiNodeKind.List),
            out var storedSelection));
        Assert.Equal("buttons", storedSelection);

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        Assert.Equal("Buttons", Assert.Single(preview.Children).Label);
    }

    private static EditorPanelLifecycleContext CreateLifecycleContext(string panelId = "render.frameDebugger")
    {
        return new EditorPanelLifecycleContext(
            panelId,
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

    private sealed class ListDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        private static readonly GuiListItem[] Sections =
        [
            new("overview", "Overview"),
            new("buttons", "Buttons"),
        ];

        public string? SelectedSectionId { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            using (gui.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
            {
                using (gui.Panel("catalog", "Catalog"))
                {
                    SelectedSectionId = gui.List("sections", Sections, "overview");
                }

                using (gui.Panel("preview", "Preview"))
                {
                    gui.Label("title", SelectedSectionId == "buttons" ? "Buttons" : "Overview");
                }
            }
        }
    }
}
