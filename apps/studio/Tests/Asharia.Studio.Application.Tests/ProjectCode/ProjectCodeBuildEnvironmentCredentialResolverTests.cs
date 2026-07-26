using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using System.Xml.Linq;
using Asharia.Editor.Extensions;
using Asharia.Runtime;
using Asharia.Studio.Application.Bootstrap.Distribution;
using Asharia.Studio.Application.ProjectCode;
using Xunit;
using DistributionFixture =
    Asharia.Studio.Application.Tests.Bootstrap.Distribution.VerifiedEditorImageInventoryTests.DistributionFixture;

namespace Asharia.Studio.Application.Tests.ProjectCode;

public sealed class ProjectCodeBuildEnvironmentCredentialResolverTests
{
    [Fact]
    public async Task ResolveAsync_issues_one_exact_revocable_credential()
    {
        using var fixture = new SemanticEnvironmentFixture();
        var managedLease = await fixture.LoadManagedLeaseAsync();

        var first =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);
        var second =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.True(first.Succeeded, Render(first));
        Assert.True(second.Succeeded, Render(second));
        var lease = first.Lease!;
        var credential = lease.Credential;
        Assert.Equal(
            managedLease.Projection.EngineGenerationId,
            credential.EngineGenerationId);
        Assert.Equal(
            managedLease.Projection.ProjectionId,
            credential.ProjectionId);
        Assert.Equal(fixture.SdkVersion, credential.SdkVersion);
        Assert.Equal(fixture.RuntimeVersion, credential.HostFxrVersion);
        Assert.Equal(fixture.RuntimeVersion, credential.HostRuntimeVersion);
        Assert.Equal(
            fixture.ReferencePackVersion,
            credential.ReferencePackVersion);
        Assert.Equal("dotnet", credential.SdkEntryIdentity.SimpleName);
        Assert.Equal(
            "System.Private.CoreLib",
            credential.HostRuntimeCoreIdentity.SimpleName);
        Assert.Equal(
            "Asharia.Runtime.Contracts",
            credential.RuntimeContract.Identity.SimpleName);
        Assert.Equal(
            "Asharia.Editor",
            credential.EditorContract.Identity.SimpleName);
        Assert.Contains(
            credential.FrameworkReferences,
            identity => identity.SimpleName == "System.Runtime");
        Assert.True(credential.FrameworkReferences.Count > 1);
        Assert.Matches("^sha256-[0-9a-f]{64}$", credential.CredentialId);
        Assert.Equal(
            credential.CredentialId,
            second.Lease!.Credential.CredentialId);
        Assert.True(
            await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(lease));
        Assert.True(lease.TryGetCurrentFile(
            managedLease.Projection.DotnetHost.RelativePath,
            out var dotnetHost));
        Assert.NotNull(dotnetHost);
        Assert.False(lease.TryGetCurrentFile(
            "bin/editor.exe",
            out var unselected));
        Assert.Null(unselected);

        lease.Revoke();

