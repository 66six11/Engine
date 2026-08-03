using System;
using System.Collections.Generic;
using System.Collections.Immutable;

namespace Asharia.Studio.DevelopmentProtocol;

public static class ObservationUiRoles
{
    public const string Window = "window";
    public const string Group = "group";
    public const string Status = "status";

    internal static bool IsKnown(string? value) => value is Window or Group or Status;
}

public sealed record UiListWindowsParameters;

public sealed record UiReadTreeParameters(
    string WindowId,
    int MaxDepth,
    int MaxNodes);

public sealed record UiWindowListResult(
    DateTimeOffset CapturedAtUtc,
    ImmutableArray<ObservationUiWindow> Windows);

public sealed record ObservationUiWindow(
    string WindowId,
    string Name,
    bool IsVisible,
    bool IsEnabled);

public sealed record UiTreeReadResult(
    string WindowId,
    DateTimeOffset CapturedAtUtc,
    bool IsTruncated,
    string? TruncationReason,
    ImmutableArray<ObservationUiNode> Nodes);

public sealed record ObservationUiNode(
    string ElementId,
    string? ParentElementId,
    int Depth,
    string Name,
    string Role,
    bool IsVisible,
    bool IsEnabled);

public static class ObservationUiContract
{
    public static bool IsValidElementId(string? value) =>
        ObservationUiContractValidation.IsElementId(value);

    public static bool IsValidReadTreeParameters(UiReadTreeParameters? parameters) =>
        parameters is not null
        && ObservationUiContractValidation.Validate(parameters) is null;

    public static bool IsValidWindowListResult(UiWindowListResult? result) =>
        result is not null
        && ObservationUiContractValidation.Validate(result) is null;

    public static bool IsValidTreeReadResult(UiTreeReadResult? result) =>
        result is not null
        && ObservationUiContractValidation.Validate(result) is null;
}

internal static class ObservationUiContractValidation
{
    internal static string? Validate(UiReadTreeParameters parameters)
    {
        if (!IsElementId(parameters.WindowId)
            || parameters.MaxDepth < 0
            || parameters.MaxDepth > ObservationProtocolLimits.MaxUiDepth
            || parameters.MaxNodes <= 0
            || parameters.MaxNodes > ObservationProtocolLimits.MaxUiNodes)
        {
            return "UI tree requests require a stable window ID and bounded depth/node budgets.";
        }

        return null;
    }

    internal static string? Validate(UiWindowListResult result)
    {
        if (!IsUtcTimestamp(result.CapturedAtUtc)
            || result.Windows.IsDefault
            || result.Windows.Length > ObservationProtocolLimits.MaxUiWindows)
        {
            return "UI window results require a UTC capture time and a bounded initialized window list.";
        }

        var identities = new HashSet<string>(StringComparer.Ordinal);
        foreach (var window in result.Windows)
        {
            if (window is null
                || !IsElementId(window.WindowId)
                || !IsName(window.Name)
                || !identities.Add(window.WindowId))
            {
                return "UI window results require unique stable IDs and bounded semantic names.";
            }
        }

        return null;
    }

    internal static string? Validate(UiTreeReadResult result)
    {
        if (!IsElementId(result.WindowId)
            || !IsUtcTimestamp(result.CapturedAtUtc)
            || result.Nodes.IsDefaultOrEmpty
            || result.Nodes.Length > ObservationProtocolLimits.MaxUiNodes
            || result.IsTruncated != !string.IsNullOrWhiteSpace(result.TruncationReason)
            || (result.TruncationReason is not null
                && !IsBoundedText(
                    result.TruncationReason,
                    ObservationProtocolLimits.MaxUiTruncationReasonCharacters)))
        {
            return "UI tree results require one bounded rooted snapshot and consistent truncation metadata.";
        }

        var depths = new Dictionary<string, int>(StringComparer.Ordinal);
        for (var index = 0; index < result.Nodes.Length; ++index)
        {
            var node = result.Nodes[index];
            if (node is null
                || !IsElementId(node.ElementId)
                || !IsName(node.Name)
                || !ObservationUiRoles.IsKnown(node.Role)
                || node.Depth < 0
                || node.Depth > ObservationProtocolLimits.MaxUiDepth
                || !depths.TryAdd(node.ElementId, node.Depth))
            {
                return "UI tree nodes require unique stable IDs and bounded project-owned semantics.";
            }

            if (index == 0)
            {
                if (!string.Equals(node.ElementId, result.WindowId, StringComparison.Ordinal)
                    || node.ParentElementId is not null
                    || node.Depth != 0
                    || !string.Equals(node.Role, ObservationUiRoles.Window, StringComparison.Ordinal))
                {
                    return "The first UI tree node must be the requested window root.";
                }

                continue;
            }

            var parentElementId = node.ParentElementId;
            if (!IsElementId(parentElementId)
                || !depths.TryGetValue(parentElementId!, out var parentDepth)
                || node.Depth != parentDepth + 1)
            {
                return "UI tree nodes must be topologically ordered beneath an earlier parent.";
            }
        }

        return null;
    }

    internal static bool IsElementId(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)
            || value.Length > ObservationProtocolLimits.MaxUiElementIdCharacters
            || !IsAsciiLetter(value[0]))
        {
            return false;
        }

        foreach (var character in value)
        {
            if (!IsAsciiLetter(character)
                && !char.IsAsciiDigit(character)
                && character is not '.' and not '_' and not '-')
            {
                return false;
            }
        }

        return true;
    }

    private static bool IsName(string? value) =>
        IsBoundedText(value, ObservationProtocolLimits.MaxUiNameCharacters);

    private static bool IsBoundedText(string? value, int maxCharacters)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Length > maxCharacters)
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

    private static bool IsUtcTimestamp(DateTimeOffset value) =>
        value != default && value.Offset == TimeSpan.Zero;

    private static bool IsAsciiLetter(char value) =>
        value is >= 'A' and <= 'Z' or >= 'a' and <= 'z';
}
