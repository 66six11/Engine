#include "panels/render_graph_panel.hpp"

#include <imgui.h>
#include <optional>

#include "editor_i18n.hpp"
#include "editor_render_graph_snapshot.hpp"
#include "panels/render_graph_snapshot_view.hpp"

namespace asharia::editor {

    const EditorPanelDesc& RenderGraphPanel::desc() const {
        return desc_;
    }

    void RenderGraphPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);
        if (context.smokeMode) {
            ImGui::SetNextWindowSize(ImVec2{560.0F, 320.0F}, ImGuiCond_Always);
        }
    }

    void RenderGraphPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const std::optional<EditorRenderGraphSnapshot> liveSnapshot =
            context.renderGraphSnapshots.latestLiveRenderGraphSnapshot();
        const bool snapshotVisible = liveSnapshot && liveSnapshot->snapshot != nullptr;
        context.renderGraphSnapshots.notifyLiveRenderGraphViewDrawn(snapshotVisible);
        if (!snapshotVisible) {
            const std::string_view text = context.i18n.text("renderGraph.noLiveSnapshot");
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            return;
        }

        drawRenderGraphSnapshotView(
            RenderGraphSnapshotViewDesc{
                .sourceLabel = context.i18n.text("renderGraph.liveSource"),
                .statusLabel = context.i18n.text("renderGraph.status.latestCompiled"),
                .viewKind = liveSnapshot->viewKind,
                .requestedExtent = liveSnapshot->requestedExtent,
                .submittedFrameEpoch = liveSnapshot->submittedFrameEpoch,
                .i18n = context.i18n,
            },
            *liveSnapshot->snapshot);
    }

} // namespace asharia::editor
