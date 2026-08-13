using System.Buffers.Binary;
using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Asharia.Studio.Distribution;
using Xunit;

namespace Asharia.Studio.Distribution.Tests;

[Collection(StudioEditorImageInputCollection.Name)]
public sealed class StudioEditorImageProducerTests
{
    private const string EditorFileName = "Editor.exe";
    private readonly StudioEditorImageTestInputs inputs_;

    public StudioEditorImageProducerTests(StudioEditorImageTestInputs inputs)
    {
        inputs_ = inputs;
    }

    [Fact]
    public async Task Produce_stages_one_closed_current_editor_image()
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
            file => file.Path == "bin/Asharia.Studio.Application.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/Asharia.Runtime.Contracts.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/Asharia.Studio.EngineBridge.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/Asharia.Studio.Presentation.Avalonia.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/asharia_project_native.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/asharia_editor_content_native.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/asharia_scene_native.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/editor_native.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/shaders/renderer-basic/world_grid.vert.spv");
        Assert.Contains(
            receipt.Files,
            file => file.Path == "bin/shaders/renderer-basic/basic_mesh3d.vert.spv");
        Assert.DoesNotContain(
            receipt.Files,
            file => file.Path is "bin/Asharia.Editor.dll"
                or "bin/Asharia.Studio.DevelopmentHost.dll"
                or "bin/Asharia.Studio.DevelopmentProtocol.dll"
                or "bin/slang.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path
                == $"managed/dotnet/host/fxr/{inputs_.HostFxrVersion}/hostfxr.dll");
        Assert.Contains(
            receipt.Files,
            file => file.Path
                == $"managed/dotnet/shared/Microsoft.NETCore.App/{inputs_.HostRuntimeVersion}/coreclr.dll");
        Assert.DoesNotContain(
            receipt.Files,
            file => file.Path == "managed/dotnet/dotnet.exe"
                || file.Path.StartsWith("managed/dotnet/sdk/", StringComparison.Ordinal)
                || file.Path.StartsWith("managed/dotnet/packs/", StringComparison.Ordinal));
        Assert.All(receipt.Files, file =>
        {
            Assert.True(file.Size >= 0);
            Assert.Matches("^[0-9a-f]{64}$", file.Sha256);
        });
        using var stagedEditor = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = Path.Combine(fixture.OutputRoot, "bin", EditorFileName),
                WorkingDirectory = Path.Combine(fixture.OutputRoot, "bin"),
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };
        stagedEditor.StartInfo.Environment["DOTNET_ROOT"] =
            Path.Combine(fixture.Root, "ambient-dotnet-must-not-be-used");
        stagedEditor.StartInfo.Environment["DOTNET_ROOT_X64"] =
            Path.Combine(fixture.Root, "ambient-dotnet-x64-must-not-be-used");
        stagedEditor.StartInfo.Environment["DOTNET_MULTILEVEL_LOOKUP"] = "0";
        Assert.True(stagedEditor.Start(), "Could not start the staged Editor apphost.");
        var stagedStdout = stagedEditor.StandardOutput.ReadToEndAsync();
        var stagedStderr = stagedEditor.StandardError.ReadToEndAsync();
        await WaitForExitOrKillAsync(
            stagedEditor,
            TimeSpan.FromSeconds(30),
            "The staged Editor apphost");
        var output = await stagedStdout;
        var error = await stagedStderr;

        Assert.True(
            stagedEditor.ExitCode == 0,
            $"Staged Editor exited with {stagedEditor.ExitCode}:"
                + $"{Environment.NewLine}{output}"
                + $"{Environment.NewLine}{error}");
        Assert.Contains(
            StudioEditorImageTestInputs.StagedEditorMainMarker,
            output,
            StringComparison.Ordinal);
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
            "shared",
            "Microsoft.NETCore.App",
            inputs_.HostRuntimeVersion,
            "System.Private.CoreLib.dll");
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
            "shared",
            "Microsoft.NETCore.App",
            inputs_.HostRuntimeVersion,
            "System.Private.CoreLib.dll")));
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
        var allowedPaths = PythonProductPayloadFixture.AllowedPaths
            .Except(
                [
                    "controls/dotnet.exe",
                    "controls/sdk/10.0.302/dotnet.dll",
                    "controls/editor_native.dll",
                    "controls/slang.dll",
                ],
                StringComparer.OrdinalIgnoreCase)
            .ToArray();
        foreach (var relativePath in allowedPaths)
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
            allowedPaths,
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
        var relativePath =
            $"shared/Microsoft.NETCore.App/{inputs_.HostRuntimeVersion}/{selectedRelativePath}";
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
                    == $"managed/dotnet/{relativePath}");
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

    [Theory]
    [InlineData("nested/Asharia.Editor.dll")]
    [InlineData("dev/Asharia.Studio.DevelopmentHost.dll")]
    [InlineData("dev/Asharia.Studio.DevelopmentProtocol.dll")]
    [InlineData("deep/asharia_editor_content_native")]
    [InlineData("deep/ASHARIA_EDITOR_CONTENT_NATIVE.PDB")]
    [InlineData("deep/asharia_scene_native")]
    [InlineData("deep/ASHARIA_SCENE_NATIVE.PDB")]
    [InlineData("plugins/Editor_Native.DlL")]
    [InlineData("assets/SLANG.JsOn")]
    public void Produce_rejects_a_retired_studio_publish_artifact_at_any_depth(
        string relativePath)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var artifactPath = Path.Combine(
            fixture.PublishRoot,
            relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(artifactPath)!);
        File.WriteAllText(artifactPath, "retired");

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.forbidden-product-artifact"
                && diagnostic.Location == $"bin/{relativePath}");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_project_adapter_with_missing_exports()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, "asharia_project_native.dll"),
            "asharia_project_native.dll",
            ["asharia_project_open"]);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location == "publishRoot/asharia_project_native.dll");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_an_editor_content_adapter_without_the_exact_query_export()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, "asharia_editor_content_native.dll"),
            "asharia_editor_content_native.dll",
            ["asharia_editor_content_query_v1"]);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location
                    == "publishRoot/asharia_editor_content_native.dll"
                && diagnostic.Message.Contains(
                    "asharia_editor_content_query",
                    StringComparison.Ordinal));
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_scene_adapter_with_missing_document_exports()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, "asharia_scene_native.dll"),
            "asharia_scene_native.dll",
            ["asharia_scene_document_open_default"]);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location == "publishRoot/asharia_scene_native.dll");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_a_viewport_adapter_with_missing_lifecycle_exports()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, "editor_native.dll"),
            "editor_native.dll",
            ["editor_viewport_open_stream_v7"]);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location == "publishRoot/editor_native.dll");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Theory]
    [InlineData("editor_viewport_open_stream_v7_for_test")]
    [InlineData("editor_viewport_acquire_present_packet")]
    [InlineData("editor_viewport_release_present_packet")]
    [InlineData("editor_viewport_acquire_present_packet_v2")]
    [InlineData("editor_viewport_create_present_slot_v3")]
    [InlineData("editor_viewport_render_present_slot_v3")]
    [InlineData("editor_viewport_create_present_slot_v4")]
    [InlineData("editor_viewport_open_stream_v5")]
    [InlineData("editor_viewport_submit_latest_v5")]
    [InlineData("editor_viewport_try_take_ready_v5")]
    [InlineData("editor_viewport_complete_frame_v5")]
    [InlineData("editor_viewport_release_slot_import_v5")]
    [InlineData("editor_viewport_close_stream_v5")]
    [InlineData("editor_viewport_poll_stream_v5")]
    [InlineData("editor_viewport_destroy_stream_v5")]
    [InlineData("editor_viewport_open_stream_v6")]
    [InlineData("editor_viewport_submit_latest_v6")]
    [InlineData("editor_viewport_try_take_ready_v6")]
    [InlineData("editor_viewport_complete_frame_v6")]
    [InlineData("editor_viewport_release_slot_import_v6")]
    [InlineData("editor_viewport_close_stream_v6")]
    [InlineData("editor_viewport_poll_stream_v6")]
    [InlineData("editor_viewport_destroy_stream_v6")]
    public void Produce_rejects_nonproduction_viewport_stream_abi_export(string legacyExport)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, "editor_native.dll"),
            "editor_native.dll",
            StudioEditorImageTestInputs.CreateViewportNativeExports(legacyExport));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.native-identity-invalid"
                && diagnostic.Location == "publishRoot/editor_native.dll"
                && diagnostic.Message.Contains(legacyExport, StringComparison.Ordinal));
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_allows_historical_viewport_runtime_stats_v5_export()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        StudioEditorImageTestInputs.WriteNativeDll(
            Path.Combine(fixture.PublishRoot, "editor_native.dll"),
            "editor_native.dll",
            StudioEditorImageTestInputs.CreateViewportNativeExports(
                "editor_viewport_query_runtime_stats_v5"));

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.True(result.Succeeded, Render(result));
    }

    [Fact]
    public void Produce_rejects_an_undeclared_renderer_basic_shader()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var relativePath = "shaders/renderer-basic/undeclared.spv";
        File.WriteAllText(
            Path.Combine(
                fixture.PublishRoot,
                relativePath.Replace('/', Path.DirectorySeparatorChar)),
            "undeclared shader");

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.forbidden-product-artifact"
                && diagnostic.Location == $"bin/{relativePath}");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Fact]
    public void Produce_rejects_the_wrong_engine_bridge_managed_identity()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        File.Copy(
            Path.Combine(fixture.PublishRoot, "Asharia.Studio.Application.dll"),
            Path.Combine(fixture.PublishRoot, "Asharia.Studio.EngineBridge.dll"),
            overwrite: true);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.managed-identity-invalid"
                && diagnostic.Location == "publishRoot/Asharia.Studio.EngineBridge.dll");
        Assert.False(Directory.Exists(fixture.OutputRoot));
    }

    [Theory]
    [InlineData("tools/DoTnEt.ExE")]
    [InlineData("third-party/SdK/tool.dll")]
    [InlineData("references/PACKS/System.Runtime.dll")]
    [InlineData("MANAGED/runtime/coreclr.dll")]
    [InlineData("payload/MeTaDaTa/receipt.json")]
    [InlineData("nested/MANAGED-BUILD-ENVIRONMENT.JSON")]
    public void Produce_rejects_publish_payload_without_a_current_runtime_reader(
        string relativePath)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var artifactPath = Path.Combine(
            fixture.PublishRoot,
            relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(artifactPath)!);
        File.WriteAllText(artifactPath, "unread product payload");

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.forbidden-product-artifact"
                && diagnostic.Location == $"bin/{relativePath}");
        Assert.False(Directory.Exists(fixture.OutputRoot));
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
            Path.Combine(
                fixture.DotnetRoot,
                "sdk",
                inputs_.SdkVersion,
                "AppHostTemplate",
                "apphost.exe"),
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
            Path.Combine(fixture.PublishRoot, "Asharia.Studio.Application.dll"),
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
                && diagnostic.Location == "hostFxrRoot/hostfxr.dll");
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
        var hostFxr = Path.Combine(
            fixture.DotnetRoot,
            "host",
            "fxr",
            inputs_.HostFxrVersion,
            "hostfxr.dll");
        var contents = File.ReadAllBytes(hostFxr);
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
        File.WriteAllBytes(hostFxr, contents);

        var result = StudioEditorImageProducer.Produce(fixture.Request);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                    == "studio-distribution.editor-image.component-version-invalid"
                && diagnostic.Location == "hostFxrRoot/hostfxr.dll");
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
    public void Produce_rejects_editor_deps_with_a_mismatched_application_version()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        using var fixture = new ProducerFixture(inputs_);
        var depsPath = Path.Combine(fixture.PublishRoot, "Editor.deps.json");
        var document = JsonNode.Parse(File.ReadAllText(depsPath))!;
        var runtimeTarget = document["runtimeTarget"]!["name"]!.GetValue<string>();
        document["targets"]![runtimeTarget]!["Editor/1.0.0"]!["dependencies"]!["Asharia.Studio.Application"] = "9.9.9";
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

    private static string Render(StudioEditorImageProductionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static async Task WaitForExitOrKillAsync(
        Process process,
        TimeSpan timeoutValue,
        string description)
    {
        using var timeout = new CancellationTokenSource(timeoutValue);
        try
        {
            await process.WaitForExitAsync(timeout.Token);
            return;
        }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested)
        {
            // Continue into the bounded kill path.
        }

        if (!HasExited(process))
        {
            try
            {
                process.Kill(entireProcessTree: true);
            }
            catch (InvalidOperationException)
            {
                throw new TimeoutException($"{description} did not exit within {timeoutValue}.");
            }

            using var killTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await process.WaitForExitAsync(killTimeout.Token);
            }
            catch (InvalidOperationException)
            {
                // The process exited between Kill and the bounded wait.
            }
            catch (OperationCanceledException) when (killTimeout.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"{description} did not exit within 5 seconds after it was killed.");
            }
        }

        throw new TimeoutException($"{description} did not exit within {timeoutValue}.");
    }

    private static bool HasExited(Process process)
    {
        try
        {
            return process.HasExited;
        }
        catch (InvalidOperationException)
        {
            return true;
        }
    }

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
