#include "panels/log_panel.hpp"

#include <imgui.h>
#include <span>
#include <string>

#include "editor_event.hpp"
#include "editor_i18n.hpp"
#include "editor_input_router.hpp"

namespace {

    struct LogPanelContext {
        const asharia::editor::EditorFrameUiContext* ui{};
        const asharia::editor::EditorInputSnapshot* input{};
        asharia::editor::EditorDiagnosticsLog* diagnosticsLog{};
    };

    std::string yesNo(const asharia::editor::EditorI18n& i18n, bool value) {
        return std::string{i18n.text(value ? "common.yes" : "common.no")};
    }

} // namespace

namespace asharia::editor {

    const EditorPanelDesc& LogPanel::desc() const {
        return desc_;
    }

    void LogPanel::drawLogPanel(EditorLogPanelDrawContext& context, EditorPanelState& state) {
        static_cast<void>(state);

        LogPanelContext panelContext{
            .ui = &context.ui,
            .input = &context.inputRouter.snapshot(),
            .diagnosticsLog = &context.diagnosticsLog,
        };
        const EditorI18n& i18n = panelContext.ui->i18n;
        const std::string modeText =
            std::string{i18n.text("log.mode")} + ": " +
            std::string{
                i18n.text(panelContext.ui->smokeMode ? "log.mode.smoke" : "log.mode.interactive")};
        const EditorInputSnapshot& input = *panelContext.input;
        const std::string inputCaptureText =
            std::string{i18n.text("log.inputCapture")} + ": " +
            std::string{i18n.text("log.mouse")} + "=" + yesNo(i18n, input.imguiWantsMouse) + ", " +
            std::string{i18n.text("log.keyboard")} + "=" + yesNo(i18n, input.imguiWantsKeyboard) +
            ", " + std::string{i18n.text("log.text")} + "=" +
            yesNo(i18n, input.imguiWantsTextInput);
        const std::string sceneViewInputText =
            std::string{i18n.text("log.sceneViewInput")} + ": " +
            std::string{i18n.text("log.hovered")} + "=" + yesNo(i18n, input.sceneViewHovered) +
            ", " + std::string{i18n.text("log.focused")} + "=" +
            yesNo(i18n, input.sceneViewFocused) + ", " +
            std::string{i18n.text("log.acceptsMouse")} + "=" +
            yesNo(i18n, input.sceneViewCanReceiveMouse) + ", " +
            std::string{i18n.text("log.shortcuts")} + "=" + yesNo(i18n, input.shortcutsEnabled);
        ImGui::TextUnformatted(i18n.text("log.initialized").data(),
                               i18n.text("log.initialized").data() +
                                   i18n.text("log.initialized").size());
        ImGui::TextUnformatted(modeText.c_str());
        ImGui::TextUnformatted(inputCaptureText.c_str());
        ImGui::TextUnformatted(sceneViewInputText.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(i18n.text("log.recentEvents").data(),
                               i18n.text("log.recentEvents").data() +
                                   i18n.text("log.recentEvents").size());
        const std::span<const EditorDiagnosticEvent> recentEvents =
            panelContext.diagnosticsLog->recentEvents();
        if (recentEvents.empty()) {
            ImGui::TextUnformatted(i18n.text("log.noEvents").data(),
                                   i18n.text("log.noEvents").data() +
                                       i18n.text("log.noEvents").size());
            return;
        }
        for (const EditorDiagnosticEvent& diagnostic : recentEvents) {
            const std::string eventText = std::to_string(diagnostic.sequence) + " " +
                                          editorEventDisplayText(diagnostic.event);
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(eventText.c_str());
        }
    }

} // namespace asharia::editor
