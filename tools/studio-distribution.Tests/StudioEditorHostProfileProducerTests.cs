using System.Security.Cryptography;
using System.Text.Json;
using Xunit;

namespace Asharia.Studio.Distribution.Tests;

public sealed class StudioEditorHostProfileProducerTests
{
    private const string ExpectedSha256 =
        "f7b3431d51ceb663123444c95c43fe7f095c413875effa1385c73c39c21d9d6d";

    [Fact]
    public void Produces_the_canonical_production_editor_profile_as_one_closed_input()
    {
        using var temporary = new TemporaryDirectory();
        var outputRoot = Path.Combine(temporary.Path, "editor-host-profile");

        var result = StudioEditorHostProfileProducer.Produce(
            new StudioEditorHostProfileProductionRequest(
                new DirectoryInfo(outputRoot)));

        Assert.True(
            result.Succeeded,
            string.Join(Environment.NewLine, result.Diagnostics.Select(Format)));
        var receipt = Assert.IsType<StudioEditorHostProfileProductionReceipt>(
            result.Receipt);
        Assert.Equal(Path.GetFullPath(outputRoot), receipt.Root);
        Assert.Equal(
            "profiles/editor/asharia.host-profile.json",
            receipt.Profile.Path);
        Assert.Equal("editor", receipt.Profile.HostKind);
        Assert.Equal(
            "com.asharia.platform.windows",
            receipt.Profile.TargetPlatform);
        Assert.Equal(ExpectedSha256, receipt.Profile.Sha256);

        var profilePath = Path.Combine(
            outputRoot,
            receipt.Profile.Path.Replace('/', Path.DirectorySeparatorChar));
        var actual = File.ReadAllBytes(profilePath);
        var repositoryRoot = FindRepositoryRoot();
        var productionSource = File.ReadAllBytes(Path.Combine(
            repositoryRoot,
            "apps",
            "studio",
            "Distribution",
            "profiles",
            "editor",
            "asharia.host-profile.json"));
        var contractOracle = File.ReadAllBytes(Path.Combine(
            repositoryRoot,
            "tools",
            "tests",
            "fixtures",
            "package-contracts",
            "valid-host-profile-editor.json"));
        Assert.Equal(productionSource, actual);
        Assert.Equal(contractOracle, actual);
        Assert.Equal(actual.LongLength, receipt.Profile.Size);
        Assert.Equal(ExpectedSha256, Hash(actual));
        Assert.False(actual.AsSpan().StartsWith(new byte[] { 0xef, 0xbb, 0xbf }));
        Assert.DoesNotContain((byte)'\r', actual);
        Assert.Equal((byte)'\n', actual[^1]);

        using var document = JsonDocument.Parse(actual);
        Assert.Equal(
            "com.asharia.host-profile",
            document.RootElement.GetProperty("schema").GetString());
        Assert.Equal(1, document.RootElement.GetProperty("schemaVersion").GetInt32());
        Assert.Equal(
            ["profiles", "profiles/editor"],
            Directory
                .EnumerateDirectories(outputRoot, "*", SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(outputRoot, path)
                    .Replace(Path.DirectorySeparatorChar, '/'))
                .Order(StringComparer.Ordinal));
        Assert.Equal(
            [receipt.Profile.Path],
            Directory
                .EnumerateFiles(outputRoot, "*", SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(outputRoot, path)
                    .Replace(Path.DirectorySeparatorChar, '/')));
    }

    [Fact]
    public void Existing_output_is_not_overwritten()
    {
        using var temporary = new TemporaryDirectory();
        var outputRoot = Path.Combine(temporary.Path, "existing");
        Directory.CreateDirectory(outputRoot);
        var sentinel = Path.Combine(outputRoot, "sentinel.txt");
        File.WriteAllText(sentinel, "owned");

        var result = StudioEditorHostProfileProducer.Produce(
            new StudioEditorHostProfileProductionRequest(
                new DirectoryInfo(outputRoot)));

        Assert.False(result.Succeeded);
        Assert.Null(result.Receipt);
        Assert.Equal("owned", File.ReadAllText(sentinel));
        Assert.Single(result.Diagnostics);
    }

    [Fact]
    public void Extended_output_path_is_published_as_a_standard_receipt_path()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var temporary = new TemporaryDirectory();
        var outputRoot = Path.Combine(temporary.Path, "editor-host-profile-extended");

        var result = StudioEditorHostProfileProducer.Produce(
            new StudioEditorHostProfileProductionRequest(
                new DirectoryInfo(@"\\?\" + Path.GetFullPath(outputRoot))));

        Assert.True(
            result.Succeeded,
            string.Join(Environment.NewLine, result.Diagnostics.Select(Format)));
        var receipt = Assert.IsType<StudioEditorHostProfileProductionReceipt>(
            result.Receipt);
        Assert.Equal(Path.GetFullPath(outputRoot), receipt.Root);
        Assert.DoesNotContain(@"\\?\", receipt.Root, StringComparison.Ordinal);
        Assert.True(File.Exists(Path.Combine(
            outputRoot,
            "profiles",
            "editor",
            "asharia.host-profile.json")));
    }

    [Fact]
    public void Missing_output_parent_fails_without_creating_a_partial_root()
    {
        using var temporary = new TemporaryDirectory();
        var missingParent = Path.Combine(temporary.Path, "missing");
        var outputRoot = Path.Combine(missingParent, "profile");

        var result = StudioEditorHostProfileProducer.Produce(
            new StudioEditorHostProfileProductionRequest(
                new DirectoryInfo(outputRoot)));

        Assert.False(result.Succeeded);
        Assert.Null(result.Receipt);
        Assert.False(Directory.Exists(missingParent));
    }

    private static string Hash(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    private static string Format(StudioEditorHostProfileProductionDiagnostic value) =>
        $"{value.Code} {value.Location}: {value.Message}";

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt"))
                && File.Exists(Path.Combine(
                    current.FullName,
                    "schemas",
                    "package-runtime",
                    "host-profile-v1.schema.json")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not locate the Asharia repository root.");
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(
                System.IO.Path.GetTempPath(),
                $"asharia-editor-host-profile-test-{Guid.NewGuid():N}");
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose()
        {
            if (Directory.Exists(Path))
            {
                Directory.Delete(Path, recursive: true);
            }
        }
    }
}
