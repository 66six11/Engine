using System.Diagnostics.CodeAnalysis;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Distribution;

public static partial class StudioEditorImageProducer
{
    private const string DotnetRelativeRoot = "managed/dotnet";
    private const int MaxEditorFileCount = 65_536;
    private const long MaxEditorImageBytes = 4L * 1024 * 1024 * 1024;
    private const int CopyBufferSize = 1024 * 1024;
    private const string CoreLibraryPublicKeyToken = "7cec85d7bea7798e";
    private static readonly Version StudioManagedAssemblyVersion = new(1, 0, 0, 0);
    private static readonly Version Net10FrameworkAssemblyVersion = new(10, 0, 0, 0);
    private static readonly StringComparer FileSystemPathComparer =
        OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
    // Accepted v1 logical paths are ASCII, so this is identical to the
    // Distribution assembler's Unicode case-folding policy within the accepted subset.
    private static readonly StringComparer LogicalPathComparer =
        StringComparer.OrdinalIgnoreCase;
    private static readonly IComparer<string> Utf8Comparer =
        Comparer<string>.Create(CompareUtf8);
    private static readonly HashSet<string> PythonPayloadExtensions = new(
        StringComparer.OrdinalIgnoreCase)
    {
        ".egg",
        ".pth",
        ".py",
        ".pyc",
        ".pyd",
        ".pyi",
        ".pyo",
        ".pyw",
        ".pyz",
        ".whl",
    };
    private static readonly HashSet<string> PythonPayloadSegments = new(
        StringComparer.OrdinalIgnoreCase)
    {
        ".venv",
        "__pycache__",
        "dist-packages",
        "graalpy",
        "ironpython",
        "jython",
        "pypy",
        "pypy3",
        "pythonnet",
        "site-packages",
        "venv",
        "virtualenv",
    };
    private static readonly string[] ForbiddenStudioArtifactStems =
    [
        "Asharia.Editor",
        "Asharia.Studio.DevelopmentHost",
        "Asharia.Studio.DevelopmentProtocol",
        "slang",
    ];
    private static readonly string[] HostFxrRequiredExports =
    [
        "hostfxr_close",
        "hostfxr_get_runtime_delegate",
        "hostfxr_initialize_for_runtime_config",
        "hostfxr_main_startupinfo",
        "hostfxr_set_error_writer",
    ];
    private static readonly string[] ProjectNativeRequiredExports =
    [
        "asharia_project_create_minimal",
        "asharia_project_open",
    ];
    private static readonly string[] SceneNativeRequiredExports =
    [
        "asharia_scene_document_open_default",
        "asharia_scene_document_close",
        "asharia_scene_document_snapshot",
        "asharia_scene_document_create_entity",
        "asharia_scene_document_set_entity_name",
        "asharia_scene_document_set_entity_transform",
        "asharia_scene_document_save",
    ];
    private static readonly string[] ViewportNativeRequiredExports =
    [
        "editor_viewport_create_present_slot_v4",
        "editor_viewport_release_present_packet",
        "editor_viewport_shutdown",
    ];
    private static readonly string[] ViewportShaderFiles =
    [
        "debug_line.frag.reflection.json",
        "debug_line.frag.spv",
        "debug_line.vert.reflection.json",
        "debug_line.vert.spv",
        "descriptor_layout.frag.reflection.json",
        "descriptor_layout.frag.spv",
        "descriptor_layout.vert.reflection.json",
        "descriptor_layout.vert.spv",
        "world_grid.frag.reflection.json",
        "world_grid.frag.spv",
        "world_grid.vert.reflection.json",
        "world_grid.vert.spv",
    ];

    public static StudioEditorImageProductionResult Produce(
        StudioEditorImageProductionRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);

