using System;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Diagnostics;

public sealed class StudioOperationDiagnosticWriterTests
{
    [Fact]
    public void Internal_project_session_failure_preserves_operation_and_edit_context()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var operationId = Guid.NewGuid();
        var correlationId = Guid.NewGuid();
        var parentCorrelationId = Guid.NewGuid();
        var projectEditId = ProjectEditId.CreateNew();
        var scope = new StudioDiagnosticScope(
            "project-session",
            Guid.NewGuid().ToString("D"),
            Generation: 7);
        var context = new StudioUnexpectedOperationContext(
            "studio.project-session.transform.failed",
            "project-session",
            "project-session",
            scope,
            operationId,
            correlationId,
            parentCorrelationId,
            "Refresh the authoritative scene before retrying.");
        var result = ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            ProjectSessionFailureKind.InternalError,
            "The authoritative Transform outcome is unknown.",
            projectEditId);

        var record = writer.PublishProjectSessionFailure(result, context);

        Assert.NotNull(record);
        Assert.Equal(StudioDiagnosticSeverity.Error, record.Severity);
        Assert.Equal(StudioDiagnosticChannel.Problem, record.Channel);
        Assert.Equal(context.Code, record.Code);
        Assert.Equal(scope, record.Context.Scope);
        Assert.Equal(operationId, record.Context.OperationId);
        Assert.Equal(correlationId, record.Context.CorrelationId);
        Assert.Equal(parentCorrelationId, record.Context.ParentCorrelationId);
        Assert.Equal(StudioDataSensitivity.ProjectPath, record.Context.Sensitivity);
        Assert.Equal(
            ProjectSessionFailureKind.InternalError.ToString(),
            Attribute(record, "failureKind"));
        Assert.Equal(
            projectEditId.Value.ToString("D"),
            Attribute(record, "projectEditId"));
        Assert.Equal("The project operation failed unexpectedly.", record.Message);
        Assert.DoesNotContain("unknown", record.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Same(record, hub.GetLatestDiagnostic());
    }

    [Theory]
    [InlineData(ProjectSessionFailureKind.InvalidInput)]
    [InlineData(ProjectSessionFailureKind.Busy)]
    [InlineData(ProjectSessionFailureKind.RevisionConflict)]
    [InlineData(ProjectSessionFailureKind.NoProject)]
    [InlineData(ProjectSessionFailureKind.StaleDocumentTransition)]
    public void Expected_project_session_failure_is_a_single_warning(
        ProjectSessionFailureKind failureKind)
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var result = ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            failureKind,
            "Expected typed rejection.");

        var record = writer.PublishProjectSessionFailure(result, Context());

        Assert.NotNull(record);
        Assert.Equal(StudioDiagnosticSeverity.Warning, record.Severity);
        Assert.Equal(failureKind.ToString(), Attribute(record, "failureKind"));
        Assert.Single(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Theory]
    [InlineData(ProjectSessionFailureKind.IoFailure)]
    [InlineData(ProjectSessionFailureKind.NativeUnavailable)]
    [InlineData(ProjectSessionFailureKind.InternalError)]
    public void Operational_project_session_failure_is_an_error(
        ProjectSessionFailureKind failureKind)
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var result = ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            failureKind,
            "The operation failed at its authoritative boundary.");

        var record = writer.PublishProjectSessionFailure(result, Context());

        Assert.NotNull(record);
        Assert.Equal(StudioDiagnosticSeverity.Error, record.Severity);
        Assert.Equal(failureKind.ToString(), Attribute(record, "failureKind"));
        Assert.Single(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Fact]
    public void Project_failure_does_not_publish_raw_adapter_details()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        const string privateDetail = @"C:\Users\private\secret-token";
        var result = ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            ProjectSessionFailureKind.InternalError,
            privateDetail);

        var record = writer.PublishProjectSessionFailure(result, Context());

        Assert.NotNull(record);
        Assert.Equal("The project operation failed unexpectedly.", record.Message);
        Assert.DoesNotContain(privateDetail, record.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Successful_project_session_operation_is_not_published()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var result = ProjectSessionOperationResult.Success(
            ProjectSessionSnapshot.NoProject,
            "No project is active.");

        var record = writer.PublishProjectSessionFailure(result, Context());

        Assert.Null(record);
        Assert.Empty(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Fact]
    public void Busy_document_transition_is_a_single_typed_warning()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);

        var record = writer.PublishDocumentTransitionFailure(
            ProjectDocumentTransitionResult.Busy(),
            Context());

        Assert.NotNull(record);
        Assert.Equal(StudioDiagnosticSeverity.Warning, record.Severity);
        Assert.Equal(
            ProjectDocumentTransitionStatus.Busy.ToString(),
            Attribute(record, "transitionStatus"));
        Assert.DoesNotContain(
            record.Attributes,
            attribute => attribute.Name == "failureKind");
        Assert.Single(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Fact]
    public void Save_failed_document_transition_preserves_both_typed_failures()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var operation = ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            ProjectSessionFailureKind.IoFailure,
            "The authoritative save failed.");

        var record = writer.PublishDocumentTransitionFailure(
            ProjectDocumentTransitionResult.SaveFailed(operation),
            Context());

        Assert.NotNull(record);
        Assert.Equal(StudioDiagnosticSeverity.Error, record.Severity);
        Assert.Equal(
            ProjectDocumentTransitionStatus.SaveFailed.ToString(),
            Attribute(record, "transitionStatus"));
        Assert.Equal(
            ProjectSessionFailureKind.IoFailure.ToString(),
            Attribute(record, "failureKind"));
        Assert.Single(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Theory]
    [InlineData(ProjectDocumentTransitionStatus.Completed)]
    [InlineData(ProjectDocumentTransitionStatus.Cancelled)]
    public void Successful_or_cancelled_document_transition_is_not_published(
        ProjectDocumentTransitionStatus status)
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var result = status == ProjectDocumentTransitionStatus.Completed
            ? ProjectDocumentTransitionResult.Completed()
            : ProjectDocumentTransitionResult.Cancelled();

        var record = writer.PublishDocumentTransitionFailure(result, Context());

        Assert.Null(record);
        Assert.Empty(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    [Fact]
    public void Internal_project_session_failure_without_an_edit_id_is_published()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var result = ProjectSessionOperationResult.Failed(
            ProjectSessionSnapshot.NoProject,
            ProjectSessionFailureKind.InternalError,
            "Unexpected session failure without an edit scope.");

        var record = writer.PublishProjectSessionFailure(result, Context());

        Assert.NotNull(record);
        Assert.Equal(
            ProjectSessionFailureKind.InternalError.ToString(),
            Attribute(record, "failureKind"));
        Assert.DoesNotContain(
            record.Attributes,
            attribute => attribute.Name == "projectEditId");
    }

    [Fact]
    public void Shell_exception_projection_records_only_the_typed_exception_identity()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);
        var projectEditId = ProjectEditId.CreateNew();
        var exception = new InvalidOperationException(
            "private adapter detail must not become the diagnostic message");

        var record = writer.PublishUnexpectedException(
            Context(),
            "The shell operation failed unexpectedly.",
            exception,
            projectEditId);

        Assert.Equal("The shell operation failed unexpectedly.", record.Message);
        Assert.DoesNotContain(exception.Message, record.Message, StringComparison.Ordinal);
        Assert.Equal(
            typeof(InvalidOperationException).FullName,
            Attribute(record, "exceptionType"));
        Assert.Equal(
            projectEditId.Value.ToString("D"),
            Attribute(record, "projectEditId"));
    }

    [Fact]
    public void Shell_exception_without_an_edit_id_is_published()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);

        var record = writer.PublishUnexpectedException(
            Context(),
            "The shell operation failed unexpectedly.",
            new InvalidOperationException());

        Assert.Equal(
            typeof(InvalidOperationException).FullName,
            Attribute(record, "exceptionType"));
        Assert.DoesNotContain(
            record.Attributes,
            attribute => attribute.Name == "projectEditId");
    }

    [Fact]
    public void Operation_context_rejects_empty_correlation_identity()
    {
        Assert.Throws<ArgumentException>(() => new StudioUnexpectedOperationContext(
            "studio.shell.operation.failed",
            "shell",
            "shell",
            new StudioDiagnosticScope("process", Guid.NewGuid().ToString("D"), 1),
            Guid.NewGuid(),
            Guid.Empty));
    }

    [Fact]
    public void Action_registration_collision_is_a_safe_typed_problem()
    {
        var registry = new StudioActionRegistry();
        var actionId = new StudioActionId("studio.test.duplicate");
        var definition = new StudioActionDefinition(
            actionId,
            "Test",
            "Tests registration diagnostics.",
            "Test");
        var placement = new StudioActionPlacement(
            new StudioActionPlacementId("studio.test.duplicate.menu"),
            StudioActionPlacementKind.Menu,
            "Test/Duplicate",
            "test",
            order: 1,
            StudioActionScope.Workspace);
        registry.Register(
            definition,
            [placement],
            _ => StudioActionState.Available(),
            (_, _) => ValueTask.FromResult(
                StudioActionCompletion.Succeeded("Test action completed.")));
        var exception = Assert.Throws<StudioActionRegistrationException>(() =>
            registry.Register(
                definition,
                [placement],
                _ => StudioActionState.Available(),
                (_, _) => ValueTask.FromResult(
                    StudioActionCompletion.Succeeded("Test action completed."))));
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 4, logCapacity: 2);
        var writer = new StudioOperationDiagnosticWriter(hub);

        var record = writer.PublishActionRegistrationFailure(exception, Context());

        Assert.Equal("Studio action registration failed during shell startup.", record.Message);
        Assert.DoesNotContain(exception.Message, record.Message, StringComparison.Ordinal);
        Assert.Equal(exception.Kind.ToString(), Attribute(record, "failureKind"));
        Assert.Equal(actionId.Value, Attribute(record, "actionId"));
        Assert.Equal(actionId.Value, Attribute(record, "conflictingActionId"));
        Assert.Equal(
            typeof(StudioActionRegistrationException).FullName,
            Attribute(record, "exceptionType"));
        Assert.Single(hub.ReadDiagnostics(maxCount: 4).Items);
    }

    private static StudioUnexpectedOperationContext Context() =>
        new(
            "studio.shell.operation.failed",
            "shell",
            "shell",
            new StudioDiagnosticScope("process", Guid.NewGuid().ToString("D"), 1),
            Guid.NewGuid(),
            Guid.NewGuid());

    private static string Attribute(
        StudioDiagnosticRecord record,
        string name) =>
        record.Attributes.Single(attribute => attribute.Name == name).Value;
}
