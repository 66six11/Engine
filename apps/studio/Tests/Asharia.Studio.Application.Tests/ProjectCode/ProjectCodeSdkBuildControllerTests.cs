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
using Asharia.Studio.Application.Extensions;
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
        const string StaticConstructorMarker =
            "ASHARIA_TEST_PROJECT_CODE_PINNED_LOAD_STATIC_CONSTRUCTOR";
        const string ModuleStaticConstructorMarker =
            "ASHARIA_TEST_PROJECT_CODE_MODULE_STATIC_CONSTRUCTOR";
        const string ModuleInstanceConstructorMarker =
            "ASHARIA_TEST_PROJECT_CODE_MODULE_INSTANCE_CONSTRUCTOR";
        const string AttributeConstructorMarker =
            "ASHARIA_TEST_PROJECT_CODE_ATTRIBUTE_CONSTRUCTOR";
        const string ModuleConfigureMarker =
            "ASHARIA_TEST_PROJECT_CODE_MODULE_CONFIGURE";
        const string ModuleConfigureFailureTrigger =
            "ASHARIA_TEST_PROJECT_CODE_MODULE_CONFIGURE_FAILURE";
        const string ModuleActivateMarker =
            "ASHARIA_TEST_PROJECT_CODE_MODULE_ACTIVATE";
        const string ModuleConstructorFailureTrigger =
            "ASHARIA_TEST_PROJECT_CODE_MODULE_CONSTRUCTOR_FAILURE";
        Environment.SetEnvironmentVariable(
            StaticConstructorMarker,
            null);
        Environment.SetEnvironmentVariable(
            ModuleStaticConstructorMarker,
            null);
        Environment.SetEnvironmentVariable(
            ModuleInstanceConstructorMarker,
            null);
        Environment.SetEnvironmentVariable(
            AttributeConstructorMarker,
            null);
        Environment.SetEnvironmentVariable(
            ModuleConfigureMarker,
            null);
        Environment.SetEnvironmentVariable(
            ModuleConfigureFailureTrigger,
            null);
        Environment.SetEnvironmentVariable(
            ModuleActivateMarker,
            null);
        Environment.SetEnvironmentVariable(
            ModuleConstructorFailureTrigger,
            null);
        moduleProject.WriteEditorSource(
            "RealModule.cs",
            """
            using System;
            using System.Threading;
            using System.Threading.Tasks;
            using Asharia.Editor.Extensions;

            namespace Fixture;

            [AttributeUsage(AttributeTargets.Class, Inherited = false)]
            internal sealed class ResolutionProbeAttribute : Attribute
            {
                public ResolutionProbeAttribute()
                {
                    Environment.SetEnvironmentVariable(
                        "ATTRIBUTE_CONSTRUCTOR_MARKER",
                        "executed");
                }
            }

            [ResolutionProbe]
            [EditorModule(
                "fixture.module",
                Scope = EditorModuleScopeKind.Project,
                Activation = EditorModuleActivationPolicy.OnDemand,
                Handover = EditorModuleHandoverPolicy.RestartRequired)]
            public sealed class RealModule : EditorModule
            {
                static RealModule()
                {
                    Environment.SetEnvironmentVariable(
                        "MODULE_STATIC_CONSTRUCTOR_MARKER",
                        "1");
                }

                public RealModule()
                {
                    var current = Environment.GetEnvironmentVariable(
                        "MODULE_INSTANCE_CONSTRUCTOR_MARKER");
                    var count = int.TryParse(current, out var value)
                        ? value
                        : 0;
                    Environment.SetEnvironmentVariable(
                        "MODULE_INSTANCE_CONSTRUCTOR_MARKER",
                        (count + 1).ToString());
                    if (string.Equals(
                            Environment.GetEnvironmentVariable(
                                "MODULE_CONSTRUCTOR_FAILURE_TRIGGER"),
                            "fail",
                            StringComparison.Ordinal))
                    {
                        throw new InvalidOperationException(
                            "Injected module constructor failure.");
                    }
                }

                public override void Configure(EditorModuleBuilder editor)
                {
                    var current = Environment.GetEnvironmentVariable(
                        "MODULE_CONFIGURE_MARKER");
                    var count = int.TryParse(current, out var value)
                        ? value
                        : 0;
                    Environment.SetEnvironmentVariable(
                        "MODULE_CONFIGURE_MARKER",
                        (count + 1).ToString());
                    if (string.Equals(
                            Environment.GetEnvironmentVariable(
                                "MODULE_CONFIGURE_FAILURE_TRIGGER"),
                            "fail",
                            StringComparison.Ordinal))
                    {
                        throw new InvalidOperationException(
                            "Injected module configuration failure.");
                    }

                    editor.Capabilities.Provide(
                        EditorCapabilityId.Create(
                            "fixture.module.v1"));
                }

                public override ValueTask<IEditorModuleActivation> ActivateAsync(
                    EditorModuleContext context,
                    CancellationToken cancellationToken)
                {
                    Environment.SetEnvironmentVariable(
                        "MODULE_ACTIVATE_MARKER",
                        "executed");
                    return base.ActivateAsync(context, cancellationToken);
                }
            }

            [EditorModule(
                "policy.on-demand.coexist",
                Scope = EditorModuleScopeKind.Project,
                Activation = EditorModuleActivationPolicy.OnDemand,
                Handover = EditorModuleHandoverPolicy.Coexist)]
            public sealed class OnDemandCoexistModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule(
                "policy.on-demand.quiesce",
                Scope = EditorModuleScopeKind.Project,
                Activation = EditorModuleActivationPolicy.OnDemand,
                Handover = EditorModuleHandoverPolicy.QuiesceThenActivate)]
            public sealed class OnDemandQuiesceModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule(
                "policy.on-ready.coexist",
                Scope = EditorModuleScopeKind.Project,
                Activation = EditorModuleActivationPolicy.OnScopeReady,
                Handover = EditorModuleHandoverPolicy.Coexist)]
            public sealed class OnReadyCoexistModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            [EditorModule(
                "policy.on-ready.quiesce",
                Scope = EditorModuleScopeKind.Project,
                Activation = EditorModuleActivationPolicy.OnScopeReady,
                Handover = EditorModuleHandoverPolicy.QuiesceThenActivate)]
            public sealed class OnReadyQuiesceModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            """.Replace(
                "ATTRIBUTE_CONSTRUCTOR_MARKER",
                AttributeConstructorMarker,
                StringComparison.Ordinal)
            .Replace(
                "MODULE_STATIC_CONSTRUCTOR_MARKER",
                ModuleStaticConstructorMarker,
                StringComparison.Ordinal)
            .Replace(
                "MODULE_INSTANCE_CONSTRUCTOR_MARKER",
                ModuleInstanceConstructorMarker,
                StringComparison.Ordinal)
            .Replace(
                "MODULE_CONSTRUCTOR_FAILURE_TRIGGER",
                ModuleConstructorFailureTrigger,
                StringComparison.Ordinal)
            .Replace(
                "MODULE_CONFIGURE_MARKER",
                ModuleConfigureMarker,
                StringComparison.Ordinal)
            .Replace(
                "MODULE_CONFIGURE_FAILURE_TRIGGER",
                ModuleConfigureFailureTrigger,
                StringComparison.Ordinal)
            .Replace(
                "MODULE_ACTIVATE_MARKER",
                ModuleActivateMarker,
                StringComparison.Ordinal));
        moduleProject.WriteEditorSource(
            "LoadProbe.cs",
            """
            using System;

            namespace Fixture;

            internal static class LoadProbe
            {
                static LoadProbe()
                {
                    Environment.SetEnvironmentVariable(
                        "STATIC_CONSTRUCTOR_MARKER",
                        "executed");
                }
            }

            """.Replace(
                "STATIC_CONSTRUCTOR_MARKER",
                StaticConstructorMarker,
                StringComparison.Ordinal));
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
        Assert.Equal(5, moduleIndex.Report!.Entries.Count);
        var moduleEntry = Assert.Single(moduleIndex.Report.Entries, entry =>
                entry.DefinitionId.Module.Value == "fixture.module");
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
        var policyMetadata = moduleIndex.Report.Entries
            .Where(entry => entry.DefinitionId.Module.Value.StartsWith(
                "policy.",
                StringComparison.Ordinal))
            .Select(entry => (entry.Activation, entry.Handover))
            .ToHashSet();
        Assert.Equal(4, policyMetadata.Count);
        Assert.Contains(
            (EditorModuleActivationPolicy.OnDemand,
                EditorModuleHandoverPolicy.Coexist),
            policyMetadata);
        Assert.Contains(
            (EditorModuleActivationPolicy.OnDemand,
                EditorModuleHandoverPolicy.QuiesceThenActivate),
            policyMetadata);
        Assert.Contains(
            (EditorModuleActivationPolicy.OnScopeReady,
                EditorModuleHandoverPolicy.Coexist),
            policyMetadata);
        Assert.Contains(
            (EditorModuleActivationPolicy.OnScopeReady,
                EditorModuleHandoverPolicy.QuiesceThenActivate),
            policyMetadata);
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
        var modulePolicy =
            await ProjectCodeHostPolicySelector.SelectAsync(
                moduleCandidateReceipt);
        Assert.True(modulePolicy.Succeeded, Render(modulePolicy));
        var modulePolicyReceipt = modulePolicy.Receipt!;
        Assert.Equal(
            moduleCandidateReceipt.CandidateId,
            modulePolicyReceipt.Candidate.CandidateId);
        Assert.Equal(
            ProjectCodeHostKind.Pinned,
            modulePolicyReceipt.HostKind);
        Assert.Equal(
            ProjectCodeReplacementPolicy.RestartRequired,
            modulePolicyReceipt.ReplacementPolicy);
        Assert.Equal(
            ProjectCodeHostPolicyReason
                .ExternalBuildReloadEvidenceUnavailable,
            modulePolicyReceipt.Reason);
        Assert.True(
            await ProjectCodeHostPolicySelector
                .IsPolicyCurrentAsync(modulePolicyReceipt));
        var forgedPolicyId = "sha256-"
            + new string(
                modulePolicyReceipt.PolicyId[7] == '0' ? '1' : '0',
                64);
        var forgedPolicy = new ProjectCodeHostPolicyReceipt(
            forgedPolicyId,
            moduleCandidateReceipt,
            ProjectCodeHostKind.Pinned,
            ProjectCodeReplacementPolicy.RestartRequired,
            ProjectCodeHostPolicyReason
                .ExternalBuildReloadEvidenceUnavailable);
        Assert.False(
            await ProjectCodeHostPolicySelector
                .IsPolicyCurrentAsync(forgedPolicy));
        var pinnedLoadImage =
            await ProjectCodePinnedLoadImageBuilder.BuildAsync(
                modulePolicyReceipt);
        Assert.True(
            pinnedLoadImage.Succeeded,
            Render(pinnedLoadImage));
        var loadImageSnapshot = pinnedLoadImage.Snapshot!;
        Assert.Equal(
            modulePolicyReceipt.PolicyId,
            loadImageSnapshot.Policy.PolicyId);
        Assert.Equal(
            modulePublicationReceipt.Implementation.Size,
            loadImageSnapshot.ImplementationSize);
        Assert.Equal(
            modulePublicationReceipt.PortablePdb.Size,
            loadImageSnapshot.PortablePdbSize);
        byte[] loadImageImplementation;
        using (var stream = loadImageSnapshot.OpenImplementationStream())
        {
            Assert.False(stream.CanWrite);
            Assert.False(
                Assert.IsType<MemoryStream>(stream)
                    .TryGetBuffer(out _));
            loadImageImplementation =
                Assert.IsType<MemoryStream>(stream).ToArray();
        }

        byte[] loadImagePortablePdb;
        using (var stream = loadImageSnapshot.OpenPortablePdbStream())
        {
            Assert.False(stream.CanWrite);
            Assert.False(
                Assert.IsType<MemoryStream>(stream)
                    .TryGetBuffer(out _));
            loadImagePortablePdb =
                Assert.IsType<MemoryStream>(stream).ToArray();
        }

        Assert.Equal(
            File.ReadAllBytes(Path.Combine(
                modulePublicationReceipt.AbsoluteRoot,
                modulePublicationReceipt.Implementation.RelativePath
                    .Replace('/', Path.DirectorySeparatorChar))),
            loadImageImplementation);
        Assert.Equal(
            File.ReadAllBytes(Path.Combine(
                modulePublicationReceipt.AbsoluteRoot,
                modulePublicationReceipt.PortablePdb.RelativePath
                    .Replace('/', Path.DirectorySeparatorChar))),
            loadImagePortablePdb);
        Assert.True(
            await ProjectCodePinnedLoadImageBuilder
                .IsSnapshotCurrentAsync(loadImageSnapshot));
        var forgedImageId = "sha256-"
            + new string(
                loadImageSnapshot.ImageId[7] == '0' ? '1' : '0',
                64);
        var forgedImage = new ProjectCodePinnedLoadImageSnapshot(
            forgedImageId,
            modulePolicyReceipt,
            loadImageImplementation.ToArray(),
            loadImagePortablePdb.ToArray());
        Assert.False(
            await ProjectCodePinnedLoadImageBuilder
                .IsSnapshotCurrentAsync(forgedImage));
        var pinnedAssemblyLoader =
            new ProjectCodePinnedAssemblyLoader();
        AssertDiagnostic(
            await pinnedAssemblyLoader.LoadAsync(forgedImage),
            "project-code.pinned-assembly-load.image-not-current");

        var failedLoadCount = 0;
        var failingPinnedAssemblyLoader =
            new ProjectCodePinnedAssemblyLoader((_, _, _) =>
            {
                ++failedLoadCount;
                throw new FileLoadException();
            });
        AssertDiagnostic(
            await failingPinnedAssemblyLoader.LoadAsync(
                loadImageSnapshot),
            "project-code.pinned-assembly-load.failed-restart-required");
        AssertDiagnostic(
            await failingPinnedAssemblyLoader.LoadAsync(
                loadImageSnapshot),
            "project-code.pinned-assembly-load.previous-attempt-failed");
        Assert.Equal(1, failedLoadCount);

        var concurrentPinnedLoads = await Task.WhenAll(
            pinnedAssemblyLoader.LoadAsync(loadImageSnapshot),
            pinnedAssemblyLoader.LoadAsync(loadImageSnapshot));
        Assert.All(
            concurrentPinnedLoads,
            result => Assert.True(result.Succeeded, Render(result)));
        var pinnedAssemblyHost = concurrentPinnedLoads[0].Host!;
        Assert.Same(
            pinnedAssemblyHost,
            concurrentPinnedLoads[1].Host);
        Assert.Same(
            pinnedAssemblyHost.Assembly,
            concurrentPinnedLoads[1].Host!.Assembly);
        Assert.Equal(
            loadImageSnapshot.ImageId,
            pinnedAssemblyHost.Image.ImageId);
        Assert.False(pinnedAssemblyHost.IsCollectible);
        Assert.Equal(1, pinnedAssemblyHost.AssemblyCount);
        Assert.False(string.IsNullOrWhiteSpace(
            pinnedAssemblyHost.LoadContextName));
        Assert.Empty(pinnedAssemblyHost.Assembly.Location);
        Assert.Equal(
            modulePublicationReceipt.Report.Implementation.Mvid,
            pinnedAssemblyHost.Assembly.ManifestModule.ModuleVersionId);
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleStaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleInstanceConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));

        var moduleTypeResolution =
            ProjectCodePinnedModuleTypeResolver.Resolve(
                pinnedAssemblyHost);
        Assert.True(
            moduleTypeResolution.Succeeded,
            Render(moduleTypeResolution));
        var pinnedModuleTypes = moduleTypeResolution.ModuleTypes!;
        Assert.Matches(
            "^sha256-[0-9a-f]{64}$",
            pinnedModuleTypes.ModuleTypeSetId);
        Assert.Same(pinnedAssemblyHost, pinnedModuleTypes.Host);
        var expectedModuleEntries =
            pinnedAssemblyHost.Image.Policy.Candidate
                .ModuleIndex.Entries;
        Assert.Equal(
            expectedModuleEntries.Count,
            pinnedModuleTypes.Modules.Count);
        for (var index = 0;
             index < expectedModuleEntries.Count;
             ++index)
        {
            var expectedEntry = expectedModuleEntries[index];
            var resolvedModule = pinnedModuleTypes.Modules[index];
            Assert.Same(expectedEntry, resolvedModule.Entry);
            Assert.Equal(
                expectedEntry.TypeName,
                resolvedModule.Type.FullName);
            Assert.Same(
                pinnedAssemblyHost.Assembly,
                resolvedModule.Type.Assembly);
            Assert.True(resolvedModule.Type.IsClass);
            Assert.True(resolvedModule.Type.IsPublic);
            Assert.True(resolvedModule.Type.IsSealed);
            Assert.False(resolvedModule.Type.IsAbstract);
            Assert.False(resolvedModule.Type.IsNested);
            Assert.False(resolvedModule.Type.IsGenericType);
            Assert.False(
                resolvedModule.Type.ContainsGenericParameters);
            Assert.Same(
                typeof(EditorModule),
                resolvedModule.Type.BaseType);
            Assert.NotNull(
                resolvedModule.Type.GetConstructor(Type.EmptyTypes));
            Assert.Same(
                resolvedModule.Type,
                resolvedModule.Constructor.DeclaringType);
            Assert.True(resolvedModule.Constructor.IsPublic);
            Assert.False(resolvedModule.Constructor.IsStatic);
            Assert.Empty(resolvedModule.Constructor.GetParameters());
        }

        var repeatedModuleTypeResolution =
            ProjectCodePinnedModuleTypeResolver.Resolve(
                pinnedAssemblyHost);
        Assert.True(
            repeatedModuleTypeResolution.Succeeded,
            Render(repeatedModuleTypeResolution));
        Assert.Equal(
            pinnedModuleTypes.ModuleTypeSetId,
            repeatedModuleTypeResolution.ModuleTypes!
                .ModuleTypeSetId);
        Assert.Equal(
            pinnedModuleTypes.Modules.Select(module => module.Type),
            repeatedModuleTypeResolution.ModuleTypes.Modules
                .Select(module => module.Type));
        var nonModuleType = pinnedAssemblyHost.Assembly.GetType(
            "Fixture.LoadProbe",
            throwOnError: false,
            ignoreCase: false);
        Assert.NotNull(nonModuleType);
        Assert.Throws<ArgumentException>(() =>
            new ProjectCodePinnedModuleType(
                pinnedAssemblyHost,
                expectedModuleEntries[0],
                nonModuleType));
        Assert.Equal(1, pinnedAssemblyHost.AssemblyCount);
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleStaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleInstanceConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));

        Environment.SetEnvironmentVariable(
            ModuleConstructorFailureTrigger,
            "fail");
        var failingModuleConstructor =
            new ProjectCodePinnedModuleConstructor();
        var failedConstruction = failingModuleConstructor.Construct(
            pinnedModuleTypes);
        AssertDiagnostic(
            failedConstruction,
            "project-code.pinned-module-construction.constructor-failed-restart-required");
        AssertNoAbsolutePathLeak(
            Render(failedConstruction),
            moduleProject.ProjectRoot,
            modulePublicationReceipt.AbsoluteRoot);
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleStaticConstructorMarker));
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleInstanceConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));

        Environment.SetEnvironmentVariable(
            ModuleConstructorFailureTrigger,
            null);
        var repeatedFailedConstruction =
            failingModuleConstructor.Construct(pinnedModuleTypes);
        Assert.Same(failedConstruction, repeatedFailedConstruction);
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleInstanceConstructorMarker));

        var moduleConstructor =
            new ProjectCodePinnedModuleConstructor();
        var concurrentConstructions = await Task.WhenAll(
            Task.Run(() => moduleConstructor.Construct(
                pinnedModuleTypes)),
            Task.Run(() => moduleConstructor.Construct(
                pinnedModuleTypes)));
        Assert.All(
            concurrentConstructions,
            result => Assert.True(result.Succeeded, Render(result)));
        Assert.Same(
            concurrentConstructions[0],
            concurrentConstructions[1]);
        var construction =
            concurrentConstructions[0].Construction!;
        Assert.Matches(
            "^sha256-[0-9a-f]{64}$",
            construction.ConstructionId);
        Assert.Same(pinnedModuleTypes, construction.ModuleTypes);
        Assert.Equal(
            pinnedModuleTypes.Modules.Count,
            construction.Modules.Count);
        for (var index = 0;
             index < pinnedModuleTypes.Modules.Count;
             ++index)
        {
            var expectedModuleType = pinnedModuleTypes.Modules[index];
            var constructedModule = construction.Modules[index];
            Assert.Same(
                expectedModuleType,
                constructedModule.ModuleType);
            Assert.Same(
                expectedModuleType.Type,
                constructedModule.Module.GetType());
        }

        var repeatedConstruction = moduleConstructor.Construct(
            repeatedModuleTypeResolution.ModuleTypes);
        Assert.Same(
            concurrentConstructions[0],
            repeatedConstruction);
        Assert.Same(
            construction,
            repeatedConstruction.Construction);
        Assert.Equal(
            construction.Modules.Select(module => module.Module),
            repeatedConstruction.Construction!.Modules
                .Select(module => module.Module));
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleStaticConstructorMarker));
        Assert.Equal(
            "2",
            Environment.GetEnvironmentVariable(
                ModuleInstanceConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Equal(1, pinnedAssemblyHost.AssemblyCount);

        var moduleConfigurator =
            new ProjectCodePinnedModuleConfigurator();
        var concurrentConfigurations = await Task.WhenAll(
            Task.Run(() => moduleConfigurator.Configure(
                construction)),
            Task.Run(() => moduleConfigurator.Configure(
                construction)));
        Assert.All(
            concurrentConfigurations,
            result => Assert.True(result.Succeeded, Render(result)));
        Assert.Same(
            concurrentConfigurations[0],
            concurrentConfigurations[1]);
        var configuration =
            concurrentConfigurations[0].Configuration!;
        Assert.Matches(
            "^sha256-[0-9a-f]{64}$",
            configuration.ConfigurationId);
        Assert.Same(construction, configuration.Construction);
        Assert.Equal(
            construction.Modules.Count,
            configuration.Modules.Count);
        for (var index = 0;
             index < construction.Modules.Count;
             ++index)
        {
            var moduleObject = construction.Modules[index];
            var entry = moduleObject.ModuleType.Entry;
            var configuredModule = configuration.Modules[index];
            Assert.Same(
                moduleObject,
                configuredModule.ModuleObject);
            Assert.Equal(
                entry.DefinitionId,
                configuredModule.Metadata.DefinitionId);
            Assert.Equal(
                entry.TypeName,
                configuredModule.Metadata.EntryTypeName);
            Assert.Equal(
                entry.Activation,
                configuredModule.Metadata.Activation);
            Assert.Equal(
                entry.Handover,
                configuredModule.Metadata.Handover);
            Assert.Equal(
                entry.DefinitionId,
                configuredModule.Declaration.DefinitionContext
                    .DefinitionId);
        }

        var configuredRealModule = Assert.Single(
            configuration.Modules,
            module => module.Metadata.DefinitionId.Module.Value
                == "fixture.module");
        var providedCapability = Assert.Single(
            configuredRealModule.Declaration.ProvidedCapabilities);
        Assert.Equal("fixture.module.v1", providedCapability.Value);
        var equivalentConstruction =
            new ProjectCodePinnedModuleConstruction(
                construction.ModuleTypes,
                construction.Modules);
        var repeatedConfiguration = moduleConfigurator.Configure(
            equivalentConstruction);
        Assert.Same(
            concurrentConfigurations[0],
            repeatedConfiguration);
        Assert.Same(
            configuration,
            repeatedConfiguration.Configuration);
        Assert.Equal(
            configuration.Modules.Select(module =>
                module.Declaration),
            repeatedConfiguration.Configuration!.Modules.Select(
                module => module.Declaration));
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));

        var definitionSet =
            ProjectCodePinnedModuleDefinitionSet.Create(configuration);
        Assert.Same(configuration, definitionSet.Configuration);
        Assert.Equal(
            configuration.Modules.Count,
            definitionSet.Definitions.Count);
        Assert.Equal(
            configuration.Modules.Count,
            definitionSet.DefinitionsById.Count);
        for (var index = 0;
             index < configuration.Modules.Count;
             ++index)
        {
            var configuredModule = configuration.Modules[index];
            var definition = definitionSet.Definitions[index];
            Assert.Equal(
                configuredModule.Metadata.DefinitionId,
                definition.Id);
            Assert.Same(
                configuredModule.ModuleObject.Module,
                definition.Module);
            Assert.Same(
                configuredModule.Metadata,
                definition.Metadata);
            Assert.Same(
                configuredModule.Declaration,
                definition.Declaration);
            Assert.Same(
                definition,
                definitionSet.DefinitionsById[definition.Id]);
        }

        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));

        var scopeRegistry = new EditorModuleRegistry();
        Assert.Throws<ArgumentException>(() =>
            ProjectCodePinnedModuleScopePreparer.Prepare(
                definitionSet,
                ScopeInstanceId.Application,
                scopeRegistry));
        Assert.False(scopeRegistry.TryGetPartition(
            ScopeInstanceId.Application,
            out _));
        Assert.Throws<ArgumentException>(() =>
            ProjectCodePinnedModuleScopePreparer.Prepare(
                definitionSet,
                default,
                scopeRegistry));

        var projectScope = ScopeInstanceId.ForProject(
            Guid.Parse("80808080-8080-8080-8080-808080808080"));
        var hostCapability = EditorCapabilityId.Create(
            "fixture.host.v1");
        var invalidScopePreparation =
            ProjectCodePinnedModuleScopePreparer.Prepare(
                definitionSet,
                projectScope,
                scopeRegistry,
                [hostCapability, hostCapability]);
        AssertDiagnostic(
            invalidScopePreparation,
            "project-code.pinned-module-scope-preparation.validation-failed");
        AssertNoAbsolutePathLeak(
            Render(invalidScopePreparation),
            moduleProject.ProjectRoot,
            modulePublicationReceipt.AbsoluteRoot);
        Assert.False(scopeRegistry.TryGetPartition(
            projectScope,
            out _));

        var hostCapabilities = new[]
        {
            hostCapability,
        };
        var scopePreparationResult =
            ProjectCodePinnedModuleScopePreparer.Prepare(
                definitionSet,
                projectScope,
                scopeRegistry,
                hostCapabilities);
        Assert.True(
            scopePreparationResult.Succeeded,
            Render(scopePreparationResult));
        var scopePreparation =
            scopePreparationResult.Preparation!;
        hostCapabilities[0] = EditorCapabilityId.Create(
            "fixture.changed.v1");
        Assert.Same(
            definitionSet,
            scopePreparation.DefinitionSet);
        Assert.Equal(
            projectScope,
            scopePreparation.ScopeInstanceId);
        Assert.Equal(
            [hostCapability],
            scopePreparation.HostCapabilities);
        Assert.Equal(
            projectScope,
            scopePreparation.Candidate.ScopeInstanceId);
        Assert.Contains(
            hostCapability,
            scopePreparation.Candidate.HostCapabilities);
        Assert.DoesNotContain(
            hostCapabilities[0],
            scopePreparation.Candidate.HostCapabilities);
        Assert.Equal(
            definitionSet.Definitions.Count,
            scopePreparation.Candidate.RegistrationOrder.Count);
        for (var index = 0;
             index < definitionSet.Definitions.Count;
             ++index)
        {
            var definition = definitionSet.Definitions[index];
            var instance =
                scopePreparation.Candidate.RegistrationOrder[index];
            Assert.Same(definition, instance.Definition);
            Assert.Same(
                instance,
                scopePreparation.Candidate.Instances[
                    definition.Id]);
            Assert.Equal(
                EditorModuleInstanceId.Create(
                    definition.Id,
                    projectScope),
                instance.Id);
        }

        var realDefinition = definitionSet.DefinitionsById[
            configuredRealModule.Metadata.DefinitionId];
        Assert.Equal(
            EditorModuleInstanceId.Create(
                realDefinition.Id,
                projectScope),
            scopePreparation.Candidate.CapabilityProviders[
                providedCapability]);
        Assert.False(scopeRegistry.TryGetPartition(
            projectScope,
            out _));
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));

        EditorScopeTransaction.Prepare(
            scopeRegistry,
            ScopeInstanceId.Application,
            []).Commit();
        var staleScopeCommit =
            ProjectCodePinnedModuleScopeCommitter.CommitInitial(
                scopePreparation);
        AssertDiagnostic(
            staleScopeCommit,
            "project-code.pinned-module-scope-registration.conflict");
        AssertNoAbsolutePathLeak(
            Render(staleScopeCommit),
            moduleProject.ProjectRoot,
            modulePublicationReceipt.AbsoluteRoot);
        Assert.False(scopeRegistry.TryGetPartition(
            projectScope,
            out _));

        var refreshedScopePreparationResult =
            ProjectCodePinnedModuleScopePreparer.Prepare(
                definitionSet,
                projectScope,
                scopeRegistry,
                [hostCapability]);
        Assert.True(
            refreshedScopePreparationResult.Succeeded,
            Render(refreshedScopePreparationResult));
        var refreshedScopePreparation =
            refreshedScopePreparationResult.Preparation!;
        var scopeCommit =
            ProjectCodePinnedModuleScopeCommitter.CommitInitial(
                refreshedScopePreparation);
        Assert.True(scopeCommit.Succeeded, Render(scopeCommit));
        var scopeRegistration = scopeCommit.Registration!;
        Assert.Same(
            refreshedScopePreparation,
            scopeRegistration.Preparation);
        Assert.Equal(
            projectScope,
            scopeRegistration.ScopeInstanceId);
        Assert.Same(
            refreshedScopePreparation.Candidate,
            scopeRegistration.Partition);
        Assert.Same(
            scopeRegistration.Partition,
            scopeRegistry.GetRequiredPartition(projectScope));

        var repeatedScopeCommit =
            ProjectCodePinnedModuleScopeCommitter.CommitInitial(
                refreshedScopePreparation);
        AssertDiagnostic(
            repeatedScopeCommit,
            "project-code.pinned-module-scope-registration.conflict");
        Assert.Same(
            scopeRegistration.Partition,
            scopeRegistry.GetRequiredPartition(projectScope));

        scopeRegistration.Dispose();
        scopeRegistration.Dispose();
        Assert.False(scopeRegistry.TryGetPartition(
            projectScope,
            out _));
        Assert.True(scopeRegistry.TryGetPartition(
            ScopeInstanceId.Application,
            out _));

        var existingTransaction = EditorScopeTransaction.Prepare(
            scopeRegistry,
            projectScope,
            definitionSet.Definitions,
            [hostCapability]);
        existingTransaction.Commit();
        var existingScopePreparationResult =
            ProjectCodePinnedModuleScopePreparer.Prepare(
                definitionSet,
                projectScope,
                scopeRegistry,
                [hostCapability]);
        Assert.True(
            existingScopePreparationResult.Succeeded,
            Render(existingScopePreparationResult));
        var existingScopeCommit =
            ProjectCodePinnedModuleScopeCommitter.CommitInitial(
                existingScopePreparationResult.Preparation!);
        AssertDiagnostic(
            existingScopeCommit,
            "project-code.pinned-module-scope-registration.conflict");
        Assert.Same(
            existingTransaction.Candidate,
            scopeRegistry.GetRequiredPartition(projectScope));

        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));

        var alternateConstructionResult =
            new ProjectCodePinnedModuleConstructor().Construct(
                pinnedModuleTypes);
        Assert.True(
            alternateConstructionResult.Succeeded,
            Render(alternateConstructionResult));
        var alternateConstruction =
            alternateConstructionResult.Construction!;
        Assert.Equal(
            construction.ConstructionId,
            alternateConstruction.ConstructionId);
        Assert.NotSame(construction, alternateConstruction);
        Assert.NotSame(
            construction.Modules[0].Module,
            alternateConstruction.Modules[0].Module);
        Assert.Equal(
            "3",
            Environment.GetEnvironmentVariable(
                ModuleInstanceConstructorMarker));
        AssertDiagnostic(
            moduleConfigurator.Configure(alternateConstruction),
            "project-code.pinned-module-configuration.restart-required");
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));

        Environment.SetEnvironmentVariable(
            ModuleConfigureFailureTrigger,
            "fail");
        var failingModuleConfigurator =
            new ProjectCodePinnedModuleConfigurator();
        var failedConfiguration =
            failingModuleConfigurator.Configure(
                alternateConstruction);
        AssertDiagnostic(
            failedConfiguration,
            "project-code.pinned-module-configuration.configure-failed-restart-required");
        AssertNoAbsolutePathLeak(
            Render(failedConfiguration),
            moduleProject.ProjectRoot,
            modulePublicationReceipt.AbsoluteRoot);
        Assert.Equal(
            "2",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));

        Environment.SetEnvironmentVariable(
            ModuleConfigureFailureTrigger,
            null);
        var repeatedFailedConfiguration =
            failingModuleConfigurator.Configure(
                alternateConstruction);
        Assert.Same(
            failedConfiguration,
            repeatedFailedConfiguration);
        Assert.Equal(
            "2",
            Environment.GetEnvironmentVariable(
                ModuleConfigureMarker));
        Assert.Equal(
            "1",
            Environment.GetEnvironmentVariable(
                ModuleStaticConstructorMarker));
        Assert.Equal(
            "3",
            Environment.GetEnvironmentVariable(
                ModuleInstanceConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            AttributeConstructorMarker));
        Assert.Null(Environment.GetEnvironmentVariable(
            ModuleActivateMarker));
        Assert.Equal(1, pinnedAssemblyHost.AssemblyCount);
        var mutatedLoadImageImplementation =
            loadImageImplementation.ToArray();
        mutatedLoadImageImplementation[^1] ^= 0xff;
        Assert.Throws<ArgumentException>(() =>
            new ProjectCodePinnedLoadImageSnapshot(
                loadImageSnapshot.ImageId,
                modulePolicyReceipt,
                mutatedLoadImageImplementation,
                loadImagePortablePdb.ToArray()));

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
        var equivalentModulePolicy =
            await ProjectCodeHostPolicySelector.SelectAsync(
                equivalentModuleCandidate.Receipt);
        Assert.True(
            equivalentModulePolicy.Succeeded,
            Render(equivalentModulePolicy));
        Assert.Equal(
            modulePolicyReceipt.PolicyId,
            equivalentModulePolicy.Receipt!.PolicyId);
        Assert.Equal(
            modulePolicyReceipt.HostKind,
            equivalentModulePolicy.Receipt.HostKind);
        Assert.Equal(
            modulePolicyReceipt.ReplacementPolicy,
            equivalentModulePolicy.Receipt.ReplacementPolicy);
        Assert.Equal(
            modulePolicyReceipt.Reason,
            equivalentModulePolicy.Receipt.Reason);
        var equivalentPinnedLoadImage =
            await ProjectCodePinnedLoadImageBuilder.BuildAsync(
                equivalentModulePolicy.Receipt);
        Assert.True(
            equivalentPinnedLoadImage.Succeeded,
            Render(equivalentPinnedLoadImage));
        Assert.Equal(
            loadImageSnapshot.ImageId,
            equivalentPinnedLoadImage.Snapshot!.ImageId);
        using (var stream =
               equivalentPinnedLoadImage.Snapshot
                   .OpenImplementationStream())
        {
            Assert.Equal(
                loadImageImplementation,
                Assert.IsType<MemoryStream>(stream).ToArray());
        }

        using (var stream =
               equivalentPinnedLoadImage.Snapshot.OpenPortablePdbStream())
        {
            Assert.Equal(
                loadImagePortablePdb,
                Assert.IsType<MemoryStream>(stream).ToArray());
        }

        var equivalentPinnedAssemblyLoad =
            await pinnedAssemblyLoader.LoadAsync(
                equivalentPinnedLoadImage.Snapshot);
        Assert.True(
            equivalentPinnedAssemblyLoad.Succeeded,
            Render(equivalentPinnedAssemblyLoad));
        Assert.Same(
            pinnedAssemblyHost,
            equivalentPinnedAssemblyLoad.Host);
        Assert.Same(
            pinnedAssemblyHost.Assembly,
            equivalentPinnedAssemblyLoad.Host!.Assembly);
        Assert.Null(Environment.GetEnvironmentVariable(
            StaticConstructorMarker));

        using var replacementModuleProject = new ProjectFixture();
        replacementModuleProject.WriteEditorSource(
            "ReplacementModule.cs",
            """
            using Asharia.Editor.Extensions;

            namespace Fixture;

            [EditorModule("fixture.replacement")]
            public sealed class ReplacementModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            """);
        var replacementModuleWorkspace = await CreateWorkspaceAsync(
            replacementModuleProject,
            credential);
        var replacementModuleBuild =
            await controller.ExecuteLatestAsync(
                new ProjectCodeSdkBuildRequest(
                    replacementModuleWorkspace,
                    OutputRoot(
                        replacementModuleProject,
                        "replacement-module")));
        Assert.True(
            replacementModuleBuild.Succeeded,
            Render(replacementModuleBuild));
        var replacementModulePublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                replacementModuleBuild.Lease!,
                PublicationRoot(
                    replacementModuleProject,
                    "replacement-module"));
        Assert.True(
            replacementModulePublication.Succeeded,
            Render(replacementModulePublication));
        var replacementModuleCandidate =
            await ProjectCodeStagingCandidateAdmitter.AdmitAsync(
                replacementModulePublication.Receipt!);
        Assert.True(
            replacementModuleCandidate.Succeeded,
            Render(replacementModuleCandidate));
        var replacementModulePolicy =
            await ProjectCodeHostPolicySelector.SelectAsync(
                replacementModuleCandidate.Receipt!);
        Assert.True(
            replacementModulePolicy.Succeeded,
            Render(replacementModulePolicy));
        var replacementLoadImage =
            await ProjectCodePinnedLoadImageBuilder.BuildAsync(
                replacementModulePolicy.Receipt!);
        Assert.True(
            replacementLoadImage.Succeeded,
            Render(replacementLoadImage));
        Assert.NotEqual(
            loadImageSnapshot.ImageId,
            replacementLoadImage.Snapshot!.ImageId);
        AssertDiagnostic(
            await pinnedAssemblyLoader.LoadAsync(
                replacementLoadImage.Snapshot),
            "project-code.pinned-assembly-load.restart-required");

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
            Assert.False(
                await ProjectCodeHostPolicySelector
                    .IsPolicyCurrentAsync(modulePolicyReceipt));
            Assert.False(
                await ProjectCodePinnedLoadImageBuilder
                    .IsSnapshotCurrentAsync(loadImageSnapshot));
            var residentLoad =
                await pinnedAssemblyLoader.LoadAsync(loadImageSnapshot);
            Assert.True(residentLoad.Succeeded, Render(residentLoad));
            Assert.Same(pinnedAssemblyHost, residentLoad.Host);
            AssertDiagnostic(
                await ProjectCodePinnedLoadImageBuilder.BuildAsync(
                    modulePolicyReceipt),
                "project-code.pinned-load-image.policy-not-current");
            var stalePolicy =
                await ProjectCodeHostPolicySelector.SelectAsync(
                    moduleCandidateReceipt);
            AssertDiagnostic(
                stalePolicy,
                "project-code.host-policy.candidate-not-current");
            AssertNoAbsolutePathLeak(
                Render(stalePolicy),
                moduleProject.ProjectRoot,
                moduleProject.WorkspaceRoot,
                modulePublicationReceipt.AbsoluteRoot);
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

        using var moduleInitializerProject = new ProjectFixture();
        moduleInitializerProject.WriteEditorSource(
            "ModuleInitializer.cs",
            """
            using System.Runtime.CompilerServices;
            using Asharia.Editor.Extensions;

            namespace Fixture;

            internal static class Bootstrap
            {
                [ModuleInitializer]
                internal static void Initialize()
                {
                }
            }

            [EditorModule("fixture.module-initializer")]
            public sealed class ModuleInitializerModule : EditorModule
            {
                public override void Configure(EditorModuleBuilder editor)
                {
                }
            }

            """);
        var moduleInitializerWorkspace = await CreateWorkspaceAsync(
            moduleInitializerProject,
            credential);
        var moduleInitializerBuild =
            await controller.ExecuteLatestAsync(
                new ProjectCodeSdkBuildRequest(
                    moduleInitializerWorkspace,
                    OutputRoot(
                        moduleInitializerProject,
                        "module-initializer")));
        Assert.True(
            moduleInitializerBuild.Succeeded,
            Render(moduleInitializerBuild));
        var moduleInitializerPublication =
            await ProjectCodeArtifactPublisher.PublishAsync(
                moduleInitializerBuild.Lease!,
                PublicationRoot(
                    moduleInitializerProject,
                    "module-initializer"));
        Assert.True(
            moduleInitializerPublication.Succeeded,
            Render(moduleInitializerPublication));
        var moduleInitializerCandidate =
            await ProjectCodeStagingCandidateAdmitter.AdmitAsync(
                moduleInitializerPublication.Receipt!);
        Assert.True(
            moduleInitializerCandidate.Succeeded,
            Render(moduleInitializerCandidate));
        var moduleInitializerPolicy =
            await ProjectCodeHostPolicySelector.SelectAsync(
                moduleInitializerCandidate.Receipt!);
        Assert.True(
            moduleInitializerPolicy.Succeeded,
            Render(moduleInitializerPolicy));
        var rejectedModuleInitializer =
            await ProjectCodePinnedLoadImageBuilder.BuildAsync(
                moduleInitializerPolicy.Receipt!);
        AssertDiagnostic(
            rejectedModuleInitializer,
            "project-code.pinned-load-image.module-initializer-unsupported");
        AssertNoAbsolutePathLeak(
            Render(rejectedModuleInitializer),
            moduleInitializerProject.ProjectRoot,
            moduleInitializerProject.WorkspaceRoot,
            moduleInitializerPublication.Receipt!.AbsoluteRoot);

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

    private static void AssertDiagnostic(
        ProjectCodeHostPolicySelectionResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodePinnedLoadImageResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodePinnedAssemblyLoadResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodePinnedModuleConstructionResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodePinnedModuleConfigurationResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodePinnedModuleScopePreparationResult result,
        string code)
    {
        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == code);
    }

    private static void AssertDiagnostic(
        ProjectCodePinnedModuleScopeCommitResult result,
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

    private static string Render(
        ProjectCodeHostPolicySelectionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedLoadImageResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedAssemblyLoadResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedModuleTypeResolutionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedModuleConstructionResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedModuleConfigurationResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedModuleScopePreparationResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private static string Render(
        ProjectCodePinnedModuleScopeCommitResult result) =>
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