        string? cleanupRoot = null;
        try
        {
            var inputs = ValidateAndPlan(request);
            var outputRoot = inputs.OutputRoot;
            var outputParent = Path.GetDirectoryName(outputRoot)!;
            EnsureTrustedOutputParent(outputParent);
            var stagingRoot = Path.Combine(
                outputParent,
                $".asharia-editor-image-staging-{Guid.NewGuid():N}");
            Directory.CreateDirectory(stagingRoot);
            cleanupRoot = stagingRoot;
            EnsurePathHasNoReparsePoint(stagingRoot, "stagingRoot");

            foreach (var file in inputs.Files)
            {
                CopyFile(
                    file.SourcePath,
                    Resolve(stagingRoot, file.DestinationPath),
                    file.Fingerprint,
                    file.DestinationPath);
            }

            ValidateStagedIdentities(stagingRoot, inputs);

            var bindings = CreateClosedOutputBindings(
                inputs,
                inputs.EntryPointDestination);
            VerifyClosedOutput(stagingRoot, bindings);

            EnsureTrustedOutputParent(outputParent);
            EnsurePathHasNoReparsePoint(stagingRoot, "stagingRoot");
            if (File.Exists(outputRoot) || Directory.Exists(outputRoot))
            {
                Fail(
                    "studio-distribution.editor-image.output-exists",
                    "outputRoot",
                    "Output root must not already exist.");
            }
            Directory.Move(stagingRoot, outputRoot);
            cleanupRoot = null;
            VerifyClosedOutput(outputRoot, bindings);
            return StudioEditorImageProductionResult.Success(
                outputRoot,
                inputs.EntryPointDestination,
                bindings);
        }
        catch (ProductionFailure failure)
        {
            return StudioEditorImageProductionResult.Failure(failure.Diagnostic);
        }
        catch (ArgumentException error)
        {
            return StudioEditorImageProductionResult.Failure(Diagnostic(
                "studio-distribution.editor-image.path-invalid",
                string.Empty,
                $"Editor Image request contains an invalid path: {error.Message}"));
        }
        catch (Exception error) when (
            error is IOException
                or UnauthorizedAccessException
                or NotSupportedException)
        {
            return StudioEditorImageProductionResult.Failure(Diagnostic(
                "studio-distribution.editor-image.io-failed",
                string.Empty,
                $"Editor Image production failed: {error.Message}"));
        }
        finally
        {
            if (cleanupRoot is not null)
            {
                TryDeleteOwnedStaging(cleanupRoot, request.OutputRoot);
            }
        }
    }

    private static ProductionInputs ValidateAndPlan(
        StudioEditorImageProductionRequest request)
    {
        if (!OperatingSystem.IsWindows())
        {
            Fail(
                "studio-distribution.editor-image.platform-unsupported",
                string.Empty,
                "The v1 Studio Editor Image release pipeline supports Windows only.");
        }

        if (request.PublishRoot is null
            || request.DotnetRoot is null
            || request.OutputRoot is null)
        {
            Fail(
                "studio-distribution.editor-image.request-invalid",
                string.Empty,
                "Editor Image request paths must not be null.");
        }

        if (!string.Equals(request.TargetFramework, "net10.0", StringComparison.Ordinal))
        {
            Fail(
                "studio-distribution.editor-image.framework-unsupported",
                "targetFramework",
                "The current Editor runtime contract requires net10.0.");
        }

        ValidateVersion(request.SdkVersion, "sdkVersion");
        ValidateVersion(request.HostFxrVersion, "hostFxrVersion");
        ValidateVersion(request.HostRuntimeVersion, "hostRuntimeVersion");

        var publishRoot = InspectDirectory(request.PublishRoot, "publishRoot");
        var dotnetRoot = InspectDirectory(request.DotnetRoot, "dotnetRoot");
        var outputRoot = NormalizeOutputRoot(request.OutputRoot);
        EnsureDisjointOutput(outputRoot, publishRoot, "publishRoot");
        EnsureDisjointOutput(outputRoot, dotnetRoot, "dotnetRoot");
        EnsureDisjointInputRoots(publishRoot, dotnetRoot);

        var entryPointRelative = NormalizeRelativePath(request.EntryPoint, "entryPoint");
        var entryPoint = InspectFile(
            Path.Combine(publishRoot, ToNativePath(entryPointRelative)),
            "entryPoint");
        EnsureDescendant(publishRoot, entryPoint, "entryPoint");
        EnsureExactPublishFile(publishRoot, entryPoint, "Editor.exe", "entryPoint");
        ValidatePortableExecutable(entryPoint, expectDll: false, "entryPoint");
        var editorManagedEntry = InspectFile(
            Path.Combine(publishRoot, "Editor.dll"),
            "publishRoot/Editor.dll");
        ValidateManagedPortableExecutable(editorManagedEntry, "publishRoot/Editor.dll");
        ValidateManagedIdentity(
            editorManagedEntry,
            "Editor",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Editor.dll");
        var applicationManagedEntry = InspectFile(
            Path.Combine(publishRoot, "Asharia.Studio.Application.dll"),
            "publishRoot/Asharia.Studio.Application.dll");
        ValidateManagedPortableExecutable(
            applicationManagedEntry,
            "publishRoot/Asharia.Studio.Application.dll");
        ValidateManagedIdentity(
            applicationManagedEntry,
            "Asharia.Studio.Application",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Studio.Application.dll");
        var runtimeContractsManagedEntry = InspectFile(
            Path.Combine(publishRoot, "Asharia.Runtime.Contracts.dll"),
            "publishRoot/Asharia.Runtime.Contracts.dll");
        ValidateManagedPortableExecutable(
            runtimeContractsManagedEntry,
            "publishRoot/Asharia.Runtime.Contracts.dll");
        ValidateManagedIdentity(
            runtimeContractsManagedEntry,
            "Asharia.Runtime.Contracts",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Runtime.Contracts.dll");
        var engineBridgeManagedEntry = InspectFile(
            Path.Combine(publishRoot, "Asharia.Studio.EngineBridge.dll"),
            "publishRoot/Asharia.Studio.EngineBridge.dll");
        ValidateManagedPortableExecutable(
            engineBridgeManagedEntry,
            "publishRoot/Asharia.Studio.EngineBridge.dll");
        ValidateManagedIdentity(
            engineBridgeManagedEntry,
            "Asharia.Studio.EngineBridge",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Studio.EngineBridge.dll");
        var presentationManagedEntry = InspectFile(
            Path.Combine(publishRoot, "Asharia.Studio.Presentation.Avalonia.dll"),
            "publishRoot/Asharia.Studio.Presentation.Avalonia.dll");
        ValidateManagedPortableExecutable(
            presentationManagedEntry,
            "publishRoot/Asharia.Studio.Presentation.Avalonia.dll");
        ValidateManagedIdentity(
            presentationManagedEntry,
            "Asharia.Studio.Presentation.Avalonia",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Studio.Presentation.Avalonia.dll");
        var projectNativeEntry = InspectFile(
            Path.Combine(publishRoot, "asharia_project_native.dll"),
            "publishRoot/asharia_project_native.dll");
        ValidatePortableExecutable(
            projectNativeEntry,
            expectDll: true,
            "publishRoot/asharia_project_native.dll");
        ValidateRequiredExports(
            projectNativeEntry,
            "asharia_project_native.dll",
            ProjectNativeRequiredExports,
            "publishRoot/asharia_project_native.dll");
        var sceneNativeEntry = InspectFile(
            Path.Combine(publishRoot, "asharia_scene_native.dll"),
            "publishRoot/asharia_scene_native.dll");
        ValidatePortableExecutable(
            sceneNativeEntry,
            expectDll: true,
            "publishRoot/asharia_scene_native.dll");
        ValidateRequiredExports(
            sceneNativeEntry,
            "asharia_scene_native.dll",
            SceneNativeRequiredExports,
            "publishRoot/asharia_scene_native.dll");
        var viewportNativeEntry = InspectFile(
            Path.Combine(publishRoot, "editor_native.dll"),
            "publishRoot/editor_native.dll");
        ValidatePortableExecutable(
            viewportNativeEntry,
            expectDll: true,
            "publishRoot/editor_native.dll");
        ValidateRequiredExports(
            viewportNativeEntry,
            "editor_native.dll",
            ViewportNativeRequiredExports,
            "publishRoot/editor_native.dll");
        var viewportShaderEntries = ViewportShaderFiles.ToDictionary(
            fileName => fileName,
            fileName => InspectFile(
                Path.Combine(publishRoot, "shaders", "renderer-basic", fileName),
                $"publishRoot/shaders/renderer-basic/{fileName}"),
            StringComparer.Ordinal);
        var editorDeps = InspectFile(
            Path.Combine(publishRoot, "Editor.deps.json"),
            "publishRoot/Editor.deps.json");
        var editorRuntimeConfig = InspectFile(
            Path.Combine(publishRoot, "Editor.runtimeconfig.json"),
            "publishRoot/Editor.runtimeconfig.json");
        ValidateEditorRuntimeEvidence(
            editorDeps,
            editorRuntimeConfig,
            request.TargetFramework,
            request.HostRuntimeVersion);

        var sdkRoot = InspectDirectory(
            new DirectoryInfo(Path.Combine(dotnetRoot, "sdk", request.SdkVersion)),
            "sdkRoot");
        var hostFxrRoot = InspectDirectory(
            new DirectoryInfo(Path.Combine(dotnetRoot, "host", "fxr", request.HostFxrVersion)),
            "hostFxrRoot");
        var hostRuntimeRoot = InspectDirectory(
            new DirectoryInfo(Path.Combine(
                dotnetRoot,
                "shared",
                "Microsoft.NETCore.App",
                request.HostRuntimeVersion)),
            "hostRuntimeRoot");
        var sdkAppHostTemplate = InspectFile(
            Path.Combine(sdkRoot, "AppHostTemplate", "apphost.exe"),
            "sdkRoot/AppHostTemplate/apphost.exe");
        var hostFxrEntry = InspectFile(
            Path.Combine(hostFxrRoot, "hostfxr.dll"),
            "hostFxrRoot/hostfxr.dll");
        var hostRuntimeCore = InspectFile(
            Path.Combine(hostRuntimeRoot, "System.Private.CoreLib.dll"),
            "hostRuntimeRoot/System.Private.CoreLib.dll");
        var sdkAppHostTemplateFingerprint = CaptureFingerprint(sdkAppHostTemplate);
        ValidatePortableExecutable(
            sdkAppHostTemplate,
            expectDll: false,
            "sdkRoot/AppHostTemplate/apphost.exe");
        ValidateBoundAppHost(
            entryPoint,
            sdkAppHostTemplate,
            editorManagedEntry,
            "entryPoint");
        ValidatePortableExecutable(
            hostFxrEntry,
            expectDll: true,
            "hostFxrRoot/hostfxr.dll");
        ValidateRequiredExports(
            hostFxrEntry,
            "hostfxr.dll",
            HostFxrRequiredExports,
            "hostFxrRoot/hostfxr.dll");
        ValidateProductVersion(
            hostFxrEntry,
            request.HostFxrVersion,
            "hostFxrRoot/hostfxr.dll");
        ValidateManagedPortableExecutable(
            hostRuntimeCore,
            "hostRuntimeRoot/System.Private.CoreLib.dll",
            expectAmd64: true);
        ValidateManagedIdentity(
            hostRuntimeCore,
            "System.Private.CoreLib",
            Net10FrameworkAssemblyVersion,
            CoreLibraryPublicKeyToken,
            "hostRuntimeRoot/System.Private.CoreLib.dll");
        ValidateProductVersion(
            hostRuntimeCore,
            request.HostRuntimeVersion,
            "hostRuntimeRoot/System.Private.CoreLib.dll");
        var files = new List<PlannedFile>();
        AddTree(files, publishRoot, "bin");
        RejectForbiddenStudioPublishArtifacts(files);
        AddTree(files, hostFxrRoot, $"{DotnetRelativeRoot}/host/fxr/{request.HostFxrVersion}");
        AddTree(
            files,
            hostRuntimeRoot,
            $"{DotnetRelativeRoot}/shared/Microsoft.NETCore.App/{request.HostRuntimeVersion}");
        var duplicate = files
            .GroupBy(file => file.DestinationPath, LogicalPathComparer)
            .FirstOrDefault(group => group.Count() != 1);
        if (duplicate is not null)
        {
            Fail(
                "studio-distribution.editor-image.destination-collision",
                duplicate.Key,
                "Two Editor Image inputs resolve to the same destination path.");
        }

        EnsurePlannedDestination(
            files,
            editorManagedEntry,
            "bin/Editor.dll",
            "publishRoot/Editor.dll");
        EnsurePlannedDestination(
            files,
            applicationManagedEntry,
            "bin/Asharia.Studio.Application.dll",
            "publishRoot/Asharia.Studio.Application.dll");
        EnsurePlannedDestination(
            files,
            runtimeContractsManagedEntry,
            "bin/Asharia.Runtime.Contracts.dll",
            "publishRoot/Asharia.Runtime.Contracts.dll");
        EnsurePlannedDestination(
            files,
            engineBridgeManagedEntry,
            "bin/Asharia.Studio.EngineBridge.dll",
            "publishRoot/Asharia.Studio.EngineBridge.dll");
        EnsurePlannedDestination(
            files,
            presentationManagedEntry,
            "bin/Asharia.Studio.Presentation.Avalonia.dll",
            "publishRoot/Asharia.Studio.Presentation.Avalonia.dll");
        EnsurePlannedDestination(
            files,
            projectNativeEntry,
            "bin/asharia_project_native.dll",
            "publishRoot/asharia_project_native.dll");
        EnsurePlannedDestination(
            files,
            sceneNativeEntry,
            "bin/asharia_scene_native.dll",
            "publishRoot/asharia_scene_native.dll");
        EnsurePlannedDestination(
            files,
            viewportNativeEntry,
            "bin/editor_native.dll",
            "publishRoot/editor_native.dll");
        foreach (var (fileName, shaderEntry) in viewportShaderEntries)
        {
            EnsurePlannedDestination(
                files,
                shaderEntry,
                $"bin/shaders/renderer-basic/{fileName}",
                $"publishRoot/shaders/renderer-basic/{fileName}");
        }
        EnsurePlannedDestination(
            files,
            editorDeps,
            "bin/Editor.deps.json",
            "publishRoot/Editor.deps.json");
        EnsurePlannedDestination(
            files,
            editorRuntimeConfig,
            "bin/Editor.runtimeconfig.json",
            "publishRoot/Editor.runtimeconfig.json");
        EnsurePlannedDestination(
            files,
            hostFxrEntry,
            $"{DotnetRelativeRoot}/host/fxr/{request.HostFxrVersion}/hostfxr.dll",
            "hostFxrRoot/hostfxr.dll");
        EnsurePlannedDestination(
            files,
            hostRuntimeCore,
            $"{DotnetRelativeRoot}/shared/Microsoft.NETCore.App/{request.HostRuntimeVersion}/System.Private.CoreLib.dll",
            "hostRuntimeRoot/System.Private.CoreLib.dll");
        var entryPointDestination = FindPlannedDestination(
            files,
            entryPoint,
            "entryPoint");
        EnsureOutputLimits(files);
        return new ProductionInputs(
            outputRoot,
            entryPointDestination,
            sdkAppHostTemplate,
            sdkAppHostTemplateFingerprint,
            request.HostFxrVersion,
            request.HostRuntimeVersion,
            request.TargetFramework,
            files
                .OrderBy(file => file.DestinationPath, Utf8Comparer)
                .ToArray());
    }

    private static string InspectDirectory(DirectoryInfo directory, string location)
    {
        ArgumentNullException.ThrowIfNull(directory);
        if (!Path.IsPathFullyQualified(directory.ToString()))
        {
            Fail(
                "studio-distribution.editor-image.path-invalid",
                location,
                "Input and output roots must be fully qualified paths.");
        }

        return InspectDirectory(directory.FullName, location);
    }

    private static string InspectDirectory(string path, string location)
    {
        var fullPath = Path.GetFullPath(path);
        if (!Directory.Exists(fullPath))
        {
            Fail(
                "studio-distribution.editor-image.directory-invalid",
                location,
                "Required input directory does not exist.");
        }

        EnsurePathHasNoReparsePoint(fullPath, location);
        return Path.TrimEndingDirectorySeparator(fullPath);
    }

    private static string InspectFile(string path, string location)
    {
        var fullPath = Path.GetFullPath(path);
        if (!File.Exists(fullPath))
        {
            Fail(
                "studio-distribution.editor-image.file-invalid",
                location,
                "Required input file does not exist.");
        }

        EnsurePathHasNoReparsePoint(fullPath, location);
        return fullPath;
    }

    private static string InspectFile(FileInfo file, string location)
    {
        ArgumentNullException.ThrowIfNull(file);
        if (!Path.IsPathFullyQualified(file.ToString()))
        {
            Fail(
                "studio-distribution.editor-image.path-invalid",
                location,
                "Input files must use fully qualified paths.");
        }

        return InspectFile(file.FullName, location);
    }

    private static string NormalizeOutputRoot(DirectoryInfo directory)
    {
        ArgumentNullException.ThrowIfNull(directory);
        if (!Path.IsPathFullyQualified(directory.ToString()))
        {
            Fail(
                "studio-distribution.editor-image.path-invalid",
                "outputRoot",
                "Input and output roots must be fully qualified paths.");
        }

        string fullPath;
        try
        {
            fullPath = StudioDistributionPath.NormalizePublicAbsolutePath(
                directory.FullName);
        }
        catch (InvalidDataException error)
        {
            Fail(
                "studio-distribution.editor-image.path-invalid",
                "outputRoot",
                error.Message);
            throw;
        }
        if (File.Exists(fullPath) || Directory.Exists(fullPath))
        {
            Fail(
                "studio-distribution.editor-image.output-exists",
                "outputRoot",
                "Output root must not already exist.");
        }

        var parent = Path.GetDirectoryName(fullPath);
        if (string.IsNullOrEmpty(parent))
        {
            Fail(
                "studio-distribution.editor-image.output-invalid",
                "outputRoot",
                "Output root must have one containing directory.");
        }

        if (!Directory.Exists(parent))
        {
            Fail(
                "studio-distribution.editor-image.output-parent-invalid",
                "outputRoot",
                "Output root must have an existing trusted parent directory.");
        }

        EnsureTrustedOutputParent(parent);
        return fullPath;
    }

    private static void EnsureTrustedOutputParent(string outputParent)
    {
        if (!Directory.Exists(outputParent))
        {
            Fail(
                "studio-distribution.editor-image.output-parent-invalid",
                "outputRoot",
                "Output parent disappeared during Editor Image production.");
        }

        EnsurePathHasNoReparsePoint(outputParent, "outputRoot");
    }

    private static void EnsureDisjointOutput(
        string outputRoot,
        string inputRoot,
        string location)
    {
        if (IsSameOrDescendant(outputRoot, inputRoot)
            || IsSameOrDescendant(inputRoot, outputRoot))
        {
            Fail(
                "studio-distribution.editor-image.root-overlap",
                location,
                "Output root and input roots must not overlap.");
        }
    }

    private static void EnsureDisjointInputRoots(string publishRoot, string dotnetRoot)
    {
        if (IsSameOrDescendant(publishRoot, dotnetRoot)
            || IsSameOrDescendant(dotnetRoot, publishRoot))
        {
            Fail(
                "studio-distribution.editor-image.input-root-overlap",
                "dotnetRoot",
                "Publish and .NET input roots must not overlap.");
        }
    }

    private static void EnsureExactPublishFile(
        string publishRoot,
        string selectedPath,
        string requiredFileName,
        string location)
    {
        var requiredPath = InspectFile(
            Path.Combine(publishRoot, requiredFileName),
            location);
        if (!FileSystemPathComparer.Equals(requiredPath, selectedPath))
        {
            Fail(
                "studio-distribution.editor-image.contract-location-invalid",
                location,
                $"Contract must be the fixed publish runtime asset '{requiredFileName}'.");
        }
    }

    private static void RejectForbiddenStudioPublishArtifacts(
        IReadOnlyList<PlannedFile> publishFiles)
    {
        foreach (var file in publishFiles)
        {
            var segments = file.DestinationPath.Split('/');
            var fileName = segments[^1];
            var isForbiddenStudioArtifact = ForbiddenStudioArtifactStems.Any(
                stem => fileName.Equals(stem, StringComparison.OrdinalIgnoreCase)
                    || fileName.StartsWith(stem + ".", StringComparison.OrdinalIgnoreCase));
            var isUnexpectedSceneNativeArtifact =
                (fileName.Equals("asharia_scene_native", StringComparison.OrdinalIgnoreCase)
                 || fileName.StartsWith(
                     "asharia_scene_native.",
                     StringComparison.OrdinalIgnoreCase))
                && !file.DestinationPath.Equals(
                    "bin/asharia_scene_native.dll",
                    StringComparison.OrdinalIgnoreCase);
            var isUnexpectedViewportNativeArtifact =
                (fileName.Equals("editor_native", StringComparison.OrdinalIgnoreCase)
                 || fileName.StartsWith(
                     "editor_native.",
                     StringComparison.OrdinalIgnoreCase))
                && !file.DestinationPath.Equals(
                    "bin/editor_native.dll",
                    StringComparison.OrdinalIgnoreCase);
            var hasForbiddenDirectory = segments
                .Skip(1)
                .Take(segments.Length - 2)
                .Any(segment => segment.Equals("managed", StringComparison.OrdinalIgnoreCase)
                    || segment.Equals("metadata", StringComparison.OrdinalIgnoreCase)
                    || segment.Equals("sdk", StringComparison.OrdinalIgnoreCase)
                    || segment.Equals("packs", StringComparison.OrdinalIgnoreCase));
            var isUnexpectedViewportShader =
                file.DestinationPath.StartsWith(
                    "bin/shaders/renderer-basic/",
                    StringComparison.OrdinalIgnoreCase)
                && !ViewportShaderFiles.Contains(fileName, StringComparer.Ordinal);
            if (isForbiddenStudioArtifact
                || isUnexpectedSceneNativeArtifact
                || isUnexpectedViewportNativeArtifact
                || isUnexpectedViewportShader
                || hasForbiddenDirectory
                || fileName.Equals("dotnet.exe", StringComparison.OrdinalIgnoreCase)
                || fileName.Equals(
                    "managed-build-environment.json",
                    StringComparison.OrdinalIgnoreCase))
            {
                Fail(
                    "studio-distribution.editor-image.forbidden-product-artifact",
                    file.DestinationPath,
                    $"Publish artifact '{file.DestinationPath}' has no current Studio runtime reader and must not enter the Editor Image.");
            }
        }
    }

    private static string FindPlannedDestination(
        IEnumerable<PlannedFile> files,
        string sourcePath,
        string location)
    {
        var matches = files
            .Where(file => FileSystemPathComparer.Equals(file.SourcePath, sourcePath))
            .Select(file => file.DestinationPath)
            .ToArray();
        if (matches.Length != 1)
        {
            Fail(
                "studio-distribution.editor-image.binding-missing",
                location,
                "Selected publish asset must have one exact staged destination.");
        }

        return matches[0];
    }

    private static void EnsurePlannedDestination(
        IEnumerable<PlannedFile> files,
        string sourcePath,
        string expectedDestination,
        string location)
    {
        var actual = FindPlannedDestination(files, sourcePath, location);
        if (!string.Equals(actual, expectedDestination, StringComparison.Ordinal))
        {
            Fail(
                "studio-distribution.editor-image.binding-invalid",
                location,
                $"Required asset must bind to exact logical path '{expectedDestination}'.");
        }
    }

    private static void EnsureOutputLimits(
        IReadOnlyCollection<PlannedFile> files)
    {
        if (files.Count > MaxEditorFileCount)
        {
            Fail(
                "studio-distribution.editor-image.file-count-exceeded",
                string.Empty,
                $"Editor Image may contain at most {MaxEditorFileCount} files.");
        }

        try
        {
            var totalSize = files.Aggregate(
                0L,
                (total, file) => checked(total + file.Fingerprint.Size));
            if (totalSize > MaxEditorImageBytes)
            {
                Fail(
                    "studio-distribution.editor-image.size-exceeded",
                    string.Empty,
                    "Editor Image may contain at most 4 GiB of declared files.");
            }
        }
        catch (OverflowException)
        {
            Fail(
                "studio-distribution.editor-image.size-exceeded",
                string.Empty,
                "Editor Image declared size overflowed its supported range.");
        }
    }

    private static void AddTree(
        ICollection<PlannedFile> output,
        string sourceRoot,
        string destinationRoot)
    {
        var remainingFileCount = MaxEditorFileCount - output.Count;
        var usedBytes = output.Sum(file => file.Fingerprint.Size);
        var remainingBytes = MaxEditorImageBytes - usedBytes;
        if (remainingFileCount <= 0 || remainingBytes < 0)
        {
            Fail(
                "studio-distribution.editor-image.budget-exceeded",
                destinationRoot,
                "Editor Image input exceeded its file-count or byte budget.");
        }

        var sources = EnumerateTree(
            sourceRoot,
            remainingFileCount,
            remainingBytes);
        if (sources.Count == 0)
        {
            Fail(
                "studio-distribution.editor-image.tree-empty",
                destinationRoot,
                "Selected Editor Image input trees must contain at least one file.");
        }

        foreach (var source in sources)
        {
            var relative = PortableRelativePath(sourceRoot, source.Path, destinationRoot);
            var destinationPath = $"{destinationRoot}/{relative}";
            var normalizedDestination = NormalizeRelativePath(
                destinationPath,
                destinationPath);
            EnsureNoPythonProductPayload(normalizedDestination);
            output.Add(new PlannedFile(
                source.Path,
                normalizedDestination,
                source.Fingerprint));
        }
    }

    private static void EnsureNoPythonProductPayload(string destinationPath)
    {
        var segments = destinationPath.Split('/');
        var fileName = segments[^1];
        var extension = Path.GetExtension(fileName);
        var containsPythonPackageTree = segments.Any(segment =>
            PythonPayloadSegments.Contains(segment)
            || segment.EndsWith(".dist-info", StringComparison.OrdinalIgnoreCase)
            || segment.EndsWith(".egg-info", StringComparison.OrdinalIgnoreCase)
            || PythonRuntimeDirectoryPattern().IsMatch(segment));
        var isPythonArtifact = PythonPayloadExtensions.Contains(extension)
            || fileName.Equals("py.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pyw.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pymanager.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pywmanager.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pyvenv.cfg", StringComparison.OrdinalIgnoreCase)
            || PythonRuntimeFilePattern().IsMatch(fileName)
            || PipRuntimeFilePattern().IsMatch(fileName)
            || LibPythonRuntimeFilePattern().IsMatch(fileName)
            || ManagedPythonRuntimeFilePattern().IsMatch(fileName)
            || AlternativePythonRuntimeFilePattern().IsMatch(fileName)
            || LibPyPyRuntimeFilePattern().IsMatch(fileName);
        if (containsPythonPackageTree || isPythonArtifact)
        {
            Fail(
                "studio-distribution.editor-image.python-payload-forbidden",
                destinationPath,
                "Python is repository-only development tooling and must not be present in a product Editor Image.");
        }
    }

    private static IReadOnlyList<SourceFile> EnumerateTree(
        string root,
        int maxFileCount = MaxEditorFileCount,
        long maxBytes = MaxEditorImageBytes)
    {
        var files = new List<SourceFile>();
        long totalBytes = 0;
        var pending = new Stack<string>();
        pending.Push(root);
        while (pending.Count != 0)
        {
            var directory = pending.Pop();
            foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
            {
                var attributes = File.GetAttributes(entry);
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                {
                    Fail(
                        "studio-distribution.editor-image.source-link",
                        PortableRelativePath(root, entry, root),
                        "Editor Image inputs must not contain links or reparse points.");
                }

                if ((attributes & FileAttributes.Directory) != 0)
                {
                    pending.Push(entry);
                    continue;
                }

                if (!File.Exists(entry))
                {
                    Fail(
                        "studio-distribution.editor-image.source-special-file",
                        PortableRelativePath(root, entry, root),
                        "Editor Image inputs may contain only regular files and directories.");
                }

                if (files.Count >= maxFileCount)
                {
                    Fail(
                        "studio-distribution.editor-image.file-count-exceeded",
                        root,
                        $"Editor Image may contain at most {MaxEditorFileCount} files.");
                }

                var fingerprint = CaptureFingerprint(entry);
                totalBytes = checked(totalBytes + fingerprint.Size);
                if (totalBytes > maxBytes)
                {
                    Fail(
                        "studio-distribution.editor-image.size-exceeded",
                        root,
                        "Editor Image may contain at most 4 GiB of declared files.");
                }

                files.Add(new SourceFile(entry, fingerprint));
            }
        }

        return files
            .OrderBy(file => PortableRelativePath(root, file.Path, root), Utf8Comparer)
            .ToArray();
    }

    private static IReadOnlyList<StudioEditorImageFileBinding> CreateClosedOutputBindings(
        ProductionInputs inputs,
        string entryPoint)
    {
        var bindings = inputs.Files
            .Select(file => new StudioEditorImageFileBinding(
                file.DestinationPath,
                ClassifyRole(file.DestinationPath, entryPoint),
                ClassifyMediaType(file.DestinationPath),
                file.Fingerprint.Size,
                file.Fingerprint.Sha256))
            .OrderBy(file => file.Path, Utf8Comparer)
            .ToArray();
        if (bindings.Length == 0
            || !bindings.Any(file => string.Equals(
                file.Path,
                entryPoint,
                StringComparison.Ordinal)))
        {
            Fail(
                "studio-distribution.editor-image.output-incomplete",
                string.Empty,
                "Staged Editor Image is missing its entry point.");
        }

        return bindings;
    }

    private static void VerifyClosedOutput(
        string root,
        IReadOnlyList<StudioEditorImageFileBinding> expected)
    {
        EnsurePathHasNoReparsePoint(root, "outputRoot");
        var actualFiles = EnumerateTree(root);
        if (actualFiles.Count != expected.Count)
        {
            Fail(
                "studio-distribution.editor-image.output-drift",
                string.Empty,
                "Closed Editor Image file count changed before publication completed.");
        }

        var expectedByPath = expected.ToDictionary(file => file.Path, StringComparer.Ordinal);
        var observedLogicalPaths = new HashSet<string>(LogicalPathComparer);
        foreach (var actual in actualFiles)
        {
            var relative = PortableRelativePath(root, actual.Path, root);
            if (!observedLogicalPaths.Add(relative))
            {
                Fail(
                    "studio-distribution.editor-image.output-drift",
                    relative,
                    "Closed Editor Image logical paths changed before publication completed.");
            }

            if (!expectedByPath.TryGetValue(relative, out var binding))
            {
                Fail(
                    "studio-distribution.editor-image.output-drift",
                    relative,
                    "Closed Editor Image contains one unplanned logical path.");
            }

            if (binding.Size != actual.Fingerprint.Size
                || !string.Equals(
                    binding.Sha256,
                    actual.Fingerprint.Sha256,
                    StringComparison.Ordinal))
            {
                Fail(
                    "studio-distribution.editor-image.output-drift",
                    relative,
                    "Closed Editor Image bytes or logical paths changed before publication completed.");
            }

        }
    }

    private static string ClassifyRole(
        string relativePath,
        string entryPoint)
    {
        if (string.Equals(relativePath, entryPoint, StringComparison.Ordinal))
        {
            return "executable";
        }

        var extension = Path.GetExtension(relativePath);
        if (extension.Equals(".pdb", StringComparison.OrdinalIgnoreCase))
        {
            return "debug-symbol";
        }

        if (extension.Equals(".json", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".xml", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".props", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".targets", StringComparison.OrdinalIgnoreCase))
        {
            return "metadata";
        }

        if (extension.Equals(".dll", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".exe", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".so", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".dylib", StringComparison.OrdinalIgnoreCase))
        {
            return "runtime-library";
        }

        return "resource";
    }

    private static string ClassifyMediaType(string relativePath)
    {
        var extension = Path.GetExtension(relativePath);
        if (extension.Equals(".json", StringComparison.OrdinalIgnoreCase))
        {
            return "application/json";
        }

        if (extension.Equals(".xml", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".props", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".targets", StringComparison.OrdinalIgnoreCase))
        {
            return "application/xml";
        }

        if (extension.Equals(".dll", StringComparison.OrdinalIgnoreCase)
            || extension.Equals(".exe", StringComparison.OrdinalIgnoreCase))
        {
            return "application/vnd.microsoft.portable-executable";
        }

        return "application/octet-stream";
    }

    private static void CopyFile(
        string source,
        string destination,
        SourceFingerprint expected,
        string location)
    {
        EnsureDestinationDirectory(Path.GetDirectoryName(destination)!);
        using var input = new FileStream(
            source,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            CopyBufferSize,
            FileOptions.SequentialScan);
        using var output = new FileStream(
            destination,
            FileMode.CreateNew,
            FileAccess.Write,
            FileShare.None,
            CopyBufferSize,
            FileOptions.SequentialScan);
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer = new byte[CopyBufferSize];
        long copied = 0;
        int read;
        while ((read = input.Read(buffer, 0, buffer.Length)) != 0)
        {
            output.Write(buffer, 0, read);
            hash.AppendData(buffer, 0, read);
            copied = checked(copied + read);
        }

        output.Flush(flushToDisk: false);
        var copiedSha256 = Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
        if (copied != expected.Size
            || input.Length != expected.Size
            || !string.Equals(copiedSha256, expected.Sha256, StringComparison.Ordinal))
        {
            Fail(
                "studio-distribution.editor-image.source-drift",
                location,
                "An Editor Image input changed while its planned bytes were copied.");
        }

    }

    private static void ValidateStagedIdentities(
        string stagingRoot,
        ProductionInputs inputs)
    {
        var entryPoint = Resolve(stagingRoot, inputs.EntryPointDestination);
        var editorManagedEntry = Resolve(stagingRoot, "bin/Editor.dll");
        ValidatePortableExecutable(
            inputs.SdkAppHostTemplate,
            expectDll: false,
            "sdkRoot/AppHostTemplate/apphost.exe");
        ValidateBoundAppHost(
            entryPoint,
            inputs.SdkAppHostTemplate,
            editorManagedEntry,
            "entryPoint");
        VerifySourceFingerprint(
            inputs.SdkAppHostTemplate,
            inputs.SdkAppHostTemplateFingerprint,
            "sdkRoot/AppHostTemplate/apphost.exe");

        ValidateManagedPortableExecutable(editorManagedEntry, "publishRoot/Editor.dll");
        ValidateManagedIdentity(
            editorManagedEntry,
            "Editor",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Editor.dll");
        var applicationManagedEntry = Resolve(
            stagingRoot,
            "bin/Asharia.Studio.Application.dll");
        ValidateManagedPortableExecutable(
            applicationManagedEntry,
            "publishRoot/Asharia.Studio.Application.dll");
        ValidateManagedIdentity(
            applicationManagedEntry,
            "Asharia.Studio.Application",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Studio.Application.dll");
        var runtimeContractsManagedEntry = Resolve(
            stagingRoot,
            "bin/Asharia.Runtime.Contracts.dll");
        ValidateManagedPortableExecutable(
            runtimeContractsManagedEntry,
            "publishRoot/Asharia.Runtime.Contracts.dll");
        ValidateManagedIdentity(
            runtimeContractsManagedEntry,
            "Asharia.Runtime.Contracts",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Runtime.Contracts.dll");
        var engineBridgeManagedEntry = Resolve(
            stagingRoot,
            "bin/Asharia.Studio.EngineBridge.dll");
        ValidateManagedPortableExecutable(
            engineBridgeManagedEntry,
            "publishRoot/Asharia.Studio.EngineBridge.dll");
        ValidateManagedIdentity(
            engineBridgeManagedEntry,
            "Asharia.Studio.EngineBridge",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Studio.EngineBridge.dll");
        var presentationManagedEntry = Resolve(
            stagingRoot,
            "bin/Asharia.Studio.Presentation.Avalonia.dll");
        ValidateManagedPortableExecutable(
            presentationManagedEntry,
            "publishRoot/Asharia.Studio.Presentation.Avalonia.dll");
        ValidateManagedIdentity(
            presentationManagedEntry,
            "Asharia.Studio.Presentation.Avalonia",
            StudioManagedAssemblyVersion,
            expectedPublicKeyToken: string.Empty,
            "publishRoot/Asharia.Studio.Presentation.Avalonia.dll");
        var projectNativeEntry = Resolve(
            stagingRoot,
            "bin/asharia_project_native.dll");
        ValidatePortableExecutable(
            projectNativeEntry,
            expectDll: true,
            "publishRoot/asharia_project_native.dll");
        ValidateRequiredExports(
            projectNativeEntry,
            "asharia_project_native.dll",
            ProjectNativeRequiredExports,
            "publishRoot/asharia_project_native.dll");
        var sceneNativeEntry = Resolve(
            stagingRoot,
            "bin/asharia_scene_native.dll");
        ValidatePortableExecutable(
            sceneNativeEntry,
            expectDll: true,
            "publishRoot/asharia_scene_native.dll");
        ValidateRequiredExports(
            sceneNativeEntry,
            "asharia_scene_native.dll",
            SceneNativeRequiredExports,
            "publishRoot/asharia_scene_native.dll");
        var viewportNativeEntry = Resolve(stagingRoot, "bin/editor_native.dll");
        ValidatePortableExecutable(
            viewportNativeEntry,
            expectDll: true,
            "publishRoot/editor_native.dll");
        ValidateRequiredExports(
            viewportNativeEntry,
            "editor_native.dll",
            ViewportNativeRequiredExports,
            "publishRoot/editor_native.dll");
        foreach (var fileName in ViewportShaderFiles)
        {
            _ = InspectFile(
                Resolve(stagingRoot, $"bin/shaders/renderer-basic/{fileName}"),
                $"publishRoot/shaders/renderer-basic/{fileName}");
        }
        ValidateEditorRuntimeEvidence(
            Resolve(stagingRoot, "bin/Editor.deps.json"),
            Resolve(stagingRoot, "bin/Editor.runtimeconfig.json"),
            inputs.TargetFramework,
            inputs.HostRuntimeVersion);

        var hostFxr = Resolve(
            stagingRoot,
            $"{DotnetRelativeRoot}/host/fxr/{inputs.HostFxrVersion}/hostfxr.dll");
        ValidatePortableExecutable(hostFxr, expectDll: true, "hostFxrRoot/hostfxr.dll");
        ValidateRequiredExports(
            hostFxr,
            "hostfxr.dll",
            HostFxrRequiredExports,
            "hostFxrRoot/hostfxr.dll");
        ValidateProductVersion(hostFxr, inputs.HostFxrVersion, "hostFxrRoot/hostfxr.dll");

        var runtimeCore = Resolve(
            stagingRoot,
            $"{DotnetRelativeRoot}/shared/Microsoft.NETCore.App/{inputs.HostRuntimeVersion}/System.Private.CoreLib.dll");
        ValidateManagedPortableExecutable(
            runtimeCore,
            "hostRuntimeRoot/System.Private.CoreLib.dll",
            expectAmd64: true);
        ValidateManagedIdentity(
            runtimeCore,
            "System.Private.CoreLib",
            Net10FrameworkAssemblyVersion,
            CoreLibraryPublicKeyToken,
            "hostRuntimeRoot/System.Private.CoreLib.dll");
        ValidateProductVersion(
            runtimeCore,
            inputs.HostRuntimeVersion,
            "hostRuntimeRoot/System.Private.CoreLib.dll");

    }

    private static void ValidateBoundAppHost(
        string path,
        string templatePath,
        string resourceSourcePath,
        string location)
    {
        if (!StudioEditorImageIdentityInspector.IsBoundAppHost(
                path,
                templatePath,
                resourceSourcePath,
                "Editor.dll",
                $"../{DotnetRelativeRoot}",
                out var error))
        {
            Fail(
                "studio-distribution.editor-image.apphost-identity-invalid",
                location,
                error);
        }
    }

    private static void ValidateManagedIdentity(
        string path,
        string expectedName,
        Version expectedVersion,
        string expectedPublicKeyToken,
        string location)
    {
        if (!StudioEditorImageIdentityInspector.HasManagedAssemblyIdentity(
                path,
                expectedName,
                expectedVersion,
                expectedPublicKeyToken,
                out var error))
        {
            Fail(
                "studio-distribution.editor-image.managed-identity-invalid",
                location,
                error);
        }
    }

    private static void ValidateProductVersion(
        string path,
        string expectedVersion,
        string location)
    {
        if (!StudioEditorImageIdentityInspector.HasProductVersion(
                path,
                expectedVersion,
                out var error))
        {
            Fail(
                "studio-distribution.editor-image.component-version-invalid",
                location,
                error);
        }
    }

    private static void ValidateRequiredExports(
        string path,
        string expectedDllName,
        IReadOnlyCollection<string> requiredExports,
        string location)
    {
        if (!StudioEditorImageIdentityInspector.HasRequiredExports(
                path,
                expectedDllName,
                requiredExports,
                out var error))
        {
            Fail(
                "studio-distribution.editor-image.native-identity-invalid",
                location,
                error);
        }
    }

    private static void ValidateEditorRuntimeEvidence(
        string depsPath,
        string runtimeConfigPath,
        string targetFramework,
        string selectedRuntimeVersion)
    {
        if (!StudioEditorImageIdentityInspector.HasEditorRuntimeEvidence(
                depsPath,
                runtimeConfigPath,
                targetFramework,
                selectedRuntimeVersion,
                out var error))
        {
            Fail(
                "studio-distribution.editor-image.managed-runtime-evidence-invalid",
                "publishRoot",
                error);
        }
    }

    private static void ValidatePortableExecutable(
        string path,
        bool expectDll,
        string location)
    {
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read);
            using var reader = new PEReader(stream, PEStreamOptions.LeaveOpen);
            var headers = reader.PEHeaders;
            var isDll = (headers.CoffHeader.Characteristics & Characteristics.Dll) != 0;
            if (headers.PEHeader is null
                || reader.HasMetadata
                || headers.CoffHeader.Machine != Machine.Amd64
                || headers.PEHeader.Magic != PEMagic.PE32Plus
                || isDll != expectDll)
            {
                throw new BadImageFormatException();
            }
        }
        catch (Exception error) when (
            error is BadImageFormatException
                or IOException
                or UnauthorizedAccessException)
        {
            Fail(
                "studio-distribution.editor-image.pe-invalid",
                location,
                expectDll
                    ? "Required native runtime dependency must be one native Windows x64 DLL."
                    : "Required host entry point must be one native Windows x64 executable.");
        }
    }

    private static void ValidateManagedPortableExecutable(
        string path,
        string location,
        bool expectAmd64 = false)
    {
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read);
            using var reader = new PEReader(stream, PEStreamOptions.LeaveOpen);
            if (reader.PEHeaders.PEHeader is null
                || reader.PEHeaders.CorHeader is null
                || !reader.HasMetadata
                || (expectAmd64
                    && (reader.PEHeaders.CoffHeader.Machine != Machine.Amd64
                        || reader.PEHeaders.PEHeader.Magic != PEMagic.PE32Plus)))
            {
                throw new BadImageFormatException();
            }
        }
        catch (Exception error) when (
            error is BadImageFormatException
                or IOException
                or UnauthorizedAccessException)
        {
            Fail(
                "studio-distribution.editor-image.managed-entry-invalid",
                location,
                expectAmd64
                    ? "Selected host runtime anchor must be one managed Windows x64 PE32+ assembly."
                    : "Fixed Studio managed entry must be one valid managed PE assembly.");
        }
    }

    private static void ValidateVersion(string value, string location)
    {
        var parts = value?.Split('.') ?? [];
        if (string.IsNullOrEmpty(value)
            || !StableVersionPattern().IsMatch(value)
            || !Version.TryParse(value, out var parsed)
            || parts.Length != 3
            || parsed.Major.ToString() != parts[0]
            || parsed.Minor.ToString() != parts[1]
            || parsed.Build.ToString() != parts[2])
        {
            Fail(
                "studio-distribution.editor-image.version-invalid",
                location,
                "Component version must be one exact stable three-part version.");
        }
    }

    private static string NormalizeRelativePath(string value, string location)
    {
        if (string.IsNullOrWhiteSpace(value)
            || value.Length > 500
            || !value.IsNormalized(NormalizationForm.FormC)
            || value.StartsWith("/", StringComparison.Ordinal)
            || value.Contains('\\')
            || value.Contains(':')
            || value.Any(char.IsControl)
            || value.Any(character => character > 0x7f)
            || Path.IsPathFullyQualified(value)
            || value.Split('/').Any(IsInvalidWindowsPathComponent))
        {
            Fail(
                "studio-distribution.editor-image.path-invalid",
                location,
                "Windows v1 image paths must be portable ASCII, relative, forward-slash paths.");
        }

        return value;
    }

    private static bool IsInvalidWindowsPathComponent(string component)
    {
        if (component is "" or "." or ".."
            || component.EndsWith(' ')
            || component.EndsWith('.')
            || component.IndexOfAny(['<', '>', '"', '|', '?', '*']) >= 0)
        {
            return true;
        }

        var stem = component.Split('.')[0];
        return stem.Equals("CON", StringComparison.OrdinalIgnoreCase)
            || stem.Equals("PRN", StringComparison.OrdinalIgnoreCase)
            || stem.Equals("AUX", StringComparison.OrdinalIgnoreCase)
            || stem.Equals("NUL", StringComparison.OrdinalIgnoreCase)
            || (stem.Length == 4
                && stem.StartsWith("COM", StringComparison.OrdinalIgnoreCase)
                && stem[3] is >= '1' and <= '9')
            || (stem.Length == 4
                && stem.StartsWith("LPT", StringComparison.OrdinalIgnoreCase)
                && stem[3] is >= '1' and <= '9');
    }

    private static string PortableRelativePath(
        string root,
        string path,
        string location)
    {
        var relative = Path.GetRelativePath(root, path)
            .Replace(Path.DirectorySeparatorChar, '/');
        return NormalizeRelativePath(relative, location);
    }

    private static string ToNativePath(string relativePath) =>
        relativePath.Replace('/', Path.DirectorySeparatorChar);

    private static string Resolve(string root, string relativePath) =>
        Path.Combine(root, ToNativePath(relativePath));

    private static void EnsureDescendant(string root, string path, string location)
    {
        if (!IsSameOrDescendant(path, root) || FileSystemPathComparer.Equals(path, root))
        {
            Fail(
                "studio-distribution.editor-image.path-outside-root",
                location,
                "File must belong to the explicit publish root.");
        }
    }

    private static bool IsSameOrDescendant(string candidate, string root)
    {
        var relative = Path.GetRelativePath(root, candidate);
        return relative == "."
            || (!relative.Equals("..", StringComparison.Ordinal)
                && !relative.StartsWith(
                    ".." + Path.DirectorySeparatorChar,
                    StringComparison.Ordinal)
                && !Path.IsPathFullyQualified(relative));
    }

    private static void EnsurePathHasNoReparsePoint(string path, string location)
    {
        FileSystemInfo? current = File.Exists(path)
            ? new FileInfo(path)
            : new DirectoryInfo(path);
        while (current is not null)
        {
            if (current.Exists
                && (current.Attributes & FileAttributes.ReparsePoint) != 0)
            {
                Fail(
                    "studio-distribution.editor-image.source-link",
                    location,
                    "Editor Image paths must not cross links or reparse points.");
            }

            current = current switch
            {
                FileInfo file => file.Directory,
                DirectoryInfo directory => directory.Parent,
                _ => null,
            };
        }
    }

    private static SourceFingerprint CaptureFingerprint(string path)
    {
        using var input = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            CopyBufferSize,
            FileOptions.SequentialScan);
        if (!input.CanSeek)
        {
            Fail(
                "studio-distribution.editor-image.source-special-file",
                path,
                "Editor Image inputs must be seekable regular build-output files.");
        }

        var expectedSize = input.Length;
        if (expectedSize > MaxEditorImageBytes)
        {
            Fail(
                "studio-distribution.editor-image.size-exceeded",
                path,
                "One Editor Image input exceeds the 4 GiB image limit.");
        }

        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer = new byte[CopyBufferSize];
        long total = 0;
        int read;
        while ((read = input.Read(buffer, 0, buffer.Length)) != 0)
        {
            hash.AppendData(buffer, 0, read);
            total = checked(total + read);
        }

        if (total != expectedSize || input.Length != expectedSize)
        {
            Fail(
                "studio-distribution.editor-image.source-drift",
                path,
                "An Editor Image input changed while its content identity was captured.");
        }

        return new SourceFingerprint(
            total,
            Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant());
    }

    private static void VerifySourceFingerprint(
        string path,
        SourceFingerprint expected,
        string location)
    {
        var actual = CaptureFingerprint(path);
        if (actual != expected)
        {
            Fail(
                "studio-distribution.editor-image.source-drift",
                location,
                "A build-time qualification input changed during Editor Image production.");
        }
    }

    private static void EnsureDestinationDirectory(string directory)
    {
        Directory.CreateDirectory(directory);
        EnsurePathHasNoReparsePoint(directory, "stagingRoot");
    }

    private static void TryDeleteOwnedStaging(
        string cleanupRoot,
        DirectoryInfo requestedOutput)
    {
        try
        {
            var requestedRoot = StudioDistributionPath.NormalizePublicAbsolutePath(
                requestedOutput.FullName);
            var requestedParent = Path.GetDirectoryName(requestedRoot);
            var cleanupFullPath = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(cleanupRoot));
            var cleanupParent = Path.GetDirectoryName(cleanupFullPath);
            var cleanupName = Path.GetFileName(cleanupFullPath);
            const string expectedStagingPrefix = ".asharia-editor-image-staging-";
            var isOwnedName = cleanupName.StartsWith(
                expectedStagingPrefix,
                StringComparison.Ordinal);
            if (isOwnedName
                && FileSystemPathComparer.Equals(cleanupParent, requestedParent)
                && Directory.Exists(cleanupFullPath)
                && (File.GetAttributes(cleanupFullPath) & FileAttributes.ReparsePoint) == 0)
            {
                Directory.Delete(cleanupFullPath, recursive: true);
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private static int CompareUtf8(string? left, string? right)
    {
        if (ReferenceEquals(left, right))
        {
            return 0;
        }

        if (left is null)
        {
            return -1;
        }

        if (right is null)
        {
            return 1;
        }

        return Encoding.UTF8.GetBytes(left).AsSpan()
            .SequenceCompareTo(Encoding.UTF8.GetBytes(right));
    }

    [DoesNotReturn]
    private static void Fail(string code, string location, string message) =>
        throw new ProductionFailure(Diagnostic(code, location, message));

    private static StudioEditorImageProductionDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    [GeneratedRegex("^[0-9]+\\.[0-9]+\\.[0-9]+$", RegexOptions.CultureInvariant)]
    private static partial Regex StableVersionPattern();

    [GeneratedRegex(
        "^python(?:(?:[0-9]+(?:\\.[0-9]+)*)t?)?(?:_d)?$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex PythonRuntimeDirectoryPattern();

    [GeneratedRegex(
        "^python[a-z0-9._-]*(?:\\.exe|\\.dll|\\.lib|\\.zip|\\._pth|\\.pth)$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex PythonRuntimeFilePattern();

    [GeneratedRegex(
        "^pip(?:[0-9]+(?:\\.[0-9]+)*)?(?:\\.exe|\\.dll|\\.zip|\\._pth|\\.pth)$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex PipRuntimeFilePattern();

    [GeneratedRegex(
        "^libpython[a-z0-9._-]*\\.(?:a|dll|dylib|lib|so)(?:\\.[0-9]+)*$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex LibPythonRuntimeFilePattern();

    [GeneratedRegex(
        "^(?:python\\.runtime|ironpython)(?:[.-][a-z0-9_-]+)*\\.dll$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex ManagedPythonRuntimeFilePattern();

    [GeneratedRegex(
        "^(?:ipyw?|pypy(?:3)?|jython|graalpy)[a-z0-9._-]*\\.(?:exe|dll|jar|zip)$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex AlternativePythonRuntimeFilePattern();

    [GeneratedRegex(
        "^libpypy(?:3)?[a-z0-9._-]*\\.(?:a|dll|dylib|lib|so)(?:\\.[0-9]+)*$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex LibPyPyRuntimeFilePattern();

    private sealed record SourceFingerprint(long Size, string Sha256);

    private sealed record SourceFile(string Path, SourceFingerprint Fingerprint);

    private sealed record PlannedFile(
        string SourcePath,
        string DestinationPath,
        SourceFingerprint Fingerprint);

    private sealed record ProductionInputs(
        string OutputRoot,
        string EntryPointDestination,
        string SdkAppHostTemplate,
        SourceFingerprint SdkAppHostTemplateFingerprint,
        string HostFxrVersion,
        string HostRuntimeVersion,
        string TargetFramework,
        IReadOnlyList<PlannedFile> Files);

    private sealed class ProductionFailure : Exception
    {
        public ProductionFailure(StudioEditorImageProductionDiagnostic diagnostic)
            : base(diagnostic.Message)
        {
            Diagnostic = diagnostic;
        }

        public StudioEditorImageProductionDiagnostic Diagnostic { get; }
    }
}
