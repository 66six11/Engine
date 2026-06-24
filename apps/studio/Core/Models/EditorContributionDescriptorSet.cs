using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record EditorContributionDescriptorSet(
    EditorContributionSourceId SourceId,
    EditorContributionSourceKind SourceKind,
    IReadOnlyList<EditorPanelContributionDescriptor> Panels,
    IReadOnlyList<EditorActionContributionDescriptor> Actions,
    IReadOnlyList<EditorDiagnosticSourceDescriptor> DiagnosticSources);
