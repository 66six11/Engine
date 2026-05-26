#include "imgui_editor_shell.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>

#include "editor_dock_layout.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_ui.hpp"

namespace asharia::editor {

    namespace {

        constexpr float kCommandBarHeight = 34.0F;
        constexpr float kStatusBarHeight = 24.0F;

        [[nodiscard]] ImGuiID editorDockspaceId() {
            return ImGui::GetID("asharia-editor-dockspace");
        }

        [[nodiscard]] std::string actionLabel(const EditorActionDesc& action,
                                              const EditorI18n& i18n) {
            return i18n.label(EditorI18nLabelDesc{
                .key = action.labelKey,
                .stableId = action.id.value,
                .fallback = action.label,
            });
        }

        void drawActionMenuItem(EditorActionRegistry& actionRegistry, EditorContext& editorContext,
                                std::string_view actionId, bool selected = false) {
            const EditorActionDesc* action = actionRegistry.findAction(actionId);
            if (action == nullptr) {
                return;
            }

            const std::string label = actionLabel(*action, editorContext.i18n());
            const char* shortcut = action->shortcut.empty() ? nullptr : action->shortcut.c_str();
            if (ImGui::MenuItem(label.c_str(), shortcut, selected, action->enabled)) {
                static_cast<void>(
                    actionRegistry.invoke(action->id.value, editorContext.actionInvokeContext()));
            }
        }

        void drawActionButton(EditorActionRegistry& actionRegistry, EditorContext& editorContext,
                              std::string_view actionId) {
            const EditorActionDesc* action = actionRegistry.findAction(actionId);
            if (action == nullptr) {
                return;
            }

            const std::string label = actionLabel(*action, editorContext.i18n());
            ImGui::BeginDisabled(!action->enabled);
            if (ImGui::SmallButton(label.c_str()) && action->enabled) {
                static_cast<void>(
                    actionRegistry.invoke(action->id.value, editorContext.actionInvokeContext()));
            }
            ImGui::EndDisabled();
        }

        [[nodiscard]] bool drawToolbarSlot(EditorActionRegistry& actionRegistry,
                                           EditorContext& editorContext, EditorToolbarSlot slot) {
            bool drewAction = false;
            editorContext.tools().visitToolbarActions(
                slot, [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                    static_cast<void>(tool);
                    if (drewAction) {
                        ImGui::SameLine();
                    }
                    drawActionButton(actionRegistry, editorContext, action.actionId);
                    drewAction = true;
                });
            return drewAction;
        }

        [[nodiscard]] bool hasToolbarSlot(const EditorContext& editorContext,
                                          EditorToolbarSlot slot) {
            bool hasAction = false;
            editorContext.tools().visitToolbarActions(
                slot, [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                    static_cast<void>(tool);
                    static_cast<void>(action);
                    hasAction = true;
                });
            return hasAction;
        }

        void text(std::string_view value) {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }

        void sameLineSeparator() {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            text("|");
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        [[nodiscard]] std::string extentText(EditorExtent2D extent) {
            return std::to_string(extent.width) + "x" + std::to_string(extent.height);
        }

    } // namespace

    void drawEditorDockspace(EditorContext& editorContext) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) {
            return;
        }

