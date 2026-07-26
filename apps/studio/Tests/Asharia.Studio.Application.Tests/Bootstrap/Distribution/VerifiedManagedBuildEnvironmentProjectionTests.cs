using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Asharia.Studio.Application.Bootstrap.Distribution;
using Xunit;
using DistributionFixture =
    Asharia.Studio.Application.Tests.Bootstrap.Distribution.VerifiedEditorImageInventoryTests.DistributionFixture;

namespace Asharia.Studio.Application.Tests.Bootstrap.Distribution;

public sealed class VerifiedManagedBuildEnvironmentProjectionTests
{
    private const string SdkVersion = "10.0.302";
    private const string ComponentVersion = "10.0.10";
    private const string DotnetRoot = "managed/dotnet";
    private const string RuntimeContract =
        "bin/Asharia.Runtime.Contracts.dll";
    private const string EditorContract = "bin/Asharia.Editor.dll";

    [Fact]
    public async Task LoadAsync_projects_one_exact_revocable_environment()
    {
        using var fixture = CreateFixture();
        var editorLease = await VerifyEditorImageAsync(fixture);

        var first =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);
        var second =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.True(first.Succeeded, Render(first));
        Assert.True(second.Succeeded, Render(second));
        var lease = Assert.IsType<VerifiedManagedBuildEnvironmentLease>(
            first.Lease);
        var projection = lease.Projection;
        Assert.Equal(fixture.EngineGenerationId, projection.EngineGenerationId);
        Assert.Equal(CurrentPlatform(), projection.TargetPlatform);
        Assert.Equal(CurrentArchitecture(), projection.TargetArchitecture);
        Assert.Equal("asharia-dotnet-10", projection.EnvironmentId);
        Assert.Equal("net10.0", projection.TargetFramework);
        Assert.Equal(SdkVersion, projection.SdkVersion);
        Assert.Equal(ComponentVersion, projection.HostFxrVersion);
        Assert.Equal(ComponentVersion, projection.HostRuntimeVersion);
        Assert.Equal(ComponentVersion, projection.ReferencePackVersion);
        Assert.Equal(DotnetHostPath(), projection.DotnetHost.RelativePath);
        Assert.Equal(3, projection.Sdk.Files.Count);
        Assert.Single(projection.HostFxr.Files);
        Assert.Single(projection.HostRuntime.Files);
        Assert.Single(projection.ReferencePack.Files);
        Assert.Equal(RuntimeContract, projection.RuntimeContract.RelativePath);
        Assert.Equal(EditorContract, projection.EditorContract.RelativePath);
        Assert.Equal(10, projection.SelectedFiles.Count);
        Assert.Matches("^sha256-[0-9a-f]{64}$", projection.ProjectionId);
        Assert.Equal(
            projection.ProjectionId,
            second.Lease!.Projection.ProjectionId);
        Assert.True(lease.TryGetCurrentFile(
            DotnetHostPath(),
            out var dotnetHost));
        Assert.Equal(projection.DotnetHost, dotnetHost);
        Assert.False(lease.TryGetCurrentFile(
            "bin/editor.exe",
            out var unselected));
        Assert.Null(unselected);

        lease.Revoke();

