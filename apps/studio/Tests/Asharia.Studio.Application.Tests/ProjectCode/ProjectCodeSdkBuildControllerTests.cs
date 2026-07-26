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
        File.AppendAllText(
            first.Lease!.Output.Files[0].AbsolutePath,
            "drift");

        Assert.False(
            await ProjectCodeSdkBuildController
                .IsRawOutputCurrentAsync(first.Lease));
        var driftedInspection =
            await ProjectCodeArtifactInspector.InspectAsync(first.Lease);
        Assert.Contains(
            driftedInspection.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.raw-output-not-current");

        var second = await controller.ExecuteLatestAsync(
            new ProjectCodeSdkBuildRequest(
                workspace,
                OutputRoot(project, "output-revoke")));
        Assert.True(second.Succeeded, Render(second));
        second.Lease!.Revoke();
        Assert.False(second.Lease.IsCurrent);
        Assert.False(
            await ProjectCodeSdkBuildController
                .IsRawOutputCurrentAsync(second.Lease));
        var revokedInspection =
            await ProjectCodeArtifactInspector.InspectAsync(second.Lease);
        Assert.Contains(
            revokedInspection.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.artifact.raw-output-not-current");
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

    private static string FileEnvelope(
        ProjectCodeRawBuildOutputFile file) =>
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

    private static void AssertDiagnostic(
        ProjectCodeArtifactInspectionResult result,
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
