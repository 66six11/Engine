using System;
using System.Collections.Generic;
using System.Text.Json;
using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Services;

internal static class FrameDebuggerSnapshotJsonReader
{
    public static FrameDebuggerSnapshot? TryRead(NativeFrameDebuggerSnapshotPayload payload)
    {
        if (payload.Format != NativeFrameDebuggerSnapshotFormat.JsonUtf8)
        {
            return null;
        }

        try
        {
            using var document = JsonDocument.Parse(payload.Bytes);
            var root = document.RootElement;
            var schemaVersion = GetInt32(root, "schemaVersion");
            if (schemaVersion != payload.SchemaVersion)
            {
                return null;
            }

            return new FrameDebuggerSnapshot(
                GetInt32(root, "version"),
                ReadState(GetString(root, "state")),
                ReadCapture(root),
                ReadArray(root, "passes", ReadPass),
                ReadArray(root, "commands", ReadCommand),
                ReadArray(root, "resources", ReadResource),
                ReadArray(root, "accessEdges", ReadAccessEdge),
                ReadArray(root, "dependencyEdges", ReadDependencyEdge),
                ReadArray(root, "transitions", ReadTransition),
                ReadArray(root, "executionEvents", ReadExecutionEvent),
                ReadPreview(root),
                GetString(root, "message"));
        }
        catch (ArgumentException)
        {
            return null;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static FrameDebugCaptureSnapshot? ReadCapture(JsonElement root)
    {
        if (!root.TryGetProperty("capture", out var capture) ||
            capture.ValueKind == JsonValueKind.Null)
        {
            return null;
        }

        return new FrameDebugCaptureSnapshot(
            GetString(capture, "captureId"),
            GetInt64(capture, "frameIndex"),
            GetUInt64(capture, "submittedFrameEpoch"),
            GetString(capture, "viewKind"),
            GetInt32(capture, "requestedWidth"),
            GetInt32(capture, "requestedHeight"),
            ReadOptionalDateTimeOffset(capture, "capturedAtUtc") ?? DateTimeOffset.UnixEpoch);
    }

    private static FrameDebugPassSnapshot ReadPass(JsonElement pass)
    {
        return new FrameDebugPassSnapshot(
            GetString(pass, "id"),
            GetInt32(pass, "passIndex"),
            GetInt32(pass, "declarationIndex"),
            GetString(pass, "name"),
            GetNullableString(pass, "type"),
            GetNullableString(pass, "paramsType"),
            GetBoolean(pass, "allowCulling"),
            GetBoolean(pass, "hasSideEffects"),
            GetInt32(pass, "commandCount"),
            GetInt32(pass, "imageTransitionCount"),
            GetInt32(pass, "bufferTransitionCount"));
    }

    private static FrameDebugCommandSnapshot ReadCommand(JsonElement command)
    {
        return new FrameDebugCommandSnapshot(
            GetString(command, "id"),
            GetString(command, "passId"),
            GetInt32(command, "commandIndex"),
            GetInt32(command, "declarationIndex"),
            GetString(command, "passName"),
            GetString(command, "kind"),
            GetNullableString(command, "detail"));
    }

    private static FrameDebugResourceSnapshot ReadResource(JsonElement resource)
    {
        var kind = GetString(resource, "kind");
        return new FrameDebugResourceSnapshot(
            GetString(resource, "id"),
            kind,
            GetUInt32(resource, "resourceIndex"),
            GetString(resource, "name"),
            GetNullableString(resource, "lifetime"),
            ReadResourceFormatOrSize(resource, kind),
            ReadResourceExtent(resource),
            ReadResourceAccess(resource, kind, initial: true),
            ReadResourceAccess(resource, kind, initial: false));
    }

    private static FrameDebugAccessEdgeSnapshot ReadAccessEdge(JsonElement edge)
    {
        return new FrameDebugAccessEdgeSnapshot(
            GetString(edge, "id"),
            GetString(edge, "passId"),
            GetString(edge, "resourceId"),
            GetString(edge, "passName"),
            GetString(edge, "resourceName"),
            GetNullableString(edge, "slotName"),
            GetNullableString(edge, "access"),
            GetNullableString(edge, "shaderStage"));
    }

    private static FrameDebugDependencyEdgeSnapshot ReadDependencyEdge(JsonElement edge)
    {
        return new FrameDebugDependencyEdgeSnapshot(
            GetString(edge, "id"),
            GetString(edge, "fromPassId"),
            GetString(edge, "toPassId"),
            GetString(edge, "resourceId"),
            GetString(edge, "resourceName"),
            GetNullableString(edge, "reason"));
    }

    private static FrameDebugTransitionSnapshot ReadTransition(JsonElement transition)
    {
        return new FrameDebugTransitionSnapshot(
            GetString(transition, "id"),
            GetString(transition, "phase"),
            GetString(transition, "passId"),
            GetString(transition, "resourceId"),
            GetString(transition, "passName"),
            GetString(transition, "resourceName"),
            GetNullableString(transition, "oldImageAccess") ??
            GetNullableString(transition, "oldBufferAccess"),
            GetNullableString(transition, "newImageAccess") ??
            GetNullableString(transition, "newBufferAccess"));
    }

    private static FrameDebugExecutionEventSnapshot ReadExecutionEvent(JsonElement executionEvent)
    {
        return new FrameDebugExecutionEventSnapshot(
            GetString(executionEvent, "id"),
            GetInt32(executionEvent, "eventIndex"),
            GetString(executionEvent, "kind"),
            GetString(executionEvent, "passId"),
            GetString(executionEvent, "passName"),
            GetNullableString(executionEvent, "commandId"),
            GetString(executionEvent, "label"),
            GetNullableString(executionEvent, "sourceResourceId"),
            GetNullableString(executionEvent, "targetResourceId"),
            GetUInt32(executionEvent, "vertexCount"),
            GetUInt32(executionEvent, "indexCount"),
            GetUInt32(executionEvent, "instanceCount"),
            GetUInt32(executionEvent, "groupCountX"),
            GetUInt32(executionEvent, "groupCountY"),
            GetUInt32(executionEvent, "groupCountZ"));
    }

    private static FrameDebugPreviewSnapshot ReadPreview(JsonElement root)
    {
        if (!root.TryGetProperty("preview", out var preview) ||
            preview.ValueKind == JsonValueKind.Null)
        {
            return new FrameDebugPreviewSnapshot("NotRequested", null, null, string.Empty);
        }

        return new FrameDebugPreviewSnapshot(
            GetString(preview, "status"),
            GetNullableString(preview, "selectedPassId"),
            GetNullableString(preview, "selectedExecutionEventId"),
            GetNullableString(preview, "message"));
    }

    private static FrameDebuggerState ReadState(string value)
    {
        return value switch
        {
            "Running" => FrameDebuggerState.Running,
            "CaptureRequested" => FrameDebuggerState.CaptureRequested,
            "CapturingFrame" => FrameDebuggerState.CapturingFrame,
            "WaitingGpuFence" => FrameDebuggerState.WaitingGpuFence,
            "PausedFrameDebug" => FrameDebuggerState.PausedFrameDebug,
            "Resume" or "ResumeRequested" => FrameDebuggerState.ResumeRequested,
            "Faulted" => FrameDebuggerState.Faulted,
            _ => FrameDebuggerState.Unavailable,
        };
    }

    private static IReadOnlyList<T> ReadArray<T>(
        JsonElement root,
        string propertyName,
        Func<JsonElement, T> readItem)
    {
        if (!root.TryGetProperty(propertyName, out var array) ||
            array.ValueKind != JsonValueKind.Array)
        {
            return [];
        }

        var values = new List<T>();
        foreach (var item in array.EnumerateArray())
        {
            values.Add(readItem(item));
        }

        return values;
    }

    private static string? ReadResourceFormatOrSize(JsonElement resource, string kind)
    {
        if (string.Equals(kind, "Buffer", StringComparison.Ordinal))
        {
            return resource.TryGetProperty("bufferByteSize", out var bytes)
                ? bytes.GetUInt64().ToString()
                : null;
        }

        return GetNullableString(resource, "imageFormat");
    }

    private static string? ReadResourceExtent(JsonElement resource)
    {
        if (!resource.TryGetProperty("imageExtent", out var extent) ||
            extent.ValueKind != JsonValueKind.Object)
        {
            return null;
        }

        return $"{GetInt32(extent, "width")}x{GetInt32(extent, "height")}";
    }

    private static string? ReadResourceAccess(JsonElement resource, string kind, bool initial)
    {
        if (string.Equals(kind, "Buffer", StringComparison.Ordinal))
        {
            return GetNullableString(resource, initial ? "bufferInitialAccess" : "bufferFinalAccess");
        }

        return GetNullableString(resource, initial ? "imageInitialAccess" : "imageFinalAccess");
    }

    private static DateTimeOffset? ReadOptionalDateTimeOffset(
        JsonElement value,
        string propertyName)
    {
        var text = GetNullableString(value, propertyName);
        if (text is null)
        {
            return null;
        }

        return DateTimeOffset.TryParse(text, out var parsed) ? parsed : null;
    }

    private static string GetString(JsonElement value, string propertyName)
    {
        return GetNullableString(value, propertyName) ?? string.Empty;
    }

    private static string? GetNullableString(JsonElement value, string propertyName)
    {
        if (!value.TryGetProperty(propertyName, out var property) ||
            property.ValueKind == JsonValueKind.Null)
        {
            return null;
        }

        return property.GetString();
    }

    private static bool GetBoolean(JsonElement value, string propertyName)
    {
        return value.TryGetProperty(propertyName, out var property) && property.GetBoolean();
    }

    private static int GetInt32(JsonElement value, string propertyName)
    {
        if (!value.TryGetProperty(propertyName, out var property))
        {
            return 0;
        }

        return property.GetInt32();
    }

    private static long GetInt64(JsonElement value, string propertyName)
    {
        if (!value.TryGetProperty(propertyName, out var property))
        {
            return 0L;
        }

        return property.GetInt64();
    }

    private static uint GetUInt32(JsonElement value, string propertyName)
    {
        if (!value.TryGetProperty(propertyName, out var property))
        {
            return 0U;
        }

        return property.GetUInt32();
    }

    private static ulong GetUInt64(JsonElement value, string propertyName)
    {
        if (!value.TryGetProperty(propertyName, out var property))
        {
            return 0UL;
        }

        return property.GetUInt64();
    }
}