        Assert.False(lease.IsCurrent);
        Assert.True(managedLease.IsCurrent);
        Assert.False(
            await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(lease));
        Assert.False(lease.TryGetCurrentFile(
            managedLease.Projection.DotnetHost.RelativePath,
            out var revoked));
        Assert.Null(revoked);
    }

    [Theory]
    [InlineData(
        "props-reference-pack",
        "project-code.build-environment.sdk-metadata-invalid")]
    [InlineData(
        "props-import",
        "project-code.build-environment.sdk-metadata-invalid")]
    [InlineData(
        "props-condition",
        "project-code.build-environment.sdk-metadata-invalid")]
    [InlineData(
        "runtime-tfm",
        "project-code.build-environment.sdk-runtime-config-invalid")]
    [InlineData(
        "runtime-roll-forward",
        "project-code.build-environment.sdk-runtime-config-invalid")]
    [InlineData(
        "runtime-duplicate",
        "project-code.build-environment.sdk-runtime-config-invalid")]
    public async Task ResolveAsync_rejects_sdk_semantic_drift(
        string mutation,
        string expectedCode)
    {
        using var fixture = new SemanticEnvironmentFixture(mutation);
        var managedLease = await fixture.LoadManagedLeaseAsync();

        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == expectedCode);
    }

    [Theory]
    [InlineData(
        "sdk-entry-contract",
        "project-code.build-environment.runtime-identity-mismatch")]
    [InlineData(
        "runtime-core-contract",
        "project-code.build-environment.runtime-identity-mismatch")]
    [InlineData(
        "system-runtime-contract",
        "project-code.build-environment.framework-set-invalid")]
    [InlineData(
        "runtime-contract-editor",
        "project-code.build-environment.contract-identity-mismatch")]
    [InlineData(
        "reference-duplicate",
        "project-code.build-environment.framework-set-invalid")]
    [InlineData(
        "reference-invalid",
        "project-code.build-environment.managed-metadata-invalid")]
    [InlineData(
        "reference-path-identity",
        "project-code.build-environment.framework-set-invalid")]
    [InlineData(
        "contract-reference-unknown",
        "project-code.build-environment.contract-reference-closure-invalid")]
    public async Task ResolveAsync_rejects_managed_identity_or_set_forgery(
        string mutation,
        string expectedCode)
    {
        using var fixture = new SemanticEnvironmentFixture(mutation);
        var managedLease = await fixture.LoadManagedLeaseAsync();

        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == expectedCode);
    }

    [Theory]
    [InlineData("dotnet-host-invalid")]
    [InlineData("hostfxr-invalid")]
    public async Task ResolveAsync_rejects_non_native_host_entry(
        string mutation)
    {
        using var fixture = new SemanticEnvironmentFixture(mutation);
        var managedLease = await fixture.LoadManagedLeaseAsync();

        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build-environment.native-host-invalid");
    }

    [Fact]
    public async Task ResolveAsync_rejects_unsupported_distribution_context()
    {
        using var fixture = new SemanticEnvironmentFixture("context-linux");
        var managedLease = await fixture.LoadManagedLeaseAsync();

        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build-environment.context-unsupported");
    }

    [Fact]
    public async Task ResolveAsync_rejects_selected_file_drift()
    {
        using var fixture = new SemanticEnvironmentFixture();
        var managedLease = await fixture.LoadManagedLeaseAsync();
        File.AppendAllText(
            fixture.AbsolutePath(fixture.RuntimeContractPath),
            "drift");

        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build-environment.file-evidence-mismatch");
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task ResolveAsync_rejects_unregistered_dotnet_entry(
        bool emptyDirectory)
    {
        using var fixture = new SemanticEnvironmentFixture();
        var managedLease = await fixture.LoadManagedLeaseAsync();
        if (emptyDirectory)
        {
            Directory.CreateDirectory(
                fixture.AbsolutePath("managed/dotnet/rogue-empty"));
        }
        else
        {
            var roguePath = fixture.AbsolutePath(
                $"managed/dotnet/sdk/{fixture.SdkVersion}/rogue.targets");
            Directory.CreateDirectory(Path.GetDirectoryName(roguePath)!);
            File.WriteAllText(roguePath, "rogue");
        }

        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build-environment.dotnet-closure-invalid");
    }

    [Fact]
    public async Task Current_check_detects_byte_and_closure_drift()
    {
        using var byteFixture = new SemanticEnvironmentFixture();
        var byteManaged = await byteFixture.LoadManagedLeaseAsync();
        var byteCredential =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                byteManaged);
        Assert.True(byteCredential.Succeeded, Render(byteCredential));
        File.AppendAllText(
            byteFixture.AbsolutePath(byteFixture.EditorContractPath),
            "drift");

        Assert.False(
            await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(byteCredential.Lease!));

        using var closureFixture = new SemanticEnvironmentFixture();
        var closureManaged = await closureFixture.LoadManagedLeaseAsync();
        var closureCredential =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                closureManaged);
        Assert.True(closureCredential.Succeeded, Render(closureCredential));
        var roguePath = closureFixture.AbsolutePath(
            "managed/dotnet/host/rogue.bin");
        Directory.CreateDirectory(Path.GetDirectoryName(roguePath)!);
        File.WriteAllText(roguePath, "rogue");

        Assert.False(
            await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(
                    closureCredential.Lease!));
    }

    [Fact]
    public async Task Source_revocation_invalidates_credential()
    {
        using var fixture = new SemanticEnvironmentFixture();
        var managedLease = await fixture.LoadManagedLeaseAsync();
        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managedLease);
        Assert.True(result.Succeeded, Render(result));

        managedLease.Revoke();

        Assert.False(result.Lease!.IsCurrent);
        Assert.False(
            await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(result.Lease));
    }

    private static string Render(
        ProjectCodeBuildEnvironmentCredentialResolveResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private sealed class SemanticEnvironmentFixture : IDisposable
    {
        private static readonly Lazy<InstalledInputs> Installed = new(
            InstalledInputs.Load);
        private readonly DistributionFixture distribution_;
        private readonly string targetPlatform_;

        public SemanticEnvironmentFixture(string? mutation = null)
        {
            var installed = Installed.Value;
            SdkVersion = installed.SdkVersion;
            RuntimeVersion = installed.RuntimeVersion;
            ReferencePackVersion = installed.ReferencePackVersion;
            RuntimeContractPath = "bin/Asharia.Runtime.Contracts.dll";
            EditorContractPath = "bin/Asharia.Editor.dll";
            targetPlatform_ = mutation == "context-linux"
                ? "com.asharia.platform.linux"
                : "com.asharia.platform.windows";
            var files = installed.Files.ToDictionary(
                item => item.Key,
                item => item.Value,
                StringComparer.Ordinal);
            if (mutation is not null && mutation != "context-linux")
            {
                ApplyMutation(files, installed, mutation);
            }

            distribution_ = new DistributionFixture();
            distribution_.AddInventoryFiles(files);
            if (mutation == "context-linux")
            {
                distribution_.RewriteManifest(
                    root => root["context"]!["targetPlatform"] =
                        targetPlatform_,
                    recomputeIdentity: true);
            }
        }

        public string SdkVersion { get; }

        public string RuntimeVersion { get; }

        public string ReferencePackVersion { get; }

        public string RuntimeContractPath { get; }

        public string EditorContractPath { get; }

        public string AbsolutePath(string relativePath) =>
            Path.Combine(
                distribution_.GenerationRoot,
                relativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar));

        public async Task<VerifiedManagedBuildEnvironmentLease>
            LoadManagedLeaseAsync()
        {
            var verified =
                await EngineDistributionEditorImageVerifier.VerifyAsync(
                    distribution_.EngineGenerationId,
                    distribution_.GenerationRoot);
            Assert.True(
                verified.Succeeded,
                string.Join(
                    Environment.NewLine,
                    verified.Diagnostics.Select(diagnostic =>
                        $"{diagnostic.Code}: {diagnostic.Message}")));
            var loaded =
                await EngineDistributionManagedBuildEnvironmentLoader
                    .LoadAsync(
                        verified.Lease!,
                        new ManagedBuildProcessContext(
                            targetPlatform_,
                            "x86_64"));
            Assert.True(
                loaded.Succeeded,
                string.Join(
                    Environment.NewLine,
                    loaded.Diagnostics.Select(diagnostic =>
                        $"{diagnostic.Code}: {diagnostic.Message}")));
            return loaded.Lease!;
        }

        public void Dispose() => distribution_.Dispose();

        private static void ApplyMutation(
            IDictionary<string, byte[]> files,
            InstalledInputs installed,
            string mutation)
        {
            switch (mutation)
            {
                case "props-reference-pack":
                    MutateProps(
                        files,
                        installed,
                        document =>
                        {
                            var reference = FindNet10Reference(document);
                            reference.SetAttributeValue(
                                "TargetingPackVersion",
                                "10.0.0");
                        });
                    break;
                case "props-import":
                    MutateProps(
                        files,
                        installed,
                        document => document.Root!.Add(
                            new XElement(
                                "Import",
                                new XAttribute(
                                    "Project",
                                    "rogue.props"))));
                    break;
                case "props-condition":
                    MutateProps(
                        files,
                        installed,
                        document => FindNet10Reference(document)
                            .SetAttributeValue(
                                "Condition",
                                "'$(Rogue)' == 'true'"));
                    break;
                case "runtime-tfm":
                    MutateRuntimeConfig(
                        files,
                        installed,
                        root => root["runtimeOptions"]!["tfm"] = "net9.0");
                    break;
                case "runtime-roll-forward":
                    MutateRuntimeConfig(
                        files,
                        installed,
                        root => root["runtimeOptions"]!["rollForward"] =
                            "LatestMajor");
                    break;
                case "runtime-duplicate":
                    {
                        var text = Encoding.UTF8.GetString(
                            files[installed.SdkRuntimeConfigPath]);
                        files[installed.SdkRuntimeConfigPath] =
                            Encoding.UTF8.GetBytes(text.Replace(
                                "\"tfm\": \"net10.0\",",
                                "\"tfm\": \"net10.0\", \"tfm\": \"net10.0\",",
                                StringComparison.Ordinal));
                        break;
                    }
                case "sdk-entry-contract":
                    files[installed.SdkEntryPath] =
                        files[installed.RuntimeContractPath];
                    break;
                case "runtime-core-contract":
                    files[installed.RuntimeCorePath] =
                        files[installed.RuntimeContractPath];
                    break;
                case "system-runtime-contract":
                    files[installed.ReferenceSystemRuntimePath] =
                        files[installed.RuntimeContractPath];
                    break;
                case "runtime-contract-editor":
                    files[installed.RuntimeContractPath] =
                        files[installed.EditorContractPath];
                    break;
                case "contract-reference-unknown":
                    files[installed.RuntimeContractPath] =
                        ReplaceFirstAscii(
                            files[installed.RuntimeContractPath],
                            "System.Runtime",
                            "Unknown.Runtim");
                    break;
                case "reference-duplicate":
                    files[
                        installed.ReferenceAssemblyPrefix
                        + "System.Runtime.Copy.dll"] =
                        files[installed.ReferenceSystemRuntimePath];
                    break;
                case "reference-invalid":
                    files[
                        installed.ReferenceAssemblyPrefix
                        + "Invalid.dll"] =
                        Encoding.UTF8.GetBytes("not a managed assembly");
                    break;
                case "reference-path-identity":
                    {
                        var original =
                            installed.ReferenceAssemblyPrefix
                            + "System.Xml.dll";
                        var renamed =
                            installed.ReferenceAssemblyPrefix
                            + "Rogue.dll";
                        files[renamed] = files[original];
                        files.Remove(original);
                        break;
                    }
                case "dotnet-host-invalid":
                    files[installed.DotnetHostPath] =
                        Encoding.UTF8.GetBytes("not a native host");
                    break;
                case "hostfxr-invalid":
                    files[installed.HostFxrPath] =
                        Encoding.UTF8.GetBytes("not a native hostfxr");
                    break;
                default:
                    throw new ArgumentOutOfRangeException(
                        nameof(mutation),
                        mutation,
                        "Unknown semantic environment mutation.");
            }
        }

        private static void MutateProps(
            IDictionary<string, byte[]> files,
            InstalledInputs installed,
            Action<XDocument> mutation)
        {
            var document = XDocument.Parse(
                Encoding.UTF8.GetString(files[installed.SdkPropsPath]),
                LoadOptions.PreserveWhitespace);
            mutation(document);
            files[installed.SdkPropsPath] = Encoding.UTF8.GetBytes(
                document.ToString(SaveOptions.DisableFormatting));
        }

        private static XElement FindNet10Reference(XDocument document) =>
            document.Descendants().Single(element =>
                element.Name.LocalName == "KnownFrameworkReference"
                && (string?)element.Attribute("Include")
                    == "Microsoft.NETCore.App"
                && (string?)element.Attribute("TargetFramework")
                    == "net10.0");

        private static void MutateRuntimeConfig(
            IDictionary<string, byte[]> files,
            InstalledInputs installed,
            Action<JsonObject> mutation)
        {
            var root = JsonNode.Parse(
                files[installed.SdkRuntimeConfigPath])!.AsObject();
            mutation(root);
            files[installed.SdkRuntimeConfigPath] =
                Encoding.UTF8.GetBytes(root.ToJsonString());
        }

        private static byte[] ReplaceFirstAscii(
            byte[] contents,
            string oldValue,
            string newValue)
        {
            var oldBytes = Encoding.ASCII.GetBytes(oldValue);
            var newBytes = Encoding.ASCII.GetBytes(newValue);
            Assert.Equal(oldBytes.Length, newBytes.Length);
            var offset = contents.AsSpan().IndexOf(oldBytes);
            Assert.True(
                offset >= 0,
                $"Could not locate '{oldValue}' in managed fixture.");
            var result = contents.ToArray();
            newBytes.CopyTo(result, offset);
            return result;
        }
    }

    private sealed record InstalledInputs(
        string SdkVersion,
        string RuntimeVersion,
        string ReferencePackVersion,
        string DotnetHostPath,
        string HostFxrPath,
        string SdkEntryPath,
        string SdkPropsPath,
        string SdkRuntimeConfigPath,
        string RuntimeCorePath,
        string ReferenceAssemblyPrefix,
        string ReferenceSystemRuntimePath,
        string RuntimeContractPath,
        string EditorContractPath,
        IReadOnlyDictionary<string, byte[]> Files)
    {
        public static InstalledInputs Load()
        {
            if (!OperatingSystem.IsWindows())
            {
                throw new PlatformNotSupportedException(
                    "Current producer semantic fixture requires Windows.");
            }

            const string sdkVersion = "10.0.302";
            var runtimeCoreSource = typeof(object).Assembly.Location;
            var runtimeRoot = Path.GetDirectoryName(runtimeCoreSource)!;
            var runtimeVersion = Path.GetFileName(runtimeRoot);
            var dotnetRoot = Directory.GetParent(
                Directory.GetParent(
                    Directory.GetParent(runtimeRoot)!.FullName)!.FullName)!
                .FullName;
            var sdkSourceRoot = Path.Combine(
                dotnetRoot,
                "sdk",
                sdkVersion);
            var referencePackSourceRoot = Path.Combine(
                dotnetRoot,
                "packs",
                "Microsoft.NETCore.App.Ref",
                runtimeVersion);
            var referenceSourceRoot = Path.Combine(
                referencePackSourceRoot,
                "ref",
                "net10.0");
            if (!Directory.Exists(sdkSourceRoot)
                || !Directory.Exists(referenceSourceRoot))
            {
                throw new DirectoryNotFoundException(
                    "Pinned .NET 10 SDK/reference pack fixture is unavailable.");
            }

            const string dotnetHostPath = "managed/dotnet/dotnet.exe";
            var sdkRelativeRoot = $"managed/dotnet/sdk/{sdkVersion}";
            var sdkEntryPath = sdkRelativeRoot + "/dotnet.dll";
            var sdkPropsPath = sdkRelativeRoot
                + "/Microsoft.NETCoreSdk.BundledVersions.props";
            var sdkRuntimeConfigPath =
                sdkRelativeRoot + "/dotnet.runtimeconfig.json";
            var hostFxrPath =
                $"managed/dotnet/host/fxr/{runtimeVersion}/hostfxr.dll";
            var runtimeCorePath =
                "managed/dotnet/shared/Microsoft.NETCore.App/"
                + runtimeVersion
                + "/System.Private.CoreLib.dll";
            var referencePackRelativeRoot =
                "managed/dotnet/packs/Microsoft.NETCore.App.Ref/"
                + runtimeVersion;
            var referenceAssemblyPrefix =
                referencePackRelativeRoot + "/ref/net10.0/";
            var referenceSystemRuntimePath =
                referenceAssemblyPrefix + "System.Runtime.dll";
            const string runtimeContractPath =
                "bin/Asharia.Runtime.Contracts.dll";
            const string editorContractPath = "bin/Asharia.Editor.dll";
            var files = new Dictionary<string, byte[]>(
                StringComparer.Ordinal)
            {
                [dotnetHostPath] = File.ReadAllBytes(
                    Path.Combine(dotnetRoot, "dotnet.exe")),
                [sdkEntryPath] = File.ReadAllBytes(
                    Path.Combine(sdkSourceRoot, "dotnet.dll")),
                [sdkPropsPath] = File.ReadAllBytes(
                    Path.Combine(
                        sdkSourceRoot,
                        "Microsoft.NETCoreSdk.BundledVersions.props")),
                [sdkRuntimeConfigPath] = File.ReadAllBytes(
                    Path.Combine(
                        sdkSourceRoot,
                        "dotnet.runtimeconfig.json")),
                [hostFxrPath] = File.ReadAllBytes(Path.Combine(
                    dotnetRoot,
                    "host",
                    "fxr",
                    runtimeVersion,
                    "hostfxr.dll")),
                [runtimeCorePath] = File.ReadAllBytes(runtimeCoreSource),
                [runtimeContractPath] = File.ReadAllBytes(
                    typeof(EntityId).Assembly.Location),
                [editorContractPath] = File.ReadAllBytes(
                    typeof(EditorModule).Assembly.Location),
            };
            foreach (var source in Directory.EnumerateFiles(
                referenceSourceRoot,
                "*.dll",
                SearchOption.TopDirectoryOnly))
            {
                files.Add(
                    referenceAssemblyPrefix + Path.GetFileName(source),
                    File.ReadAllBytes(source));
            }

            var metadata = CreateMetadata(
                sdkVersion,
                runtimeVersion,
                runtimeVersion,
                dotnetHostPath,
                sdkRelativeRoot,
                referencePackRelativeRoot,
                runtimeContractPath,
                editorContractPath);
            files.Add(
                EngineDistributionManagedBuildEnvironmentLoader
                    .MetadataRelativePath,
                metadata);
            return new InstalledInputs(
                sdkVersion,
                runtimeVersion,
                runtimeVersion,
                dotnetHostPath,
                hostFxrPath,
                sdkEntryPath,
                sdkPropsPath,
                sdkRuntimeConfigPath,
                runtimeCorePath,
                referenceAssemblyPrefix,
                referenceSystemRuntimePath,
                runtimeContractPath,
                editorContractPath,
                files);
        }

        private static byte[] CreateMetadata(
            string sdkVersion,
            string runtimeVersion,
            string referencePackVersion,
            string dotnetHostPath,
            string sdkRoot,
            string referencePackRoot,
            string runtimeContractPath,
            string editorContractPath)
        {
            var hostFxrRoot =
                $"managed/dotnet/host/fxr/{runtimeVersion}";
            var hostRuntimeRoot =
                "managed/dotnet/shared/Microsoft.NETCore.App/"
                + runtimeVersion;
            var value = new JsonObject
            {
                ["schema"] =
                    "com.asharia.managed-build-environment",
                ["schemaVersion"] = 1,
                ["environmentId"] = "asharia-dotnet-10",
                ["targetFramework"] = "net10.0",
                ["dotnetRoot"] = "managed/dotnet",
                ["dotnetHostPath"] = dotnetHostPath,
                ["sdk"] = new JsonObject
                {
                    ["version"] = sdkVersion,
                    ["root"] = sdkRoot,
                    ["entryPath"] = sdkRoot + "/dotnet.dll",
                    ["bundledVersionsPath"] = sdkRoot
                        + "/Microsoft.NETCoreSdk.BundledVersions.props",
                    ["runtimeConfigPath"] =
                        sdkRoot + "/dotnet.runtimeconfig.json",
                },
                ["hostFxr"] = new JsonObject
                {
                    ["version"] = runtimeVersion,
                    ["root"] = hostFxrRoot,
                },
                ["hostRuntime"] = new JsonObject
                {
                    ["version"] = runtimeVersion,
                    ["root"] = hostRuntimeRoot,
                },
                ["referencePack"] = new JsonObject
                {
                    ["name"] = "Microsoft.NETCore.App.Ref",
                    ["version"] = referencePackVersion,
                    ["root"] = referencePackRoot,
                    ["assembliesRoot"] =
                        referencePackRoot + "/ref/net10.0",
                },
                ["contracts"] = new JsonObject
                {
                    ["runtimePath"] = runtimeContractPath,
                    ["editorPath"] = editorContractPath,
                },
            };
            return DistributionFixture.RenderJson(value);
        }
    }
}
