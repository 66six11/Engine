using System.Text;
using System.Text.Json;

namespace Asharia.Studio.Distribution;

internal static class StudioEditorHostProfilePayload
{
    public const string RelativePath =
        "profiles/editor/asharia.host-profile.json";

    public const string HostKind = "editor";
    public const string TargetPlatform = "com.asharia.platform.windows";

    private const string ResourceName =
        "Asharia.Studio.Distribution.Profiles.editor.asharia.host-profile.json";
    private const int MaxProfileBytes = 64 * 1024;
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    public static byte[] ReadExactBytes()
    {
        using var source = typeof(StudioEditorHostProfilePayload).Assembly
            .GetManifestResourceStream(ResourceName)
            ?? throw new InvalidDataException(
                "The production Editor Host Profile resource is missing.");
        if (source.Length is < 1 or > MaxProfileBytes)
        {
            throw new InvalidDataException(
                "The production Editor Host Profile resource has an invalid size.");
        }

        var bytes = new byte[checked((int)source.Length)];
        source.ReadExactly(bytes);
        ValidateIdentity(bytes);
        return bytes;
    }

    private static void ValidateIdentity(byte[] bytes)
    {
        if (bytes.Length >= 3
            && bytes[0] == 0xef
            && bytes[1] == 0xbb
            && bytes[2] == 0xbf)
        {
            throw new InvalidDataException(
                "The production Editor Host Profile must not contain a UTF-8 BOM.");
        }

        if (bytes[^1] != (byte)'\n' || bytes.AsSpan().Contains((byte)'\r'))
        {
            throw new InvalidDataException(
                "The production Editor Host Profile must use LF and end with one newline.");
        }

        _ = StrictUtf8.GetString(bytes);
        using var document = JsonDocument.Parse(
            bytes,
            new JsonDocumentOptions
            {
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
                MaxDepth = 32,
            });
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object
            || ReadString(root, "schema") != "com.asharia.host-profile"
            || ReadInt32(root, "schemaVersion") != 1
            || ReadString(root, "hostKind") != HostKind
            || ReadString(root, "targetPlatform") != TargetPlatform)
        {
            throw new InvalidDataException(
                "The embedded Host Profile is not the production Windows Editor policy.");
        }
    }

    private static string? ReadString(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var value)
            && value.ValueKind == JsonValueKind.String
                ? value.GetString()
                : null;

    private static int? ReadInt32(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var value)
            && value.ValueKind == JsonValueKind.Number
            && value.TryGetInt32(out var result)
                ? result
                : null;
}
