#include "panels/log_panel.hpp"

#include <cstddef>
#include <imgui.h>
#include <string>

#include "editor_event.hpp"

namespace asharia::editor {

    const EditorPanelDesc& LogPanel::desc() const {
        return desc_;
    }

    void LogPanel::appendFrameEvents(const EditorFrameContext& context) {
        constexpr std::size_t kMaxRecentEvents = 12;

        for (const EditorEvent& event : context.eventQueue.events()) {
            recentEvents_.push_back(std::string{editorEventKindName(event.kind)} + ": " +
                                    event.sourceId.value);
        }
        if (recentEvents_.size() > kMaxRecentEvents) {
            recentEvents_.erase(recentEvents_.begin(),
                                recentEvents_.end() -
                                    static_cast<std::ptrdiff_t>(kMaxRecentEvents));
        }
    }

    void LogPanel::draw(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        appendFrameEvents(context);
        const std::string modeText =
            std::string{"Mode: "} + (context.smokeMode ? "smoke" : "interactive");
        ImGui::TextUnformatted("Editor shell initialized with GLFW + Vulkan + Dear ImGui.");
        ImGui::TextUnformatted(modeText.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Recent editor events:");
        if (recentEvents_.empty()) {
            ImGui::TextUnformatted("No editor events this session.");
            return;
        }
        for (const std::string& event : recentEvents_) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(event.c_str());
        }
    }

} // namespace asharia::editor
