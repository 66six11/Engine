using System;
using System.Linq;
using System.Threading;
using Editor.Core.Abstractions;
using Editor.Core.CodeFirstUI.Abstractions;
using Editor.Core.CodeFirstUI.Authoring;
using Editor.Core.CodeFirstUI.Models;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Models.FrameDebug;
using Editor.Core.Models.Panels;

namespace Editor.Features.FrameDebugger;

internal sealed class FrameDebuggerPanel : CodeFirstEditorPanel
{
    private const string DiagnosticSource = "FrameDebugger";
    private const string DiagnosticCategory = "Interop";

    private readonly IEditorDiagnosticService diagnostics_;
    private readonly IFrameDebuggerSnapshotProvider snapshotProvider_;
    private int pendingSnapshotRefresh_;
    private string? selectedPassId_;

    public FrameDebuggerPanel(
        IFrameDebuggerSnapshotProvider snapshotProvider,
        IEditorDiagnosticService diagnostics)
    {
        ArgumentNullException.ThrowIfNull(snapshotProvider);
        ArgumentNullException.ThrowIfNull(diagnostics);

        snapshotProvider_ = snapshotProvider;
        diagnostics_ = diagnostics;
    }

    public override EditorPanelFrameUpdateRequest FrameUpdateRequest =>
        EditorPanelFrameUpdateRequest.Visible(targetFramesPerSecond: 10d);

    protected override void OnCreate(EditorPanelLifecycleContext context)
    {
        snapshotProvider_.SnapshotChanged += OnSnapshotChanged;
    }

    protected override void OnGui(EditorGui gui)
    {
        var snapshot = snapshotProvider_.GetCurrentSnapshot();

        using (gui.Vertical("content"))
        {
            DrawToolbar(gui, snapshot);
            if (snapshot.State == FrameDebuggerState.Unavailable)
            {
                gui.ValidationMessage(
                    "unavailable",
                    snapshot.Message,
                    EditorDiagnosticSeverity.Warning);
                return;
            }

            DrawSummary(gui, snapshot);
            if (snapshot.Passes.Count == 0)
            {
                gui.ValidationMessage(
                    "empty",
                    "Frame Debugger snapshot has no passes.",
                    EditorDiagnosticSeverity.Info);
                return;
            }

            DrawBody(gui, snapshot);
        }
    }

    protected override void OnFrame(EditorPanelFrameContext context)
    {
        if (Interlocked.Exchange(ref pendingSnapshotRefresh_, 0) == 1)
        {
            context.RequestRepaint();
        }
    }

    protected override void OnDestroy()
    {
        snapshotProvider_.SnapshotChanged -= OnSnapshotChanged;
    }

    private void DrawToolbar(EditorGui gui, FrameDebuggerSnapshot snapshot)
    {
        using (gui.Toolbar("toolbar"))
        {
            if (gui.Button("capture", "Capture"))
            {
                PublishReadOnlyDiagnostic("Capture request ignored: native ABI is not connected for read-only v0.");
            }

            if (gui.Button("resume", "Resume"))
            {
                PublishReadOnlyDiagnostic("Resume request ignored: Frame Debugger v0 is read-only.");
            }

            gui.Label(
                "state",
                snapshot.State.ToString(),
                GuiTextTone.Muted,
                GuiTextSize.Caption);
        }
    }

    private static void DrawSummary(EditorGui gui, FrameDebuggerSnapshot snapshot)
    {
        using (gui.Panel("summary", "Capture"))
        {
            gui.Property("state", "State", snapshot.State);
            if (snapshot.Capture is { } capture)
            {
                gui.Property("frame-index", "Frame", capture.FrameIndex);
                gui.Property("view-kind", "View", capture.ViewKind);
                gui.Property("frame-epoch", "Epoch", capture.SubmittedFrameEpoch);
                gui.Property("extent", "Extent", $"{capture.RequestedWidth} x {capture.RequestedHeight}");
                gui.Property("captured-at", "Captured", capture.CapturedAtUtc.ToString("u"));
            }
            else
            {
                gui.Property("capture", "Capture", "None");
            }

            gui.Property("passes", "Passes", snapshot.Passes.Count);
            gui.Property("events", "Events", snapshot.ExecutionEvents.Count);
            if (!string.IsNullOrWhiteSpace(snapshot.Message))
            {
                gui.Text("message", snapshot.Message, GuiTextTone.Secondary, GuiTextSize.Caption);
            }
        }
    }

    private void DrawBody(EditorGui gui, FrameDebuggerSnapshot snapshot)
    {
        using (gui.Split("body", GuiSplitDirection.Horizontal, ratio: 0.35d))
        {
            var selectedPass = DrawPassList(gui, snapshot);
            DrawDetails(gui, snapshot, selectedPass);
        }
    }

    private FrameDebugPassSnapshot? DrawPassList(EditorGui gui, FrameDebuggerSnapshot snapshot)
    {
        using (gui.Panel("passes", "Passes"))
        {
            var items = snapshot.Passes
                .Select(pass => new GuiListItem(
                    pass.Id,
                    $"{pass.PassIndex}: {pass.Name} ({pass.Type})"))
                .ToArray();

            selectedPassId_ = gui.List("pass-list", items, selectedPassId_);
            return selectedPassId_ is not null
                && snapshotProvider_.TryGetPass(selectedPassId_, out var selectedPass)
                    ? selectedPass
                    : snapshot.Passes[0];
        }
    }

