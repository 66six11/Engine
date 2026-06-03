#include "panels/render_graph_panel.hpp"

#include <imgui.h>
#include <optional>

#include "editor_i18n.hpp"
#include "editor_render_graph_snapshot.hpp"
#include "panels/render_graph_snapshot_view.hpp"

namespace asharia::editor {
    namespace {

        struct RenderGraphPanelContext {
            const EditorFrameUiContext* ui{};
            EditorRenderGraphSnapshotProvider* snapshots{};
        };

    } // namespace

    const EditorPanelDesc& RenderGraphPanel::desc() const {
        return desc_;
    }

    void RenderGraphPanel::prepareWindow(EditorPanelWindowContext& context,
                                         EditorPanelState& state) {
        static_cast<void>(state);
        if (context.ui.smokeMode) {
            ImGui::SetNextWindowSize(ImVec2{560.0F, 320.0F}, ImGuiCond_Always);
        }
    }

    void RenderGraphPanel::drawRenderGraphPanel(EditorRenderGraphPanelDrawContext& context,
                                                EditorPanelState& state) {
        static_cast<void>(state);

        RenderGraphPanelContext panelContext{
            .ui = &context.ui,
            .snapshots = &context.snapshots,
        };
        const std::optional<EditorRenderGraphSnapshot> liveSnapshot =
            panelContext.snapshots->latestLiveRenderGraphSnapshot();
        const bool snapshotVisible = liveSnapshot && liveSnapshot->snapshot != nullptr;
        panelContext.snapshots->notifyLiveRenderGraphViewDrawn(snapshotVisible);
        if (!snapshotVisible) {
            const std::string_view text = panelContext.ui->i18n.text("renderGraph.noLiveSnapshot");
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            return;
        }

        drawRenderGraphSnapshotView(
            RenderGraphSnapshotViewDesc{
                .sourceLabel = panelContext.ui->i18n.text("renderGraph.liveSource"),
                .statusLabel = panelContext.ui->i18n.text("renderGraph.status.latestCompiled"),
                .viewKind = liveSnapshot->viewKind,
                .requestedExtent = liveSnapshot->requestedExtent,
                .submittedFrameEpoch = liveSnapshot->submittedFrameEpoch,
                .i18n = panelContext.ui->i18n,
            },
            *liveSnapshot->snapshot);
    }

} // namespace asharia::editor
