#pragma once

#include <cstdint>
#include <string_view>

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

#include "editor_viewport.hpp"

namespace asharia::editor {

    class EditorI18n;

    struct RenderGraphSnapshotViewDesc {
        std::string_view sourceLabel;
        std::string_view statusLabel;
        EditorViewportKind viewKind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        std::uint64_t submittedFrameEpoch{};
        const EditorI18n& i18n;
    };

    void drawRenderGraphSnapshotView(const RenderGraphSnapshotViewDesc& desc,
                                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot);

} // namespace asharia::editor
