using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.ProjectCode;
using Xunit;
using ProjectFixture =
    Asharia.Studio.Application.Tests.ProjectCode.ProjectCodeImplicitSdkWorkspaceBuilderTests.ProjectFixture;
using SemanticEnvironmentFixture =
    Asharia.Studio.Application.Tests.ProjectCode.ProjectCodeBuildEnvironmentCredentialResolverTests.SemanticEnvironmentFixture;

namespace Asharia.Studio.Application.Tests.ProjectCode;

public sealed class ProjectCodeSdkBuildControllerTests
{
    private static readonly Guid ProjectId =
        Guid.Parse("6cae6eea-23c2-4124-9cc3-f97108bec1d0");

    [Fact]
    public async Task Real_sdk_build_publishes_clean_current_raw_output()
    {
        using var environment = new SemanticEnvironmentFixture(
            executable: true);
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource(
            "RealBuild.cs",
            "namespace Fixture; public sealed class RealBuild {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var outputRoot = OutputRoot(project, "real");
        using var controller = new ProjectCodeSdkBuildController();

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.True(result.Succeeded, Render(result));
        Assert.Equal(ProjectCodeSdkBuildOutcome.Succeeded, result.Outcome);
        Assert.Equal(
            [
                ProjectCodeSdkBuildStepKind.ProbeSdk,
                ProjectCodeSdkBuildStepKind.Restore,
                ProjectCodeSdkBuildStepKind.Build,
            ],
            result.Steps.Select(step => step.Kind).ToArray());
        Assert.All(
            result.Steps,
            step =>
            {
                Assert.Equal(0, step.ExitCode);
                Assert.False(step.OutputTruncated);
            });
        var output = result.Lease!.Output;
        Assert.Equal(workspace.Workspace.WorkspaceId, output.WorkspaceId);
        Assert.Equal(workspace.Workspace.CredentialId, output.CredentialId);
        Assert.Equal(
            ExpectedOutputPaths(workspace.Workspace).Order(),
            output.Files.Select(file => file.RelativePath).Order());
        Assert.Equal(
            output.Files.Select(file => file.RelativePath).Order(),
            Directory
                .EnumerateFiles(
                    output.AbsoluteRoot,
                    "*",
                    SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(
                        output.AbsoluteRoot,
                        path)
                    .Replace(Path.DirectorySeparatorChar, '/'))
                .Order());
        Assert.True(
            await ProjectCodeSdkBuildController
                .IsRawOutputCurrentAsync(result.Lease));
        var inspection =
            await ProjectCodeArtifactInspector.InspectAsync(result.Lease);
        Assert.True(inspection.Succeeded, Render(inspection));
        var report = inspection.Report!;
        Assert.Equal(output.OutputId, report.RawOutputId);
        Assert.Equal(output.ProjectId, report.ProjectId);
        Assert.Equal(output.WorkspaceId, report.WorkspaceId);
        Assert.Equal(output.CredentialId, report.CredentialId);
        Assert.Equal(output.SdkVersion, report.SdkVersion);
        Assert.Equal(output.TargetFramework, report.TargetFramework);
        Assert.Equal(output.AssemblyName, report.AssemblyName);
        Assert.Equal(CorFlags.ILOnly, report.Implementation.ImageFlags);
        Assert.Equal(CorFlags.ILOnly, report.ReferenceAssembly.ImageFlags);
        Assert.False(report.Implementation.IsReferenceAssembly);
        Assert.True(report.ReferenceAssembly.IsReferenceAssembly);
        Assert.Equal(
            report.Implementation.Identity.FullName,
            report.ReferenceAssembly.Identity.FullName);
        Assert.Contains(
            "/_/Project/Editor/RealBuild.cs",
            report.PortablePdb.Documents);
        Assert.Equal(
            ExpectedOutputPaths(workspace.Workspace).Order(),
            new[]
            {
                report.Implementation.File.RelativePath,
                report.ReferenceAssembly.File.RelativePath,
                report.PortablePdb.File.RelativePath,
                report.Dependencies.File.RelativePath,
            }.Order());
        using var equivalentProject = new ProjectFixture();
        equivalentProject.WriteEditorSource(
            "RealBuild.cs",
            "namespace Fixture; public sealed class RealBuild {}\n");
        var equivalentWorkspace = await CreateWorkspaceAsync(
            equivalentProject,
            credential);
        Assert.Equal(
            workspace.Workspace.WorkspaceId,
            equivalentWorkspace.Workspace.WorkspaceId);
        var equivalent = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                equivalentWorkspace,
                OutputRoot(equivalentProject, "equivalent")));
        Assert.True(equivalent.Succeeded, Render(equivalent));
        var equivalentLease = equivalent.Lease!;
        Assert.Equal(
            output.OutputId,
            equivalentLease.Output.OutputId);
        Assert.Equal(
            output.Files.Select(FileEnvelope),
            equivalentLease.Output.Files.Select(FileEnvelope));
        var equivalentInspection =
            await ProjectCodeArtifactInspector.InspectAsync(
                equivalentLease);
        Assert.True(
            equivalentInspection.Succeeded,
            Render(equivalentInspection));
        Assert.Equal(
            report.ReportId,
            equivalentInspection.Report!.ReportId);

