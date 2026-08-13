using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using Asharia.Studio.Application.Assets;

namespace Asharia.Studio.EngineBridge.Assets;

internal sealed class AssetCatalogSnapshotJsonParser(TimeProvider timeProvider)
{
    private const string Schema = "com.asharia.editor.assetCatalogSnapshot";
    private const int SchemaVersion = 1;
    private const int MaximumStringBytes = 64 * 1024;
    private const int MaximumSourceRoots = 1_024;
    private const int MaximumNavigationNodes = 100_000;
    private const int MaximumRows = 100_000;
    private const int MaximumDiagnostics = 10_000;
    private const int MaximumSubAssets = 100_000;
    private const int MaximumNavigationDepth = 128;

    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    private static readonly string[] RootProperties =
    [
        "schema",
        "schemaVersion",
        "state",
        "projectId",
        "projectFile",
        "productManifestFile",
        "targetProfile",
        "sourceRoots",
        "navigationNodes",
        "rows",
        "diagnostics",
    ];

    private static readonly string[] SourceRootProperties =
    [
        "name",
        "sourcePathPrefix",
        "directory",
        "resolvedDirectory",
    ];

    private static readonly string[] NavigationProperties =
    [
        "kind",
        "key",
        "parentKey",
        "displayName",
        "scopePath",
        "sourcePath",
        "sourceRootName",
        "sourceRootPrefix",
        "sourceRootDirectory",
        "guid",
        "stableId",
        "assetType",
        "importer",
        "extension",
        "importProfile",
        "assetRole",
        "subAssetCount",
        "productState",
    ];

    private static readonly string[] RowProperties =
    [
        "guid",
        "sourcePath",
        "sourceRootName",
        "sourceRootPrefix",
        "sourceRootDirectory",
        "sourceFilePath",
        "metadataFilePath",
        "displayName",
        "extension",
        "assetType",
        "importer",
        "importerVersion",
        "importProfile",
        "assetRole",
        "productState",
        "currentProductCount",
        "staleProductCount",
        "subAssets",
        "diagnostics",
    ];

    private static readonly string[] SubAssetProperties =
    [
        "stableId",
        "displayName",
        "assetRole",
    ];

    private static readonly string[] DiagnosticProperties =
    [
        "severity",
        "code",
        "sourcePath",
        "path",
        "message",
    ];

    private static readonly string[] RowDiagnosticProperties =
    [
        "severity",
        "code",
        "sourcePath",
        "message",
    ];

    private long nextRevision_;

