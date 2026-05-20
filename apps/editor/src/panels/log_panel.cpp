#include "panels/log_panel.hpp"

#include <imgui.h>
#include <span>
#include <string>

#include "editor_event.hpp"
#include "editor_input_router.hpp"

namespace {

    const char* yesNo(bool value) {
        return value ? "yes" : "no";
    }

} // namespace

namespace asharia::editor {

    const EditorPanelDesc& LogPanel::desc() const {
        return desc_;
    }

    void LogPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        const std::string modeText =
            std::string{"Mode: "} + (context.smokeMode ? "smoke" : "interactive");
        const EditorInputSnapshot& input = context.inputRouter.snapshot();
        const std::string inputCaptureText =
            "Input capture: mouse=" + std::string{yesNo(input.imguiWantsMouse)} +
            ", keyboard=" + yesNo(input.imguiWantsKeyboard) +
            ", text=" + yesNo(input.imguiWantsTextInput);
        const std::string sceneViewInputText =
            "Scene View input: hovered=" + std::string{yesNo(input.sceneViewHovered)} +
            ", focused=" + yesNo(input.sceneViewFocused) +
            ", accepts mouse=" + yesNo(input.sceneViewCanReceiveMouse) +
            ", shortcuts=" + yesNo(input.shortcutsEnabled);
        ImGui::TextUnformatted("Editor shell initialized with GLFW + Vulkan + Dear ImGui.");
        ImGui::TextUnformatted(modeText.c_str());
        ImGui::TextUnformatted(inputCaptureText.c_str());
        ImGui::TextUnformatted(sceneViewInputText.c_str());
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