        const ImGuiID dockspaceId = editorDockspaceId();
        const bool resetRequested = editorContext.workspace().consumeLayoutResetRequest();
        if (!editorDockLayoutExists(dockspaceId) || resetRequested) {
            buildEditorDockLayout(EditorDockLayoutBuildDesc{
                .panelRegistry = editorContext.panelRegistry(),
                .i18n = editorContext.i18n(),
                .viewport = *viewport,
                .dockspaceId = dockspaceId,
                .preset = editorContext.workspace().activePreset(),
            });
            editorContext.workspace().notifyLayoutApplied();
        }

        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
    }

    void drawEditorMainMenu(EditorActionRegistry& actionRegistry, EditorContext& editorContext) {
        if (ImGui::BeginMainMenuBar()) {
            const EditorI18n& i18n = editorContext.i18n();
            const std::string fileMenu = i18n.label(EditorI18nLabelDesc{
                .key = "menu.file",
                .stableId = "menu.file",
                .fallback = "File",
            });
            const std::string viewMenu = i18n.label(EditorI18nLabelDesc{
                .key = "menu.view",
                .stableId = "menu.view",
                .fallback = "View",
            });
            const std::string debugMenu = i18n.label(EditorI18nLabelDesc{
                .key = "menu.debug",
                .stableId = "menu.debug",
                .fallback = "Debug",
            });

            if (ImGui::BeginMenu(fileMenu.c_str())) {
                drawActionMenuItem(actionRegistry, editorContext, "file.new-scene");
                drawActionMenuItem(actionRegistry, editorContext, "file.open");
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, editorContext, "file.exit");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(viewMenu.c_str())) {
                drawActionMenuItem(actionRegistry, editorContext, "view.scene-view",
                                   editorContext.panelRegistry().isOpen("scene-view"));
                drawActionMenuItem(actionRegistry, editorContext, "view.log",
                                   editorContext.panelRegistry().isOpen("log"));
                drawActionMenuItem(actionRegistry, editorContext, "view.render-graph",
                                   editorContext.panelRegistry().isOpen("render-graph"));
                drawActionMenuItem(actionRegistry, editorContext, "view.frame-debugger",
                                   editorContext.panelRegistry().isOpen("frame-debugger"));
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, editorContext, "view.ui-style-preview",
                                   editorContext.panelRegistry().isOpen("ui-style-preview"));
                drawActionMenuItem(actionRegistry, editorContext, "view.editor-settings",
                                   editorContext.panelRegistry().isOpen("editor-settings"));
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, editorContext, "view.reset-layout");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(debugMenu.c_str())) {
                drawActionMenuItem(actionRegistry, editorContext, "debug.capture-frame");
                drawActionMenuItem(actionRegistry, editorContext, "debug.resume-frame");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void drawEditorCommandBar(EditorActionRegistry& actionRegistry, EditorContext& editorContext) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) {
            return;
        }

        const EditorUiTheme& theme = editorUiTheme();
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toImGuiEncodedSrgbVec4(theme.panelBackground));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 4.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{5.0F, 4.0F});
        if (ImGui::BeginViewportSideBar("##asharia-editor-command-bar", viewport, ImGuiDir_Up,
                                        kCommandBarHeight, kWindowFlags)) {
            const bool hasDebug = hasToolbarSlot(editorContext, EditorToolbarSlot::Debug);
            const bool hasView = hasToolbarSlot(editorContext, EditorToolbarSlot::View);
            const bool hasUtility = hasToolbarSlot(editorContext, EditorToolbarSlot::Utility);
            if (hasDebug) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, editorContext, EditorToolbarSlot::Debug));
            }
            if (hasDebug && hasView) {
                sameLineSeparator();
            }
            if (hasView) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, editorContext, EditorToolbarSlot::View));
            }
            if ((hasDebug || hasView) && hasUtility) {
                sameLineSeparator();
            }
            if (hasUtility) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, editorContext, EditorToolbarSlot::Utility));
            }

            const std::string themeLabel = std::string{"Theme: "} + std::string{theme.name};
            const float labelWidth = ImGui::CalcTextSize(themeLabel.c_str()).x;
            const float labelX = ImGui::GetWindowContentRegionMax().x - labelWidth;
            if (labelX > ImGui::GetCursorPosX() + 12.0F) {
                ImGui::SameLine(labelX);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                text(themeLabel);
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void drawEditorStatusBar(const EditorFrameContext& frameContext,
                             const EditorContext& editorContext) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) {
            return;
        }

        const EditorUiTheme& theme = editorUiTheme();
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toImGuiEncodedSrgbVec4(theme.menuBackground));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 3.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0F, 3.0F});
        if (ImGui::BeginViewportSideBar("##asharia-editor-status-bar", viewport, ImGuiDir_Down,
                                        kStatusBarHeight, kWindowFlags)) {
            text("Ready");
            sameLineSeparator();
            text(editorContext.frameDebugger().stateName());
            sameLineSeparator();
            const std::string frameText =
                std::string{"Frame "} + std::to_string(frameContext.ui.frameIndex);
            text(frameText);
            sameLineSeparator();
            const std::string extent = extentText(frameContext.ui.swapchainExtent);
            text(extent);
            sameLineSeparator();
            const std::string panelCount =
                std::string{"Panels "} +
                std::to_string(editorContext.panelRegistry().openPanelCount()) + "/" +
                std::to_string(editorContext.panelRegistry().panelCount());
            text(panelCount);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

} // namespace asharia::editor
