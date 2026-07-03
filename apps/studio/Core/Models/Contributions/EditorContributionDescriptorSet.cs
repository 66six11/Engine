using System.Collections.Generic;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Models.Panels;

namespace Editor.Core.Models.Contributions;

public sealed record EditorContributionDescriptorSet(
    EditorContributionSourceId SourceId,
    EditorContributionSourceKind SourceKind,
    IReadOnlyList<EditorPanelContributionDescriptor> Panels,
    IReadOnlyList<EditorActionContributionDescriptor> Actions,
    IReadOnlyList<EditorDiagnosticSourceDescriptor> DiagnosticSources);
