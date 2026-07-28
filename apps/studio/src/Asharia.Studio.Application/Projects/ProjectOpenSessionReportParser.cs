using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using Asharia.Editor.Projects;

namespace Asharia.Studio.Application.Projects;

internal enum ProjectOpenSessionReportParseErrorCode
{
    InvalidEncoding = 0,
    InvalidJson = 1,
    InvalidShape = 2,
    UnsupportedSchema = 3,
    InvalidValue = 4,
    InvariantViolation = 5,
    NonCanonical = 6,
}

internal sealed record ProjectOpenSessionReportParseError(
    ProjectOpenSessionReportParseErrorCode Code,
    string Pointer,
    string Message);

internal sealed class ProjectOpenSessionReportParseResult
{
    private ProjectOpenSessionReportParseResult(
        ProjectOpenSessionSnapshot? snapshot,
        ProjectOpenSessionReportParseError? error)
    {
        Snapshot = snapshot;
        Error = error;
    }

    public ProjectOpenSessionSnapshot? Snapshot { get; }

    public ProjectOpenSessionReportParseError? Error { get; }

    public bool Succeeded => Snapshot is not null && Error is null;

    public static ProjectOpenSessionReportParseResult Success(
        ProjectOpenSessionSnapshot snapshot) =>
        new(snapshot, error: null);

    public static ProjectOpenSessionReportParseResult Failure(
        ProjectOpenSessionReportParseError error) =>
        new(snapshot: null, error);
}

