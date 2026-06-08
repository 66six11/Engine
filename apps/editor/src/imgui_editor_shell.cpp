#include "imgui_editor_shell.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>

#include "editor_dirty_state.hpp"
#include "editor_dock_layout.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_panel.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"
#include "editor_workspace.hpp"

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

        void drawActionMenuItem(EditorActionRegistry& actionRegistry, const EditorI18n& i18n,
                                const EditorActionInvokeContext& actionInvoke,
                                std::string_view actionId, bool selected = false) {
            const EditorActionDesc* action = actionRegistry.findAction(actionId);
            if (action == nullptr) {
                return;
            }

            const std::string label = actionLabel(*action, i18n);
            const char* shortcut = action->shortcut.empty() ? nullptr : action->shortcut.c_str();
            if (ImGui::MenuItem(label.c_str(), shortcut, selected, action->enabled)) {
                static_cast<void>(actionRegistry.invoke(action->id.value, actionInvoke));
            }
        }

        void drawActionButton(EditorActionRegistry& actionRegistry, const EditorI18n& i18n,
                              const EditorActionInvokeContext& actionInvoke,
                              std::string_view actionId) {
            const EditorActionDesc* action = actionRegistry.findAction(actionId);
            if (action == nullptr) {
                return;
            }

            const std::string label = actionLabel(*action, i18n);
            ImGui::BeginDisabled(!action->enabled);
            if (ImGui::SmallButton(label.c_str()) && action->enabled) {
                static_cast<void>(actionRegistry.invoke(action->id.value, actionInvoke));
            }
            ImGui::EndDisabled();
        }

        [[nodiscard]] bool drawToolbarSlot(EditorActionRegistry& actionRegistry,
                                           const EditorCommandBarContext& context,
                                           EditorToolbarSlot slot) {
            bool drewAction = false;
            context.tools.visitToolbarActions(
                slot, [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                    static_cast<void>(tool);
                    if (drewAction) {
                        ImGui::SameLine();
                    }
                    drawActionButton(actionRegistry, context.i18n, context.actionInvoke,
                                     action.actionId);
                    drewAction = true;
                });
            return drewAction;
        }

        [[nodiscard]] bool hasToolbarSlot(const EditorCommandBarContext& context,
                                          EditorToolbarSlot slot) {
            bool hasAction = false;
            context.tools.visitToolbarActions(
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

        [[nodiscard]] std::string dirtyStatusText(const EditorDirtySnapshot& dirty) {
            if (dirty.clean()) {
                return "Clean";
            }
            if (dirty.hasPersistentDirty()) {
                return "Dirty " + std::to_string(dirty.persistentDirtyCount());
            }
            if (dirty.hasPendingReimport()) {
                return "Pending reimport " + std::to_string(dirty.pendingReimportCount);
            }
            return "Transient";
        }

    } // namespace

    void drawEditorDockspace(EditorDockspaceContext& context) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) {
            return;
        }

        const ImGuiID dockspaceId = editorDockspaceId();
        const bool resetRequested = context.workspace.consumeLayoutResetRequest();
        if (!editorDockLayoutExists(dockspaceId) || resetRequested) {
            buildEditorDockLayout(EditorDockLayoutBuildDesc{
                .panelRegistry = context.panels,
                .i18n = context.i18n,
                .viewport = *viewport,
                .dockspaceId = dockspaceId,
                .preset = context.workspace.activePreset(),
            });
            context.workspace.notifyLayoutApplied();
        }

        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
    }

    void drawEditorMainMenu(EditorActionRegistry& actionRegistry,
                            const EditorMenuContext& context) {
        if (ImGui::BeginMainMenuBar()) {
            const EditorI18n& i18n = context.i18n;
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
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "file.new-scene");
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke, "file.open");
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke, "file.exit");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(viewMenu.c_str())) {
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.scene-tree", context.panels.isOpen("scene-tree"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.scene-view", context.panels.isOpen("scene-view"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.inspector", context.panels.isOpen("inspector"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.asset-browser", context.panels.isOpen("asset-browser"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke, "view.log",
                                   context.panels.isOpen("log"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.render-graph", context.panels.isOpen("render-graph"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.frame-debugger", context.panels.isOpen("frame-debugger"));
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.ui-style-preview",
                                   context.panels.isOpen("ui-style-preview"));
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.editor-settings",
                                   context.panels.isOpen("editor-settings"));
                ImGui::Separator();
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "view.reset-layout");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(debugMenu.c_str())) {
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "debug.capture-frame");
                drawActionMenuItem(actionRegistry, context.i18n, context.actionInvoke,
                                   "debug.resume-frame");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void drawEditorCommandBar(EditorActionRegistry& actionRegistry,
                              const EditorCommandBarContext& context) {
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
            const bool hasDebug = hasToolbarSlot(context, EditorToolbarSlot::Debug);
            const bool hasView = hasToolbarSlot(context, EditorToolbarSlot::View);
            const bool hasUtility = hasToolbarSlot(context, EditorToolbarSlot::Utility);
            if (hasDebug) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, context, EditorToolbarSlot::Debug));
            }
            if (hasDebug && hasView) {
                sameLineSeparator();
            }
            if (hasView) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, context, EditorToolbarSlot::View));
            }
            if ((hasDebug || hasView) && hasUtility) {
                sameLineSeparator();
            }
            if (hasUtility) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, context, EditorToolbarSlot::Utility));
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

    void drawEditorStatusBar(const EditorStatusBarContext& context) {
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
            text(dirtyStatusText(context.dirtyState.snapshot()));
            sameLineSeparator();
            text(context.frameDebugger.stateName());
            sameLineSeparator();
            const std::string frameText =
                std::string{"Frame "} + std::to_string(context.ui.frameIndex);
            text(frameText);
            sameLineSeparator();
            const std::string extent = extentText(context.ui.swapchainExtent);
            text(extent);
            sameLineSeparator();
            const std::string panelCount = std::string{"Panels "} +
                                           std::to_string(context.panels.openPanelCount()) + "/" +
                                           std::to_string(context.panels.panelCount());
            text(panelCount);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

} // namespace asharia::editor
