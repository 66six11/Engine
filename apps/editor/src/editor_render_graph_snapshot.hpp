#pragma once

#include <cstdint>
#include <optional>

#include "asharia/rendergraph/render_graph.hpp"

#include "editor_viewport.hpp"

namespace asharia::editor {

    struct EditorRenderGraphSnapshot {
        EditorViewportKind viewKind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        std::uint64_t submittedFrameEpoch{};
        const asharia::RenderGraphDiagnosticsSnapshot* snapshot{};
    };

    class EditorRenderGraphSnapshotProvider {
    public:
        EditorRenderGraphSnapshotProvider() = default;
        EditorRenderGraphSnapshotProvider(const EditorRenderGraphSnapshotProvider&) = delete;
        EditorRenderGraphSnapshotProvider&
        operator=(const EditorRenderGraphSnapshotProvider&) = delete;
        EditorRenderGraphSnapshotProvider(EditorRenderGraphSnapshotProvider&&) = delete;
        EditorRenderGraphSnapshotProvider& operator=(EditorRenderGraphSnapshotProvider&&) = delete;
        virtual ~EditorRenderGraphSnapshotProvider() = default;

        [[nodiscard]] virtual std::optional<EditorRenderGraphSnapshot>
        latestLiveRenderGraphSnapshot() const = 0;
        virtual void notifyLiveRenderGraphViewDrawn(bool snapshotVisible) = 0;
    };

} // namespace asharia::editor
