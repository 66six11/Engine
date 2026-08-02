#if DEBUG
using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentProtocol;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Automation.Peers;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Observation;

internal sealed class StudioShellUiObservationProjection : IStudioUiObservationSource
{
    private readonly MainWindow window_;

    public StudioShellUiObservationProjection(MainWindow window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window_ = window;
    }

    public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
        UiListWindowsParameters parameters,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(parameters);
        return InvokeOnUiThreadAsync(ProjectWindows, cancellationToken);
    }

    public ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
        UiReadTreeParameters parameters,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(parameters);
        if (!ObservationUiContract.IsValidReadTreeParameters(parameters))
        {
            return ValueTask.FromResult(Failed<UiTreeReadResult>(
                "observation.ui.request-invalid",
                "UI tree depth and node budgets are outside the protocol limits."));
        }

        return InvokeOnUiThreadAsync(
            () => ProjectTree(parameters),
            cancellationToken);
    }

    private ObservationProtocolReadResult<UiWindowListResult> ProjectWindows()
    {
        if (!window_.IsEffectivelyVisible)
        {
            return Succeeded(new UiWindowListResult(DateTimeOffset.UtcNow, []));
        }

        var semantics = ReadSemantics(window_, isWindow: true);
        if (semantics.Failure is not null)
        {
            return Failed<UiWindowListResult>(
                semantics.Failure.Code,
                semantics.Failure.Message);
        }

        return Succeeded(new UiWindowListResult(
            DateTimeOffset.UtcNow,
            [
                new ObservationUiWindow(
                    semantics.ElementId!,
                    semantics.Name!,
                    window_.IsEffectivelyVisible,
                    window_.IsEffectivelyEnabled),
            ]));
    }

    private ObservationProtocolReadResult<UiTreeReadResult> ProjectTree(
        UiReadTreeParameters parameters)
    {
        if (!window_.IsEffectivelyVisible)
        {
            return WindowNotFound<UiTreeReadResult>();
        }

        var windowId = AutomationProperties.GetAutomationId(window_);
        if (!string.Equals(parameters.WindowId, windowId, StringComparison.Ordinal))
        {
            return WindowNotFound<UiTreeReadResult>();
        }

        var nodes = ImmutableArray.CreateBuilder<ObservationUiNode>(parameters.MaxNodes);
        var identities = new HashSet<string>(StringComparer.Ordinal);
        var stack = new Stack<TraversalFrame>();
        stack.Push(new TraversalFrame(
            window_,
            ParentElementId: null,
            ParentDepth: -1,
            VisualDepth: 0));

        var visualsVisited = 0;
        var isTruncated = false;
        string? truncationReason = null;
        while (stack.Count != 0)
        {
            if (visualsVisited >= ObservationProtocolLimits.MaxUiVisualsVisited)
            {
                MarkTruncated(ref isTruncated, ref truncationReason, "ui.visual-budget");
                break;
            }

            var frame = stack.Pop();
            ++visualsVisited;
            var elementId = AutomationProperties.GetAutomationId(frame.Visual);
            var parentElementId = frame.ParentElementId;
            var parentDepth = frame.ParentDepth;
            if (!string.IsNullOrWhiteSpace(elementId))
            {
                var depth = parentDepth + 1;
                if (depth > parameters.MaxDepth)
                {
                    MarkTruncated(ref isTruncated, ref truncationReason, "ui.max-depth");
                    continue;
                }

                if (nodes.Count >= parameters.MaxNodes)
                {
                    MarkTruncated(ref isTruncated, ref truncationReason, "ui.max-nodes");
                    break;
                }

                var semantics = ReadSemantics(
                    frame.Visual,
                    isWindow: ReferenceEquals(frame.Visual, window_));
                if (semantics.Failure is not null)
                {
                    return Failed<UiTreeReadResult>(
                        semantics.Failure.Code,
                        semantics.Failure.Message);
                }

                if (!identities.Add(semantics.ElementId!))
                {
                    return Failed<UiTreeReadResult>(
                        "observation.ui.identity-conflict",
                        "The shell contains duplicate stable UI element IDs.");
                }

                nodes.Add(new ObservationUiNode(
                    semantics.ElementId!,
                    parentElementId,
                    depth,
                    semantics.Name!,
                    semantics.Role!,
                    frame.Visual.IsEffectivelyVisible,
                    frame.Visual is InputElement input && input.IsEffectivelyEnabled));
                parentElementId = semantics.ElementId;
                parentDepth = depth;

                if (depth == parameters.MaxDepth)
                {
                    if (frame.Visual.GetVisualChildren().Any())
                    {
                        MarkTruncated(ref isTruncated, ref truncationReason, "ui.max-depth");
                    }

                    continue;
                }
            }

            if (frame.VisualDepth >= ObservationProtocolLimits.MaxUiVisualDepth)
            {
                if (frame.Visual.GetVisualChildren().Any())
                {
                    MarkTruncated(ref isTruncated, ref truncationReason, "ui.visual-depth");
                }

                continue;
            }

            var remainingVisualBudget = ObservationProtocolLimits.MaxUiVisualsVisited
                - visualsVisited
                - stack.Count;
            if (remainingVisualBudget <= 0)
            {
                if (frame.Visual.GetVisualChildren().Any())
                {
                    MarkTruncated(ref isTruncated, ref truncationReason, "ui.visual-budget");
                }

                continue;
            }

            var children = new List<Visual>(Math.Min(remainingVisualBudget, 16));
            foreach (var child in frame.Visual.GetVisualChildren())
            {
                if (children.Count >= remainingVisualBudget)
                {
                    MarkTruncated(ref isTruncated, ref truncationReason, "ui.visual-budget");
                    break;
                }

                children.Add(child);
            }

            for (var index = children.Count - 1; index >= 0; --index)
            {
                stack.Push(new TraversalFrame(
                    children[index],
                    parentElementId,
                    parentDepth,
                    frame.VisualDepth + 1));
            }
        }

        return Succeeded(new UiTreeReadResult(
            windowId!,
            DateTimeOffset.UtcNow,
            isTruncated,
            truncationReason,
            nodes.ToImmutable()));
    }

    private static StudioShellUiSemantics ReadSemantics(Visual visual, bool isWindow)
    {
        var elementId = AutomationProperties.GetAutomationId(visual);
        var name = AutomationProperties.GetName(visual);
        var role = isWindow
            ? ObservationUiRoles.Window
            : AutomationProperties.GetControlTypeOverride(visual) switch
            {
                AutomationControlType.Group => ObservationUiRoles.Group,
                AutomationControlType.StatusBar => ObservationUiRoles.Status,
                _ => null,
            };
        if (!IsElementId(elementId)
            || !IsBoundedName(name)
            || role is null)
        {
            return new StudioShellUiSemantics(
                ElementId: null,
                Name: null,
                Role: null,
                new ObservationFailure(
                    "observation.ui.semantic-invalid",
                    "provider",
                    "A projected shell element lacks bounded project-owned automation semantics.",
                    Retryable: false,
                    Remediation: "Assign a unique AutomationId, accessible name, and allowlisted role.",
                    CapabilityId: "ui.shell"));
        }

        return new StudioShellUiSemantics(elementId, name, role, Failure: null);
    }

    private static bool IsElementId(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)
            || value.Length > ObservationProtocolLimits.MaxUiElementIdCharacters
            || value[0] is not (>= 'A' and <= 'Z' or >= 'a' and <= 'z'))
        {
            return false;
        }

        foreach (var character in value)
        {
            if (character is not (>= 'A' and <= 'Z' or >= 'a' and <= 'z')
                && !char.IsAsciiDigit(character)
                && character is not '.' and not '_' and not '-')
            {
                return false;
            }
        }

        return true;
    }

    private static bool IsBoundedName(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)
            || value.Length > ObservationProtocolLimits.MaxUiNameCharacters)
        {
            return false;
        }

        foreach (var character in value)
        {
            if (char.IsControl(character))
            {
                return false;
            }
        }

        return true;
    }

    private static async ValueTask<T> InvokeOnUiThreadAsync<T>(
        Func<T> capture,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (Dispatcher.UIThread.CheckAccess())
        {
            return capture();
        }

        return await Dispatcher.UIThread.InvokeAsync(
            capture,
            DispatcherPriority.Background,
            cancellationToken);
    }

    private static void MarkTruncated(
        ref bool isTruncated,
        ref string? truncationReason,
        string reason)
    {
        isTruncated = true;
        truncationReason ??= reason;
    }

    private static ObservationProtocolReadResult<T> Succeeded<T>(T value)
        where T : class =>
        new(value, Failure: null);

    private static ObservationProtocolReadResult<T> Failed<T>(string code, string message)
        where T : class =>
        new(
            Value: null,
            new ObservationFailure(
                code,
                "provider",
                message,
                Retryable: false,
                CapabilityId: "ui.shell"));

    private static ObservationProtocolReadResult<T> WindowNotFound<T>()
        where T : class =>
        Failed<T>(
            "observation.ui.window-not-found",
            "The requested Studio shell window is not currently available.");

    private readonly record struct TraversalFrame(
        Visual Visual,
        string? ParentElementId,
        int ParentDepth,
        int VisualDepth);

    private sealed record StudioShellUiSemantics(
        string? ElementId,
        string? Name,
        string? Role,
        ObservationFailure? Failure);
}
#endif