        Assert.False(lease.IsCurrent);
        Assert.True(editorLease.IsCurrent);
        Assert.False(lease.TryGetCurrentFile(
            DotnetHostPath(),
            out var revoked));
        Assert.Null(revoked);
    }

    [Theory]
    [InlineData("bom")]
    [InlineData("crlf")]
    [InlineData("unknown")]
    [InlineData("reordered")]
    [InlineData("duplicate")]
    [InlineData("target-framework")]
    [InlineData("version")]
    [InlineData("path")]
    [InlineData("alternate-dotnet-root")]
    [InlineData("tree-overlap")]
    [InlineData("contract-path")]
    public async Task LoadAsync_rejects_noncanonical_or_invalid_metadata(
        string mutation)
    {
        using var fixture = CreateFixture(MutateMetadata(mutation));
        var editorLease = await VerifyEditorImageAsync(fixture);

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.metadata-invalid");
    }

    [Fact]
    public async Task LoadAsync_rejects_metadata_drift_after_editor_verification()
    {
        using var fixture = CreateFixture();
        var editorLease = await VerifyEditorImageAsync(fixture);
        File.AppendAllText(
            Path.Combine(
                fixture.GenerationRoot,
                EngineDistributionManagedBuildEnvironmentLoader
                    .MetadataRelativePath),
            "drift");

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.metadata-drift");
    }

    [Fact]
    public async Task LoadAsync_rejects_empty_declared_component_tree()
    {
        using var fixture = CreateFixture(
            omitPath: HostFxrEntryPath());
        var editorLease = await VerifyEditorImageAsync(fixture);

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.tree-not-in-inventory"
                && diagnostic.Location == "hostFxr/root");
    }

    [Fact]
    public async Task LoadAsync_rejects_declared_file_outside_inventory()
    {
        using var fixture = CreateFixture(omitPath: RuntimeContract);
        var editorLease = await VerifyEditorImageAsync(fixture);

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.file-not-in-inventory"
                && diagnostic.Location == "contracts/runtimePath");
    }

    [Fact]
    public async Task LoadAsync_rejects_missing_semantic_file_in_nonempty_tree()
    {
        using var fixture = CreateFixture(
            omitPath: HostFxrEntryPath(),
            additionalFiles: new Dictionary<string, byte[]>
            {
                [$"{DotnetRoot}/host/fxr/{ComponentVersion}/other.bin"] =
                    Bytes("other"),
            });
        var editorLease = await VerifyEditorImageAsync(fixture);

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.semantic-path-mismatch");
    }

    [Fact]
    public async Task LoadAsync_rejects_unselected_file_under_dotnet_root()
    {
        using var fixture = CreateFixture(
            additionalFiles: new Dictionary<string, byte[]>
            {
                [$"{DotnetRoot}/sdk/9.0.100/rogue.dll"] = Bytes("rogue"),
            });
        var editorLease = await VerifyEditorImageAsync(fixture);

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.dotnet-inventory-not-closed");
    }

    [Fact]
    public async Task LoadAsync_rejects_wrong_process_context()
    {
        using var fixture = CreateFixture();
        var editorLease = await VerifyEditorImageAsync(fixture);

        var result =
            await EngineDistributionManagedBuildEnvironmentLoader.LoadAsync(
                editorLease,
                new ManagedBuildProcessContext(
                    CurrentPlatform(),
                    CurrentArchitecture() == "arm64" ? "x86_64" : "arm64"));

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.process-context-mismatch");
    }

    [Fact]
    public async Task LoadAsync_rejects_revoked_source_lease()
    {
        using var fixture = CreateFixture();
        var editorLease = await VerifyEditorImageAsync(fixture);
        editorLease.Revoke();

        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "distribution.managed-build-environment.generation-not-current");
    }

    [Fact]
    public async Task Derived_lease_tracks_source_revocation()
    {
        using var fixture = CreateFixture();
        var editorLease = await VerifyEditorImageAsync(fixture);
        var result =
            await LoadManagedBuildEnvironmentAsync(
                editorLease);
        Assert.True(result.Succeeded, Render(result));
        var derived = result.Lease!;

        editorLease.Revoke();

        Assert.False(derived.IsCurrent);
        Assert.False(derived.TryGetCurrentFile(
            DotnetHostPath(),
            out var file));
        Assert.Null(file);
    }

    [Fact]
    public async Task Projection_identity_changes_with_selected_evidence()
    {
        using var firstFixture = CreateFixture();
        using var secondFixture = CreateFixture(
            contentOverrides: new Dictionary<string, byte[]>
            {
                [ReferenceSystemRuntimePath()] =
                    Bytes("system-runtime-changed"),
            });
        var firstEditor = await VerifyEditorImageAsync(firstFixture);
        var secondEditor = await VerifyEditorImageAsync(secondFixture);

        var first =
            await LoadManagedBuildEnvironmentAsync(
                firstEditor);
        var second =
            await LoadManagedBuildEnvironmentAsync(
                secondEditor);

        Assert.True(first.Succeeded, Render(first));
        Assert.True(second.Succeeded, Render(second));
        Assert.NotEqual(
            first.Lease!.Projection.EngineGenerationId,
            second.Lease!.Projection.EngineGenerationId);
        Assert.NotEqual(
            first.Lease.Projection.ProjectionId,
            second.Lease.Projection.ProjectionId);
    }

    private static DistributionFixture CreateFixture(
        byte[]? metadata = null,
        string? omitPath = null,
        IReadOnlyDictionary<string, byte[]>? additionalFiles = null,
        IReadOnlyDictionary<string, byte[]>? contentOverrides = null)
    {
        var fixture = new DistributionFixture();
        var files = CreateInventory(metadata ?? CreateMetadata())
            .Where(item => item.Key != omitPath)
            .ToDictionary(item => item.Key, item => item.Value, StringComparer.Ordinal);
        if (contentOverrides is not null)
        {
            foreach (var (path, contents) in contentOverrides)
            {
                files[path] = contents;
            }
        }

        if (additionalFiles is not null)
        {
            foreach (var (path, contents) in additionalFiles)
            {
                files.Add(path, contents);
            }
        }

        fixture.AddInventoryFiles(files);
        fixture.RewriteManifest(
            root =>
            {
                root["context"]!["targetPlatform"] = CurrentPlatform();
                root["context"]!["toolchain"]!["targetArchitecture"] =
                    CurrentArchitecture();
            },
            recomputeIdentity: true);
        return fixture;
    }

    private static IReadOnlyDictionary<string, byte[]> CreateInventory(
        byte[] metadata) =>
        new Dictionary<string, byte[]>(StringComparer.Ordinal)
        {
            [EngineDistributionManagedBuildEnvironmentLoader
                .MetadataRelativePath] = metadata,
            [DotnetHostPath()] = Bytes("dotnet-host"),
            [$"{DotnetRoot}/sdk/{SdkVersion}/dotnet.dll"] =
                Bytes("sdk-entry"),
            [$"{DotnetRoot}/sdk/{SdkVersion}/Microsoft.NETCoreSdk.BundledVersions.props"] =
                Bytes("<Project />\n"),
            [$"{DotnetRoot}/sdk/{SdkVersion}/dotnet.runtimeconfig.json"] =
                Bytes("{}\n"),
            [HostFxrEntryPath()] = Bytes("hostfxr"),
            [$"{DotnetRoot}/shared/Microsoft.NETCore.App/{ComponentVersion}/System.Private.CoreLib.dll"] =
                Bytes("corelib"),
            [ReferenceSystemRuntimePath()] = Bytes("system-runtime"),
            [RuntimeContract] = Bytes("runtime-contract"),
            [EditorContract] = Bytes("editor-contract"),
        };

    private static async Task<VerifiedEditorImageInventoryLease>
        VerifyEditorImageAsync(DistributionFixture fixture)
    {
        var verified = await EngineDistributionEditorImageVerifier.VerifyAsync(
            fixture.EngineGenerationId,
            fixture.GenerationRoot);
        Assert.True(
            verified.Succeeded,
            string.Join(
                Environment.NewLine,
                verified.Diagnostics.Select(diagnostic =>
                    $"{diagnostic.Code}: {diagnostic.Message}")));
        return verified.Lease!;
    }

    private static Task<VerifiedManagedBuildEnvironmentLoadResult>
        LoadManagedBuildEnvironmentAsync(
            VerifiedEditorImageInventoryLease editorLease) =>
        EngineDistributionManagedBuildEnvironmentLoader.LoadAsync(
            editorLease,
            new ManagedBuildProcessContext(
                CurrentPlatform(),
                CurrentArchitecture()));

    private static byte[] CreateMetadata(
        Action<JsonObject>? rewrite = null)
    {
        var sdkRoot = $"{DotnetRoot}/sdk/{SdkVersion}";
        var hostFxrRoot =
            $"{DotnetRoot}/host/fxr/{ComponentVersion}";
        var hostRuntimeRoot =
            $"{DotnetRoot}/shared/Microsoft.NETCore.App/{ComponentVersion}";
        var referencePackRoot =
            $"{DotnetRoot}/packs/Microsoft.NETCore.App.Ref/{ComponentVersion}";
        var value = new JsonObject
        {
            ["schema"] = "com.asharia.managed-build-environment",
            ["schemaVersion"] = 1,
            ["environmentId"] = "asharia-dotnet-10",
            ["targetFramework"] = "net10.0",
            ["dotnetRoot"] = DotnetRoot,
            ["dotnetHostPath"] = DotnetHostPath(),
            ["sdk"] = new JsonObject
            {
                ["version"] = SdkVersion,
                ["root"] = sdkRoot,
                ["entryPath"] = $"{sdkRoot}/dotnet.dll",
                ["bundledVersionsPath"] =
                    $"{sdkRoot}/Microsoft.NETCoreSdk.BundledVersions.props",
                ["runtimeConfigPath"] =
                    $"{sdkRoot}/dotnet.runtimeconfig.json",
            },
            ["hostFxr"] = new JsonObject
            {
                ["version"] = ComponentVersion,
                ["root"] = hostFxrRoot,
            },
            ["hostRuntime"] = new JsonObject
            {
                ["version"] = ComponentVersion,
                ["root"] = hostRuntimeRoot,
            },
            ["referencePack"] = new JsonObject
            {
                ["name"] = "Microsoft.NETCore.App.Ref",
                ["version"] = ComponentVersion,
                ["root"] = referencePackRoot,
                ["assembliesRoot"] =
                    $"{referencePackRoot}/ref/net10.0",
            },
            ["contracts"] = new JsonObject
            {
                ["runtimePath"] = RuntimeContract,
                ["editorPath"] = EditorContract,
            },
        };
        rewrite?.Invoke(value);
        return DistributionFixture.RenderJson(value);
    }

    private static byte[] MutateMetadata(string mutation)
    {
        if (mutation == "bom")
        {
            return Encoding.UTF8.GetPreamble()
                .Concat(CreateMetadata())
                .ToArray();
        }

        if (mutation == "crlf")
        {
            return Encoding.UTF8.GetBytes(
                Encoding.UTF8.GetString(CreateMetadata())
                    .Replace("\n", "\r\n", StringComparison.Ordinal));
        }

        if (mutation == "duplicate")
        {
            var text = Encoding.UTF8.GetString(CreateMetadata());
            return Encoding.UTF8.GetBytes(
                text.Replace(
                    "  \"schemaVersion\": 1,\n",
                    "  \"schemaVersion\": 1,\n  \"schemaVersion\": 1,\n",
                    StringComparison.Ordinal));
        }

        if (mutation == "alternate-dotnet-root")
        {
            return Encoding.UTF8.GetBytes(
                Encoding.UTF8.GetString(CreateMetadata())
                    .Replace(
                        DotnetRoot,
                        "alternate/dotnet",
                        StringComparison.Ordinal));
        }

        return CreateMetadata(root =>
        {
            switch (mutation)
            {
                case "unknown":
                    root["unexpected"] = true;
                    break;
                case "reordered":
                    {
                        var schema = root["schema"]!.DeepClone();
                        Assert.True(root.Remove("schema"));
                        root["schema"] = schema;
                        break;
                    }
                case "target-framework":
                    root["targetFramework"] = "net9.0";
                    break;
                case "version":
                    root["sdk"]!["version"] = "010.0.302";
                    break;
                case "path":
                    root["dotnetRoot"] = "../managed/dotnet";
                    break;
                case "tree-overlap":
                    root["hostFxr"]!["root"] =
                        root["sdk"]!["root"]!.GetValue<string>();
                    break;
                case "contract-path":
                    root["contracts"]!["runtimePath"] =
                        "contracts/Asharia.Runtime.Contracts.dll";
                    break;
                default:
                    throw new ArgumentOutOfRangeException(
                        nameof(mutation),
                        mutation,
                        "Unknown metadata mutation.");
            }
        });
    }

    private static string DotnetHostPath() =>
        $"{DotnetRoot}/{(OperatingSystem.IsWindows() ? "dotnet.exe" : "dotnet")}";

    private static string HostFxrEntryPath() =>
        $"{DotnetRoot}/host/fxr/{ComponentVersion}/{HostFxrFileName()}";

    private static string HostFxrFileName() =>
        OperatingSystem.IsWindows()
            ? "hostfxr.dll"
            : OperatingSystem.IsMacOS()
                ? "libhostfxr.dylib"
                : "libhostfxr.so";

    private static string ReferenceSystemRuntimePath() =>
        $"{DotnetRoot}/packs/Microsoft.NETCore.App.Ref/{ComponentVersion}/ref/net10.0/System.Runtime.dll";

    private static string CurrentPlatform() =>
        OperatingSystem.IsWindows()
            ? "com.asharia.platform.windows"
            : OperatingSystem.IsLinux()
                ? "com.asharia.platform.linux"
                : OperatingSystem.IsMacOS()
                    ? "com.asharia.platform.macos"
                    : throw new PlatformNotSupportedException();

    private static string CurrentArchitecture() =>
        RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.X64 => "x86_64",
            Architecture.X86 => "x86",
            Architecture.Arm64 => "arm64",
            Architecture.Arm => "arm",
            _ => throw new PlatformNotSupportedException(),
        };

    private static byte[] Bytes(string value) =>
        Encoding.UTF8.GetBytes(value + "\n");

    private static string Render(
        VerifiedManagedBuildEnvironmentLoadResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));
}
