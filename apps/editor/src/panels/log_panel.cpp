#include "panels/log_panel.hpp"

#include <imgui.h>
#include <span>
#include <string>

#include "editor_event.hpp"

namespace asharia::editor {

    const EditorPanelDesc& LogPanel::desc() const {
        return desc_;
    }

    void LogPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const std::string modeText =
            std::string{"Mode: "} + (context.smokeMode ? "smoke" : "interactive");
        ImGui::TextUnformatted("Editor shell initialized with GLFW + Vulkan + Dear ImGui.");
        ImGui::TextUnformatted(modeText.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Recent editor events:");
        const std::span<const EditorDiagnosticEvent> recentEvents =
            context.diagnosticsLog.recentEvents();
        if (recentEvents.empty()) {
            ImGui::TextUnformatted("No editor events this session.");
            return;
        }
        for (const EditorDiagnosticEvent& diagnostic : recentEvents) {
            const std::string eventText = std::to_string(diagnostic.sequence) + " " +
                                          std::string{editorEventKindName(diagnostic.event.kind)} +
                                          ": " + diagnostic.event.sourceId.value;
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(eventText.c_str());
        }
    }

} // namespace asharia::editor
