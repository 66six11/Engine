using System;
using System.Collections.Immutable;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.Application.Projects;

namespace Asharia.Studio.Application.Diagnostics;

public sealed record StudioUnexpectedOperationContext
{
    public StudioUnexpectedOperationContext(
        string code,
        string category,
        string component,
        StudioDiagnosticScope scope,
        Guid operationId,
        Guid correlationId,
        Guid? parentCorrelationId = null,
        string? remediation = null,
        StudioDataSensitivity sensitivity = StudioDataSensitivity.ProjectPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(code);
        ArgumentException.ThrowIfNullOrWhiteSpace(category);
        ArgumentException.ThrowIfNullOrWhiteSpace(component);
        ArgumentNullException.ThrowIfNull(scope);
        if (string.IsNullOrWhiteSpace(scope.Kind) ||
            string.IsNullOrWhiteSpace(scope.Identity) ||
            scope.Generation < 0)
        {
            throw new ArgumentException(
                "Diagnostic scope must have a kind, identity, and non-negative generation.",
                nameof(scope));
        }
        if (operationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Operation id must not be empty.",
                nameof(operationId));
        }
        if (correlationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Correlation id must not be empty.",
                nameof(correlationId));
        }
        if (parentCorrelationId == Guid.Empty)
        {
            throw new ArgumentException(
                "Parent correlation id must not be empty.",
                nameof(parentCorrelationId));
        }

        Code = code;
        Category = category;
        Component = component;
        Scope = scope;
        OperationId = operationId;
        CorrelationId = correlationId;
        ParentCorrelationId = parentCorrelationId;
        Remediation = string.IsNullOrWhiteSpace(remediation) ? null : remediation;
        Sensitivity = sensitivity;
    }

    public string Code { get; }

    public string Category { get; }

    public string Component { get; }

    public StudioDiagnosticScope Scope { get; }

    public Guid OperationId { get; }

    public Guid CorrelationId { get; }

    public Guid? ParentCorrelationId { get; }

    public string? Remediation { get; }

    public StudioDataSensitivity Sensitivity { get; }
}

public static class StudioOperationDiagnosticMapper
{
    private const string Package = "asharia.studio";

    public static StudioDiagnosticWrite? MapProjectSessionFailure(
        ProjectSessionOperationResult result,
        StudioUnexpectedOperationContext context)
    {
        ArgumentNullException.ThrowIfNull(result);
        ArgumentNullException.ThrowIfNull(context);
        if (result.Succeeded)
        {
            return null;
        }

        var failureKind = result.FailureKind ?? throw new ArgumentException(
            "A failed project operation must carry a failure kind.",
            nameof(result));

        var attributes = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(2);
        attributes.Add(new StudioDiagnosticAttribute(
            "failureKind",
            failureKind.ToString()));
        AddProjectEditId(attributes, result.OriginatingEditId);
        return CreateWrite(
            context,
            SafeProjectFailureMessage(failureKind),
            attributes.ToImmutable(),
            Severity(failureKind));
    }

    public static StudioDiagnosticWrite? MapDocumentTransitionFailure(
        ProjectDocumentTransitionResult result,
        StudioUnexpectedOperationContext context)
    {
        ArgumentNullException.ThrowIfNull(result);
        ArgumentNullException.ThrowIfNull(context);
        if (result.Status is ProjectDocumentTransitionStatus.Completed or
            ProjectDocumentTransitionStatus.Cancelled)
        {
            return null;
        }

        var attributes = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(3);
        attributes.Add(new StudioDiagnosticAttribute(
            "transitionStatus",
            result.Status.ToString()));

        var severity = StudioDiagnosticSeverity.Warning;
        if (result.ProjectOperation is ProjectSessionOperationResult operation)
        {
            var operationFailureKind = operation.FailureKind ?? throw new ArgumentException(
                "A failed document transition operation must carry a failure kind.",
                nameof(result));
            attributes.Add(new StudioDiagnosticAttribute(
                "failureKind",
                operationFailureKind.ToString()));
            AddProjectEditId(attributes, operation.OriginatingEditId);
            severity = Severity(operationFailureKind);
        }

        return CreateWrite(
            context,
            result.ProjectOperation?.FailureKind is ProjectSessionFailureKind failureKind
                ? SafeProjectFailureMessage(failureKind)
                : SafeTransitionFailureMessage(result.Status),
            attributes.ToImmutable(),
            severity);
    }

