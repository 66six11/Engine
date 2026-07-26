using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeArtifactInspector
{
    private const int HashBufferSize = 1024 * 1024;
    private const long MaxAssemblyBytes = 256L * 1024 * 1024;
    private const long MaxPortablePdbBytes = 256L * 1024 * 1024;
    private const long MaxDependencyFileBytes = 4L * 1024 * 1024;
    private const string ExpectedTargetFramework = "net10.0";
    private const string ExpectedRuntimeTarget =
        ".NETCoreApp,Version=v10.0";
    private static readonly Version ExpectedAssemblyVersion = new(1, 0, 0, 0);
    private static readonly byte[] ReferenceAssemblyConstructorSignature =
        [0x20, 0x00, 0x01];
    private static readonly byte[] ReferenceAssemblyAttributeValue =
        [0x01, 0x00, 0x00, 0x00];

    public static async Task<ProjectCodeArtifactInspectionResult> InspectAsync(
        ProjectCodeRawBuildOutputLease lease,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(lease);
        cancellationToken.ThrowIfCancellationRequested();

        var diagnostics = new List<ProjectCodeArtifactInspectionDiagnostic>();
        var output = lease.Output;
        var workspace = lease.WorkspaceLease.Workspace;
        var credential =
            lease.WorkspaceLease.CredentialLease.Credential;
        ValidateHandoff(output, workspace, credential, diagnostics);
        ValidateFileBudgets(output, diagnostics);
        var allowedReferences = CreateAllowedReferences(
            credential,
            diagnostics);
        if (diagnostics.Count != 0)
        {
            return ProjectCodeArtifactInspectionResult.Failure(diagnostics);
        }

        if (!await ProjectCodeSdkBuildController.IsRawOutputCurrentAsync(
                lease,
                cancellationToken).ConfigureAwait(false))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.raw-output-not-current",
                "raw-output",
                "Artifact inspection requires one current, unchanged raw SDK build output."));
            return ProjectCodeArtifactInspectionResult.Failure(diagnostics);
        }

        var files = output.Files.ToDictionary(
            file => file.RelativePath,
            StringComparer.Ordinal);
        var implementationFile =
            files[output.ImplementationAssemblyRelativePath];
        var referenceFile = files[output.ReferenceAssemblyRelativePath];
        var portablePdbFile = files[output.PortablePdbRelativePath];
        var dependencyFile = files[output.DependencyFileRelativePath];
        var systemRuntime = allowedReferences["System.Runtime"];
        var expectedIdentity = new ProjectCodeAssemblyIdentity(
            output.AssemblyName,
            ExpectedAssemblyVersion,
            "neutral",
            "null");

        var implementation = InspectAssembly(
            implementationFile,
            systemRuntime,
            diagnostics,
            "implementation",
            cancellationToken);
        var referenceAssembly = InspectAssembly(
            referenceFile,
            systemRuntime,
            diagnostics,
            "reference-assembly",
            cancellationToken);
        if (implementation is not null)
        {
            ValidateAssembly(
                implementation,
                expectedIdentity,
                output.AssemblyName,
                shouldBeReference: false,
                allowedReferences,
                diagnostics,
                "implementation");
        }

        if (referenceAssembly is not null)
        {
            ValidateAssembly(
                referenceAssembly,
                expectedIdentity,
                output.AssemblyName,
                shouldBeReference: true,
                allowedReferences,
                diagnostics,
                "reference-assembly");
        }

        if (implementation is not null
            && referenceAssembly is not null
            && !HasExactIdentity(
                implementation.Identity,
                referenceAssembly.Identity))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.assembly-pair-mismatch",
                "reference-assembly",
                "Implementation and reference assembly identities differ."));
        }

        var portablePdb = InspectPortablePdb(
            implementationFile,
            portablePdbFile,
            workspace,
            diagnostics,
            cancellationToken);
        var dependencies = InspectDependencies(
            dependencyFile,
            output.AssemblyName,
            diagnostics,
            cancellationToken);
        if (diagnostics.Count != 0
            || implementation is null
            || referenceAssembly is null
            || portablePdb is null
            || dependencies is null)
        {
            return ProjectCodeArtifactInspectionResult.Failure(diagnostics);
        }

        if (!await ProjectCodeSdkBuildController.IsRawOutputCurrentAsync(
                lease,
                cancellationToken).ConfigureAwait(false))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.raw-output-changed",
                "raw-output",
                "Raw SDK build output changed while artifact metadata was inspected."));
            return ProjectCodeArtifactInspectionResult.Failure(diagnostics);
        }

        var reportId = ComputeReportId(
            output,
            implementation,
            referenceAssembly,
            portablePdb,
            dependencies);
        return ProjectCodeArtifactInspectionResult.Success(
            new ProjectCodeArtifactMetadataReport(
                reportId,
                output.OutputId,
                output.ProjectId,
                output.WorkspaceId,
                output.CredentialId,
                output.SdkVersion,
                output.TargetFramework,
                output.AssemblyName,
                implementation,
                referenceAssembly,
                portablePdb,
                dependencies));
    }

    private static void ValidateHandoff(
        ProjectCodeRawBuildOutput output,
        ProjectCodeImplicitSdkWorkspace workspace,
        ProjectCodeBuildEnvironmentCredential credential,
        ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics)
    {
        var bindingsMatch =
            output.ProjectId == workspace.ProjectId
            && string.Equals(
                output.WorkspaceId,
                workspace.WorkspaceId,
                StringComparison.Ordinal)
            && string.Equals(
                output.CredentialId,
                workspace.CredentialId,
                StringComparison.Ordinal)
            && string.Equals(
                output.CredentialId,
                credential.CredentialId,
                StringComparison.Ordinal)
            && string.Equals(
                output.SdkVersion,
                workspace.SdkVersion,
                StringComparison.Ordinal)
            && string.Equals(
                output.SdkVersion,
                credential.SdkVersion,
                StringComparison.Ordinal)
            && string.Equals(
                output.TargetFramework,
                workspace.TargetFramework,
                StringComparison.Ordinal)
            && string.Equals(
                output.TargetFramework,
                credential.TargetFramework,
                StringComparison.Ordinal)
            && string.Equals(
                output.AssemblyName,
                workspace.AssemblyName,
                StringComparison.Ordinal)
            && string.Equals(
                output.ImplementationAssemblyRelativePath,
                workspace.OutputAssemblyRelativePath,
                StringComparison.Ordinal)
            && string.Equals(
                output.ReferenceAssemblyRelativePath,
                workspace.ReferenceAssemblyRelativePath,
                StringComparison.Ordinal)
            && string.Equals(
                output.PortablePdbRelativePath,
                workspace.PortablePdbRelativePath,
                StringComparison.Ordinal)
            && string.Equals(
                output.DependencyFileRelativePath,
                workspace.DependencyFileRelativePath,
                StringComparison.Ordinal)
            && string.Equals(
                output.OutputId,
                ProjectCodeSdkBuildController.ComputeRawOutputId(
                    workspace,
                    output.Files),
                StringComparison.Ordinal);
        if (!bindingsMatch)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.handoff-mismatch",
                "raw-output",
                "Raw build output claims do not match its workspace and build credential."));
        }

        if (!string.Equals(
                output.TargetFramework,
                ExpectedTargetFramework,
                StringComparison.Ordinal))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.target-framework-unsupported",
                "raw-output",
                "Artifact inspection supports only the fixed net10.0 project-code target."));
        }
    }

    private static void ValidateFileBudgets(
        ProjectCodeRawBuildOutput output,
        ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics)
    {
        var limits = new Dictionary<string, (string Location, long Limit)>(
            StringComparer.Ordinal)
        {
            [output.ImplementationAssemblyRelativePath] =
                ("implementation", MaxAssemblyBytes),
            [output.ReferenceAssemblyRelativePath] =
                ("reference-assembly", MaxAssemblyBytes),
            [output.PortablePdbRelativePath] =
                ("portable-pdb", MaxPortablePdbBytes),
            [output.DependencyFileRelativePath] =
                ("dependencies", MaxDependencyFileBytes),
        };
        foreach (var file in output.Files)
        {
            var limit = limits[file.RelativePath];
            if (file.Size > limit.Limit)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.artifact.file-budget-exceeded",
                    limit.Location,
                    $"Artifact exceeds the {limit.Limit}-byte inspection limit."));
            }
        }
    }

    private static IReadOnlyDictionary<string, ProjectCodeAssemblyIdentity>
        CreateAllowedReferences(
            ProjectCodeBuildEnvironmentCredential credential,
            ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics)
    {
        var allowed = new Dictionary<string, ProjectCodeAssemblyIdentity>(
            StringComparer.OrdinalIgnoreCase);
        foreach (var identity in credential.FrameworkReferences
            .Concat(
            [
                credential.RuntimeContract.Identity,
                credential.EditorContract.Identity,
            ]))
        {
            if (allowed.TryGetValue(identity.SimpleName, out var existing)
                && !HasExactIdentity(existing, identity))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.artifact.reference-policy-conflict",
                    "credential",
                    $"Build credential has conflicting identities for '{identity.SimpleName}'."));
                continue;
            }

            allowed[identity.SimpleName] = identity;
        }

        if (!allowed.ContainsKey("System.Runtime"))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.reference-policy-incomplete",
                "credential",
                "Build credential does not contain the System.Runtime reference identity."));
        }

        return allowed;
    }

    private static ProjectCodeInspectedAssembly? InspectAssembly(
        ProjectCodeRawBuildOutputFile file,
        ProjectCodeAssemblyIdentity referenceAssemblyAttributeScope,
        ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics,
        string location,
        CancellationToken cancellationToken)
    {
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            using var stream = OpenRead(file.AbsolutePath);
            if (stream.Length != file.Size)
            {
                throw new ArtifactChangedException();
            }

            using var peReader = new PEReader(
                stream,
                PEStreamOptions.PrefetchEntireImage);
            var image = peReader.GetEntireImage().GetContent();
            if (image.Length != file.Size
                || !string.Equals(
                    Hash(image.AsSpan()),
                    file.Sha256,
                    StringComparison.Ordinal))
            {
                throw new ArtifactChangedException();
            }

            var assembly = ReadAssemblyMetadata(
                peReader,
                Evidence(file),
                referenceAssemblyAttributeScope);
            cancellationToken.ThrowIfCancellationRequested();
            if (!MatchesEvidence(file))
            {
                throw new ArtifactChangedException();
            }

            return assembly;
        }
        catch (ArtifactChangedException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.file-changed",
                location,
                "Artifact no longer matches its raw build output evidence."));
        }
        catch (Exception error) when (
            error is ArgumentException
                or BadImageFormatException
                or InvalidDataException
                or InvalidOperationException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.managed-image-invalid",
                location,
                "Artifact is not one supported managed assembly metadata image."));
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.file-unreadable",
                location,
                "Artifact could not be read for metadata inspection."));
        }

        return null;
    }

    private static ProjectCodeInspectedAssembly ReadAssemblyMetadata(
        PEReader peReader,
        ProjectCodeArtifactFileEvidence file,
        ProjectCodeAssemblyIdentity referenceAssemblyAttributeScope)
    {
        if (!peReader.HasMetadata
            || peReader.PEHeaders.CorHeader is null
            || (peReader.PEHeaders.CoffHeader.Characteristics
                & Characteristics.Dll) == 0)
        {
            throw new BadImageFormatException();
        }

        var reader = peReader.GetMetadataReader(MetadataReaderOptions.None);
        if (!reader.IsAssembly)
        {
            throw new BadImageFormatException();
        }

        var module = reader.GetModuleDefinition();
        if (module.Mvid.IsNil)
        {
            throw new InvalidDataException();
        }

        var mvid = reader.GetGuid(module.Mvid);
        if (mvid == Guid.Empty)
        {
            throw new InvalidDataException();
        }

        var definition = reader.GetAssemblyDefinition();
        var references = reader.AssemblyReferences
            .Select(handle => ReadReferenceIdentity(
                reader,
                reader.GetAssemblyReference(handle)))
            .OrderBy(reference => reference.SimpleName, StringComparer.Ordinal)
            .ThenBy(reference => reference.FullName, StringComparer.Ordinal)
            .ToArray();
        if (references
            .GroupBy(
                reference => reference.SimpleName,
                StringComparer.OrdinalIgnoreCase)
            .Any(group => group.Count() != 1))
        {
            throw new InvalidDataException();
        }

        var markerCount = CountReferenceAssemblyAttributes(
            reader,
            definition,
            referenceAssemblyAttributeScope);
        if (markerCount > 1)
        {
            throw new InvalidDataException();
        }

        return new ProjectCodeInspectedAssembly(
            file,
            reader.GetString(module.Name),
            mvid,
            ReadDefinitionIdentity(reader, definition),
            Array.AsReadOnly(references),
            peReader.PEHeaders.CorHeader.Flags,
            markerCount == 1);
    }

    private static void ValidateAssembly(
        ProjectCodeInspectedAssembly assembly,
        ProjectCodeAssemblyIdentity expectedIdentity,
        string assemblyName,
        bool shouldBeReference,
        IReadOnlyDictionary<string, ProjectCodeAssemblyIdentity>
            allowedReferences,
        ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics,
        string location)
    {
        if (!HasExactIdentity(assembly.Identity, expectedIdentity))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.definition-identity-mismatch",
                location,
                $"Expected '{expectedIdentity.FullName}' but found '{assembly.Identity.FullName}'."));
        }

        if (!string.Equals(
                assembly.ModuleName,
                assemblyName + ".dll",
                StringComparison.Ordinal))
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.module-name-mismatch",
                location,
                "Managed module name does not match the generated project assembly name."));
        }

        if (assembly.ImageFlags != CorFlags.ILOnly)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.image-flags-invalid",
                location,
                "Managed project assembly must be one unsigned architecture-neutral IL-only image."));
        }

        if (assembly.IsReferenceAssembly != shouldBeReference)
        {
            diagnostics.Add(Diagnostic(
                shouldBeReference
                    ? "project-code.artifact.reference-marker-missing"
                    : "project-code.artifact.implementation-marked-reference",
                location,
                shouldBeReference
                    ? "Reference assembly lacks its exact framework ReferenceAssemblyAttribute."
                    : "Implementation assembly is marked as a reference assembly."));
        }

        foreach (var reference in assembly.References)
        {
            if (!allowedReferences.TryGetValue(
                    reference.SimpleName,
                    out var expected))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.artifact.reference-not-credentialed",
                    location,
                    $"Assembly reference '{reference.SimpleName}' is outside the build credential."));
                continue;
            }

            if (!HasExactIdentity(reference, expected))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.artifact.reference-identity-mismatch",
                    location,
                    $"Assembly reference '{reference.SimpleName}' does not match the build credential."));
            }
        }
    }

    private static ProjectCodePortablePdbMetadata? InspectPortablePdb(
        ProjectCodeRawBuildOutputFile implementation,
        ProjectCodeRawBuildOutputFile portablePdb,
        ProjectCodeImplicitSdkWorkspace workspace,
        ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics,
        CancellationToken cancellationToken)
    {
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!MatchesEvidence(portablePdb))
            {
                throw new ArtifactChangedException();
            }

            using var implementationStream = OpenRead(
                implementation.AbsolutePath);
            using var peReader = new PEReader(
                implementationStream,
                PEStreamOptions.PrefetchEntireImage);
            var codeViewPdbPath =
                "/_/Build/" + workspace.PortablePdbRelativePath;
            if (!peReader.TryOpenAssociatedPortablePdb(
                    implementation.AbsolutePath,
                    path => OpenExpectedPortablePdb(
                        path,
                        portablePdb.AbsolutePath,
                        codeViewPdbPath),
                    out var provider,
                    out var pdbPath))
            {
                throw new InvalidDataException();
            }

            var pdbProvider = provider
                ?? throw new InvalidDataException();
            using (pdbProvider)
            {
                if (pdbPath is null
                    || !IsExpectedPortablePdbPath(
                        pdbPath,
                        portablePdb.AbsolutePath,
                        codeViewPdbPath))
                {
                    throw new InvalidDataException();
                }

                var reader = pdbProvider.GetMetadataReader(
                    MetadataReaderOptions.None);
                var header = reader.DebugMetadataHeader
                    ?? throw new InvalidDataException();
                var contentId = new BlobContentId(header.Id);
                if (contentId.IsDefault || contentId.Guid == Guid.Empty)
                {
                    throw new InvalidDataException();
                }

                var documents = reader.Documents
                    .Select(handle => reader.GetString(
                        reader.GetDocument(handle).Name))
                    .OrderBy(path => path, StringComparer.Ordinal)
                    .ToArray();
                if (documents.Length == 0
                    || documents.Any(path =>
                        !IsCanonicalDocumentPath(path))
                    || documents
                        .GroupBy(path => path, StringComparer.Ordinal)
                        .Any(group => group.Count() != 1)
                    || !workspace.Sources.All(source =>
                        documents.Contains(
                            "/_/Project/" + source.ProjectRelativePath,
                            StringComparer.Ordinal)))
                {
                    throw new InvalidDataException();
                }

                cancellationToken.ThrowIfCancellationRequested();
                if (!MatchesEvidence(portablePdb))
                {
                    throw new ArtifactChangedException();
                }

                return new ProjectCodePortablePdbMetadata(
                    Evidence(portablePdb),
                    contentId.Guid,
                    contentId.Stamp,
                    Array.AsReadOnly(documents));
            }
        }
        catch (ArtifactChangedException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.file-changed",
                "portable-pdb",
                "Portable PDB no longer matches its raw build output evidence."));
        }
        catch (Exception error) when (
            error is ArgumentException
                or BadImageFormatException
                or InvalidDataException
                or InvalidOperationException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.portable-pdb-invalid",
                "portable-pdb",
                "Portable PDB is not matched to the implementation image or has non-canonical documents."));
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.file-unreadable",
                "portable-pdb",
                "Portable PDB could not be read for metadata inspection."));
        }

        return null;
    }

    private static Stream OpenExpectedPortablePdb(
        string requestedPath,
        string expectedPath,
        string codeViewPath)
    {
        if (!IsExpectedPortablePdbPath(
                requestedPath,
                expectedPath,
                codeViewPath))
        {
            return null!;
        }

        return OpenRead(expectedPath);
    }

    private static bool IsExpectedPortablePdbPath(
        string value,
        string physicalPath,
        string codeViewPath) =>
        string.Equals(value, codeViewPath, StringComparison.Ordinal)
        || IsSamePath(value, physicalPath);

    private static ProjectCodeDependencyMetadata? InspectDependencies(
        ProjectCodeRawBuildOutputFile file,
        string assemblyName,
        ICollection<ProjectCodeArtifactInspectionDiagnostic> diagnostics,
        CancellationToken cancellationToken)
    {
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            using var stream = OpenRead(file.AbsolutePath);
            if (stream.Length != file.Size
                || stream.Length > MaxDependencyFileBytes
                || stream.Length > int.MaxValue)
            {
                throw new ArtifactChangedException();
            }

            var contents = new byte[(int)stream.Length];
            stream.ReadExactly(contents);
            if (!string.Equals(
                    Hash(contents),
                    file.Sha256,
                    StringComparison.Ordinal))
            {
                throw new ArtifactChangedException();
            }

            using var document = JsonDocument.Parse(
                contents,
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = false,
                    CommentHandling = JsonCommentHandling.Disallow,
                    MaxDepth = 16,
                });
            var library = assemblyName + "/1.0.0";
            var runtimeAsset = assemblyName + ".dll";
            if (!HasExactDependencyShape(
                    document.RootElement,
                    library,
                    runtimeAsset))
            {
                throw new InvalidDataException();
            }

            cancellationToken.ThrowIfCancellationRequested();
            if (!MatchesEvidence(file))
            {
                throw new ArtifactChangedException();
            }

            return new ProjectCodeDependencyMetadata(
                Evidence(file),
                ExpectedRuntimeTarget,
                library,
                runtimeAsset);
        }
        catch (ArtifactChangedException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.file-changed",
                "dependencies",
                "Dependency manifest no longer matches its raw build output evidence."));
        }
        catch (Exception error) when (
            error is JsonException
                or InvalidDataException
                or InvalidOperationException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.dependencies-invalid",
                "dependencies",
                "Dependency manifest does not match the fixed single-project SDK output schema."));
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.artifact.file-unreadable",
                "dependencies",
                "Dependency manifest could not be read for metadata inspection."));
        }

        return null;
    }

    private static bool HasExactDependencyShape(
        JsonElement root,
        string library,
        string runtimeAsset)
    {
        if (!TryGetExactProperties(
                root,
                ["runtimeTarget", "compilationOptions", "targets", "libraries"],
                out var rootProperties)
            || !TryGetExactProperties(
                rootProperties["runtimeTarget"],
                ["name", "signature"],
                out var runtimeTarget)
            || !HasExactString(
                runtimeTarget["name"],
                ExpectedRuntimeTarget)
            || !HasExactString(runtimeTarget["signature"], "")
            || !IsEmptyObject(rootProperties["compilationOptions"])
            || !TryGetExactProperties(
                rootProperties["targets"],
                [ExpectedRuntimeTarget],
                out var targets)
            || !TryGetExactProperties(
                targets[ExpectedRuntimeTarget],
                [library],
                out var targetLibraries)
            || !TryGetExactProperties(
                targetLibraries[library],
                ["runtime"],
                out var targetLibrary)
            || !TryGetExactProperties(
                targetLibrary["runtime"],
                [runtimeAsset],
                out var runtime)
            || !IsEmptyObject(runtime[runtimeAsset])
            || !TryGetExactProperties(
                rootProperties["libraries"],
                [library],
                out var libraries)
            || !TryGetExactProperties(
                libraries[library],
                ["type", "serviceable", "sha512"],
                out var libraryMetadata))
        {
            return false;
        }

        return HasExactString(libraryMetadata["type"], "project")
            && libraryMetadata["serviceable"].ValueKind
                == JsonValueKind.False
            && HasExactString(libraryMetadata["sha512"], "");
    }

    private static bool TryGetExactProperties(
        JsonElement element,
        IReadOnlyList<string> expectedNames,
        out IReadOnlyDictionary<string, JsonElement> properties)
    {
        var values = new Dictionary<string, JsonElement>(
            StringComparer.Ordinal);
        properties = values;
        if (element.ValueKind != JsonValueKind.Object)
        {
            return false;
        }

        var expected = expectedNames.ToHashSet(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
        {
            if (!expected.Contains(property.Name)
                || !values.TryAdd(property.Name, property.Value))
            {
                return false;
            }
        }

        return values.Count == expected.Count;
    }

    private static bool IsEmptyObject(JsonElement element) =>
        element.ValueKind == JsonValueKind.Object
        && !element.EnumerateObject().Any();

    private static bool HasExactString(
        JsonElement element,
        string expected) =>
        element.ValueKind == JsonValueKind.String
        && string.Equals(
            element.GetString(),
            expected,
            StringComparison.Ordinal);

    private static ProjectCodeAssemblyIdentity ReadDefinitionIdentity(
        MetadataReader reader,
        AssemblyDefinition definition)
    {
        var publicKey = reader.GetBlobBytes(definition.PublicKey);
        var hasPublicKey =
            (definition.Flags & AssemblyFlags.PublicKey) != 0;
        if (hasPublicKey != (publicKey.Length != 0))
        {
            throw new InvalidDataException();
        }

        return new ProjectCodeAssemblyIdentity(
            reader.GetString(definition.Name),
            definition.Version,
            ReadCulture(reader, definition.Culture),
            hasPublicKey ? ComputePublicKeyToken(publicKey) : "null");
    }

    private static ProjectCodeAssemblyIdentity ReadReferenceIdentity(
        MetadataReader reader,
        AssemblyReference reference)
    {
        var keyOrToken = reader.GetBlobBytes(reference.PublicKeyOrToken);
        var hasPublicKey =
            (reference.Flags & AssemblyFlags.PublicKey) != 0;
        string token;
        if (hasPublicKey)
        {
            if (keyOrToken.Length == 0)
            {
                throw new InvalidDataException();
            }

            token = ComputePublicKeyToken(keyOrToken);
        }
        else if (keyOrToken.Length == 0)
        {
            token = "null";
        }
        else if (keyOrToken.Length == 8)
        {
            token = Convert.ToHexString(keyOrToken).ToLowerInvariant();
        }
        else
        {
            throw new InvalidDataException();
        }

        return new ProjectCodeAssemblyIdentity(
            reader.GetString(reference.Name),
            reference.Version,
            ReadCulture(reader, reference.Culture),
            token);
    }

    private static string ReadCulture(
        MetadataReader reader,
        StringHandle handle)
    {
        var culture = handle.IsNil ? "" : reader.GetString(handle);
        return string.IsNullOrEmpty(culture) ? "neutral" : culture;
    }

    private static int CountReferenceAssemblyAttributes(
        MetadataReader reader,
        AssemblyDefinition definition,
        ProjectCodeAssemblyIdentity expectedScope)
    {
        var count = 0;
        foreach (var handle in definition.GetCustomAttributes())
        {
            var attribute = reader.GetCustomAttribute(handle);
            if (IsExactReferenceAssemblyAttribute(
                    reader,
                    attribute,
                    expectedScope))
            {
                ++count;
            }
        }

        return count;
    }

    private static bool IsExactReferenceAssemblyAttribute(
        MetadataReader reader,
        CustomAttribute attribute,
        ProjectCodeAssemblyIdentity expectedScope)
    {
        if (attribute.Constructor.Kind == HandleKind.MethodDefinition)
        {
            var method = reader.GetMethodDefinition(
                (MethodDefinitionHandle)attribute.Constructor);
            var type = reader.GetTypeDefinition(method.GetDeclaringType());
            if (IsReferenceAssemblyAttributeType(
                    reader.GetString(type.Namespace),
                    reader.GetString(type.Name)))
            {
                throw new InvalidDataException();
            }

            return false;
        }

        if (attribute.Constructor.Kind != HandleKind.MemberReference)
        {
            return false;
        }

        var member = reader.GetMemberReference(
            (MemberReferenceHandle)attribute.Constructor);
        if (member.Parent.Kind != HandleKind.TypeReference)
        {
            return false;
        }

        var typeReference = reader.GetTypeReference(
            (TypeReferenceHandle)member.Parent);
        if (!IsReferenceAssemblyAttributeType(
                reader.GetString(typeReference.Namespace),
                reader.GetString(typeReference.Name)))
        {
            return false;
        }

        if (!string.Equals(
                reader.GetString(member.Name),
                ".ctor",
                StringComparison.Ordinal)
            || typeReference.ResolutionScope.Kind
                != HandleKind.AssemblyReference
            || !reader.GetBlobBytes(member.Signature)
                .SequenceEqual(ReferenceAssemblyConstructorSignature)
            || !reader.GetBlobBytes(attribute.Value)
                .SequenceEqual(ReferenceAssemblyAttributeValue))
        {
            throw new InvalidDataException();
        }

        var scope = ReadReferenceIdentity(
            reader,
            reader.GetAssemblyReference(
                (AssemblyReferenceHandle)typeReference.ResolutionScope));
        if (!HasExactIdentity(scope, expectedScope))
        {
            throw new InvalidDataException();
        }

        return true;
    }

    private static bool IsReferenceAssemblyAttributeType(
        string namespaceName,
        string typeName) =>
        string.Equals(
            namespaceName,
            "System.Runtime.CompilerServices",
            StringComparison.Ordinal)
        && string.Equals(
            typeName,
            "ReferenceAssemblyAttribute",
            StringComparison.Ordinal);

    private static bool HasExactIdentity(
        ProjectCodeAssemblyIdentity left,
        ProjectCodeAssemblyIdentity right) =>
        string.Equals(left.FullName, right.FullName, StringComparison.Ordinal);

    private static bool IsCanonicalDocumentPath(string path)
    {
        if (!string.Equals(
                path,
                path.Normalize(NormalizationForm.FormC),
                StringComparison.Ordinal)
            || path.Any(char.IsControl)
            || path.Contains('\\')
            || path.Contains(':')
            || !path.StartsWith("/", StringComparison.Ordinal)
            || !ProjectCodeSdkBuildPath.IsPortableRelativePath(path[1..]))
        {
            return false;
        }

        return path.StartsWith("/_/Project/", StringComparison.Ordinal)
            || string.Equals(
                path,
                "/_/Build/Generated/AssemblyInfo.cs",
                StringComparison.Ordinal)
            || string.Equals(
                path,
                "/_/Build/obj/.NETCoreApp,Version=v10.0.AssemblyAttributes.cs",
                StringComparison.Ordinal);
    }

    private static ProjectCodeArtifactFileEvidence Evidence(
        ProjectCodeRawBuildOutputFile file) =>
        new(file.RelativePath, file.Size, file.Sha256);

    private static FileStream OpenRead(string path) =>
        new(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            HashBufferSize,
            FileOptions.SequentialScan);

    private static bool MatchesEvidence(
        ProjectCodeRawBuildOutputFile file)
    {
        using var stream = OpenRead(file.AbsolutePath);
        return stream.Length == file.Size
            && string.Equals(
                Hash(stream),
                file.Sha256,
                StringComparison.Ordinal);
    }

    private static string Hash(Stream stream)
    {
        using var sha256 = SHA256.Create();
        return Convert.ToHexString(sha256.ComputeHash(stream))
            .ToLowerInvariant();
    }

    private static string Hash(ReadOnlySpan<byte> contents) =>
        Convert.ToHexString(SHA256.HashData(contents)).ToLowerInvariant();

    private static string ComputePublicKeyToken(byte[] publicKey)
    {
#pragma warning disable CA5350 // CLR public-key tokens are defined using SHA-1.
        var hash = SHA1.HashData(publicKey);
#pragma warning restore CA5350
        var token = hash[^8..];
        Array.Reverse(token);
        return Convert.ToHexString(token).ToLowerInvariant();
    }

    private static bool IsSamePath(string left, string right)
    {
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        return string.Equals(
            Path.GetFullPath(left),
            Path.GetFullPath(right),
            comparison);
    }

    private static string ComputeReportId(
        ProjectCodeRawBuildOutput output,
        ProjectCodeInspectedAssembly implementation,
        ProjectCodeInspectedAssembly referenceAssembly,
        ProjectCodePortablePdbMetadata portablePdb,
        ProjectCodeDependencyMetadata dependencies)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        Append(hash, "project-code-artifact-metadata-report-v1");
        Append(hash, output.OutputId);
        Append(hash, output.ProjectId.ToString("N"));
        Append(hash, output.WorkspaceId);
        Append(hash, output.CredentialId);
        Append(hash, output.SdkVersion);
        Append(hash, output.TargetFramework);
        Append(hash, output.AssemblyName);
        Append(hash, implementation);
        Append(hash, referenceAssembly);
        Append(hash, portablePdb.File);
        Append(hash, portablePdb.ContentId.ToString("N"));
        Append(hash, portablePdb.Stamp.ToString(
            System.Globalization.CultureInfo.InvariantCulture));
        foreach (var document in portablePdb.Documents)
        {
            Append(hash, document);
        }

        Append(hash, dependencies.File);
        Append(hash, dependencies.RuntimeTarget);
        Append(hash, dependencies.Library);
        Append(hash, dependencies.RuntimeAsset);
        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static void Append(
        IncrementalHash hash,
        ProjectCodeInspectedAssembly assembly)
    {
        Append(hash, assembly.File);
        Append(hash, assembly.ModuleName);
        Append(hash, assembly.Mvid.ToString("N"));
        Append(hash, assembly.Identity.FullName);
        Append(
            hash,
            ((int)assembly.ImageFlags).ToString(
                System.Globalization.CultureInfo.InvariantCulture));
        Append(hash, assembly.IsReferenceAssembly ? "reference" : "implementation");
        foreach (var reference in assembly.References)
        {
            Append(hash, reference.FullName);
        }
    }

    private static void Append(
        IncrementalHash hash,
        ProjectCodeArtifactFileEvidence file)
    {
        Append(hash, file.RelativePath);
        Span<byte> size = stackalloc byte[sizeof(long)];
        BinaryPrimitives.WriteInt64LittleEndian(size, file.Size);
        hash.AppendData(size);
        hash.AppendData(Convert.FromHexString(file.Sha256));
    }

    private static void Append(IncrementalHash hash, string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }

    private static ProjectCodeArtifactInspectionDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    private sealed class ArtifactChangedException : Exception;
}
