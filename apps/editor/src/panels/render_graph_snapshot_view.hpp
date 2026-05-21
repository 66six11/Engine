#pragma once

#include <cstdint>
#include <string_view>

#include "asharia/rendergraph/render_graph.hpp"

#include "editor_viewport.hpp"

namespace asharia::editor {

    struct RenderGraphSnapshotViewDesc {
        std::string_view sourceLabel;
        std::string_view statusLabel;
        EditorViewportKind viewKind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        std::uint64_t submittedFrameEpoch{};
    };

    void drawRenderGraphSnapshotView(const RenderGraphSnapshotViewDesc& desc,
                                     const asharia::RenderGraphDiagnosticsSnapshot& snapshot);

} // namespace asharia::editor