    public static StudioDiagnosticWrite MapUnexpectedException(
        StudioUnexpectedOperationContext context,
        string message,
        Exception exception,
        ProjectEditId? projectEditId = null)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        ArgumentNullException.ThrowIfNull(exception);

        var attributes = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(2);
        attributes.Add(new StudioDiagnosticAttribute(
            "exceptionType",
            exception.GetType().FullName ?? exception.GetType().Name));
        AddProjectEditId(attributes, projectEditId);
        return CreateWrite(context, message, attributes.ToImmutable());
    }

    public static StudioDiagnosticWrite MapUnclassifiedActionFailure(
        StudioActionId actionId,
        string message,
        StudioUnexpectedOperationContext context)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        ArgumentNullException.ThrowIfNull(context);

        return CreateWrite(
            context,
            message,
            [new StudioDiagnosticAttribute("actionId", actionId.Value)]);
    }

    public static StudioDiagnosticWrite MapActionRegistrationFailure(
        StudioActionRegistrationException exception,
        StudioUnexpectedOperationContext context)
    {
        ArgumentNullException.ThrowIfNull(exception);
        ArgumentNullException.ThrowIfNull(context);
        var attributes = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(6);
        attributes.Add(new StudioDiagnosticAttribute(
            "exceptionType",
            exception.GetType().FullName ?? exception.GetType().Name));
        attributes.Add(new StudioDiagnosticAttribute(
            "failureKind",
            exception.Kind.ToString()));
        attributes.Add(new StudioDiagnosticAttribute(
            "actionId",
            exception.ActionId.Value));
        attributes.Add(new StudioDiagnosticAttribute(
            "conflictingActionId",
            exception.ConflictingActionId.Value));
        if (exception.PlacementId is StudioActionPlacementId placementId)
        {
            attributes.Add(new StudioDiagnosticAttribute(
                "placementId",
                placementId.Value));
        }
        if (exception.Shortcut is StudioShortcutChord shortcut)
        {
            attributes.Add(new StudioDiagnosticAttribute(
                "shortcut",
                shortcut.ToString()));
        }

        return CreateWrite(
            context,
            "Studio action registration failed during shell startup.",
            attributes.ToImmutable());
    }

    private static StudioDiagnosticWrite CreateWrite(
        StudioUnexpectedOperationContext context,
        string message,
        ImmutableArray<StudioDiagnosticAttribute> attributes,
        StudioDiagnosticSeverity severity = StudioDiagnosticSeverity.Error) =>
        new(
            severity,
            StudioDiagnosticChannel.Problem,
            context.Code,
            context.Category,
            new StudioDiagnosticContext(
                StudioRecordOrigin.Managed,
                Package,
                context.Component,
                context.Scope,
                context.OperationId,
                context.CorrelationId,
                context.ParentCorrelationId,
                context.Sensitivity),
            message,
            context.Remediation,
            attributes);

    private static StudioDiagnosticSeverity Severity(
        ProjectSessionFailureKind failureKind) => failureKind switch
        {
            ProjectSessionFailureKind.IoFailure or
            ProjectSessionFailureKind.NativeUnavailable or
            ProjectSessionFailureKind.InternalError => StudioDiagnosticSeverity.Error,
            _ => StudioDiagnosticSeverity.Warning,
        };

    private static string SafeProjectFailureMessage(
        ProjectSessionFailureKind failureKind) => failureKind switch
        {
            ProjectSessionFailureKind.InvalidInput =>
                "The project operation rejected invalid input.",
            ProjectSessionFailureKind.InvalidProject or
            ProjectSessionFailureKind.InvalidScene or
            ProjectSessionFailureKind.InvalidObject or
            ProjectSessionFailureKind.InvalidTransform or
            ProjectSessionFailureKind.InvalidAssetReference =>
                "The project operation rejected an invalid authoritative target or value.",
            ProjectSessionFailureKind.AlreadyExists =>
                "The project operation rejected a duplicate value.",
            ProjectSessionFailureKind.Busy =>
                "The project operation could not run while another operation was active.",
            ProjectSessionFailureKind.RevisionConflict or
            ProjectSessionFailureKind.StaleDocumentTransition =>
                "The project operation was rejected because its document state was stale.",
            ProjectSessionFailureKind.IoFailure =>
                "The project operation failed while accessing persistent storage.",
            ProjectSessionFailureKind.NativeUnavailable =>
                "The project operation could not reach its native engine boundary.",
            ProjectSessionFailureKind.NoProject =>
                "The project operation requires an active project.",
            ProjectSessionFailureKind.InternalError =>
                "The project operation failed unexpectedly.",
            _ => throw new ArgumentOutOfRangeException(
                nameof(failureKind), failureKind, null),
        };

    private static string SafeTransitionFailureMessage(
        ProjectDocumentTransitionStatus status) => status switch
        {
            ProjectDocumentTransitionStatus.Stale =>
                "The document transition was rejected because its document state was stale.",
            ProjectDocumentTransitionStatus.Busy =>
                "The document transition could not start while another transition was active.",
            _ => "The document transition failed.",
        };

    private static void AddProjectEditId(
        ImmutableArray<StudioDiagnosticAttribute>.Builder attributes,
        ProjectEditId? projectEditId)
    {
        if (projectEditId is not ProjectEditId editId)
        {
            return;
        }
        if (!editId.IsValid)
        {
            throw new ArgumentException(
                "Project edit id must not be empty.",
                nameof(projectEditId));
        }

        // The process-wide diagnostic wire context has no project-domain field. Keep the
        // typed ingress value lossless as a canonical attribute until that schema evolves.
        attributes.Add(new StudioDiagnosticAttribute(
            "projectEditId",
            editId.Value.ToString("D")));
    }
}