    private static void DrawDetails(
        EditorGui gui,
        FrameDebuggerSnapshot snapshot,
        FrameDebugPassSnapshot? selectedPass)
    {
        using (gui.Panel("details", "Details"))
        {
            if (selectedPass is null)
            {
                gui.ValidationMessage(
                    "missing-pass",
                    "Selected pass is no longer available.",
                    EditorDiagnosticSeverity.Warning);
                return;
            }

            gui.Text(
                "pass-title",
                selectedPass.Name,
                GuiTextTone.Primary,
                GuiTextSize.Title);
            gui.Property("pass-id", "Id", selectedPass.Id);
            gui.Property("pass-index", "Index", selectedPass.PassIndex);
            gui.Property("pass-type", "Type", selectedPass.Type);
            gui.Property("pass-params", "Params", selectedPass.ParamsType);
            gui.Property("pass-commands", "Commands", selectedPass.CommandCount);
            gui.Property("image-transitions", "Image Transitions", selectedPass.ImageTransitionCount);
            gui.Property("buffer-transitions", "Buffer Transitions", selectedPass.BufferTransitionCount);
            gui.Property("allow-culling", "Allow Culling", selectedPass.AllowCulling);
            gui.Property("side-effects", "Side Effects", selectedPass.HasSideEffects);

            DrawExecutionEvents(gui, snapshot, selectedPass);
            DrawResourceAccess(gui, snapshot, selectedPass);
            DrawTransitions(gui, snapshot, selectedPass);
        }
    }

    private static void DrawExecutionEvents(
        EditorGui gui,
        FrameDebuggerSnapshot snapshot,
        FrameDebugPassSnapshot selectedPass)
    {
        var events = snapshot.ExecutionEvents
            .Where(executionEvent => string.Equals(
                executionEvent.PassId,
                selectedPass.Id,
                StringComparison.Ordinal))
            .OrderBy(executionEvent => executionEvent.EventIndex)
            .ToArray();

        using (var foldout = gui.Foldout("execution-events", "Execution Events"))
        {
            if (!foldout.IsExpanded)
            {
                return;
            }

            gui.Property("event-count", "Count", events.Length);
            for (var index = 0; index < events.Length; index++)
            {
                var executionEvent = events[index];
                using (gui.Panel($"event-{index}", $"{executionEvent.EventIndex}: {executionEvent.Label}"))
                {
                    gui.Property("kind", "Kind", executionEvent.Kind);
                    gui.Property("command", "Command", executionEvent.CommandId ?? "None");
                    gui.Property("source", "Source", executionEvent.SourceResourceId ?? "None");
                    gui.Property("target", "Target", executionEvent.TargetResourceId ?? "None");
                    gui.Property("vertices", "Vertices", executionEvent.VertexCount);
                    gui.Property("instances", "Instances", executionEvent.InstanceCount);
                    gui.Property(
                        "groups",
                        "Groups",
                        $"{executionEvent.GroupCountX}, {executionEvent.GroupCountY}, {executionEvent.GroupCountZ}");
                }
            }
        }
    }

    private static void DrawResourceAccess(
        EditorGui gui,
        FrameDebuggerSnapshot snapshot,
        FrameDebugPassSnapshot selectedPass)
    {
        var accessEdges = snapshot.AccessEdges
            .Where(accessEdge => string.Equals(
                accessEdge.PassId,
                selectedPass.Id,
                StringComparison.Ordinal))
            .ToArray();

        using (var foldout = gui.Foldout("resources", "Resource Access", defaultExpanded: false))
        {
            if (!foldout.IsExpanded)
            {
                return;
            }

            gui.Property("access-count", "Access Edges", accessEdges.Length);
            for (var index = 0; index < accessEdges.Length; index++)
            {
                var accessEdge = accessEdges[index];
                using (gui.Panel($"access-{index}", accessEdge.ResourceName))
                {
                    gui.Property("slot", "Slot", accessEdge.SlotName);
                    gui.Property("resource", "Resource", accessEdge.ResourceId);
                    gui.Property("access", "Access", accessEdge.Access);
                    gui.Property("stage", "Stage", accessEdge.ShaderStage);
                }
            }
        }
    }

    private static void DrawTransitions(
        EditorGui gui,
        FrameDebuggerSnapshot snapshot,
        FrameDebugPassSnapshot selectedPass)
    {
        var transitions = snapshot.Transitions
            .Where(transition => string.Equals(
                transition.PassId,
                selectedPass.Id,
                StringComparison.Ordinal))
            .ToArray();

        using (var foldout = gui.Foldout("transitions", "Transitions", defaultExpanded: false))
        {
            if (!foldout.IsExpanded)
            {
                return;
            }

            gui.Property("transition-count", "Transitions", transitions.Length);
            for (var index = 0; index < transitions.Length; index++)
            {
                var transition = transitions[index];
                using (gui.Panel($"transition-{index}", transition.ResourceName))
                {
                    gui.Property("phase", "Phase", transition.Phase);
                    gui.Property("resource", "Resource", transition.ResourceId);
                    gui.Property("old-access", "Old Access", transition.OldAccess);
                    gui.Property("new-access", "New Access", transition.NewAccess);
                }
            }
        }
    }

    private void PublishReadOnlyDiagnostic(string message)
    {
        diagnostics_.Publish(
            EditorDiagnosticSeverity.Warning,
            EditorDiagnosticChannel.Debug,
            DiagnosticSource,
            DiagnosticCategory,
            message);
    }

    private void OnSnapshotChanged(object? sender, EventArgs e)
    {
        Interlocked.Exchange(ref pendingSnapshotRefresh_, 1);
    }
}
