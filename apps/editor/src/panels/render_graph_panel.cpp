#include "panels/render_graph_panel.hpp"

#include <imgui.h>
#include <optional>

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
            ImGui::TextUnformatted("No live RenderGraph snapshot yet.");
            return;
        }

        drawRenderGraphSnapshotView(
            RenderGraphSnapshotViewDesc{
                .sourceLabel = "Live RG View",
                .statusLabel = "latest compiled RenderView",
                .viewKind = liveSnapshot->viewKind,
                .requestedExtent = liveSnapshot->requestedExtent,
                .submittedFrameEpoch = liveSnapshot->submittedFrameEpoch,
            },
            *liveSnapshot->snapshot);
    }

} // namespace asharia::editor