public sealed class StudioOperationDiagnosticWriter
{
    private readonly IStudioDiagnosticHub diagnostics_;

    public StudioOperationDiagnosticWriter(IStudioDiagnosticHub diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        diagnostics_ = diagnostics;
    }

    public StudioProcessIdentity ProcessIdentity => diagnostics_.ProcessIdentity;

    public IStudioDiagnosticSource Source => diagnostics_;

    public StudioDiagnosticRecord? PublishProjectSessionFailure(
        ProjectSessionOperationResult result,
        StudioUnexpectedOperationContext context)
    {
        var write = StudioOperationDiagnosticMapper.MapProjectSessionFailure(
            result,
            context);
        return write is null ? null : diagnostics_.PublishDiagnostic(write);
    }

    public StudioDiagnosticRecord? PublishDocumentTransitionFailure(
        ProjectDocumentTransitionResult result,
        StudioUnexpectedOperationContext context)
    {
        var write = StudioOperationDiagnosticMapper.MapDocumentTransitionFailure(
            result,
            context);
        return write is null ? null : diagnostics_.PublishDiagnostic(write);
    }

    public StudioDiagnosticRecord PublishUnexpectedException(
        StudioUnexpectedOperationContext context,
        string message,
        Exception exception,
        ProjectEditId? projectEditId = null) =>
        diagnostics_.PublishDiagnostic(
            StudioOperationDiagnosticMapper.MapUnexpectedException(
                context,
                message,
                exception,
                projectEditId));

    public StudioDiagnosticRecord PublishUnclassifiedActionFailure(
        StudioActionId actionId,
        string message,
        StudioUnexpectedOperationContext context) =>
        diagnostics_.PublishDiagnostic(
            StudioOperationDiagnosticMapper.MapUnclassifiedActionFailure(
                actionId,
                message,
                context));

    public StudioDiagnosticRecord PublishActionRegistrationFailure(
        StudioActionRegistrationException exception,
        StudioUnexpectedOperationContext context) =>
        diagnostics_.PublishDiagnostic(
            StudioOperationDiagnosticMapper.MapActionRegistrationFailure(
                exception,
                context));
}