internal static class ProjectOpenSessionReportParser
{
    private const int MaxReportBytes = 1024 * 1024;
    private const string ReportSchema = "com.asharia.bootstrap-session";
    private const int ReportSchemaVersion = 1;
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    public static ProjectOpenSessionReportParseResult Parse(
        ReadOnlyMemory<byte> reportBytes)
    {
        if (reportBytes.Length == 0
            || reportBytes.Length > MaxReportBytes
            || HasUtf8Bom(reportBytes.Span)
            || reportBytes.Span[^1] != (byte)'\n'
            || reportBytes.Span.Contains((byte)'\r'))
        {
            return Failure(
                ProjectOpenSessionReportParseErrorCode.NonCanonical,
                string.Empty,
                "Project-open session report must be canonical UTF-8 JSON with one trailing LF.");
        }

        try
        {
            _ = StrictUtf8.GetString(reportBytes.Span);
        }
        catch (DecoderFallbackException)
        {
            return Failure(
                ProjectOpenSessionReportParseErrorCode.InvalidEncoding,
                string.Empty,
                "Project-open session report must contain valid UTF-8.");
        }

        try
        {
            using var document = JsonDocument.Parse(
                reportBytes,
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = false,
                    CommentHandling = JsonCommentHandling.Disallow,
                    MaxDepth = 16,
                });
            var snapshot = ParseSnapshot(document.RootElement);
            if (!reportBytes.Span.SequenceEqual(
                RenderCanonical(document.RootElement)))
            {
                return Failure(
                    ProjectOpenSessionReportParseErrorCode.NonCanonical,
                    string.Empty,
                    "Project-open session report bytes do not match the canonical v1 representation.");
            }

            return ProjectOpenSessionReportParseResult.Success(snapshot);
        }
        catch (ProjectOpenSessionReportParseException error)
        {
            return ProjectOpenSessionReportParseResult.Failure(error.Error);
        }
        catch (JsonException)
        {
            return Failure(
                ProjectOpenSessionReportParseErrorCode.InvalidJson,
                string.Empty,
                "Project-open session report is not valid JSON.");
        }
        catch (Exception error) when (
            error is ArgumentException
                or InvalidOperationException
                or OverflowException)
        {
            return Failure(
                ProjectOpenSessionReportParseErrorCode.InvariantViolation,
                string.Empty,
                "Project-open session report violates the managed presentation contract.");
        }
    }

    private static ProjectOpenSessionSnapshot ParseSnapshot(JsonElement root)
    {
        EnsureExactObject(
            root,
            string.Empty,
            [
                "schema",
                "schemaVersion",
                "state",
                "nextAction",
                "desiredSessionIntegrity",
                "currentSessionIntegrity",
                "project",
                "diagnostics",
            ]);
        if (ReadString(root, "schema", "/schema") != ReportSchema
            || ReadInt32(root, "schemaVersion", "/schemaVersion")
                != ReportSchemaVersion)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.UnsupportedSchema,
                "/schema",
                "Project-open session report must use com.asharia.bootstrap-session schema v1.");
        }

        var state = ReadState(root);
        var nextAction = ReadNextAction(root);
        ValidateOptionalIntegrity(
            root.GetProperty("desiredSessionIntegrity"),
            "/desiredSessionIntegrity");
        ValidateOptionalIntegrity(
            root.GetProperty("currentSessionIntegrity"),
            "/currentSessionIntegrity");
        var project = ReadProject(root.GetProperty("project"));
        var diagnostics = ReadDiagnostics(root.GetProperty("diagnostics"));

        try
        {
            return new ProjectOpenSessionSnapshot(
                state,
                nextAction,
                project,
                diagnostics);
        }
        catch (ArgumentException)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvariantViolation,
                "/state",
                "Project-open state, next action, and project summary do not form a valid v1 snapshot.");
        }
    }

    private static ProjectOpenSessionState ReadState(JsonElement root) =>
        ReadString(root, "state", "/state") switch
        {
            "NoProject" => ProjectOpenSessionState.NoProject,
            "Opening" => ProjectOpenSessionState.Opening,
            "Ready" => ProjectOpenSessionState.Ready,
            "PendingBuild" => ProjectOpenSessionState.PendingBuild,
            "PendingRestart" => ProjectOpenSessionState.PendingRestart,
            "RepairRequired" => ProjectOpenSessionState.RepairRequired,
            "UpgradeRequired" => ProjectOpenSessionState.UpgradeRequired,
            "SafeMode" => ProjectOpenSessionState.SafeMode,
            "FatalDistributionError" =>
                ProjectOpenSessionState.FatalDistributionError,
            _ => throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                "/state",
                "Project-open session state is not part of the v1 vocabulary."),
        };

    private static ProjectOpenNextAction ReadNextAction(JsonElement root) =>
        ReadString(root, "nextAction", "/nextAction") switch
        {
            "SelectProject" => ProjectOpenNextAction.SelectProject,
            "InspectProject" => ProjectOpenNextAction.InspectProject,
            "ActivateProjectProfile" =>
                ProjectOpenNextAction.ActivateProjectProfile,
            "BuildProjectHost" => ProjectOpenNextAction.BuildProjectHost,
            "RestartEditor" => ProjectOpenNextAction.RestartEditor,
            "RepairDistribution" =>
                ProjectOpenNextAction.RepairDistribution,
            "UpgradeEngine" => ProjectOpenNextAction.UpgradeEngine,
            "OpenSafeMode" => ProjectOpenNextAction.OpenSafeMode,
            "RepairEditorImage" =>
                ProjectOpenNextAction.RepairEditorImage,
            _ => throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                "/nextAction",
                "Project-open next action is not part of the v1 vocabulary."),
        };

    private static void ValidateOptionalIntegrity(
        JsonElement element,
        string pointer)
    {
        if (element.ValueKind == JsonValueKind.Null)
        {
            return;
        }

        EnsureExactObject(
            element,
            pointer,
            ["algorithm", "digest"]);
        var algorithm = ReadString(element, "algorithm", pointer + "/algorithm");
        var digest = ReadString(element, "digest", pointer + "/digest");
        if (algorithm != "sha256"
            || digest.Length != 64
            || digest.Any(character =>
                !char.IsAsciiHexDigit(character)
                || char.IsAsciiLetterUpper(character)))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                pointer,
                "Session integrity must be a lowercase SHA-256 digest.");
        }
    }

    private static ProjectOpenSummarySnapshot? ReadProject(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Null)
        {
            return null;
        }

        EnsureExactObject(
            element,
            "/project",
            ["projectName", "projectId", "assetSourceRootCount"]);
        var projectName = ReadString(
            element,
            "projectName",
            "/project/projectName");
        if (projectName.Length == 0)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                "/project/projectName",
                "Project name must not be empty.");
        }

        var projectIdText = ReadString(
            element,
            "projectId",
            "/project/projectId");
        if (!Guid.TryParseExact(projectIdText, "D", out var projectId)
            || projectId == Guid.Empty
            || projectId.ToString("D") != projectIdText)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                "/project/projectId",
                "Project id must be a canonical lowercase UUID.");
        }

        var assetSourceRootCount = ReadUInt64(
            element,
            "assetSourceRootCount",
            "/project/assetSourceRootCount");

        return new ProjectOpenSummarySnapshot(
            projectName,
            projectId,
            assetSourceRootCount);
    }

    private static IReadOnlyList<ProjectOpenSessionDiagnosticSnapshot>
        ReadDiagnostics(JsonElement element)
    {
        if (element.ValueKind != JsonValueKind.Array)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                "/diagnostics",
                "Project-open diagnostics must be an array.");
        }

        var diagnostics = new List<ProjectOpenSessionDiagnosticSnapshot>();
        foreach (var item in element.EnumerateArray())
        {
            var pointer = $"/diagnostics/{diagnostics.Count}";
            EnsureExactObject(
                item,
                pointer,
                ["code", "manifestPath", "pointer", "message"]);
            var diagnostic = new ProjectOpenSessionDiagnosticSnapshot(
                ReadText(
                    item,
                    "code",
                    pointer + "/code",
                    maxLength: 200),
                ReadPortablePath(
                    item,
                    "manifestPath",
                    pointer + "/manifestPath"),
                ReadJsonPointer(
                    item,
                    "pointer",
                    pointer + "/pointer"),
                ReadText(
                    item,
                    "message",
                    pointer + "/message",
                    maxLength: 4096));
            if (diagnostics.Count > 0
                && CompareDiagnostics(diagnostics[^1], diagnostic) >= 0)
            {
                throw Invalid(
                    ProjectOpenSessionReportParseErrorCode.InvariantViolation,
                    "/diagnostics",
                    "Project-open diagnostics must be unique and sorted canonically.");
            }

            diagnostics.Add(diagnostic);
        }

        return Array.AsReadOnly(diagnostics.ToArray());
    }

    private static int CompareDiagnostics(
        ProjectOpenSessionDiagnosticSnapshot left,
        ProjectOpenSessionDiagnosticSnapshot right)
    {
        var comparison = CompareUtf8(
            left.ManifestPath,
            right.ManifestPath);
        if (comparison != 0)
        {
            return comparison;
        }

        comparison = CompareUtf8(left.Pointer, right.Pointer);
        if (comparison != 0)
        {
            return comparison;
        }

        comparison = CompareUtf8(left.Code, right.Code);
        return comparison != 0
            ? comparison
            : CompareUtf8(left.Message, right.Message);
    }

    private static int CompareUtf8(string left, string right) =>
        Encoding.UTF8.GetBytes(left)
            .AsSpan()
            .SequenceCompareTo(Encoding.UTF8.GetBytes(right));

    private static string ReadPortablePath(
        JsonElement element,
        string propertyName,
        string pointer)
    {
        var value = ReadString(element, propertyName, pointer);
        if (!IsPortableRelativePath(value))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                pointer,
                "Diagnostic manifest path must be portable and relative.");
        }

        return value;
    }

    private static string ReadJsonPointer(
        JsonElement element,
        string propertyName,
        string pointer)
    {
        var value = ReadString(element, propertyName, pointer);
        if (value.Length > 1000
            || (!string.IsNullOrEmpty(value)
                && !value.StartsWith("/", StringComparison.Ordinal))
            || !value.IsNormalized(NormalizationForm.FormC)
            || value.Any(char.IsControl))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                pointer,
                "Diagnostic pointer must be a normalized JSON pointer.");
        }

        return value;
    }

    private static string ReadText(
        JsonElement element,
        string propertyName,
        string pointer,
        int maxLength)
    {
        var value = ReadString(element, propertyName, pointer);
        if (string.IsNullOrWhiteSpace(value)
            || value.Length > maxLength
            || !value.IsNormalized(NormalizationForm.FormC)
            || value.Any(char.IsControl))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidValue,
                pointer,
                "Project-open report text is empty, invalid, or too long.");
        }

        return value;
    }

    private static string ReadString(
        JsonElement element,
        string propertyName,
        string pointer)
    {
        if (!element.TryGetProperty(propertyName, out var value)
            || value.ValueKind != JsonValueKind.String)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                pointer,
                "Project-open report field must be a string.");
        }

        return value.GetString()
            ?? throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                pointer,
                "Project-open report string must not be null.");
    }

    private static int ReadInt32(
        JsonElement element,
        string propertyName,
        string pointer)
    {
        if (!element.TryGetProperty(propertyName, out var value)
            || value.ValueKind != JsonValueKind.Number
            || !value.TryGetInt32(out var result))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                pointer,
                "Project-open report field must be a 32-bit integer.");
        }

        return result;
    }

    private static ulong ReadUInt64(
        JsonElement element,
        string propertyName,
        string pointer)
    {
        if (!element.TryGetProperty(propertyName, out var value)
            || value.ValueKind != JsonValueKind.Number
            || !value.TryGetUInt64(out var result))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                pointer,
                "Project-open report field must be an unsigned 64-bit integer.");
        }

        return result;
    }

    private static void EnsureExactObject(
        JsonElement element,
        string pointer,
        IReadOnlyList<string> expectedProperties)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                pointer,
                "Project-open report field must be an object.");
        }

        var properties = element.EnumerateObject()
            .Select(property => property.Name)
            .ToArray();
        if (properties.Length != expectedProperties.Count
            || !properties.SequenceEqual(
                expectedProperties,
                StringComparer.Ordinal))
        {
            throw Invalid(
                ProjectOpenSessionReportParseErrorCode.InvalidShape,
                pointer,
                "Project-open report object must contain the exact ordered v1 field set.");
        }
    }

    private static bool IsPortableRelativePath(string value) =>
        !string.IsNullOrWhiteSpace(value)
        && value.Length <= 500
        && value.IsNormalized(NormalizationForm.FormC)
        && !value.StartsWith("/", StringComparison.Ordinal)
        && !value.Contains('\\')
        && !value.Contains(':')
        && !value.Any(char.IsControl)
        && !value.Split('/').Any(part => part is "" or "." or "..");

    private static bool HasUtf8Bom(ReadOnlySpan<byte> bytes) =>
        bytes.Length >= 3
        && bytes[0] == 0xef
        && bytes[1] == 0xbb
        && bytes[2] == 0xbf;

    private static byte[] RenderCanonical(JsonElement root)
    {
        var builder = new StringBuilder();
        WriteCanonical(builder, root, depth: 0);
        builder.Append('\n');
        return StrictUtf8.GetBytes(builder.ToString());
    }

    private static void WriteCanonical(
        StringBuilder builder,
        JsonElement element,
        int depth)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                WriteCanonicalObject(builder, element, depth);
                return;
            case JsonValueKind.Array:
                WriteCanonicalArray(builder, element, depth);
                return;
            case JsonValueKind.String:
                WriteCanonicalString(
                    builder,
                    element.GetString()
                        ?? throw Invalid(
                            ProjectOpenSessionReportParseErrorCode.InvalidShape,
                            string.Empty,
                            "Project-open report string must not be null."));
                return;
            case JsonValueKind.Number:
                WriteCanonicalInteger(builder, element);
                return;
            case JsonValueKind.Null:
                builder.Append("null");
                return;
            default:
                throw Invalid(
                    ProjectOpenSessionReportParseErrorCode.InvalidShape,
                    string.Empty,
                    "Project-open report contains an unsupported JSON value.");
        }
    }

    private static void WriteCanonicalObject(
        StringBuilder builder,
        JsonElement element,
        int depth)
    {
        var properties = element.EnumerateObject().ToArray();
        if (properties.Length == 0)
        {
            builder.Append("{}");
            return;
        }

        builder.Append("{\n");
        for (var index = 0; index < properties.Length; ++index)
        {
            builder.Append(' ', checked((depth + 1) * 2));
            WriteCanonicalString(builder, properties[index].Name);
            builder.Append(": ");
            WriteCanonical(builder, properties[index].Value, depth + 1);
            builder.Append(index + 1 == properties.Length ? '\n' : ",\n");
        }

        builder.Append(' ', checked(depth * 2));
        builder.Append('}');
    }

    private static void WriteCanonicalArray(
        StringBuilder builder,
        JsonElement element,
        int depth)
    {
        var items = element.EnumerateArray().ToArray();
        if (items.Length == 0)
        {
            builder.Append("[]");
            return;
        }

        builder.Append("[\n");
        for (var index = 0; index < items.Length; ++index)
        {
            builder.Append(' ', checked((depth + 1) * 2));
            WriteCanonical(builder, items[index], depth + 1);
            builder.Append(index + 1 == items.Length ? '\n' : ",\n");
        }

        builder.Append(' ', checked(depth * 2));
        builder.Append(']');
    }

    private static void WriteCanonicalInteger(
        StringBuilder builder,
        JsonElement element)
    {
        if (element.TryGetInt64(out var signed))
        {
            builder.Append(signed.ToString(CultureInfo.InvariantCulture));
            return;
        }

        if (element.TryGetUInt64(out var unsigned))
        {
            builder.Append(unsigned.ToString(CultureInfo.InvariantCulture));
            return;
        }

        throw Invalid(
            ProjectOpenSessionReportParseErrorCode.InvalidShape,
            string.Empty,
            "Project-open report numbers must be integers.");
    }

    private static void WriteCanonicalString(
        StringBuilder builder,
        string value)
    {
        builder.Append('"');
        foreach (var character in value)
        {
            switch (character)
            {
                case '"':
                    builder.Append("\\\"");
                    break;
                case '\\':
                    builder.Append("\\\\");
                    break;
                case '\b':
                    builder.Append("\\b");
                    break;
                case '\f':
                    builder.Append("\\f");
                    break;
                case '\n':
                    builder.Append("\\n");
                    break;
                case '\r':
                    builder.Append("\\r");
                    break;
                case '\t':
                    builder.Append("\\t");
                    break;
                case <= '\u001f':
                    builder.Append("\\u");
                    builder.Append(
                        ((int)character).ToString(
                            "x4",
                            CultureInfo.InvariantCulture));
                    break;
                default:
                    builder.Append(character);
                    break;
            }
        }

        builder.Append('"');
    }

    private static ProjectOpenSessionReportParseResult Failure(
        ProjectOpenSessionReportParseErrorCode code,
        string pointer,
        string message) =>
        ProjectOpenSessionReportParseResult.Failure(
            new ProjectOpenSessionReportParseError(
                code,
                pointer,
                message));

    private static ProjectOpenSessionReportParseException Invalid(
        ProjectOpenSessionReportParseErrorCode code,
        string pointer,
        string message) =>
        new(new ProjectOpenSessionReportParseError(
            code,
            pointer,
            message));

    private sealed class ProjectOpenSessionReportParseException(
        ProjectOpenSessionReportParseError error) : Exception
    {
        public ProjectOpenSessionReportParseError Error { get; } = error;
    }
}