        var publicationRoot = PublicationRoot(project, "real");
        var publication = await ProjectCodeArtifactPublisher.PublishAsync(
            result.Lease,
            publicationRoot);
        Assert.True(publication.Succeeded, Render(publication));
        var receipt = publication.Receipt!;
        Assert.Equal(report.ReportId, receipt.Report.ReportId);
        Assert.Equal(publicationRoot, receipt.AbsoluteRoot);
        Assert.Equal(
            [
                "artifact.json",
                $"bin/{output.AssemblyName}.deps.json",
                $"bin/{output.AssemblyName}.dll",
                $"bin/{output.AssemblyName}.pdb",
                $"ref/{output.AssemblyName}.dll",
            ],
            receipt.Files
                .Select(file => file.RelativePath)
                .Order(StringComparer.Ordinal)
                .ToArray());
        Assert.Equal(
            receipt.Files
                .Select(file => file.RelativePath)
                .Order(StringComparer.Ordinal),
            Directory
                .EnumerateFiles(
                    publicationRoot,
                    "*",
                    SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(publicationRoot, path)
                    .Replace(Path.DirectorySeparatorChar, '/'))
                .Order(StringComparer.Ordinal));
        foreach (var file in receipt.Files)
        {
            var contents = File.ReadAllBytes(Path.Combine(
                publicationRoot,
                file.RelativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar)));
            Assert.Equal(file.Size, contents.LongLength);
            Assert.Equal(
                file.Sha256,
                Convert.ToHexString(SHA256.HashData(contents))
                    .ToLowerInvariant());
        }

        Assert.Equal(
            report.Implementation.File.Sha256,
            receipt.Implementation.Sha256);
        Assert.Equal(
            report.ReferenceAssembly.File.Sha256,
            receipt.ReferenceAssembly.Sha256);
        Assert.Equal(
            report.PortablePdb.File.Sha256,
            receipt.PortablePdb.Sha256);
        Assert.Equal(
            report.Dependencies.File.Sha256,
            receipt.Dependencies.Sha256);
        var manifestBytes = File.ReadAllBytes(Path.Combine(
            publicationRoot,
            receipt.Manifest.RelativePath));
        using (var manifest = System.Text.Json.JsonDocument.Parse(
                   manifestBytes))
        {
            var root = manifest.RootElement;
            Assert.Equal(
                "com.asharia.project-code-artifact-publication",
                root.GetProperty("schema").GetString());
            Assert.Equal(1, root.GetProperty("schemaVersion").GetInt32());
            Assert.Equal(
                receipt.PublicationId,
                root.GetProperty("publicationId").GetString());
            Assert.Equal(
                report.ReportId,
                root.GetProperty("reportId").GetString());
        }

        AssertNoAbsolutePathLeak(
            Encoding.UTF8.GetString(manifestBytes),
            project.ProjectRoot,
            project.WorkspaceRoot,
            output.AbsoluteRoot,
            publicationRoot);
        var equivalentPublicationRoot =
            PublicationRoot(equivalentProject, "equivalent");
        var equivalentPublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                equivalentLease,
                equivalentPublicationRoot);
        Assert.True(
            equivalentPublication.Succeeded,
            Render(equivalentPublication));
        Assert.Equal(
            receipt.PublicationId,
            equivalentPublication.Receipt!.PublicationId);
        Assert.Equal(
            receipt.Files.Select(FileEnvelope),
            equivalentPublication.Receipt.Files.Select(FileEnvelope));
        Assert.Equal(
            manifestBytes,
            File.ReadAllBytes(Path.Combine(
                equivalentPublicationRoot,
                equivalentPublication.Receipt.Manifest.RelativePath)));
        var emptyIndex = await ProjectCodeModuleIndexer.IndexAsync(receipt);
        Assert.True(emptyIndex.Succeeded, Render(emptyIndex));
        Assert.Empty(emptyIndex.Report!.Entries);
        var equivalentEmptyIndex =
            await ProjectCodeModuleIndexer.IndexAsync(
                equivalentPublication.Receipt);
        Assert.True(
            equivalentEmptyIndex.Succeeded,
            Render(equivalentEmptyIndex));
        Assert.Equal(
            emptyIndex.Report.IndexId,
            equivalentEmptyIndex.Report!.IndexId);
        AssertDiagnostic(
            await ProjectCodeStagingCandidateAdmitter.AdmitAsync(receipt),
            "project-code.staging-candidate.modules-empty");

        var existingPublication =
            PublicationRoot(project, "existing-publication");
        Directory.CreateDirectory(existingPublication);
        var marker = Path.Combine(existingPublication, "preserve.txt");
        File.WriteAllText(marker, "preserve");
        var existingResult =
            await ProjectCodeArtifactPublisher.PublishAsync(
                result.Lease,
                existingPublication);
        AssertDiagnostic(
            existingResult,
            "project-code.artifact-publication.output-path-invalid");
        Assert.Equal("preserve", File.ReadAllText(marker));
        var overlapResult =
            await ProjectCodeArtifactPublisher.PublishAsync(
                result.Lease,
                Path.Combine(output.AbsoluteRoot, "publication"));
        AssertDiagnostic(
            overlapResult,
            "project-code.artifact-publication.output-overlap");
        var relativeResult =
            await ProjectCodeArtifactPublisher.PublishAsync(
                result.Lease,
                "relative-publication");
        AssertDiagnostic(
            relativeResult,
            "project-code.artifact-publication.output-path-invalid");
        using (var canceled = new CancellationTokenSource())
        {
            canceled.Cancel();
            var canceledRoot = PublicationRoot(project, "canceled");
            await Assert.ThrowsAsync<OperationCanceledException>(() =>
                ProjectCodeArtifactPublisher.PublishAsync(
                    result.Lease,
                    canceledRoot,
                    canceled.Token));
            Assert.False(Directory.Exists(canceledRoot));
        }

        var enlargedImplementationPath = output.Files
            .Single(file => file.RelativePath
                == output.ImplementationAssemblyRelativePath)
            .AbsolutePath;
        var implementationLength =
            new FileInfo(enlargedImplementationPath).Length;
        try
        {
            using (var stream = new FileStream(
                       enlargedImplementationPath,
                       FileMode.Open,
                       FileAccess.Write,
                       FileShare.None))
            {
                stream.SetLength(
                    implementationLength + (32L * 1024 * 1024));
            }

            var enlargedLease = RefreshRawOutputLease(
                workspace,
                output);
            var stagedTamperRoot =
                PublicationRoot(project, "staged-tamper");
            var stagedTamper = AddCandidateFileAsync(
                stagedTamperRoot,
                "unexpected.bin");
            var stagedTamperResult =
                await ProjectCodeArtifactPublisher.PublishAsync(
                    enlargedLease,
                    stagedTamperRoot);
            await stagedTamper;
            AssertDiagnostic(
                stagedTamperResult,
                "project-code.artifact-publication.staging-changed");
            Assert.False(Directory.Exists(stagedTamperRoot));
            AssertNoOwnedOutputCandidates(
                Path.GetDirectoryName(stagedTamperRoot)!);

            var sourceDriftRoot =
                PublicationRoot(project, "source-drift");
            var sourceDrift = MutateSourceAfterStagingBeginsAsync(
                sourceDriftRoot,
                enlargedImplementationPath);
            var sourceDriftResult =
                await ProjectCodeArtifactPublisher.PublishAsync(
                    enlargedLease,
                    sourceDriftRoot);
            await sourceDrift;
            Assert.False(sourceDriftResult.Succeeded);
            Assert.Contains(
                sourceDriftResult.Diagnostics,
                diagnostic => diagnostic.Code is
                    "project-code.artifact-publication.source-changed"
                    or "project-code.artifact-publication.raw-output-changed");
            Assert.False(Directory.Exists(sourceDriftRoot));
            AssertNoOwnedOutputCandidates(
                Path.GetDirectoryName(sourceDriftRoot)!);
        }
        finally
        {
            using var stream = new FileStream(
                enlargedImplementationPath,
                FileMode.Open,
                FileAccess.Write,
                FileShare.None);
            stream.SetLength(implementationLength);
        }

        AssertNoOwnedOutputCandidates(
            Path.GetDirectoryName(publicationRoot)!);

        using var moduleProject = new ProjectFixture();
        moduleProject.WriteEditorSource(
            "RealModule.cs",
            """
            using Asharia.Editor.Extensions;

            namespace Fixture;

            [EditorModule(
                "fixture.module",
                Scope = EditorModuleScopeKind.Project,
                Activation = EditorModuleActivationPolicy.OnDemand,
                Handover = EditorModuleHandoverPolicy.RestartRequired)]
            public sealed class RealModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            """);
        var moduleWorkspace = await CreateWorkspaceAsync(
            moduleProject,
            credential);
        var moduleBuild = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                moduleWorkspace,
                OutputRoot(moduleProject, "module")));
        Assert.True(moduleBuild.Succeeded, Render(moduleBuild));
        var moduleOutput = moduleBuild.Lease!.Output;
        var modulePublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                moduleBuild.Lease,
                PublicationRoot(moduleProject, "module"));
        Assert.True(
            modulePublication.Succeeded,
            Render(modulePublication));
        var modulePublicationReceipt = modulePublication.Receipt!;
        var moduleIndex = await ProjectCodeModuleIndexer.IndexAsync(
            modulePublicationReceipt);
        Assert.True(moduleIndex.Succeeded, Render(moduleIndex));
        var moduleEntry = Assert.Single(moduleIndex.Report!.Entries);
        Assert.Equal(
            $"project:{ProjectId:D}:editor",
            moduleEntry.DefinitionId.Assembly.Package.Value);
        Assert.Equal(
            moduleOutput.AssemblyName,
            moduleEntry.DefinitionId.Assembly.Assembly.Value);
        Assert.Equal(
            "fixture.module",
            moduleEntry.DefinitionId.Module.Value);
        Assert.Equal(
            EditorModuleScopeKind.Project,
            moduleEntry.DefinitionId.Scope);
        Assert.Equal("Fixture.RealModule", moduleEntry.TypeName);
        Assert.Equal(
            EditorModuleActivationPolicy.OnDemand,
            moduleEntry.Activation);
        Assert.Equal(
            EditorModuleHandoverPolicy.RestartRequired,
            moduleEntry.Handover);
        var moduleCandidate =
            await ProjectCodeStagingCandidateAdmitter.AdmitAsync(
                modulePublicationReceipt);
        Assert.True(moduleCandidate.Succeeded, Render(moduleCandidate));
        var moduleCandidateReceipt = moduleCandidate.Receipt!;
        Assert.Equal(
            modulePublicationReceipt.PublicationId,
            moduleCandidateReceipt.Publication.PublicationId);
        Assert.Equal(
            moduleIndex.Report.IndexId,
            moduleCandidateReceipt.ModuleIndex.IndexId);
        Assert.Equal(
            ProjectId,
            moduleCandidateReceipt.ModuleIndex.ProjectId);
        Assert.Equal(
            moduleOutput.AssemblyName,
            moduleCandidateReceipt.ModuleIndex.AssemblyName);
        Assert.True(
            await ProjectCodeStagingCandidateAdmitter
                .IsCandidateCurrentAsync(moduleCandidateReceipt));

        var equivalentModulePublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                moduleBuild.Lease!,
                PublicationRoot(moduleProject, "module-equivalent"));
        Assert.True(
            equivalentModulePublication.Succeeded,
            Render(equivalentModulePublication));
        var equivalentModuleCandidate =
            await ProjectCodeStagingCandidateAdmitter.AdmitAsync(
                equivalentModulePublication.Receipt!);
        Assert.True(
            equivalentModuleCandidate.Succeeded,
            Render(equivalentModuleCandidate));
        Assert.Equal(
            moduleCandidateReceipt.CandidateId,
            equivalentModuleCandidate.Receipt!.CandidateId);
        Assert.Equal(
            moduleCandidateReceipt.ModuleIndex.Entries.ToArray(),
            equivalentModuleCandidate.Receipt.ModuleIndex.Entries.ToArray());

        var moduleImplementation = moduleOutput.Files.Single(file =>
            file.RelativePath
                == moduleOutput.ImplementationAssemblyRelativePath);
        var moduleReference = moduleOutput.Files.Single(file =>
            file.RelativePath
                == moduleOutput.ReferenceAssemblyRelativePath);
        var moduleImplementationBytes =
            File.ReadAllBytes(moduleImplementation.AbsolutePath);
        var moduleReferenceBytes =
            File.ReadAllBytes(moduleReference.AbsolutePath);
        var mismatchedPublication = await PublishMutationAsync(
            moduleWorkspace,
            moduleOutput,
            new Dictionary<string, byte[]>
            {
                [moduleImplementation.RelativePath] = ReplaceUtf8(
                    moduleImplementationBytes,
                    "fixture.module",
                    "fixture.modulf"),
            },
            PublicationRoot(moduleProject, "module-mismatch"));
        Assert.True(
            mismatchedPublication.Succeeded,
            Render(mismatchedPublication));
        AssertDiagnostic(
            await ProjectCodeModuleIndexer.IndexAsync(
                mismatchedPublication.Receipt!),
            "project-code.module-index.assembly-surface-mismatch");

        var invalidAttributePublication = await PublishMutationAsync(
            moduleWorkspace,
            moduleOutput,
            new Dictionary<string, byte[]>
            {
                [moduleImplementation.RelativePath] = ReplaceUtf8(
                    moduleImplementationBytes,
                    "fixture.module",
                    "Fixture.module"),
                [moduleReference.RelativePath] = ReplaceUtf8(
                    moduleReferenceBytes,
                    "fixture.module",
                    "Fixture.module"),
            },
            PublicationRoot(moduleProject, "module-attribute-invalid"));
        Assert.True(
            invalidAttributePublication.Succeeded,
            Render(invalidAttributePublication));
        AssertDiagnostic(
            await ProjectCodeModuleIndexer.IndexAsync(
                invalidAttributePublication.Receipt!),
            "project-code.module-index.attribute-invalid");

        var unexpectedPublicationFile = Path.Combine(
            modulePublication.Receipt!.AbsoluteRoot,
            "unexpected.bin");
        try
        {
            File.WriteAllText(unexpectedPublicationFile, "drift");
            Assert.False(
                await ProjectCodeStagingCandidateAdmitter
                    .IsCandidateCurrentAsync(moduleCandidateReceipt));
            AssertDiagnostic(
                await ProjectCodeStagingCandidateAdmitter.AdmitAsync(
                    modulePublicationReceipt),
                "project-code.module-index.publication-not-current");
            AssertDiagnostic(
                await ProjectCodeModuleIndexer.IndexAsync(
                    modulePublicationReceipt),
                "project-code.module-index.publication-not-current");
        }
        finally
        {
            File.Delete(unexpectedPublicationFile);
        }

        using var invalidModuleProject = new ProjectFixture();
        invalidModuleProject.WriteEditorSource(
            "InvalidModules.cs",
            """
            using Asharia.Editor.Extensions;

            namespace Fixture;

            [EditorModule("fixture.wrong-base")]
            public sealed class WrongBase
            {
            }

            [EditorModule("fixture.abstract")]
            public abstract class AbstractModule : EditorModule
            {
            }

            [EditorModule("fixture.generic")]
            public sealed class GenericModule<T> : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule("fixture.unsealed")]
            public class UnsealedModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule("fixture.no-ctor")]
            public sealed class NoDefaultConstructorModule : EditorModule
            {
                public NoDefaultConstructorModule(int value)
                {
                }

                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            public sealed class MissingAttributeModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule("fixture.duplicate")]
            public sealed class FirstDuplicateModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule("fixture.duplicate")]
            public sealed class SecondDuplicateModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            """);
        var invalidModuleWorkspace = await CreateWorkspaceAsync(
            invalidModuleProject,
            credential);
        var invalidModuleBuild = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                invalidModuleWorkspace,
                OutputRoot(invalidModuleProject, "invalid-modules")));
        Assert.True(
            invalidModuleBuild.Succeeded,
            Render(invalidModuleBuild));
        var invalidModulePublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                invalidModuleBuild.Lease!,
                PublicationRoot(
                    invalidModuleProject,
                    "invalid-modules"));
        Assert.True(
            invalidModulePublication.Succeeded,
            Render(invalidModulePublication));
        var invalidModuleIndex = await ProjectCodeModuleIndexer.IndexAsync(
            invalidModulePublication.Receipt!);
        Assert.False(invalidModuleIndex.Succeeded);
        Assert.Contains(
            invalidModuleIndex.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.module-index.type-shape-invalid");
        Assert.Contains(
            invalidModuleIndex.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.module-index.attribute-missing");
        Assert.Contains(
            invalidModuleIndex.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.module-index.entry-duplicate");

        var mutableOutput = equivalentLease.Output;
        var dependencyPath = mutableOutput.Files
            .Single(file => file.RelativePath
                == mutableOutput.DependencyFileRelativePath)
            .AbsolutePath;
        var dependencyContents = File.ReadAllBytes(dependencyPath);
        var dependencyText = Encoding.UTF8.GetString(dependencyContents);
        const string CompilationOptions = "\"compilationOptions\": {}";
        Assert.Contains(CompilationOptions, dependencyText);
        foreach (var invalidDependencies in new[]
        {
            Encoding.UTF8.GetBytes("{}\n"),
            Encoding.UTF8.GetBytes(dependencyText.Replace(
                CompilationOptions,
                CompilationOptions + ",\n  \"unexpected\": {}",
                StringComparison.Ordinal)),
            Encoding.UTF8.GetBytes(dependencyText.Replace(
                CompilationOptions,
                CompilationOptions + ",\n  \"compilationOptions\": {}",
                StringComparison.Ordinal)),
        })
        {
            AssertDiagnostic(
                await InspectMutationAsync(
                    equivalentWorkspace,
                    mutableOutput,
                    mutableOutput.DependencyFileRelativePath,
                    invalidDependencies),
                "project-code.artifact.dependencies-invalid");
        }

        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.DependencyFileRelativePath,
                new byte[(4 * 1024 * 1024) + 1]),
            "project-code.artifact.file-budget-exceeded");

        var portablePdbPath = mutableOutput.Files
            .Single(file => file.RelativePath
                == mutableOutput.PortablePdbRelativePath)
            .AbsolutePath;
        var portablePdbContents = File.ReadAllBytes(portablePdbPath);
        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.PortablePdbRelativePath,
                [0x00, 0x01, 0x02]),
            "project-code.artifact.portable-pdb-invalid");
        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.PortablePdbRelativePath,
                ReplaceUtf8(
                    portablePdbContents,
                    "Project",
                    "C:Drive")),
            "project-code.artifact.portable-pdb-invalid");

        var implementationPath = mutableOutput.Files
            .Single(file => file.RelativePath
                == mutableOutput.ImplementationAssemblyRelativePath)
            .AbsolutePath;
        var implementationContents = File.ReadAllBytes(implementationPath);
        var wrongAssemblyName = "B" + mutableOutput.AssemblyName[1..];
        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.ImplementationAssemblyRelativePath,
                ReplaceUtf8(
                    implementationContents,
                    mutableOutput.AssemblyName,
                    wrongAssemblyName)),
            "project-code.artifact.definition-identity-mismatch");
        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.ImplementationAssemblyRelativePath,
                WithCorFlags(implementationContents, flags: 0x00000003)),
            "project-code.artifact.image-flags-invalid");
        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.ImplementationAssemblyRelativePath,
                ReplaceUtf8(
                    implementationContents,
                    "System.Runtime",
                    "System.Runtimf")),
            "project-code.artifact.reference-not-credentialed");

        AssertDiagnostic(
            await InspectMutationAsync(
                equivalentWorkspace,
                mutableOutput,
                mutableOutput.ReferenceAssemblyRelativePath,
                implementationContents),
            "project-code.artifact.reference-marker-missing");

        Assert.DoesNotContain(
            output.AbsoluteRoot,
            report.ToString(),
            StringComparison.OrdinalIgnoreCase);
        AssertNoAbsolutePathLeak(
            result,
            project.ProjectRoot,
            workspace.Workspace.AbsoluteRoot,
            outputRoot,
            Path.GetDirectoryName(
                credential.Credential.DotnetExecutable)!);
        AssertNoOwnedOutputCandidates(Path.GetDirectoryName(outputRoot)!);
    }

    [Fact]
    public async Task Artifact_inspector_rejects_non_managed_raw_output()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource(
            "Invalid.cs",
            "public sealed class Invalid {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        using var controller = new ProjectCodeSdkBuildController(
            SuccessfulRunner());
        var build = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                workspace,
                OutputRoot(project, "invalid-artifacts")));
        Assert.True(build.Succeeded, Render(build));
        var lease = build.Lease!;

        var inspection =
            await ProjectCodeArtifactInspector.InspectAsync(lease);

        Assert.False(inspection.Succeeded);
        Assert.Null(inspection.Report);
        Assert.Contains(
            inspection.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.managed-image-invalid");
        Assert.DoesNotContain(
            inspection.Diagnostics,
            diagnostic => diagnostic.Message.Contains(
                lease.Output.AbsoluteRoot,
                StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task Process_policy_uses_exact_dotnet_arguments_and_environment()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Policy.cs", "public sealed class Policy {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var runner = SuccessfulRunner();
        using var controller = new ProjectCodeSdkBuildController(runner);

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                workspace,
                OutputRoot(project, "policy")));

        Assert.True(result.Succeeded, Render(result));
        Assert.Equal(3, runner.Requests.Count);
        Assert.All(
            runner.Requests,
            request =>
            {
                Assert.Equal(
                    Path.GetFileName(
                        credential.Credential.DotnetExecutable),
                    Path.GetFileName(request.Executable));
                Assert.NotEqual(
                    credential.Credential.DotnetExecutable,
                    request.Executable);
                Assert.StartsWith(
                    request.WorkingDirectory,
                    request.Executable,
                    StringComparison.OrdinalIgnoreCase);
                Assert.True(Path.IsPathFullyQualified(
                    request.WorkingDirectory));
                Assert.DoesNotContain("PATH", request.Environment.Keys);
                Assert.All(
                    new[]
                    {
                        "HOME",
                        "APPDATA",
                        "LOCALAPPDATA",
                        "USERPROFILE",
                        "PROGRAMFILES",
                        "PROGRAMFILES(X86)",
                        "PROGRAMDATA",
                    },
                    name => Assert.StartsWith(
                        request.WorkingDirectory,
                        request.Environment[name],
                        StringComparison.OrdinalIgnoreCase));
                Assert.Equal(
                    request.Executable,
                    request.Environment["DOTNET_HOST_PATH"]);
                Assert.Equal(
                    Path.GetDirectoryName(request.Executable),
                    request.Environment["DOTNET_ROOT"]);
                Assert.StartsWith(
                    request.WorkingDirectory,
                    request.Environment["DOTNET_CLI_HOME"],
                    StringComparison.OrdinalIgnoreCase);
                Assert.All(
                    request.Arguments,
                    argument =>
                    {
                        Assert.DoesNotContain(project.ProjectRoot, argument);
                        Assert.DoesNotContain(
                            workspace.Workspace.AbsoluteRoot,
                            argument);
                    });
            });
        Assert.Single(
            runner.Requests
                .Select(request => request.Executable)
                .Distinct(StringComparer.OrdinalIgnoreCase));
        Assert.All(
            runner.Requests,
            request => Assert.False(
                Directory.Exists(request.WorkingDirectory)));
        Assert.Equal(["--version"], runner.Requests[0].Arguments);
        Assert.Equal("restore", runner.Requests[1].Arguments[0]);
        Assert.Contains("--configfile", runner.Requests[1].Arguments);
        Assert.Contains("--disable-build-servers", runner.Requests[1].Arguments);
        Assert.Equal("build", runner.Requests[2].Arguments[0]);
        Assert.Contains("--no-restore", runner.Requests[2].Arguments);
        Assert.DoesNotContain("--no-incremental", runner.Requests[2].Arguments);
    }

    [Fact]
    public async Task Real_compiler_failure_does_not_publish_output()
    {
        using var environment = new SemanticEnvironmentFixture(
            executable: true);
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource(
            "Broken.cs",
            "public sealed class Broken { this is not C#; }\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var outputRoot = OutputRoot(project, "compiler-failure");
        using var controller = new ProjectCodeSdkBuildController();

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.False(result.Succeeded);
        Assert.Equal(ProjectCodeSdkBuildOutcome.Failed, result.Outcome);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == "project-code.build.step-failed"
                && diagnostic.Step == ProjectCodeSdkBuildStepKind.Build);
        Assert.False(Directory.Exists(outputRoot));
        Assert.True(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(workspace));
        AssertNoOwnedOutputCandidates(Path.GetDirectoryName(outputRoot)!);
    }

    [Fact]
    public async Task Input_drift_fails_before_process_launch()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Drift.cs", "public sealed class Drift {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        project.WriteEditorSource(
            "Drift.cs",
            "public sealed class Changed {}\n");
        var runner = SuccessfulRunner();
        var outputRoot = OutputRoot(project, "input-drift");
        using var controller = new ProjectCodeSdkBuildController(runner);

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build.input-not-current");
        Assert.Empty(runner.Requests);
        Assert.False(Directory.Exists(outputRoot));
    }

    [Theory]
    [InlineData(
        (int)ProjectCodeBuildProcessOutcome.LaunchFailed,
        (int)ProjectCodeSdkBuildOutcome.Failed,
        "project-code.build.process-launch-failed")]
    [InlineData(
        (int)ProjectCodeBuildProcessOutcome.TimedOut,
        (int)ProjectCodeSdkBuildOutcome.TimedOut,
        "project-code.build.step-timeout")]
    [InlineData(
        (int)ProjectCodeBuildProcessOutcome.OutputLimitExceeded,
        (int)ProjectCodeSdkBuildOutcome.Failed,
        "project-code.build.output-limit-exceeded")]
    [InlineData(
        (int)ProjectCodeBuildProcessOutcome.TerminationFailed,
        (int)ProjectCodeSdkBuildOutcome.Failed,
        "project-code.build.process-termination-failed")]
    [InlineData(
        (int)ProjectCodeBuildProcessOutcome.CaptureFailed,
        (int)ProjectCodeSdkBuildOutcome.Failed,
        "project-code.build.output-capture-failed")]
    public async Task Process_failures_return_typed_evidence_and_cleanup(
        int processOutcomeValue,
        int expectedOutcomeValue,
        string expectedCode)
    {
        var processOutcome =
            (ProjectCodeBuildProcessOutcome)processOutcomeValue;
        var expectedOutcome =
            (ProjectCodeSdkBuildOutcome)expectedOutcomeValue;
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Failure.cs", "public sealed class Failure {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var runner = new CallbackProcessRunner((request, _) =>
            Task.FromResult(ProcessResult(
                processOutcome,
                processOutcome == ProjectCodeBuildProcessOutcome.LaunchFailed
                    ? null
                    : -1,
                outputTruncated: processOutcome
                    == ProjectCodeBuildProcessOutcome.OutputLimitExceeded,
                terminationConfirmed: processOutcome
                    != ProjectCodeBuildProcessOutcome.TerminationFailed)));
        var outputRoot = OutputRoot(project, "process-failure");
        using var controller = new ProjectCodeSdkBuildController(runner);

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.False(result.Succeeded);
        Assert.Equal(expectedOutcome, result.Outcome);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == expectedCode);
        Assert.False(Directory.Exists(outputRoot));
        AssertNoOwnedOutputCandidates(Path.GetDirectoryName(outputRoot)!);
    }

    [Fact]
    public async Task Wrong_sdk_probe_fails_without_restore_or_output()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Sdk.cs", "public sealed class Sdk {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var runner = new CallbackProcessRunner((_, _) =>
            Task.FromResult(ProcessResult(
                ProjectCodeBuildProcessOutcome.Exited,
                0,
                standardOutput: "0.0.0\n")));
        var outputRoot = OutputRoot(project, "wrong-sdk");
        using var controller = new ProjectCodeSdkBuildController(runner);

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build.sdk-version-mismatch");
        Assert.Single(runner.Requests);
        Assert.False(Directory.Exists(outputRoot));
    }

    [Fact]
    public async Task Caller_cancellation_is_typed_and_does_not_launch()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Cancel.cs", "public sealed class Cancel {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var runner = SuccessfulRunner();
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        using var controller = new ProjectCodeSdkBuildController(runner);
        var outputRoot = OutputRoot(project, "canceled");

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot),
            cancellation.Token);

        Assert.Equal(ProjectCodeSdkBuildOutcome.Canceled, result.Outcome);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == "project-code.build.canceled");
        Assert.Empty(runner.Requests);
        Assert.False(Directory.Exists(outputRoot));
    }

    [Fact]
    public async Task Newer_same_project_invocation_supersedes_inflight_build()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource(
            "Supersede.cs",
            "public sealed class Supersede {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var firstProbeStarted =
            new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var probeCount = 0;
        var runner = new CallbackProcessRunner(
            async (request, cancellationToken) =>
            {
                if (request.Kind == ProjectCodeSdkBuildStepKind.ProbeSdk
                    && Interlocked.Increment(ref probeCount) == 1)
                {
                    firstProbeStarted.TrySetResult();
                    try
                    {
                        await Task.Delay(
                            Timeout.InfiniteTimeSpan,
                            cancellationToken);
                    }
                    catch (OperationCanceledException)
                    {
                    }

                    return ProcessResult(
                        ProjectCodeBuildProcessOutcome.Canceled,
                        null);
                }

                return SuccessfulProcessResult(request);
            });
        using var controller = new ProjectCodeSdkBuildController(runner);
        var firstOutput = OutputRoot(project, "superseded");
        var secondOutput = OutputRoot(project, "latest");

        var firstTask = controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, firstOutput));
        await firstProbeStarted.Task.WaitAsync(TimeSpan.FromSeconds(30));
        var secondTask = controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, secondOutput));
        var results = await Task.WhenAll(firstTask, secondTask);

        Assert.Equal(
            ProjectCodeSdkBuildOutcome.Superseded,
            results[0].Outcome);
        Assert.Contains(
            results[0].Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build.superseded");
        Assert.True(results[1].Succeeded, Render(results[1]));
        Assert.False(Directory.Exists(firstOutput));
        Assert.True(Directory.Exists(secondOutput));
    }

    [Fact]
    public async Task Different_projects_execute_independently()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var firstProject = new ProjectFixture();
        using var secondProject = new ProjectFixture();
        firstProject.WriteEditorSource("First.cs", "public sealed class First {}\n");
        secondProject.WriteEditorSource(
            "Second.cs",
            "public sealed class Second {}\n");
        var firstWorkspace = await CreateWorkspaceAsync(
            firstProject,
            credential,
            ProjectId);
        var secondWorkspace = await CreateWorkspaceAsync(
            secondProject,
            credential,
            Guid.Parse("06715842-bc32-4149-a8c4-b69592712978"));
        var bothProbesStarted =
            new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var probeCount = 0;
        var runner = new CallbackProcessRunner(
            async (request, cancellationToken) =>
            {
                if (request.Kind == ProjectCodeSdkBuildStepKind.ProbeSdk)
                {
                    if (Interlocked.Increment(ref probeCount) == 2)
                    {
                        bothProbesStarted.TrySetResult();
                    }

                    await bothProbesStarted.Task.WaitAsync(
                        TimeSpan.FromSeconds(30),
                        cancellationToken);
                }

                return SuccessfulProcessResult(request);
            });
        using var controller = new ProjectCodeSdkBuildController(runner);

        var firstTask = controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                firstWorkspace,
                OutputRoot(firstProject, "parallel-first")));
        var secondTask = controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                secondWorkspace,
                OutputRoot(secondProject, "parallel-second")));
        var results = await Task.WhenAll(firstTask, secondTask);

        Assert.All(results, result => Assert.True(result.Succeeded, Render(result)));
        Assert.Equal(2, probeCount);
    }

    [Theory]
    [InlineData("execution")]
    [InlineData("sdk")]
    [InlineData("source")]
    [InlineData("credential")]
    public async Task Drift_during_process_step_fails_before_publication(
        string mutation)
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Current.cs", "public sealed class Current {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var runner = new CallbackProcessRunner((request, _) =>
        {
            if (request.Kind == ProjectCodeSdkBuildStepKind.ProbeSdk)
            {
                if (mutation == "execution")
                {
                    File.AppendAllText(
                        Path.Combine(request.WorkingDirectory, "project.csproj"),
                        "drift");
                }
                else if (mutation == "sdk")
                {
                    File.WriteAllText(
                        Path.Combine(
                            request.Environment["DOTNET_ROOT"],
                            "rogue.txt"),
                        "drift");
                }
                else if (mutation == "source")
                {
                    project.WriteEditorSource(
                        "Current.cs",
                        "public sealed class Changed {}\n");
                }
                else
                {
                    credential.Revoke();
                }
            }

            return Task.FromResult(SuccessfulProcessResult(request));
        });
        var outputRoot = OutputRoot(project, "step-drift");
        using var controller = new ProjectCodeSdkBuildController(runner);

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == (mutation switch
            {
                "execution" =>
                    "project-code.build.execution-input-drift",
                "sdk" =>
                    "project-code.build.sdk-execution-drift",
                _ => "project-code.build.input-drift",
            }));
        Assert.False(Directory.Exists(outputRoot));
    }

    [Fact]
    public async Task Existing_output_is_preserved_without_process_launch()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Existing.cs", "public sealed class Existing {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var outputRoot = OutputRoot(project, "existing");
        Directory.CreateDirectory(outputRoot);
        var marker = Path.Combine(outputRoot, "preserve.txt");
        File.WriteAllText(marker, "preserve");
        var runner = SuccessfulRunner();
        using var controller = new ProjectCodeSdkBuildController(runner);

        var result = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(workspace, outputRoot));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.build.output-path-invalid");
        Assert.Equal("preserve", File.ReadAllText(marker));
        Assert.Empty(runner.Requests);
    }

    [Fact]
    public async Task Raw_output_drift_and_revocation_fail_current_check()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Output.cs", "public sealed class Output {}\n");
        var workspace = await CreateWorkspaceAsync(project, credential);
        var runner = SuccessfulRunner();
        using var controller = new ProjectCodeSdkBuildController(runner);
        var first = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                workspace,
                OutputRoot(project, "output-drift")));
        Assert.True(first.Succeeded, Render(first));
        var firstLease = first.Lease!;
        var invalidPublicationRoot =
            PublicationRoot(project, "invalid-metadata");
        var invalidPublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                firstLease,
                invalidPublicationRoot);
        Assert.False(invalidPublication.Succeeded);
        Assert.Contains(
            invalidPublication.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.managed-image-invalid");
        Assert.False(Directory.Exists(invalidPublicationRoot));
        File.AppendAllText(
            firstLease.Output.Files[0].AbsolutePath,
            "drift");

        Assert.False(
            await ProjectCodeSdkBuildController
                .IsRawOutputCurrentAsync(firstLease));
        var driftedInspection =
            await ProjectCodeArtifactInspector.InspectAsync(firstLease);
        Assert.Contains(
            driftedInspection.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.raw-output-not-current");
        var driftedPublicationRoot =
            PublicationRoot(project, "drifted");
        var driftedPublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                firstLease,
                driftedPublicationRoot);
        Assert.Contains(
            driftedPublication.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.raw-output-not-current");
        Assert.False(Directory.Exists(driftedPublicationRoot));

        var second = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                workspace,
                OutputRoot(project, "output-revoke")));
        Assert.True(second.Succeeded, Render(second));
        var secondLease = second.Lease!;
        secondLease.Revoke();
        Assert.False(secondLease.IsCurrent);
        Assert.False(
            await ProjectCodeSdkBuildController
                .IsRawOutputCurrentAsync(secondLease));
        var revokedInspection =
            await ProjectCodeArtifactInspector.InspectAsync(secondLease);
        Assert.Contains(
            revokedInspection.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.raw-output-not-current");
        var revokedPublicationRoot =
            PublicationRoot(project, "revoked");
        var revokedPublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                secondLease,
                revokedPublicationRoot);
        Assert.Contains(
            revokedPublication.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.raw-output-not-current");
        Assert.False(Directory.Exists(revokedPublicationRoot));
        Assert.True(workspace.IsCurrent);
    }

    private static async Task<ProjectCodeBuildEnvironmentCredentialLease>
        CreateCredentialAsync(SemanticEnvironmentFixture environment)
    {
        var managed = await environment.LoadManagedLeaseAsync();
        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managed);
        Assert.True(
            result.Succeeded,
            string.Join(
                Environment.NewLine,
                result.Diagnostics.Select(diagnostic =>
                    $"{diagnostic.Code}: {diagnostic.Message}")));
        return result.Lease!;
    }

    private static async Task<ProjectCodeImplicitSdkWorkspaceLease>
        CreateWorkspaceAsync(
            ProjectFixture project,
            ProjectCodeBuildEnvironmentCredentialLease credential,
            Guid? projectId = null)
    {
        var result = await ProjectCodeImplicitSdkWorkspaceBuilder.CreateAsync(
            new ProjectCodeImplicitSdkWorkspaceRequest(
                project.ProjectRoot,
                projectId ?? ProjectId,
                project.WorkspaceRoot,
                credential));
        Assert.True(
            result.Succeeded && result.Lease is not null,
            string.Join(
                Environment.NewLine,
                result.Diagnostics.Select(diagnostic =>
                    $"{diagnostic.Code}: {diagnostic.Message}")));
        return result.Lease;
    }

    private static CallbackProcessRunner SuccessfulRunner() =>
        new((request, _) =>
            Task.FromResult(SuccessfulProcessResult(request)));

    private static ProjectCodeBuildProcessResult SuccessfulProcessResult(
        ProjectCodeBuildProcessRequest request)
    {
        if (request.Kind == ProjectCodeSdkBuildStepKind.Build)
        {
            WriteFakeOutputs(request.WorkingDirectory);
        }

        var standardOutput =
            request.Kind == ProjectCodeSdkBuildStepKind.ProbeSdk
                ? ReadSdkVersion(request.WorkingDirectory) + "\n"
                : string.Empty;
        return ProcessResult(
            ProjectCodeBuildProcessOutcome.Exited,
            0,
            standardOutput);
    }

    private static ProjectCodeBuildProcessResult ProcessResult(
        ProjectCodeBuildProcessOutcome outcome,
        int? exitCode,
        string standardOutput = "",
        string standardError = "",
        bool outputTruncated = false,
        bool terminationConfirmed = true) =>
        new(
            outcome,
            exitCode,
            TimeSpan.FromMilliseconds(1),
            standardOutput,
            standardError,
            outputTruncated,
            terminationConfirmed);

    private static void WriteFakeOutputs(string workingDirectory)
    {
        var project = XDocument.Load(Path.Combine(
            workingDirectory,
            "project.csproj"));
        var assemblyName = project
            .Descendants()
            .Single(element => element.Name.LocalName == "AssemblyName")
            .Value;
        foreach (var pair in new Dictionary<string, byte[]>
        {
            [$"out/{assemblyName}.dll"] = Encoding.UTF8.GetBytes("implementation"),
            [$"obj/ref/{assemblyName}.dll"] = Encoding.UTF8.GetBytes("reference"),
            [$"out/{assemblyName}.pdb"] = Encoding.UTF8.GetBytes("portable-pdb"),
            [$"out/{assemblyName}.deps.json"] = Encoding.UTF8.GetBytes("{}\n"),
        })
        {
            var path = Path.Combine(
                workingDirectory,
                pair.Key.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(path, pair.Value);
        }
    }

    private static string ReadSdkVersion(string workingDirectory)
    {
        using var document = System.Text.Json.JsonDocument.Parse(
            File.ReadAllBytes(Path.Combine(workingDirectory, "global.json")));
        return document.RootElement
            .GetProperty("sdk")
            .GetProperty("version")
            .GetString()!;
    }

    private static IReadOnlyList<string> ExpectedOutputPaths(
        ProjectCodeImplicitSdkWorkspace workspace) =>
        [
            workspace.OutputAssemblyRelativePath,
            workspace.ReferenceAssemblyRelativePath,
            workspace.PortablePdbRelativePath,
            workspace.DependencyFileRelativePath,
        ];

    private static string OutputRoot(
        ProjectFixture project,
        string name) =>
        Path.Combine(
            Path.GetDirectoryName(project.WorkspaceRoot)!,
            "raw-" + name);

    private static string PublicationRoot(
        ProjectFixture project,
        string name) =>
        Path.Combine(
            Path.GetDirectoryName(project.WorkspaceRoot)!,
            "publication-" + name);

    private static string FileEnvelope(
        ProjectCodeRawBuildOutputFile file) =>
        $"{file.RelativePath}|{file.Size}|{file.Sha256}";

    private static string FileEnvelope(
        ProjectCodeArtifactFileEvidence file) =>
        $"{file.RelativePath}|{file.Size}|{file.Sha256}";

    private static async Task<ProjectCodeArtifactInspectionResult>
        InspectMutationAsync(
            ProjectCodeImplicitSdkWorkspaceLease workspaceLease,
            ProjectCodeRawBuildOutput output,
            string relativePath,
            byte[] replacement)
    {
        var path = output.Files
            .Single(file => file.RelativePath == relativePath)
            .AbsolutePath;
        var original = File.ReadAllBytes(path);
        try
        {
            File.WriteAllBytes(path, replacement);
            return await ProjectCodeArtifactInspector.InspectAsync(
                RefreshRawOutputLease(workspaceLease, output));
        }
        finally
        {
            File.WriteAllBytes(path, original);
        }
    }

    private static async Task<ProjectCodeArtifactPublicationResult>
        PublishMutationAsync(
            ProjectCodeImplicitSdkWorkspaceLease workspaceLease,
            ProjectCodeRawBuildOutput output,
            IReadOnlyDictionary<string, byte[]> replacements,
            string publicationRoot)
    {
        var originals = replacements.Keys.ToDictionary(
            relativePath => relativePath,
            relativePath => File.ReadAllBytes(
                output.Files
                    .Single(file =>
                        file.RelativePath == relativePath)
                    .AbsolutePath),
            StringComparer.Ordinal);
        try
        {
            foreach (var replacement in replacements)
            {
                File.WriteAllBytes(
                    output.Files
                        .Single(file =>
                            file.RelativePath == replacement.Key)
                        .AbsolutePath,
                    replacement.Value);
            }

            return await ProjectCodeArtifactPublisher.PublishAsync(
                RefreshRawOutputLease(workspaceLease, output),
                publicationRoot);
        }
        finally
        {
            foreach (var original in originals)
            {
                File.WriteAllBytes(
                    output.Files
                        .Single(file =>
                            file.RelativePath == original.Key)
                        .AbsolutePath,
                    original.Value);
            }
        }
    }

    private static void AssertDiagnostic(
        ProjectCodeArtifactInspectionResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodeArtifactPublicationResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodeModuleIndexResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodeStagingCandidateAdmissionResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static byte[] ReplaceUtf8(
        byte[] source,
        string oldValue,
        string newValue)
    {
        var oldBytes = Encoding.UTF8.GetBytes(oldValue);
        var newBytes = Encoding.UTF8.GetBytes(newValue);
        Assert.Equal(oldBytes.Length, newBytes.Length);
        var result = source.ToArray();
        var replacements = 0;
        for (var offset = 0;
             offset <= result.Length - oldBytes.Length;
             ++offset)
        {
            if (!result.AsSpan(offset, oldBytes.Length)
                    .SequenceEqual(oldBytes))
            {
                continue;
            }

            newBytes.CopyTo(result, offset);
            ++replacements;
            offset += oldBytes.Length - 1;
        }

        Assert.NotEqual(0, replacements);
        return result;
    }

    private static byte[] WithCorFlags(byte[] source, int flags)
    {
        var result = source.ToArray();
        using var stream = new MemoryStream(result, writable: false);
        using var reader = new PEReader(stream);
        var offset = reader.PEHeaders.CorHeaderStartOffset;
        Assert.True(offset >= 0);
        BinaryPrimitives.WriteInt32LittleEndian(
            result.AsSpan(offset + 16, sizeof(int)),
            flags);
        return result;
    }

    private static ProjectCodeRawBuildOutputLease RefreshRawOutputLease(
        ProjectCodeImplicitSdkWorkspaceLease workspaceLease,
        ProjectCodeRawBuildOutput source)
    {
        var files = source.Files
            .Select(file =>
            {
                var contents = File.ReadAllBytes(file.AbsolutePath);
                return new ProjectCodeRawBuildOutputFile(
                    file.RelativePath,
                    file.AbsolutePath,
                    contents.LongLength,
                    Convert.ToHexString(SHA256.HashData(contents))
                        .ToLowerInvariant());
            })
            .ToArray();
        var output = new ProjectCodeRawBuildOutput(
            ProjectCodeSdkBuildController.ComputeRawOutputId(
                workspaceLease.Workspace,
                files),
            source.ProjectId,
            source.WorkspaceId,
            source.CredentialId,
            source.SdkVersion,
            source.TargetFramework,
            source.AssemblyName,
            source.AbsoluteRoot,
            source.ImplementationAssemblyRelativePath,
            source.ReferenceAssemblyRelativePath,
            source.PortablePdbRelativePath,
            source.DependencyFileRelativePath,
            Array.AsReadOnly(files));
        return new ProjectCodeRawBuildOutputLease(workspaceLease, output);
    }

    private static void AssertNoOwnedOutputCandidates(string parent)
    {
        Assert.DoesNotContain(
            Directory.EnumerateFileSystemEntries(parent),
            path => Path.GetFileName(path).Contains(
                ".candidate-",
                StringComparison.Ordinal));
    }

    private static async Task AddCandidateFileAsync(
        string outputRoot,
        string fileName)
    {
        var candidate = await WaitForCandidateAsync(outputRoot);
        await File.WriteAllTextAsync(
            Path.Combine(candidate, fileName),
            "unexpected");
    }

    private static async Task MutateSourceAfterStagingBeginsAsync(
        string outputRoot,
        string source)
    {
        _ = await WaitForCandidateAsync(outputRoot);
        using var timeout = new CancellationTokenSource(
            TimeSpan.FromSeconds(30));
        while (true)
        {
            timeout.Token.ThrowIfCancellationRequested();
            try
            {
                await using var stream = new FileStream(
                    source,
                    FileMode.Open,
                    FileAccess.Write,
                    FileShare.ReadWrite,
                    4096,
                    FileOptions.Asynchronous);
                stream.Position = stream.Length - 1;
                await stream.WriteAsync(
                    new byte[] { 0x7f },
                    timeout.Token);
                await stream.FlushAsync(timeout.Token);
                stream.Flush(flushToDisk: true);
                return;
            }
            catch (IOException)
            {
                await Task.Delay(1, timeout.Token);
            }
        }
    }

    private static async Task<string> WaitForCandidateAsync(
        string outputRoot)
    {
        var parent = Path.GetDirectoryName(outputRoot)!;
        var pattern = $".{Path.GetFileName(outputRoot)}.candidate-*";
        using var timeout = new CancellationTokenSource(
            TimeSpan.FromSeconds(30));
        while (true)
        {
            timeout.Token.ThrowIfCancellationRequested();
            try
            {
                var candidate = Directory
                    .EnumerateDirectories(parent, pattern)
                    .SingleOrDefault();
                if (candidate is not null)
                {
                    return candidate;
                }
            }
            catch (IOException)
            {
            }

            await Task.Delay(1, timeout.Token);
        }
    }

    private static void AssertNoAbsolutePathLeak(
        ProjectCodeSdkBuildResult result,
        params string[] roots)
    {
        var output = string.Join(
            "\n",
            result.Steps.SelectMany(step => new[]
            {
                step.StandardOutput,
                step.StandardError,
            }));
        foreach (var root in roots)
        {
            Assert.DoesNotContain(
                root,
                output,
                StringComparison.OrdinalIgnoreCase);
        }
    }

    private static void AssertNoAbsolutePathLeak(
        string value,
        params string[] roots)
    {
        foreach (var root in roots)
        {
            Assert.DoesNotContain(
                root,
                value,
                StringComparison.OrdinalIgnoreCase);
        }
    }

    private static string Render(ProjectCodeSdkBuildResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Step} {diagnostic.Location}: {diagnostic.Message}")
                .Concat(result.Steps.Select(step =>
                    $"{step.Kind} exit={step.ExitCode}\n{step.StandardOutput}\n{step.StandardError}")));

    private static string Render(
        ProjectCodeArtifactInspectionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodeArtifactPublicationResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodeModuleIndexResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodeStagingCandidateAdmissionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private sealed class CallbackProcessRunner(
        Func<
            ProjectCodeBuildProcessRequest,
            CancellationToken,
            Task<ProjectCodeBuildProcessResult>> callback) :
        IProjectCodeSdkBuildProcessRunner
    {
        private readonly object gate_ = new();
        private readonly List<ProjectCodeBuildProcessRequest> requests_ = [];

        public IReadOnlyList<ProjectCodeBuildProcessRequest> Requests
        {
            get
            {
                lock (gate_)
                {
                    return requests_.ToArray();
                }
            }
        }

        public Task<ProjectCodeBuildProcessResult> RunAsync(
            ProjectCodeBuildProcessRequest request,
            CancellationToken cancellationToken)
        {
            lock (gate_)
            {
                requests_.Add(request);
            }

            return callback(request, cancellationToken);
        }
    }
}
