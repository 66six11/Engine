using System;
using System.Linq;
using Asharia.Editor.Diagnostics;
using Editor.Core.CodeFirstUI.Models;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Models.FrameDebug;
using Editor.Core.Models.Panels;
using Editor.Core.Services;
using Editor.Features.FrameDebugger;
using Editor.Shell.CodeFirstUI.Hosting;
using Xunit;

namespace Editor.Tests.Features.FrameDebugger;

public sealed class FrameDebuggerPanelTests
{
    private static readonly EditorPanelLifecycleContext PanelContext = new(
        "frame-debugger",
        "Frame Debugger",
        DockArea.Right,
        IsFloatingWorkspace: false);

    [Fact]
    public void Attach_builds_read_only_snapshot_layout()
    {
        var gbuffer = CreatePass("pass:gbuffer", 0, "GBuffer", "Raster", commandCount: 2);
        var lighting = CreatePass("pass:lighting", 1, "Lighting", "Compute", commandCount: 1);
        var snapshot = CreateSnapshot(
            [gbuffer, lighting],
            [CreateExecutionEvent("event:draw", 0, gbuffer.Id, gbuffer.Name, "Draw")]);
        var host = CreateAttachedHost(snapshot, out _, out _);

        var content = AssertNode(host.CurrentTree!.Root, "content", GuiNodeKind.Vertical);
        var toolbar = AssertNode(content, "content/toolbar", GuiNodeKind.Toolbar);
        Assert.Contains(toolbar.Children, child => child.Id.KeyPath == "content/toolbar/capture" && child.Label == "Capture");
        Assert.Contains(toolbar.Children, child => child.Id.KeyPath == "content/toolbar/resume" && child.Label == "Resume");

        var summary = AssertNode(content, "content/summary", GuiNodeKind.Panel);
        AssertProperty(summary, "content/summary/state", "State", "PausedFrameDebug");
        AssertProperty(summary, "content/summary/frame-index", "Frame", "12");
        AssertProperty(summary, "content/summary/extent", "Extent", "1280 x 720");

        var split = AssertNode(content, "content/body", GuiNodeKind.Split);
        var passes = AssertNode(split, "content/body/passes", GuiNodeKind.Panel);
        var passList = AssertNode(passes, "content/body/passes/pass-list", GuiNodeKind.List);
        Assert.Equal("pass:gbuffer", passList.Payload.SelectedItemId);
        Assert.Contains(passList.Payload.ListItems, item => item.Id == "pass:lighting" && item.Label.Contains("Lighting", StringComparison.Ordinal));

        var details = AssertNode(split, "content/body/details", GuiNodeKind.Panel);
        Assert.Contains(details.Children, child => child.Label == "GBuffer");
        AssertProperty(details, "content/body/details/pass-type", "Type", "Raster");
        AssertProperty(details, "content/body/details/pass-commands", "Commands", "2");
    }

    [Fact]
    public void Pass_selection_rebuilds_selected_pass_details()
    {
        var gbuffer = CreatePass("pass:gbuffer", 0, "GBuffer", "Raster", commandCount: 2);
        var lighting = CreatePass("pass:lighting", 1, "Lighting", "Compute", commandCount: 1);
        var host = CreateAttachedHost(CreateSnapshot([gbuffer, lighting], []), out _, out _);

        host.SelectListItem(
            new GuiNodeId("frame-debugger", "content/body/passes/pass-list", GuiNodeKind.List),
            "pass:lighting");

        var details = AssertNode(host.CurrentTree!.Root, "content/body/details", GuiNodeKind.Panel);
        Assert.Contains(details.Children, child => child.Label == "Lighting");
        AssertProperty(details, "content/body/details/pass-type", "Type", "Compute");
        AssertProperty(details, "content/body/details/pass-commands", "Commands", "1");
    }

    [Fact]
    public void Provider_change_requests_frame_repaint_and_refreshes_snapshot()
    {
        var firstPass = CreatePass("pass:initial", 0, "Initial", "Raster", commandCount: 1);
        var nextPass = CreatePass("pass:next", 0, "Next", "Compute", commandCount: 4);
        var host = CreateAttachedHost(CreateSnapshot([firstPass], []), out var provider, out _);

        provider.ReplaceSnapshot(CreateSnapshot([nextPass], []));
        var frameContext = new EditorPanelFrameContext(
            PanelContext,
            DateTimeOffset.Parse("2026-07-04T11:00:00Z"),
            TimeSpan.FromMilliseconds(16),
            sequence: 1);
        host.OnEditorPanelFrame(frameContext);

        Assert.True(frameContext.IsRepaintRequested);
        var passList = AssertNode(host.CurrentTree!.Root, "content/body/passes/pass-list", GuiNodeKind.List);
        var passItem = Assert.Single(passList.Payload.ListItems);
        Assert.Equal("pass:next", passItem.Id);
        Assert.Equal("pass:next", passList.Payload.SelectedItemId);
        var details = AssertNode(host.CurrentTree.Root, "content/body/details", GuiNodeKind.Panel);
        Assert.Contains(details.Children, child => child.Label == "Next");
    }

