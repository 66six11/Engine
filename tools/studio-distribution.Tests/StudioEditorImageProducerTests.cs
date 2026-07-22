using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Xml.Linq;
using Asharia.Editor.Selection;
using Asharia.Runtime;
using Asharia.Studio.Distribution;
using Xunit;

namespace Asharia.Studio.Distribution.Tests;

[Collection(StudioEditorImageInputCollection.Name)]
public sealed class StudioEditorImageProducerTests
{
    private const string EditorFileName = "Editor.exe";
    private const string DotnetHostName = "dotnet.exe";
    private const string EditorNativeFileName = "editor_native.dll";
    private const string SlangFileName = "slang.dll";
    private readonly StudioEditorImageTestInputs inputs_;

    public StudioEditorImageProducerTests(StudioEditorImageTestInputs inputs)
    {
        inputs_ = inputs;
    }

    [Fact]
    public void Produce_stages_one_closed_editor_image_and_canonical_metadata()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.True(result.Succeeded, Render(result));
        var receipt = Assert.IsType<StudioEditorImageProductionReceipt>(result.Receipt);
        Assert.Equal(Path.GetFullPath(fixture.OutputRoot), receipt.Root);
        Assert.Equal("bin/" + EditorFileName, receipt.EntryPoint);
        Assert.Equal(
            receipt.Files.OrderBy(file => file.Path, StringComparer.Ordinal),
            receipt.Files);
        Assert.Equal(
            Directory.EnumerateFiles(fixture.OutputRoot, "*", SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(fixture.OutputRoot, path).Replace('\\', '/'))
                .OrderBy(path => path, StringComparer.Ordinal),
            receipt.Files.Select(file => file.Path).OrderBy(path => path, StringComparer.Ordinal));
        Assert.DoesNotContain(
            receipt.Files,
            file => file.Path.Contains("9.9.999", StringComparison.Ordinal));
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/" + EditorFileName && file.Role == "executable");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "managed/dotnet/" + DotnetHostName
                && file.Role == "executable");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "metadata/managed-build-environment.json"
                && file.Role == "metadata"
                && file.MediaType == "application/json");
        Assert.All(receipt.Files, file =>
        {
            Assert.True(file.Size >= 0);
            Assert.Matches("^[0-9a-f]{64}$", file.Sha256);
        });

        var metadataPath = Path.Combine(
            fixture.OutputRoot,
            "metadata",
            "managed-build-environment.json");
        var bytes = File.ReadAllBytes(metadataPath);
        Assert.False(bytes.AsSpan().StartsWith(new byte[] { 0xef, 0xbb, 0xbf }));
        Assert.Equal((byte)'\n', bytes[^1]);
        Assert.DoesNotContain((byte)'\r', bytes);
        using var document = JsonDocument.Parse(bytes);
        var root = document.RootElement;
        Assert.Equal("com.asharia.managed-build-environment", root.GetProperty("schema").GetString());
        Assert.Equal(1, root.GetProperty("schemaVersion").GetInt32());
        Assert.Equal("project-code-net10", root.GetProperty("environmentId").GetString());
        Assert.Equal("net10.0", root.GetProperty("targetFramework").GetString());
        Assert.Equal("managed/dotnet", root.GetProperty("dotnetRoot").GetString());
        Assert.Equal(
            $"managed/dotnet/sdk/{inputs_.SdkVersion}/dotnet.dll",
            root.GetProperty("sdk").GetProperty("entryPath").GetString());
        Assert.Equal(
            "bin/Asharia.Runtime.Contracts.dll",
            root.GetProperty("contracts").GetProperty("runtimePath").GetString());
        Assert.Equal(
            "bin/Asharia.Editor.dll",
            root.GetProperty("contracts").GetProperty("editorPath").GetString());
    }

    [Fact]
    public void Produce_accepts_a_long_extended_output_path_without_leaking_device_syntax()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var longParent = Path.Combine(
            fixture.Root,
            new string('a', 80),
            new string('b', 80));
        Directory.CreateDirectory(longParent);
        var outputRoot = Path.Combine(
            longParent,
            "editor-image-" + new string('x', 220));
        var stagingRoot = Path.Combine(
            Path.GetDirectoryName(outputRoot)!,
            $".asharia-editor-image-staging-{new string('0', 32)}");
        var stagedVersionPath = Path.Combine(
            stagingRoot,
            "managed",
            "dotnet",
            "packs",
            "Microsoft.NETCore.App.Ref",
            inputs_.ReferencePackVersion,
            "ref",
            "net10.0",
            "System.Runtime.dll");
        Assert.True(stagedVersionPath.Length > 260);

        var request = fixture.Request with
        {
            OutputRoot = new DirectoryInfo(@"\\?\" + Path.GetFullPath(outputRoot)),
        };
        var result = StudioEditorImageProducer.Produce(request);

        Assert.True(result.Succeeded, Render(result));
        var receipt = Assert.IsType<StudioEditorImageProductionReceipt>(result.Receipt);
        Assert.Equal(Path.GetFullPath(outputRoot), receipt.Root);
        Assert.DoesNotContain(@"\\?\", receipt.Root, StringComparison.Ordinal);
        Assert.True(File.Exists(Path.Combine(
            outputRoot,
            "managed",
            "dotnet",
            "packs",
            "Microsoft.NETCore.App.Ref",
            inputs_.ReferencePackVersion,
            "ref",
            "net10.0",
            "System.Runtime.dll")));
    }

    [Fact]
    public void Produce_rejects_an_unrepresentable_device_output_path_as_a_diagnostic()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var result = StudioEditorImageProducer.Produce(
            fixture.Request with
            {
                OutputRoot = new DirectoryInfo(
                    @"\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopy1\editor-image"),
            });

        Assert.False(result.Succeeded);
        Assert.Null(result.Receipt);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.path-invalid"
                && diagnostic.Location == "outputRoot");
    }

    [Fact]
    public void Produce_never_overwrites_an_existing_output_root()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_, createOutput: true);
        var sentinel = Path.Combine(fixture.OutputRoot, "sentinel.txt");
        File.WriteAllText(sentinel, "preserve", Encoding.UTF8);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == "studio-distribution.editor-image.output-exists");
        Assert.Equal("preserve", File.ReadAllText(sentinel, Encoding.UTF8));
        Assert.Single(Directory.EnumerateFiles(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_contracts_outside_the_publish_root()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var externalContract = Path.Combine(fixture.Root, "Asharia.Editor.dll");
        File.Copy(typeof(IEditorSelectionService).Assembly.Location, externalContract);
        var request = fixture.Request with
        {
            EditorContract = new FileInfo(externalContract),
        };

        var result = StudioEditorImageProducer.Produce(request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "studio-distribution.editor-image.path-outside-root"
                && diagnostic.Location == "editorContract");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_stale_contract_duplicate_inside_publish()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var duplicateRoot = Path.Combine(fixture.PublishRoot, "stale");
        Directory.CreateDirectory(duplicateRoot);
        var duplicate = Path.Combine(duplicateRoot, "Asharia.Editor.dll");
        File.Copy(typeof(IEditorSelectionService).Assembly.Location, duplicate);
        var request = fixture.Request with
        {
            EditorContract = new FileInfo(duplicate),
        };

        var result = StudioEditorImageProducer.Produce(request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "studio-distribution.editor-image.contract-location-invalid");
    }

    [Fact]
    public void Produce_rejects_overlapping_publish_and_dotnet_roots()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var request = fixture.Request with
        {
            DotnetRoot = new DirectoryInfo(fixture.PublishRoot),
        };

        var result = StudioEditorImageProducer.Produce(request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "studio-distribution.editor-image.input-root-overlap");
    }

    [Theory]
    [MemberData(
        nameof(PythonProductPayloadFixture.ForbiddenPaths),
        MemberType = typeof(PythonProductPayloadFixture))]
    public void Produce_rejects_python_payload_from_the_publish_tree(string relativePath)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var path = Path.Combine(
            fixture.PublishRoot,
            relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "repository-only Python payload", Encoding.UTF8);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        var diagnostic = Assert.Single(
            result.Diagnostics,
            value => value.Code
                == "studio-distribution.editor-image.python-payload-forbidden");
        Assert.Equal($"bin/{relativePath}", diagnostic.Location);
        Assert.DoesNotContain(fixture.Root, diagnostic.Message, StringComparison.OrdinalIgnoreCase);
        Assert.False(Directory.Exists(fixture.OutputRoot));
        Assert.Empty(
            Directory.EnumerateDirectories(
                fixture.Root,
                ".asharia-editor-image-staging-*",
                SearchOption.TopDirectoryOnly));
    }

    [Fact]
    public void Produce_accepts_shared_non_python_product_controls()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        foreach (var relativePath in PythonProductPayloadFixture.AllowedPaths)
        {
            var path = Path.Combine(
                fixture.PublishRoot,
                relativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "non-Python product control", Encoding.UTF8);
        }

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.True(result.Succeeded, Render(result));
        var receipt = Assert.IsType<StudioEditorImageProductionReceipt>(result.Receipt);
        Assert.All(
            PythonProductPayloadFixture.AllowedPaths,
            relativePath => Assert.Contains(
                receipt.Files,
                file => file.Path == $"bin/{relativePath}"));
    }

    [Theory]
    [InlineData("tools/helper.whl")]
    [InlineData("runtime/PYTHON314T.DLL")]
    [InlineData("runtime/Python.Runtime.dll")]
    public void Produce_rejects_python_payload_from_a_selected_dotnet_tree(
        string selectedRelativePath)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var relativePath = $"sdk/{inputs_.SdkVersion}/{selectedRelativePath}";
        var path = Path.Combine(
            fixture.DotnetRoot,
            relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "repository-only Python payload", Encoding.UTF8);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "studio-distribution.editor-image.python-payload-forbidden"
                && diagnostic.Location
                    == $"managed/dotnet/sdk/{inputs_.SdkVersion}/{selectedRelativePath}");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_noncanonical_component_versions()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var request = fixture.Request with
        {
            SdkVersion = $"0{inputs_.SdkVersion}",
        };

        var result = StudioEditorImageProducer.Produce(request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "studio-distribution.editor-image.version-invalid");
    }

    [Theory]
    [InlineData("Straße.exe")]
    [InlineData("CON.txt")]
    [InlineData("folder/name. ")]
    public void Produce_rejects_paths_outside_the_windows_v1_portable_subset(
        string entryPoint)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var result = StudioEditorImageProducer.Produce(
            fixture.Request with { EntryPoint = entryPoint });

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "studio-distribution.editor-image.path-invalid");
    }

    [Fact]
    public void Produce_requires_the_fixed_native_runtime_dependencies()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        File.Delete(Path.Combine(fixture.PublishRoot, SlangFileName));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == "studio-distribution.editor-image.file-invalid"
                && diagnostic.Location == $"publishRoot/{SlangFileName}");
    }

    [Fact]
    public void Produce_rejects_a_managed_assembly_masquerading_as_native_runtime()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        File.Copy(
            typeof(IEditorSelectionService).Assembly.Location,
            Path.Combine(fixture.PublishRoot, SlangFileName),
            overwrite: true);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == "studio-distribution.editor-image.pe-invalid"
                && diagnostic.Location == $"publishRoot/{SlangFileName}");
    }

    [Fact]
    public void Produce_rejects_an_executable_that_is_not_the_bound_editor_apphost()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        File.Copy(
            Path.Combine(fixture.DotnetRoot, DotnetHostName),
            Path.Combine(fixture.PublishRoot, EditorFileName),
            overwrite: true);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.apphost-identity-invalid"
                && diagnostic.Location == "entryPoint");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_console_subsystem_apphost()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var appHost = Path.Combine(fixture.PublishRoot, EditorFileName);
        var contents = File.ReadAllBytes(appHost);
        var peOffset = BinaryPrimitives.ReadInt32LittleEndian(contents.AsSpan(0x3c, 4));
        var subsystemOffset = checked(peOffset + 4 + 20 + 68);
        BinaryPrimitives.WriteUInt16LittleEndian(
            contents.AsSpan(subsystemOffset, 2),
            3);
        File.WriteAllBytes(appHost, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.apphost-identity-invalid"
                && diagnostic.Location == "entryPoint");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_an_apphost_with_a_mutated_import_data_directory()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var appHost = Path.Combine(fixture.PublishRoot, EditorFileName);
        var contents = File.ReadAllBytes(appHost);
        var peOffset = BinaryPrimitives.ReadInt32LittleEndian(contents.AsSpan(0x3c, 4));
        var optionalHeaderOffset = checked(peOffset + 4 + 20);
        var importDirectorySizeOffset = checked(optionalHeaderOffset + 112 + 8 + 4);
        contents[importDirectorySizeOffset] ^= 1;
        File.WriteAllBytes(appHost, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.apphost-identity-invalid"
                && diagnostic.Location == "entryPoint");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_an_apphost_with_a_mutated_editor_resource_payload()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var appHost = Path.Combine(fixture.PublishRoot, EditorFileName);
        var contents = File.ReadAllBytes(appHost);
        var marker = "requestedExecutionLevel"u8;
        var markerOffset = contents.AsSpan().IndexOf(marker);
        Assert.True(markerOffset >= 0);
        Assert.Equal(-1, contents.AsSpan(markerOffset + 1).IndexOf(marker));
        contents[markerOffset] ^= 1;
        File.WriteAllBytes(appHost, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.apphost-identity-invalid"
                && diagnostic.Location == "entryPoint");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_accepts_a_larger_aligned_editor_resource_raw_envelope()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var editorAssembly = Path.Combine(fixture.PublishRoot, "Editor.dll");
        var contents = File.ReadAllBytes(editorAssembly);
        var peOffset = BinaryPrimitives.ReadInt32LittleEndian(contents.AsSpan(0x3c, 4));
        var coffHeaderOffset = checked(peOffset + 4);
        var optionalHeaderOffset = checked(coffHeaderOffset + 20);
        var optionalHeaderSize = BinaryPrimitives.ReadUInt16LittleEndian(
            contents.AsSpan(coffHeaderOffset + 16, 2));
        var sectionCount = BinaryPrimitives.ReadUInt16LittleEndian(
            contents.AsSpan(coffHeaderOffset + 2, 2));
        var fileAlignment = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(optionalHeaderOffset + 36, 4));
        var sectionHeadersOffset = checked(optionalHeaderOffset + optionalHeaderSize);
        var resourceHeaderOffset = Enumerable.Range(0, sectionCount)
            .Select(index => checked(sectionHeadersOffset + index * 40))
            .Single(offset => contents.AsSpan(offset, 8).SequenceEqual(".rsrc\0\0\0"u8));
        var resourceRawSize = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 16, 4));
        var resourceRawPointer = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 20, 4));
        Assert.Equal(contents.Length, checked(resourceRawPointer + resourceRawSize));
        Array.Resize(ref contents, checked(contents.Length + fileAlignment));
        BinaryPrimitives.WriteInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 16, 4),
            checked(resourceRawSize + fileAlignment));
        File.WriteAllBytes(editorAssembly, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.True(result.Succeeded, Render(result));
        Assert.NotNull(result.Receipt);
    }

    [Fact]
    public void Produce_rejects_an_apphost_with_a_bundle_or_overlay_payload()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        using (var stream = new FileStream(
                   Path.Combine(fixture.PublishRoot, EditorFileName),
                   FileMode.Append,
                   FileAccess.Write,
                   FileShare.None))
        {
            stream.WriteByte(0x42);
        }

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.apphost-identity-invalid"
                && diagnostic.Location == "entryPoint");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_an_overlay_disguised_as_an_extended_resource_section()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var appHost = Path.Combine(fixture.PublishRoot, EditorFileName);
        var contents = File.ReadAllBytes(appHost);
        var peOffset = BinaryPrimitives.ReadInt32LittleEndian(contents.AsSpan(0x3c, 4));
        var coffHeaderOffset = checked(peOffset + 4);
        var optionalHeaderOffset = checked(coffHeaderOffset + 20);
        var optionalHeaderSize = BinaryPrimitives.ReadUInt16LittleEndian(
            contents.AsSpan(coffHeaderOffset + 16, 2));
        var sectionCount = BinaryPrimitives.ReadUInt16LittleEndian(
            contents.AsSpan(coffHeaderOffset + 2, 2));
        var resourceHeaderOffset = checked(
            optionalHeaderOffset + optionalHeaderSize + (sectionCount - 1) * 40);
        var fileAlignment = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(optionalHeaderOffset + 36, 4));
        var sectionAlignment = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(optionalHeaderOffset + 32, 4));
        var resourceVirtualAddress = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 12, 4));
        var resourceVirtualSize = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 8, 4));
        var resourceRawSize = BinaryPrimitives.ReadInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 16, 4));
        var extendedVirtualSize = checked(resourceVirtualSize + fileAlignment);
        var extendedRawSize = checked(resourceRawSize + fileAlignment);
        var extendedImageSize = AlignUp(
            checked(resourceVirtualAddress + extendedVirtualSize),
            sectionAlignment);

        Array.Resize(ref contents, checked(contents.Length + fileAlignment));
        BinaryPrimitives.WriteInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 8, 4),
            extendedVirtualSize);
        BinaryPrimitives.WriteInt32LittleEndian(
            contents.AsSpan(resourceHeaderOffset + 16, 4),
            extendedRawSize);
        BinaryPrimitives.WriteInt32LittleEndian(
            contents.AsSpan(optionalHeaderOffset + 8, 4),
            checked(BinaryPrimitives.ReadInt32LittleEndian(
                contents.AsSpan(optionalHeaderOffset + 8, 4)) + fileAlignment));
        BinaryPrimitives.WriteInt32LittleEndian(
            contents.AsSpan(optionalHeaderOffset + 56, 4),
            extendedImageSize);
        BinaryPrimitives.WriteInt32LittleEndian(
            contents.AsSpan(optionalHeaderOffset + 112 + 2 * 8 + 4, 4),
            extendedVirtualSize);
        File.WriteAllBytes(appHost, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.apphost-identity-invalid"
                && diagnostic.Location == "entryPoint");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_the_wrong_managed_editor_entry_identity()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        File.Copy(
            Path.Combine(fixture.PublishRoot, "Asharia.Editor.dll"),
            Path.Combine(fixture.PublishRoot, "Editor.dll"),
            overwrite: true);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.managed-identity-invalid"
                && diagnostic.Location == "publishRoot/Editor.dll");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_native_dependency_missing_one_required_export()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, EditorNativeFileName),
            EditorNativeFileName,
            StudioEditorImageTestInputs.EditorNativeRequiredExports[..^1]);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location == $"publishRoot/{EditorNativeFileName}");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_an_sdk_tree_whose_managed_identity_has_a_different_version()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var parsed = Version.Parse(inputs_.SdkVersion);
        var mismatchedVersion = $"{parsed.Major}.{parsed.Minor}.{parsed.Build + 1}";
        Directory.Move(
            Path.Combine(fixture.DotnetRoot, "sdk", inputs_.SdkVersion),
            Path.Combine(fixture.DotnetRoot, "sdk", mismatchedVersion));

        var result = StudioEditorImageProducer.Produce(
            fixture.Request with { SdkVersion = mismatchedVersion });

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.managed-identity-invalid"
                && diagnostic.Location == "sdkRoot/dotnet.dll");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_host_component_tree_labeled_with_the_wrong_exact_version()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var parsed = Version.Parse(inputs_.HostFxrVersion);
        var mismatchedVersion = $"{parsed.Major}.{parsed.Minor}.{parsed.Build + 1}";
        Directory.Move(
            Path.Combine(fixture.DotnetRoot, "host", "fxr", inputs_.HostFxrVersion),
            Path.Combine(fixture.DotnetRoot, "host", "fxr", mismatchedVersion));

        var result = StudioEditorImageProducer.Produce(
            fixture.Request with
            {
                HostFxrVersion = mismatchedVersion,
                OutputRoot = new DirectoryInfo(
                    @"\\?\" + Path.GetFullPath(fixture.OutputRoot)),
            });

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.component-version-invalid"
                && diagnostic.Location == "dotnetRoot/dotnetHost");
        Assert.False(Directory.Exists(fixture.OutputRoot));
        Assert.DoesNotContain(
            Directory.EnumerateDirectories(fixture.Root),
            path => Path.GetFileName(path).StartsWith(
                ".asharia-editor-image-staging-",
                StringComparison.Ordinal));
    }

    [Fact]
    public void Produce_rejects_a_component_with_corrupt_fixed_version_structure_evidence()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var dotnetHost = Path.Combine(fixture.DotnetRoot, DotnetHostName);
        var contents = File.ReadAllBytes(dotnetHost);
        ReadOnlySpan<byte> fixedVersionHeader =
        [
            0xbd, 0x04, 0xef, 0xfe,
            0x00, 0x00, 0x01, 0x00,
        ];
        var markerOffset = contents.AsSpan().IndexOf(fixedVersionHeader);
        Assert.True(markerOffset >= 0);
        Assert.Equal(
            -1,
            contents.AsSpan(markerOffset + 1).IndexOf(fixedVersionHeader));
        contents[markerOffset + sizeof(uint)] ^= 1;
        File.WriteAllBytes(dotnetHost, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.component-version-invalid"
                && diagnostic.Location == "dotnetRoot/dotnetHost");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_editor_runtime_evidence_for_another_target_framework()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var runtimeConfig = Path.Combine(fixture.PublishRoot, "Editor.runtimeconfig.json");
        var document = JsonNode.Parse(File.ReadAllText(runtimeConfig))!;
        document["runtimeOptions"]!["tfm"] = "net9.0";
        File.WriteAllText(
            runtimeConfig,
            document.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.managed-runtime-evidence-invalid"
                && diagnostic.Location == "publishRoot");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_editor_deps_with_a_mismatched_contract_version()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var depsPath = Path.Combine(fixture.PublishRoot, "Editor.deps.json");
        var document = JsonNode.Parse(File.ReadAllText(depsPath))!;
        var runtimeTarget = document["runtimeTarget"]!["name"]!.GetValue<string>();
        document["targets"]![runtimeTarget]!["Editor/1.0.0"]!["dependencies"]!["Asharia.Editor"] = "9.9.9";
        File.WriteAllText(
            depsPath,
            document.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.managed-runtime-evidence-invalid"
                && diagnostic.Location == "publishRoot");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_sdk_runtime_evidence_for_another_runtime_version()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var runtimeConfig = Path.Combine(
            fixture.DotnetRoot,
            "sdk",
            inputs_.SdkVersion,
            "dotnet.runtimeconfig.json");
        var document = JsonNode.Parse(File.ReadAllText(runtimeConfig))!;
        document["runtimeOptions"]!["framework"]!["version"] = "0.0.0";
        File.WriteAllText(
            runtimeConfig,
            document.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.sdk-runtime-evidence-invalid"
                && diagnostic.Location == "sdkRoot");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_conditional_override_of_an_sdk_identity_property()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var bundledVersions = Path.Combine(
            fixture.DotnetRoot,
            "sdk",
            inputs_.SdkVersion,
            "Microsoft.NETCoreSdk.BundledVersions.props");
        var document = XDocument.Load(bundledVersions, LoadOptions.None);
        var xmlNamespace = document.Root!.Name.Namespace;
        document.Root.Add(
            new XElement(
                xmlNamespace + "PropertyGroup",
                new XAttribute("Condition", "'1' == '1'"),
                new XElement(xmlNamespace + "NETCoreSdkVersion", "0.0.0")));
        File.WriteAllText(
            bundledVersions,
            document.ToString(SaveOptions.DisableFormatting) + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.sdk-runtime-evidence-invalid"
                && diagnostic.Location == "sdkRoot");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_case_variant_override_of_an_sdk_identity_property()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var bundledVersions = Path.Combine(
            fixture.DotnetRoot,
            "sdk",
            inputs_.SdkVersion,
            "Microsoft.NETCoreSdk.BundledVersions.props");
        var document = XDocument.Load(bundledVersions, LoadOptions.None);
        var xmlNamespace = document.Root!.Name.Namespace;
        document.Root.Add(
            new XElement(
                xmlNamespace + "PropertyGroup",
                new XElement(xmlNamespace + "netcoresdkversion", "0.0.0")));
        File.WriteAllText(
            bundledVersions,
            document.ToString(SaveOptions.DisableFormatting) + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.sdk-runtime-evidence-invalid"
                && diagnostic.Location == "sdkRoot");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_an_sdk_identity_document_with_an_import()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var bundledVersions = Path.Combine(
            fixture.DotnetRoot,
            "sdk",
            inputs_.SdkVersion,
            "Microsoft.NETCoreSdk.BundledVersions.props");
        var document = XDocument.Load(bundledVersions, LoadOptions.None);
        var xmlNamespace = document.Root!.Name.Namespace;
        document.Root.Add(
            new XElement(
                xmlNamespace + "Import",
                new XAttribute("Project", "override.props"),
                new XAttribute("Condition", "'1' == '1'")));
        File.WriteAllText(
            bundledVersions,
            document.ToString(SaveOptions.DisableFormatting) + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.sdk-runtime-evidence-invalid"
                && diagnostic.Location == "sdkRoot");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_native_dependency_with_the_wrong_internal_dll_name()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, SlangFileName),
            "kernel32.dll",
            StudioEditorImageTestInputs.SlangRequiredExports);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location == $"publishRoot/{SlangFileName}");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    private static string Render(StudioEditorImageProductionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static int AlignUp(int value, int alignment) =>
        checked((value + alignment - 1) & -alignment);

    private sealed class ProducerFixture : IDisposable
    {
        public ProducerFixture(
            StudioEditorImageTestInputs inputs,
            bool createOutput = false)
        {
            Root = Path.Combine(
                Path.GetTempPath(),
                $"asharia-studio-distribution-{Guid.NewGuid():N}");
            PublishRoot = Path.Combine(Root, "publish");
            DotnetRoot = Path.Combine(Root, "dotnet");
            OutputRoot = Path.Combine(Root, "editor-image");
            inputs.CopyPublishTo(PublishRoot);
            inputs.CopyDotnetTo(DotnetRoot);
            Write(DotnetRoot, "sdk/9.9.999/not-selected.txt", "old-sdk");
            if (createOutput)
            {
                Directory.CreateDirectory(OutputRoot);
            }

            Request = new StudioEditorImageProductionRequest(
                new DirectoryInfo(PublishRoot),
                EditorFileName,
                new DirectoryInfo(DotnetRoot),
                inputs.SdkVersion,
                inputs.HostFxrVersion,
                inputs.HostRuntimeVersion,
                inputs.ReferencePackVersion,
                new FileInfo(Path.Combine(PublishRoot, "Asharia.Runtime.Contracts.dll")),
                new FileInfo(Path.Combine(PublishRoot, "Asharia.Editor.dll")),
                new DirectoryInfo(OutputRoot));
        }

        public string Root { get; }

        public string PublishRoot { get; }

        public string DotnetRoot { get; }

        public string OutputRoot { get; }

        public StudioEditorImageProductionRequest Request { get; }

        public void Dispose()
        {
            if (Directory.Exists(Root))
            {
                Directory.Delete(Root, recursive: true);
            }
        }

        private static void Write(string root, string relativePath, string contents)
        {
            var path = Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, contents, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        }

    }
}
