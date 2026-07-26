using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Asharia.Studio.Application.Bootstrap.Distribution;
using Xunit;

namespace Asharia.Studio.Application.Tests.Bootstrap.Distribution;

public sealed class VerifiedEditorImageInventoryTests
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        WriteIndented = true,
    };

    public static IEnumerable<object[]> ForbiddenPythonProductPaths =>
        ReadProductPayloadPolicy("forbidden")
            .Select(path => new object[] { path });

    [Fact]
    public async Task VerifyAsync_issues_revocable_exact_editor_image_lease()
    {
        using var fixture = new DistributionFixture();

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.True(result.Succeeded, Render(result));
        var lease = Assert.IsType<VerifiedEditorImageInventoryLease>(result.Lease);
        Assert.Equal(fixture.EngineGenerationId, lease.EngineGenerationId);
        Assert.Equal("com.asharia.platform.windows", lease.TargetPlatform);
        Assert.Equal("x86_64", lease.TargetArchitecture);
        Assert.Equal("bin/editor.exe", lease.EntryPoint);
        Assert.Equal(2, lease.Files.Count);
        Assert.True(lease.TryGetCurrentFile(
            "metadata/bootstrap.json",
            out var metadata));
        Assert.Equal("metadata", metadata!.Role);
        Assert.True(lease.IsCurrent);

        lease.Revoke();

        Assert.False(lease.IsCurrent);
        Assert.False(lease.TryGetCurrentFile(
            "metadata/bootstrap.json",
            out var revoked));
        Assert.Null(revoked);
    }

    [Fact]
    public async Task VerifyAsync_rejects_editor_file_drift()
    {
        using var fixture = new DistributionFixture();
        File.AppendAllText(
            Path.Combine(
                fixture.GenerationRoot,
                "metadata",
                "bootstrap.json"),
            "drift");

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.file-integrity-mismatch");
    }

    [Fact]
    public async Task VerifyAsync_rejects_generation_selected_by_another_id()
    {
        using var fixture = new DistributionFixture();
        var other = "sha256-" + new string('f', 64);

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            other,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.generation-root-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_noncanonical_manifest_bytes()
    {
        using var fixture = new DistributionFixture();
        var manifestPath = Path.Combine(
            fixture.GenerationRoot,
            "asharia.engine-distribution.json");
        var text = File.ReadAllText(manifestPath);
        File.WriteAllText(
            manifestPath,
            text.Replace("\n", "\r\n", StringComparison.Ordinal));

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_malformed_manifest()
    {
        using var fixture = new DistributionFixture();
        File.WriteAllText(fixture.ManifestPath, "{\n");

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_oversized_manifest_before_parsing()
    {
        using var fixture = new DistributionFixture();
        using (var stream = new FileStream(
                   fixture.ManifestPath,
                   FileMode.Open,
                   FileAccess.Write,
                   FileShare.None))
        {
            stream.SetLength((64L * 1024 * 1024) + 1);
        }

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-read-failed");
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task VerifyAsync_rejects_unknown_or_reordered_properties(
        bool addUnknownProperty)
    {
        using var fixture = new DistributionFixture();
        fixture.RewriteManifest(
            root =>
            {
                if (addUnknownProperty)
                {
                    root["unexpected"] = true;
                    return;
                }

                var schema = root["schema"]!.DeepClone();
                Assert.True(root.Remove("schema"));
                root["schema"] = schema;
            });

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_unordered_editor_file_inventory()
    {
        using var fixture = new DistributionFixture();
        fixture.RewriteManifest(
            root =>
            {
                var files = root["editorImage"]!["files"]!.AsArray();
                var first = files[0]!.DeepClone();
                files[0] = files[1]!.DeepClone();
                files[1] = first;
            },
            recomputeIdentity: true);

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_nonportable_editor_file_path()
    {
        using var fixture = new DistributionFixture();
        fixture.RewriteManifest(root =>
        {
            var files = root["editorImage"]!["files"]!.AsArray();
            files[1]!["path"] = "../outside-image.bin";
        });

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_declared_editor_image_over_byte_budget()
    {
        using var fixture = new DistributionFixture();
        fixture.RewriteManifest(
            root =>
            {
                var files = root["editorImage"]!["files"]!.AsArray();
                files[0]!["size"] = (4L * 1024 * 1024 * 1024) + 1;
            },
            recomputeIdentity: true);

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.budget-exceeded");
    }

    [Fact]
    public async Task VerifyAsync_rejects_nonexecutable_entry_point()
    {
        using var fixture = new DistributionFixture();
        fixture.RewriteManifest(
            root =>
            {
                var files = root["editorImage"]!["files"]!.AsArray();
                var entry = Assert.Single(
                    files,
                    value => value!["path"]!.GetValue<string>()
                        == "bin/editor.exe");
                entry!["role"] = "resource";
            },
            recomputeIdentity: true);

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Fact]
    public async Task VerifyAsync_rejects_case_insensitive_duplicate_paths()
    {
        using var fixture = new DistributionFixture(
            [
                "bin/editor.exe",
                "BIN/EDITOR.EXE",
            ],
            entryPoint: "bin/editor.exe");

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.manifest-invalid");
    }

    [Theory]
    [MemberData(nameof(ForbiddenPythonProductPaths))]
    public async Task VerifyAsync_rejects_shared_python_product_policy_path(
        string forbiddenPath)
    {
        using var fixture = new DistributionFixture(
            [
                "bin/editor.exe",
                forbiddenPath,
            ],
            entryPoint: "bin/editor.exe");

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        var diagnostic = Assert.Single(
            result.Diagnostics,
            item => item.Code
                == "distribution.editor-image.python-payload-forbidden");
        Assert.Equal(forbiddenPath, diagnostic.Location);
    }

    [Fact]
    public async Task VerifyAsync_accepts_shared_non_python_product_policy_paths()
    {
        var allowed = ReadProductPayloadPolicy("allowed");
        Assert.NotEmpty(allowed);
        Assert.Contains("controls/dotnet.exe", allowed);
        using var fixture = new DistributionFixture(
            allowed,
            entryPoint: "controls/dotnet.exe");

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.True(result.Succeeded, Render(result));
        Assert.Equal(allowed.Count, result.Lease!.Files.Count);
    }

    [Fact]
    public async Task VerifyAsync_rejects_reparse_file_when_supported()
    {
        using var fixture = new DistributionFixture();
        if (!fixture.TryReplaceWithSymbolicLink("metadata/bootstrap.json"))
        {
            return;
        }

        var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.editor-image.file-invalid");
    }

    [Fact]
    public async Task VerifyAsync_matches_existing_python_canonical_v1_fixture()
    {
        var repositoryRoot = FindRepositoryRoot();
        var source = Path.Combine(
            repositoryRoot,
            "tools",
            "tests",
            "fixtures",
            "package-contracts",
            "valid-engine-distribution.json");
        using var document = JsonDocument.Parse(File.ReadAllBytes(source));
        var engineGenerationId = document.RootElement
            .GetProperty("engineGenerationId")
            .GetString()!;
        var temporaryRoot = Path.Combine(
            Path.GetTempPath(),
            $"asharia-distribution-canonical-{Guid.NewGuid():N}");
        var generationRoot = Path.Combine(temporaryRoot, engineGenerationId);
        Directory.CreateDirectory(generationRoot);
        File.Copy(
            source,
            Path.Combine(generationRoot, "asharia.engine-distribution.json"));
        try
        {
            var result = await EngineDistributionEditorImageVerifier.VerifyAsync(
                engineGenerationId,
                generationRoot);

            Assert.False(result.Succeeded);
            Assert.Null(result.Lease);
            Assert.DoesNotContain(
                result.Diagnostics,
                diagnostic => diagnostic.Code
                    == "distribution.editor-image.manifest-invalid");
            Assert.Contains(
                result.Diagnostics,
                diagnostic => diagnostic.Code
                    == "distribution.editor-image.file-invalid");
        }
        finally
        {
            Directory.Delete(temporaryRoot, recursive: true);
        }
    }

    private static string Render(
        VerifiedEditorImageInventoryVerifyResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static IReadOnlyList<string> ReadProductPayloadPolicy(
        string propertyName)
    {
        var path = Path.Combine(
            FindRepositoryRoot(),
            "tools",
            "tests",
            "fixtures",
            "product-boundaries",
            "python-product-payload-v1.json");
        using var document = JsonDocument.Parse(File.ReadAllBytes(path));
        Assert.Equal(
            1,
            document.RootElement.GetProperty("schemaVersion").GetInt32());
        return document.RootElement.GetProperty(propertyName)
            .EnumerateArray()
            .Select(element => element.GetString()!)
            .ToArray();
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt"))
                && Directory.Exists(
                    Path.Combine(current.FullName, "tools", "tests")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not locate repository root.");
    }

    internal sealed class DistributionFixture : IDisposable
    {
        public DistributionFixture(
            IReadOnlyList<string>? editorPaths = null,
            string entryPoint = "bin/editor.exe")
        {
            Root = Path.Combine(
                Path.GetTempPath(),
                $"asharia-editor-image-inventory-{Guid.NewGuid():N}");
            editorPaths ??=
            [
                "bin/editor.exe",
                "metadata/bootstrap.json",
            ];
            var evidence = editorPaths
                .Select((path, index) =>
                {
                    var bytes = Encoding.UTF8.GetBytes($"file-{index}\n");
                    return new FileEvidence(
                        path,
                        path == entryPoint
                            ? "executable"
                            : ClassifyRole(path),
                        ClassifyMediaType(path),
                        bytes);
                })
                .OrderBy(file => file.Path, Utf8Comparer.Instance)
                .ToArray();
            var payload = CreateManifest(
                evidence,
                entryPoint,
                engineGenerationId: null);
            EngineGenerationId = "sha256-"
                + Convert.ToHexString(SHA256.HashData(RenderJson(payload)))
                    .ToLowerInvariant();
            GenerationRoot = Path.Combine(Root, EngineGenerationId);
            foreach (var file in evidence)
            {
                var path = Path.Combine(
                    GenerationRoot,
                    file.Path.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllBytes(path, file.Contents);
            }

            File.WriteAllBytes(
                Path.Combine(
                    GenerationRoot,
                    "asharia.engine-distribution.json"),
                RenderJson(CreateManifest(
                    evidence,
                    entryPoint,
                    EngineGenerationId)));
        }

        public string Root { get; }

        public string EngineGenerationId { get; private set; }

        public string GenerationRoot { get; private set; }

        public string ManifestPath => Path.Combine(
            GenerationRoot,
            "asharia.engine-distribution.json");

        public void RewriteManifest(
            Action<JsonObject> rewrite,
            bool recomputeIdentity = false)
        {
            ArgumentNullException.ThrowIfNull(rewrite);
            var root = JsonNode.Parse(File.ReadAllBytes(ManifestPath))!
                .AsObject();
            rewrite(root);
            if (recomputeIdentity)
            {
                var payload = root.DeepClone().AsObject();
                Assert.True(payload.Remove("engineGenerationId"));
                var nextGenerationId = "sha256-"
                    + Convert.ToHexString(
                        SHA256.HashData(RenderJson(payload)))
                        .ToLowerInvariant();
                root["engineGenerationId"] = nextGenerationId;
                var nextGenerationRoot = Path.Combine(
                    Root,
                    nextGenerationId);
                if (!string.Equals(
                        GenerationRoot,
                        nextGenerationRoot,
                        StringComparison.Ordinal))
                {
                    Directory.Move(GenerationRoot, nextGenerationRoot);
                }

                EngineGenerationId = nextGenerationId;
                GenerationRoot = nextGenerationRoot;
            }

            File.WriteAllBytes(ManifestPath, RenderJson(root));
        }

        public void AddInventoryFiles(
            IReadOnlyDictionary<string, byte[]> files)
        {
            ArgumentNullException.ThrowIfNull(files);
            foreach (var (relativePath, contents) in files)
            {
                var absolutePath = Path.Combine(
                    GenerationRoot,
                    relativePath.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                Directory.CreateDirectory(
                    Path.GetDirectoryName(absolutePath)!);
                File.WriteAllBytes(absolutePath, contents);
            }

            RewriteManifest(
                root =>
                {
                    var editorFiles = root["editorImage"]!["files"]!
                        .AsArray();
                    var combined = editorFiles
                        .Select(value => value!.DeepClone())
                        .Concat(files.Select(item =>
                            (JsonNode)new JsonObject
                            {
                                ["path"] = item.Key,
                                ["role"] = ClassifyRole(item.Key),
                                ["mediaType"] = ClassifyMediaType(item.Key),
                                ["size"] = item.Value.LongLength,
                                ["integrity"] = new JsonObject
                                {
                                    ["algorithm"] = "sha256",
                                    ["digest"] = Convert.ToHexString(
                                            SHA256.HashData(item.Value))
                                        .ToLowerInvariant(),
                                },
                            }))
                        .OrderBy(
                            value => value["path"]!.GetValue<string>(),
                            Utf8Comparer.Instance)
                        .ToArray();
                    editorFiles.Clear();
                    foreach (var value in combined)
                    {
                        editorFiles.Add(value);
                    }
                },
                recomputeIdentity: true);
        }

        public bool TryReplaceWithSymbolicLink(string relativePath)
        {
            var link = Path.Combine(
                GenerationRoot,
                relativePath.Replace('/', Path.DirectorySeparatorChar));
            var target = Path.Combine(Root, "external-link-target.bin");
            File.WriteAllBytes(
                target,
                File.ReadAllBytes(link));
            File.Delete(link);
            try
            {
                File.CreateSymbolicLink(link, target);
                return true;
            }
            catch (Exception error) when (
                error is IOException
                    or PlatformNotSupportedException
                    or UnauthorizedAccessException)
            {
                File.WriteAllBytes(
                    link,
                    File.ReadAllBytes(target));
                return false;
            }
        }

        public void Dispose()
        {
            if (Directory.Exists(Root))
            {
                Directory.Delete(Root, recursive: true);
            }
        }

        private static JsonObject CreateManifest(
            IReadOnlyList<FileEvidence> files,
            string entryPoint,
            string? engineGenerationId)
        {
            var root = new JsonObject
            {
                ["schema"] = "com.asharia.engine-distribution",
                ["schemaVersion"] = 1,
            };
            if (engineGenerationId is not null)
            {
                root["engineGenerationId"] = engineGenerationId;
            }

            root["distribution"] = new JsonObject
            {
                ["id"] = "com.asharia.distribution.test",
                ["engineVersion"] = "0.1.0",
                ["engineApiVersion"] = "0.1.0",
            };
            root["context"] = new JsonObject
            {
                ["targetPlatform"] = "com.asharia.platform.windows",
                ["configuration"] = "Debug",
                ["toolchain"] = new JsonObject
                {
                    ["compilerId"] = "test",
                    ["compilerVersion"] = "0.1.0",
                    ["targetSystem"] = "Windows",
                    ["targetArchitecture"] = "x86_64",
                    ["runtimeLibrary"] = "test",
                },
            };
            root["editorImage"] = new JsonObject
            {
                ["entryPoint"] = entryPoint,
                ["files"] = new JsonArray(files.Select(file =>
                    (JsonNode)new JsonObject
                    {
                        ["path"] = file.Path,
                        ["role"] = file.Role,
                        ["mediaType"] = file.MediaType,
                        ["size"] = file.Contents.LongLength,
                        ["integrity"] = new JsonObject
                        {
                            ["algorithm"] = "sha256",
                            ["digest"] = Convert.ToHexString(
                                    SHA256.HashData(file.Contents))
                                .ToLowerInvariant(),
                        },
                    }).ToArray()),
            };
            root["bundledPackages"] = new JsonArray
            {
                new JsonObject
                {
                    ["id"] = "com.asharia.foundation.test",
                    ["version"] = "0.1.0",
                    ["packageKind"] = "installable-capability",
                    ["availability"] = "required",
                    ["root"] = "packages/test",
                    ["manifestIntegrity"] = Integrity('1'),
                    ["payloadIntegrity"] = Integrity('2'),
                },
            };
            root["packageArtifacts"] = new JsonArray();
            root["hostProfiles"] = new JsonArray
            {
                new JsonObject
                {
                    ["hostKind"] = "editor",
                    ["targetPlatform"] =
                        "com.asharia.platform.windows",
                    ["path"] =
                        "profiles/editor/asharia.host-profile.json",
                    ["integrity"] = Integrity('3'),
                },
            };
            return root;
        }

        private static JsonObject Integrity(char digit) =>
            new()
            {
                ["algorithm"] = "sha256",
                ["digest"] = new string(digit, 64),
            };

        private static string ClassifyRole(string path)
        {
            var extension = Path.GetExtension(path);
            if (extension.Equals(".json", StringComparison.OrdinalIgnoreCase))
            {
                return "metadata";
            }

            if (extension.Equals(".pdb", StringComparison.OrdinalIgnoreCase))
            {
                return "debug-symbol";
            }

            return "runtime-library";
        }

        private static string ClassifyMediaType(string path)
        {
            var extension = Path.GetExtension(path);
            return extension.Equals(".json", StringComparison.OrdinalIgnoreCase)
                ? "application/json"
                : "application/octet-stream";
        }

        internal static byte[] RenderJson(JsonObject value)
        {
            var text = JsonSerializer.Serialize(value, JsonOptions)
                .Replace("\r\n", "\n", StringComparison.Ordinal);
            return Encoding.UTF8.GetBytes(text + "\n");
        }
    }

    private sealed record FileEvidence(
        string Path,
        string Role,
        string MediaType,
        byte[] Contents);

    private sealed class Utf8Comparer : IComparer<string>
    {
        public static Utf8Comparer Instance { get; } = new();

        public int Compare(string? left, string? right)
        {
            ArgumentNullException.ThrowIfNull(left);
            ArgumentNullException.ThrowIfNull(right);
            return Encoding.UTF8.GetBytes(left)
                .AsSpan()
                .SequenceCompareTo(Encoding.UTF8.GetBytes(right));
        }
    }
}