    [Fact]
    public void Capture_and_resume_buttons_publish_read_only_diagnostics()
    {
        var pass = CreatePass("pass:gbuffer", 0, "GBuffer", "Raster", commandCount: 1);
        var host = CreateAttachedHost(CreateSnapshot([pass], []), out _, out var diagnostics);

        host.ClickButton(new GuiNodeId("frame-debugger", "content/toolbar/capture", GuiNodeKind.Button));
        var captureDiagnostic = diagnostics.GetLatestDiagnostic();
        Assert.NotNull(captureDiagnostic);
        Assert.Equal(EditorDiagnosticSeverity.Warning, captureDiagnostic.Severity);
        Assert.Equal(EditorDiagnosticChannel.Debug, captureDiagnostic.Channel);
        Assert.Equal("FrameDebugger", captureDiagnostic.Source);
        Assert.Contains("native ABI", captureDiagnostic.Message, StringComparison.OrdinalIgnoreCase);

        host.ClickButton(new GuiNodeId("frame-debugger", "content/toolbar/resume", GuiNodeKind.Button));
        var resumeDiagnostic = diagnostics.GetLatestDiagnostic();
        Assert.NotNull(resumeDiagnostic);
        Assert.Equal("FrameDebugger", resumeDiagnostic.Source);
        Assert.Contains("read-only", resumeDiagnostic.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Unavailable_snapshot_renders_warning_without_pass_list()
    {
        var host = CreateAttachedHost(FrameDebuggerSnapshot.Unavailable, out _, out _);

        var content = AssertNode(host.CurrentTree!.Root, "content", GuiNodeKind.Vertical);
        var unavailable = AssertNode(content, "content/unavailable", GuiNodeKind.ValidationMessage);

        Assert.Equal(EditorDiagnosticSeverity.Warning, unavailable.Payload.DiagnosticSeverity);
        Assert.DoesNotContain(content.Children, child => child.Id.KeyPath == "content/body");
    }

    private static CodeFirstPanelHostViewModel CreateAttachedHost(
        FrameDebuggerSnapshot snapshot,
        out InMemoryFrameDebuggerSnapshotProvider provider,
        out EditorDiagnosticService diagnostics)
    {
        provider = new InMemoryFrameDebuggerSnapshotProvider(snapshot);
        diagnostics = new EditorDiagnosticService();
        var host = new CodeFirstPanelHostViewModel(new FrameDebuggerPanel(provider, diagnostics));
        host.OnPanelAttached(PanelContext);
        return host;
    }

    private static FrameDebuggerSnapshot CreateSnapshot(
        FrameDebugPassSnapshot[] passes,
        FrameDebugExecutionEventSnapshot[] executionEvents)
    {
        var capture = new FrameDebugCaptureSnapshot(
            "capture:test",
            12,
            42UL,
            "Scene",
            1280,
            720,
            DateTimeOffset.Parse("2026-07-04T10:30:00Z"));

        return new FrameDebuggerSnapshot(
            1,
            FrameDebuggerState.PausedFrameDebug,
            capture,
            passes: passes,
            executionEvents: executionEvents,
            message: "Captured frame 12.");
    }

    private static FrameDebugPassSnapshot CreatePass(
        string id,
        int index,
        string name,
        string type,
        int commandCount)
    {
        return new FrameDebugPassSnapshot(
            id,
            index,
            DeclarationIndex: index,
            name,
            type,
            "BasicRenderView",
            AllowCulling: true,
            HasSideEffects: false,
            CommandCount: commandCount,
            ImageTransitionCount: 1,
            BufferTransitionCount: 0);
    }

    private static FrameDebugExecutionEventSnapshot CreateExecutionEvent(
        string id,
        int index,
        string passId,
        string passName,
        string kind)
    {
        return new FrameDebugExecutionEventSnapshot(
            id,
            index,
            kind,
            passId,
            passName,
            CommandId: null,
            $"{kind} event",
            SourceResourceId: null,
            TargetResourceId: null,
            VertexCount: 3,
            IndexCount: 0,
            InstanceCount: 1,
            GroupCountX: 0,
            GroupCountY: 0,
            GroupCountZ: 0);
    }

    private static GuiNode AssertNode(
        GuiNode root,
        string keyPath,
        GuiNodeKind kind)
    {
        var node = EnumerateNodes(root).Single(child => child.Id.KeyPath == keyPath);
        Assert.Equal(kind, node.Kind);
        return node;
    }

    private static void AssertProperty(
        GuiNode root,
        string keyPath,
        string label,
        string value)
    {
        var node = AssertNode(root, keyPath, GuiNodeKind.Property);
        Assert.Equal(label, node.Label);
        Assert.Equal(value, node.Payload.PropertyValue);
    }

    private static GuiNode[] EnumerateNodes(GuiNode root)
    {
        return root.Children
            .SelectMany(child => EnumerateNodes(child).Prepend(child))
            .ToArray();
    }
}
