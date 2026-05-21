#include "panels/frame_debugger_panel.hpp"

#include <imgui.h>
#include <optional>
#include <string>

#include "editor_frame_debugger.hpp"
#include "panels/render_graph_snapshot_view.hpp"

namespace asharia::editor {

    const EditorPanelDesc& FrameDebuggerPanel::desc() const {
        return desc_;
    }

    void FrameDebuggerPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);
        if (context.smokeMode) {
            ImGui::SetNextWindowSize(ImVec2{560.0F, 320.0F}, ImGuiCond_Always);
        }
    }

    void FrameDebuggerPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const std::optional<EditorFrameDebugCapture>& pausedCapture =
            context.frameDebugger.pausedCapture();
        const std::optional<EditorFrameDebugCapture>& latestCapture =
            pausedCapture ? pausedCapture : context.frameDebugger.latestCapture();
        context.frameDebugger.notifyFrameDebugRenderGraphViewDrawn(latestCapture.has_value());
        if (!latestCapture) {
            ImGui::TextUnformatted("No frame debug capture.");
            return;
        }

        const std::string status =
            pausedCapture ? "frozen captured frame" : "latest captured frame, not paused";
        drawRenderGraphSnapshotView(
            RenderGraphSnapshotViewDesc{
                .sourceLabel = "Frame Debug RG View",
                .statusLabel = status,
                .viewKind = latestCapture->viewKind,
                .requestedExtent = latestCapture->requestedExtent,
                .submittedFrameEpoch = latestCapture->submittedFrameEpoch,
            },
            latestCapture->diagnostics.renderGraph);
    }

} // namespace asharia::editor
