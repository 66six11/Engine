using System.Text.Json;

namespace Asharia.Studio.Distribution.Tests;

public static class PythonProductPayloadFixture
{
    private static readonly Lazy<FixtureDocument> Fixture = new(Load);

    public static IEnumerable<object[]> ForbiddenPaths() =>
        Fixture.Value.Forbidden.Select(path => new object[] { path });

    public static IReadOnlyList<string> AllowedPaths => Fixture.Value.Allowed;

    private static FixtureDocument Load()
    {
        var path = Path.Combine(
            AppContext.BaseDirectory,
            "product-boundaries",
            "python-product-payload-v1.json");
        using var stream = File.OpenRead(path);
        using var document = JsonDocument.Parse(stream);
        var root = document.RootElement;
        var propertyNames = root.ValueKind == JsonValueKind.Object
            ? root.EnumerateObject().Select(property => property.Name).ToHashSet(
                StringComparer.Ordinal)
            : [];
        if (!propertyNames.SetEquals(["schemaVersion", "forbidden", "allowed"])
            || root.GetProperty("schemaVersion").ValueKind != JsonValueKind.Number
            || root.GetProperty("schemaVersion").GetInt32() != 1)
        {
            throw new InvalidDataException(
                "Python product payload fixture must use the closed schemaVersion 1 shape.");
        }
        var forbidden = ReadPaths(root, "forbidden");
        var allowed = ReadPaths(root, "allowed");
        if (forbidden.Intersect(allowed, StringComparer.OrdinalIgnoreCase).Any())
        {
            throw new InvalidDataException(
                "Python product payload fixture allowed and forbidden paths must be disjoint.");
        }
        return new FixtureDocument(1, forbidden, allowed);
    }

    private static IReadOnlyList<string> ReadPaths(JsonElement root, string propertyName)
    {
        var element = root.GetProperty(propertyName);
        if (element.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidDataException(
                $"Python product payload fixture '{propertyName}' must be an array.");
        }
        var paths = element.EnumerateArray().Select(value => value.ValueKind
            == JsonValueKind.String
            ? value.GetString()
            : null).ToArray();
        if (paths.Length == 0
            || paths.Any(path => string.IsNullOrEmpty(path))
            || paths.Distinct(StringComparer.OrdinalIgnoreCase).Count() != paths.Length)
        {
            throw new InvalidDataException(
                $"Python product payload fixture '{propertyName}' must contain unique non-empty paths.");
        }
        return paths.Select(path => path!).ToArray();
    }

    private sealed record FixtureDocument(
        int SchemaVersion,
        IReadOnlyList<string> Forbidden,
        IReadOnlyList<string> Allowed);
}