    public AssetCatalogSnapshot Parse(ReadOnlyMemory<byte> jsonUtf8)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(
                jsonUtf8,
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = false,
                    CommentHandling = JsonCommentHandling.Disallow,
                    MaxDepth = 32,
                });
        }
        catch (JsonException exception)
        {
            throw Invalid("The asset catalog adapter returned invalid JSON.", exception);
        }

        using (document)
        {
            var root = document.RootElement;
            ValidateObject(root, RootProperties, "snapshot");
            if (!string.Equals(ReadString(root, "schema"), Schema, StringComparison.Ordinal))
            {
                throw Invalid("The asset catalog response schema is unsupported.");
            }
            if (ReadInt32(root, "schemaVersion") != SchemaVersion)
            {
                throw Invalid("The asset catalog response schema version is unsupported.");
            }

            var snapshotState = ParseSnapshotState(ReadString(root, "state"));
            var sourceRoots = ParseSourceRoots(root.GetProperty("sourceRoots"));
            var navigation = ParseNavigation(root.GetProperty("navigationNodes"));
            var diagnosticCount = 0;
            var rows = ParseRows(root.GetProperty("rows"), ref diagnosticCount);
            var diagnostics = ParseDiagnostics(
                root.GetProperty("diagnostics"),
                includePath: true,
                ref diagnosticCount);
            ValidateSnapshotState(snapshotState, diagnostics);
            ValidateNavigationReferences(navigation, rows);

            var revision = checked((ulong)Interlocked.Increment(ref nextRevision_));
            return new AssetCatalogSnapshot(
                snapshotState,
                revision,
                timeProvider.GetUtcNow(),
                ParseRequiredGuid(ReadRequiredString(root, "projectId"), "project id"),
                ReadRequiredString(root, "projectFile"),
                ReadString(root, "productManifestFile"),
                ReadRequiredString(root, "targetProfile"),
                sourceRoots,
                navigation,
                rows,
                diagnostics);
        }
    }

    private static ImmutableArray<AssetCatalogSourceRoot> ParseSourceRoots(
        JsonElement value)
    {
        ValidateArray(value, MaximumSourceRoots, "sourceRoots");
        var builder = ImmutableArray.CreateBuilder<AssetCatalogSourceRoot>(
            value.GetArrayLength());
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in value.EnumerateArray())
        {
            ValidateObject(item, SourceRootProperties, "source root");
            var root = new AssetCatalogSourceRoot(
                ReadRequiredString(item, "name"),
                ReadString(item, "sourcePathPrefix"),
                ReadRequiredString(item, "directory"),
                ReadRequiredString(item, "resolvedDirectory"));
            if (!names.Add(root.Name))
            {
                throw Invalid("The asset catalog contains a duplicate source-root name.");
            }
            builder.Add(root);
        }
        return builder.MoveToImmutable();
    }

    private static ImmutableArray<AssetCatalogNavigationEntry> ParseNavigation(
        JsonElement value)
    {
        ValidateArray(value, MaximumNavigationNodes, "navigationNodes");
        var builder = ImmutableArray.CreateBuilder<AssetCatalogNavigationEntry>(
            value.GetArrayLength());
        var depths = new Dictionary<string, (int Depth, AssetCatalogNavigationKind Kind)>(
            StringComparer.Ordinal);
        foreach (var item in value.EnumerateArray())
        {
            ValidateObject(item, NavigationProperties, "navigation node");
            var kind = ParseNavigationKind(ReadString(item, "kind"));
            var key = ReadRequiredString(item, "key");
            var parentText = ReadString(item, "parentKey");
            var parentKey = parentText.Length == 0 ? null : parentText;
            int depth;
            if (parentKey is null)
            {
                if (kind != AssetCatalogNavigationKind.SourceRoot)
                {
                    throw Invalid("Only a source root may omit a navigation parent.");
                }
                depth = 0;
            }
            else
            {
                if (!depths.TryGetValue(parentKey, out var parent))
                {
                    throw Invalid(
                        "Asset catalog navigation parents must precede their children.");
                }
                ValidateNavigationParent(kind, parent.Kind);
                depth = checked(parent.Depth + 1);
                if (depth > MaximumNavigationDepth)
                {
                    throw Invalid("The asset catalog navigation depth exceeds its limit.");
                }
            }

            var guidText = ReadString(item, "guid");
            var guid = ParseOptionalGuid(guidText, "navigation guid");
            var entry = new AssetCatalogNavigationEntry(
                key,
                parentKey,
                kind,
                ReadRequiredString(item, "displayName"),
                ReadString(item, "scopePath"),
                ReadString(item, "sourcePath"),
                ReadString(item, "sourceRootName"),
                ReadString(item, "sourceRootPrefix"),
                ReadString(item, "sourceRootDirectory"),
                guid,
                ReadString(item, "stableId"),
                ReadString(item, "assetType"),
                ReadString(item, "importer"),
                ReadString(item, "extension"),
                ReadString(item, "importProfile"),
                ReadString(item, "assetRole"),
                ReadInt32(item, "subAssetCount"),
                ParseProductState(ReadString(item, "productState")),
                depth);
            if (!depths.TryAdd(key, (depth, kind)))
            {
                throw Invalid("The asset catalog contains a duplicate navigation key.");
            }
            builder.Add(entry);
        }
        return builder.MoveToImmutable();
    }

    private static ImmutableArray<AssetCatalogEntry> ParseRows(
        JsonElement value,
        ref int diagnosticCount)
    {
        ValidateArray(value, MaximumRows, "rows");
        var builder = ImmutableArray.CreateBuilder<AssetCatalogEntry>(value.GetArrayLength());
        var identities = new HashSet<AssetSelectionKey>();
        var totalSubAssets = 0;
        foreach (var item in value.EnumerateArray())
        {
            ValidateObject(item, RowProperties, "asset row");
            var guidText = ReadString(item, "guid");
            var guid = ParseOptionalGuid(guidText, "asset guid");
            var sourcePath = ReadRequiredString(item, "sourcePath");
            var selectionKey = new AssetSelectionKey(guid, sourcePath);
            var subAssets = ParseSubAssets(
                item.GetProperty("subAssets"),
                ref totalSubAssets);
            var diagnostics = ParseDiagnostics(
                item.GetProperty("diagnostics"),
                includePath: false,
                ref diagnosticCount);
            var entry = new AssetCatalogEntry(
                selectionKey,
                guid,
                guidText,
                sourcePath,
                ReadString(item, "sourceRootName"),
                ReadString(item, "sourceRootPrefix"),
                ReadString(item, "sourceRootDirectory"),
                ReadString(item, "sourceFilePath"),
                ReadString(item, "metadataFilePath"),
                ReadRequiredString(item, "displayName"),
                ReadString(item, "extension"),
                ReadRequiredString(item, "assetType"),
                ReadString(item, "importer"),
                ReadUInt64(item, "importerVersion"),
                ReadString(item, "importProfile"),
                ReadString(item, "assetRole"),
                ParseProductState(ReadString(item, "productState")),
                ReadInt32(item, "currentProductCount"),
                ReadInt32(item, "staleProductCount"),
                subAssets,
                diagnostics);
            if (!identities.Add(selectionKey))
            {
                throw Invalid("The asset catalog contains a duplicate asset identity.");
            }
            builder.Add(entry);
        }
        return builder.MoveToImmutable();
    }

    private static ImmutableArray<AssetCatalogSubAsset> ParseSubAssets(
        JsonElement value,
        ref int totalSubAssets)
    {
        ValidateArray(value, MaximumSubAssets, "subAssets");
        totalSubAssets = checked(totalSubAssets + value.GetArrayLength());
        if (totalSubAssets > MaximumSubAssets)
        {
            throw Invalid("The asset catalog sub-asset count exceeds its limit.");
        }
        var builder = ImmutableArray.CreateBuilder<AssetCatalogSubAsset>(
            value.GetArrayLength());
        foreach (var item in value.EnumerateArray())
        {
            ValidateObject(item, SubAssetProperties, "sub-asset");
            builder.Add(new AssetCatalogSubAsset(
                ReadRequiredString(item, "stableId"),
                ReadRequiredString(item, "displayName"),
                ReadRequiredString(item, "assetRole")));
        }
        return builder.MoveToImmutable();
    }

    private static ImmutableArray<AssetCatalogDiagnostic> ParseDiagnostics(
        JsonElement value,
        bool includePath,
        ref int totalCount)
    {
        ValidateArray(value, MaximumDiagnostics, "diagnostics");
        totalCount = checked(totalCount + value.GetArrayLength());
        if (totalCount > MaximumDiagnostics)
        {
            throw Invalid("The asset catalog diagnostic count exceeds its limit.");
        }
        var builder = ImmutableArray.CreateBuilder<AssetCatalogDiagnostic>(
            value.GetArrayLength());
        foreach (var item in value.EnumerateArray())
        {
            ValidateObject(
                item,
                includePath ? DiagnosticProperties : RowDiagnosticProperties,
                "diagnostic");
            var sourcePath = ReadString(item, "sourcePath");
            var path = includePath ? ReadString(item, "path") : string.Empty;
            builder.Add(new AssetCatalogDiagnostic(
                ParseSeverity(ReadString(item, "severity")),
                ReadRequiredString(item, "code"),
                sourcePath.Length == 0 ? null : sourcePath,
                path.Length == 0 ? null : path,
                ReadRequiredString(item, "message")));
        }
        return builder.MoveToImmutable();
    }

    private static void ValidateNavigationReferences(
        ImmutableArray<AssetCatalogNavigationEntry> navigation,
        ImmutableArray<AssetCatalogEntry> rows)
    {
        var rowsByPath = new Dictionary<string, AssetCatalogEntry>(StringComparer.Ordinal);
        var assetNodesByPath = new Dictionary<string, AssetCatalogNavigationEntry>(
            StringComparer.Ordinal);
        var subAssetNodes = new HashSet<(string SourcePath, string StableId)>();
        foreach (var row in rows)
        {
            rowsByPath.Add(row.SourcePath, row);
        }
        foreach (var node in navigation)
        {
            if (node.Kind is AssetCatalogNavigationKind.SourceRoot
                or AssetCatalogNavigationKind.Folder)
            {
                continue;
            }
            if (!rowsByPath.TryGetValue(node.SourcePath, out var row)
                || node.AssetGuid != row.AssetGuid)
            {
                throw Invalid("Asset navigation does not match a catalog row.");
            }
            if (node.Kind == AssetCatalogNavigationKind.Asset)
            {
                if (!assetNodesByPath.TryAdd(node.SourcePath, node)
                    || !AssetNodeMatchesRow(node, row))
                {
                    throw Invalid("Asset navigation does not exactly match its catalog row.");
                }
                continue;
            }

            var subAsset = row.SubAssets.FirstOrDefault(
                value => string.Equals(
                    value.StableId,
                    node.StableId,
                    StringComparison.Ordinal));
            if (subAsset is null
                || !subAssetNodes.Add((node.SourcePath, node.StableId))
                || !string.Equals(subAsset.DisplayName, node.DisplayName, StringComparison.Ordinal)
                || !string.Equals(subAsset.AssetRoleName, node.AssetRoleName, StringComparison.Ordinal))
            {
                throw Invalid("Sub-asset navigation does not exactly match its catalog row.");
            }
        }

        foreach (var row in rows)
        {
            if (!assetNodesByPath.ContainsKey(row.SourcePath)
                || row.SubAssets.Any(
                    subAsset => !subAssetNodes.Contains((row.SourcePath, subAsset.StableId))))
            {
                throw Invalid("Catalog rows require complete asset navigation.");
            }
        }
    }

    private static bool AssetNodeMatchesRow(
        AssetCatalogNavigationEntry node,
        AssetCatalogEntry row) =>
        string.Equals(node.DisplayName, row.DisplayName, StringComparison.Ordinal)
        && string.Equals(node.SourceRootName, row.SourceRootName, StringComparison.Ordinal)
        && string.Equals(node.SourceRootPrefix, row.SourceRootPrefix, StringComparison.Ordinal)
        && string.Equals(node.SourceRootDirectory, row.SourceRootDirectory, StringComparison.Ordinal)
        && string.Equals(node.AssetTypeName, row.AssetTypeName, StringComparison.Ordinal)
        && string.Equals(node.ImporterName, row.ImporterName, StringComparison.Ordinal)
        && string.Equals(node.Extension, row.Extension, StringComparison.Ordinal)
        && string.Equals(node.ImportProfileName, row.ImportProfileName, StringComparison.Ordinal)
        && string.Equals(node.AssetRoleName, row.AssetRoleName, StringComparison.Ordinal)
        && node.SubAssetCount == row.SubAssets.Length
        && node.ProductState == row.ProductState;

    private static void ValidateSnapshotState(
        AssetCatalogSnapshotState state,
        ImmutableArray<AssetCatalogDiagnostic> diagnostics)
    {
        var hasWarning = diagnostics.Any(
            value => value.Severity == AssetCatalogDiagnosticSeverity.Warning);
        var hasError = diagnostics.Any(
            value => value.Severity == AssetCatalogDiagnosticSeverity.Error);
        var valid = state switch
        {
            AssetCatalogSnapshotState.Ready => !hasWarning && !hasError,
            AssetCatalogSnapshotState.Degraded => hasWarning && !hasError,
            AssetCatalogSnapshotState.Failed => hasError,
            _ => false,
        };
        if (!valid)
        {
            throw Invalid("The asset catalog state does not match its diagnostics.");
        }
    }

    private static void ValidateNavigationParent(
        AssetCatalogNavigationKind child,
        AssetCatalogNavigationKind parent)
    {
        var valid = child switch
        {
            AssetCatalogNavigationKind.Folder =>
                parent is AssetCatalogNavigationKind.SourceRoot
                    or AssetCatalogNavigationKind.Folder,
            AssetCatalogNavigationKind.Asset =>
                parent is AssetCatalogNavigationKind.SourceRoot
                    or AssetCatalogNavigationKind.Folder,
            AssetCatalogNavigationKind.SubAsset => parent == AssetCatalogNavigationKind.Asset,
            _ => false,
        };
        if (!valid)
        {
            throw Invalid("The asset catalog navigation hierarchy is invalid.");
        }
    }

    private static void ValidateArray(
        JsonElement value,
        int maximumCount,
        string name)
    {
        if (value.ValueKind != JsonValueKind.Array)
        {
            throw Invalid($"Asset catalog '{name}' must be an array.");
        }
        if (value.GetArrayLength() > maximumCount)
        {
            throw Invalid($"Asset catalog '{name}' exceeds its count limit.");
        }
    }

    private static void ValidateObject(
        JsonElement value,
        IReadOnlyList<string> expected,
        string name)
    {
        if (value.ValueKind != JsonValueKind.Object)
        {
            throw Invalid($"Asset catalog {name} must be an object.");
        }

        ulong seen = 0;
        foreach (var property in value.EnumerateObject())
        {
            var index = IndexOf(expected, property.Name);
            if (index < 0)
            {
                throw Invalid($"Asset catalog {name} contains an unknown property.");
            }
            var bit = 1UL << index;
            if ((seen & bit) != 0)
            {
                throw Invalid($"Asset catalog {name} contains a duplicate property.");
            }
            seen |= bit;
        }
        var required = (1UL << expected.Count) - 1UL;
        if (seen != required)
        {
            throw Invalid($"Asset catalog {name} is missing a required property.");
        }
    }

    private static int IndexOf(IReadOnlyList<string> values, string value)
    {
        for (var index = 0; index < values.Count; index++)
        {
            if (string.Equals(values[index], value, StringComparison.Ordinal))
            {
                return index;
            }
        }
        return -1;
    }

    private static string ReadRequiredString(JsonElement owner, string name)
    {
        var value = ReadString(owner, name);
        return string.IsNullOrWhiteSpace(value)
            ? throw Invalid($"Asset catalog '{name}' must not be empty.")
            : value;
    }

    private static string ReadString(JsonElement owner, string name)
    {
        var property = owner.GetProperty(name);
        if (property.ValueKind != JsonValueKind.String)
        {
            throw Invalid($"Asset catalog '{name}' must be a string.");
        }
        var value = property.GetString() ?? string.Empty;
        try
        {
            if (StrictUtf8.GetByteCount(value) > MaximumStringBytes)
            {
                throw Invalid($"Asset catalog '{name}' exceeds its size limit.");
            }
        }
        catch (EncoderFallbackException exception)
        {
            throw Invalid($"Asset catalog '{name}' is not valid Unicode.", exception);
        }
        return value;
    }

    private static int ReadInt32(JsonElement owner, string name)
    {
        var property = owner.GetProperty(name);
        return property.ValueKind == JsonValueKind.Number
            && property.TryGetInt32(out var value)
            && value >= 0
                ? value
                : throw Invalid($"Asset catalog '{name}' must be a non-negative integer.");
    }

    private static ulong ReadUInt64(JsonElement owner, string name)
    {
        var property = owner.GetProperty(name);
        return property.ValueKind == JsonValueKind.Number
            && property.TryGetUInt64(out var value)
                ? value
                : throw Invalid($"Asset catalog '{name}' must be an unsigned integer.");
    }

    private static Guid? ParseOptionalGuid(string value, string name)
    {
        if (value.Length == 0)
        {
            return null;
        }
        return Guid.TryParseExact(value, "D", out var guid) && guid != Guid.Empty
            ? guid
            : throw Invalid($"The asset catalog {name} is invalid.");
    }

    private static Guid ParseRequiredGuid(string value, string name) =>
        ParseOptionalGuid(value, name)
        ?? throw Invalid($"The asset catalog {name} is required.");

    private static AssetCatalogSnapshotState ParseSnapshotState(string value) => value switch
    {
        "ready" => AssetCatalogSnapshotState.Ready,
        "degraded" => AssetCatalogSnapshotState.Degraded,
        "failed" => AssetCatalogSnapshotState.Failed,
        _ => throw Invalid("The asset catalog snapshot state is unknown."),
    };

    private static AssetCatalogNavigationKind ParseNavigationKind(string value) => value switch
    {
        "source-root" => AssetCatalogNavigationKind.SourceRoot,
        "folder" => AssetCatalogNavigationKind.Folder,
        "asset" => AssetCatalogNavigationKind.Asset,
        "sub-asset" => AssetCatalogNavigationKind.SubAsset,
        _ => throw Invalid("The asset catalog navigation kind is unknown."),
    };

    private static AssetCatalogProductState ParseProductState(string value) => value switch
    {
        "not-tracked" => AssetCatalogProductState.NotTracked,
        "ready" => AssetCatalogProductState.Current,
        "missing-product" => AssetCatalogProductState.Missing,
        "stale-product" => AssetCatalogProductState.Stale,
        "invalid-product" => AssetCatalogProductState.Invalid,
        _ => throw Invalid("The asset catalog product state is unknown."),
    };

    private static AssetCatalogDiagnosticSeverity ParseSeverity(string value) => value switch
    {
        "info" => AssetCatalogDiagnosticSeverity.Info,
        "warning" => AssetCatalogDiagnosticSeverity.Warning,
        "error" => AssetCatalogDiagnosticSeverity.Error,
        _ => throw Invalid("The asset catalog diagnostic severity is unknown."),
    };

    private static InvalidDataException Invalid(string message, Exception? inner = null) =>
        new(message, inner);
}
