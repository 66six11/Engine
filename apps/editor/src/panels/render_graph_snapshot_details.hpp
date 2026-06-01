#pragma once

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

namespace asharia::editor {

    class EditorI18n;

    void drawRenderGraphSnapshotDetails(const EditorI18n& i18n,
                                        const asharia::RenderGraphDiagnosticsSnapshot& snapshot);

} // namespace asharia::editor
