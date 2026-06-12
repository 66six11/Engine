#include "imgui_editor_shell.hpp"

#include <algorithm>
#include <cstddef>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "editor_asset_icon.hpp"
#include "editor_dirty_state.hpp"
#include "editor_dock_layout.hpp"
#include "editor_event.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_panel.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    namespace {

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

        [[nodiscard]] EditorIconTint iconTint(ColorSrgba8 color, const float alpha = 1.0F) {
            constexpr float kByteToFloat = 1.0F / 255.0F;
            return editorIconTint(static_cast<float>(color.r) * kByteToFloat,
                                  static_cast<float>(color.g) * kByteToFloat,
                                  static_cast<float>(color.b) * kByteToFloat, alpha);
        }

        struct ActionTooltipDesc {
            const EditorActionDesc* action{};
            std::string_view label;
            std::string_view disabledReason;
        };

        [[nodiscard]] std::string actionTooltip(ActionTooltipDesc desc) {
            std::string tooltip{desc.label};
            if (desc.action != nullptr && !desc.action->shortcut.empty()) {
                tooltip += "\nShortcut: ";
                tooltip += desc.action->shortcut;
            }
            if (!desc.disabledReason.empty()) {
                tooltip += "\nDisabled: ";
                tooltip.append(desc.disabledReason.data(), desc.disabledReason.size());
            }
            return tooltip;
        }

        [[nodiscard]] std::optional<std::string_view>
        actionToolbarIconName(std::string_view actionId) {
            if (actionId == "view.scene-tree") {
                return "list-tree";
            }
            if (actionId == "view.scene-view") {
                return "eye";
            }
            if (actionId == "view.render-graph") {
                return "braces";
            }
            if (actionId == "view.frame-debugger") {
                return "layout-dashboard";
            }
            if (actionId == "view.log") {
                return "file-text";
            }
            if (actionId == "view.asset-browser") {
                return "folder";
            }
            if (actionId == "view.ui-style-preview") {
                return "palette";
            }
            if (actionId == "view.editor-settings") {
                return "settings";
            }
            if (actionId == "view.reset-layout") {
                return "rotate-ccw";
            }
            if (actionId == "debug.capture-frame") {
                return "camera";
            }
            if (actionId == "debug.resume-frame") {
                return "play";
            }
            return std::nullopt;
        }

        [[nodiscard]] EditorIconDescriptor toolbarIcon(std::string_view iconName,
                                                       std::string_view tooltip,
                                                       EditorUiTone tone = EditorUiTone::Muted) {
            const EditorUiTheme& theme = editorUiTheme();
            ColorSrgba8 color = theme.textSecondary;
            switch (tone) {
            case EditorUiTone::Info:
                color = theme.info;
                break;
            case EditorUiTone::Success:
                color = theme.success;
                break;
            case EditorUiTone::Warning:
                color = theme.warning;
                break;
            case EditorUiTone::Danger:
                color = theme.danger;
                break;
            case EditorUiTone::Neutral:
                color = theme.text;
                break;
            case EditorUiTone::Muted:
            default:
                color = theme.textSecondary;
                break;
            }
            return makeLucideEditorIconDescriptor(iconName, iconTint(color), {}, tooltip);
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
            if (const std::optional<std::string_view> iconName =
                    actionToolbarIconName(action->id.value)) {
                const std::string disabledReason =
                    action->enabled ? std::string{} : std::string{"Action is not available yet."};
                const EditorIconDescriptor icon = toolbarIcon(*iconName,
                                                              actionTooltip(ActionTooltipDesc{
                                                                  .action = action,
                                                                  .label = label,
                                                                  .disabledReason = disabledReason,
                                                              }),
                                                              EditorUiTone::Neutral);
                if (drawEditorUiIconButton(icon, action->id.value, false, action->enabled,
                                           icon.tooltipFallback)) {
                    static_cast<void>(actionRegistry.invoke(action->id.value, actionInvoke));
                }
                return;
            }

            ImGui::BeginDisabled(!action->enabled);
            const bool pressed = drawEditorUiCompactButton(label);
            if (!action->enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                const std::string tooltip = actionTooltip(ActionTooltipDesc{
                    .action = action,
                    .label = label,
                    .disabledReason = "Action is not available yet.",
                });
                ImGui::TextUnformatted(tooltip.c_str());
                ImGui::EndTooltip();
            }
            if (pressed && action->enabled) {
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
            drawEditorUiToolbarSeparator();
            ImGui::SameLine();
        }

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        void drawPendingToolIcon(std::string_view iconName, std::string_view label,
                                 std::string_view stableId, std::string_view disabledReason) {
            std::string tooltip{label};
            tooltip += "\nDisabled: ";
            tooltip.append(disabledReason.data(), disabledReason.size());
            const EditorIconDescriptor icon = toolbarIcon(iconName, tooltip, EditorUiTone::Muted);
            static_cast<void>(drawEditorUiIconButton(icon, stableId, false, false, tooltip));
        }

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        void drawToolbarIconText(std::string_view iconName, std::string_view label,
                                 std::string_view tooltip,
                                 EditorUiTone tone = EditorUiTone::Muted) {
            const EditorIconDescriptor icon = toolbarIcon(iconName, tooltip, tone);
            drawEditorIconGlyph(icon, editorUiMetrics().toolbarIconSize);
            ImGui::SameLine(0.0F, 3.0F);
            text(label);
            if (!tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(std::string{tooltip}.c_str());
                ImGui::EndTooltip();
            }
        }

        void drawClippedStatusText(std::string_view value, float width, ColorSrgba8 color) {
            width = std::max(1.0F, width);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetTextLineHeight();
            const ImVec2 max{min.x + width, min.y + height};
            ImGui::Dummy(ImVec2{width, height});
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(min, max, true);
            const std::string textValue{value};
            drawList->AddText(min, toImGuiEncodedSrgbU32(color), textValue.c_str());
            drawList->PopClipRect();
            if (ImGui::IsItemHovered() && !textValue.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(textValue.c_str());
                ImGui::EndTooltip();
            }
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

        struct DiagnosticsSummary {
            std::size_t errorCount{};
            std::size_t warningCount{};
            std::size_t infoCount{};
            const EditorDiagnosticEvent* latest{};
            const EditorDiagnosticEvent* latestImportant{};
        };

        [[nodiscard]] DiagnosticsSummary summarizeDiagnostics(const EditorDiagnosticsLog& log) {
            DiagnosticsSummary summary;
            const std::span<const EditorDiagnosticEvent> recentEvents = log.recentEvents();
            for (const EditorDiagnosticEvent& diagnostic : recentEvents) {
                switch (diagnostic.event.metadata.severity) {
                case EditorEventSeverity::Error:
                    ++summary.errorCount;
                    break;
                case EditorEventSeverity::Warning:
                    ++summary.warningCount;
                    break;
                case EditorEventSeverity::Info:
                default:
                    ++summary.infoCount;
                    break;
                }
            }
            for (std::size_t index = recentEvents.size(); index > 0U; --index) {
                const EditorDiagnosticEvent& diagnostic = recentEvents[index - 1U];
                if (summary.latest == nullptr) {
                    summary.latest = &diagnostic;
                }
                if (summary.latestImportant == nullptr &&
                    diagnostic.event.metadata.severity != EditorEventSeverity::Info) {
                    summary.latestImportant = &diagnostic;
                }
            }
            return summary;
        }

        [[nodiscard]] std::string latestLogMirrorText(const DiagnosticsSummary& summary,
                                                      bool consoleVisible) {
            const EditorDiagnosticEvent* diagnostic =
                summary.latestImportant != nullptr ? summary.latestImportant : summary.latest;
            if (consoleVisible) {
                return "Console visible | " + std::to_string(summary.errorCount) + " errors, " +
                       std::to_string(summary.warningCount) + " warnings, " +
                       std::to_string(summary.infoCount) + " info";
            }
            if (diagnostic == nullptr) {
                return "Ready";
            }
            return "Latest log | #" + std::to_string(diagnostic->sequence) + " " +
                   editorEventDisplayText(diagnostic->event);
        }

        [[nodiscard]] EditorUiTone toneForEvent(const EditorDiagnosticEvent* diagnostic) {
            if (diagnostic == nullptr) {
                return EditorUiTone::Muted;
            }
            switch (diagnostic->event.metadata.severity) {
            case EditorEventSeverity::Error:
                return EditorUiTone::Danger;
            case EditorEventSeverity::Warning:
                return EditorUiTone::Warning;
            case EditorEventSeverity::Info:
            default:
                return EditorUiTone::Info;
            }
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
        const EditorUiMetrics& metrics = editorUiMetrics();
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toImGuiEncodedSrgbVec4(theme.panelBackground));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0F, 3.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0F, 3.0F});
        if (ImGui::BeginViewportSideBar("##asharia-editor-command-bar", viewport, ImGuiDir_Up,
                                        metrics.commandBarHeight, kWindowFlags)) {
            const bool hasDebug = hasToolbarSlot(context, EditorToolbarSlot::Debug);
            const bool hasView = hasToolbarSlot(context, EditorToolbarSlot::View);
            const bool hasUtility = hasToolbarSlot(context, EditorToolbarSlot::Utility);
            const ImGuiStyle& style = ImGui::GetStyle();
            const float contentMaxX = ImGui::GetWindowContentRegionMax().x;
            if (hasView) {
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, context, EditorToolbarSlot::View));
            }

            constexpr std::string_view kPlayDisabledReason =
                "Pending play session and Game View support.";
            const float playGroupWidth =
                (metrics.toolbarButtonSize * 4.0F) + (style.ItemSpacing.x * 3.0F);
            const float playGroupX = std::max(0.0F, (contentMaxX - playGroupWidth) * 0.5F);
            if (playGroupX > ImGui::GetCursorPosX() + 12.0F) {
                ImGui::SameLine(playGroupX);
            } else if (hasView) {
                sameLineSeparator();
            }
            drawPendingToolIcon("play", "Play", "toolbar-play", kPlayDisabledReason);
            ImGui::SameLine();
            drawPendingToolIcon("pause", "Pause", "toolbar-pause", kPlayDisabledReason);
            ImGui::SameLine();
            drawPendingToolIcon("step-forward", "Step", "toolbar-step", kPlayDisabledReason);
            ImGui::SameLine();
            drawPendingToolIcon("square", "Stop", "toolbar-stop", kPlayDisabledReason);

            if (hasDebug) {
                sameLineSeparator();
                static_cast<void>(
                    drawToolbarSlot(actionRegistry, context, EditorToolbarSlot::Debug));
            }

            const std::string themeLabel{theme.name};
            const float rightWidth =
                (hasUtility ? 86.0F : 0.0F) + ImGui::CalcTextSize(themeLabel.c_str()).x + 190.0F;
            const float rightX = contentMaxX - rightWidth;
            if (rightX > ImGui::GetCursorPosX() + 12.0F) {
                ImGui::SameLine(rightX);
                drawToolbarIconText("search", "Search commands",
                                    "Global command search is pending.", EditorUiTone::Muted);
                if (hasUtility) {
                    sameLineSeparator();
                    static_cast<void>(
                        drawToolbarSlot(actionRegistry, context, EditorToolbarSlot::Utility));
                }
                sameLineSeparator();
                drawToolbarIconText("settings", themeLabel, "Current editor theme",
                                    EditorUiTone::Info);
                sameLineSeparator();
                drawToolbarIconText("play", "Edit", "Active mode: Edit", EditorUiTone::Success);
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
        const EditorUiMetrics& metrics = editorUiMetrics();
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, toImGuiEncodedSrgbVec4(theme.menuBackground));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0F, 2.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0F, 2.0F});
        if (ImGui::BeginViewportSideBar("##asharia-editor-status-bar", viewport, ImGuiDir_Down,
                                        metrics.statusBarHeight, kWindowFlags)) {
            const DiagnosticsSummary diagnosticsSummary =
                summarizeDiagnostics(context.diagnosticsLog);
            const bool consoleVisible = context.panels.isOpen("log");
            const std::string mirrorText = latestLogMirrorText(diagnosticsSummary, consoleVisible);
            const EditorDiagnosticEvent* latestToneEvent =
                diagnosticsSummary.latestImportant != nullptr ? diagnosticsSummary.latestImportant
                                                              : diagnosticsSummary.latest;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const bool narrow = availableWidth < 430.0F;
            const bool compact = availableWidth < 680.0F;
            float rightWidth = 470.0F;
            if (narrow) {
                rightWidth = 142.0F;
            } else if (compact) {
                rightWidth = 274.0F;
            }
            const float mirrorWidth = std::max(80.0F, availableWidth - rightWidth);
            drawClippedStatusText(mirrorText, mirrorWidth, editorUiTheme().textSecondary);
            ImGui::SameLine();

            drawToolbarIconText("triangle-alert", std::to_string(diagnosticsSummary.errorCount),
                                "Error count", EditorUiTone::Danger);
            ImGui::SameLine();
            drawToolbarIconText("circle-alert", std::to_string(diagnosticsSummary.warningCount),
                                "Warning count", EditorUiTone::Warning);
            ImGui::SameLine();
            drawToolbarIconText("file-text", std::to_string(diagnosticsSummary.infoCount),
                                "Info count", toneForEvent(latestToneEvent));
            if (!narrow) {
                ImGui::SameLine();
                const EditorDirtySnapshot dirty = context.dirtyState.snapshot();
                drawToolbarIconText(dirty.clean() ? "circle-help" : "circle-alert",
                                    dirtyStatusText(dirty), "Persistent dirty and import status",
                                    dirty.clean() ? EditorUiTone::Muted : EditorUiTone::Warning);
            }
            if (!compact) {
                ImGui::SameLine();
                drawToolbarIconText("play", "Edit", "Active mode: Edit", EditorUiTone::Success);
                ImGui::SameLine();
                drawToolbarIconText("layout-dashboard", context.frameDebugger.stateName(),
                                    "Frame debugger state", EditorUiTone::Muted);
                ImGui::SameLine();
                const std::string frameText =
                    std::string{"Frame "} + std::to_string(context.ui.frameIndex);
                drawToolbarIconText("refresh-cw", frameText, "Rendered editor frame",
                                    EditorUiTone::Muted);
                ImGui::SameLine();
                drawToolbarIconText("box", extentText(context.ui.swapchainExtent),
                                    "Swapchain extent", EditorUiTone::Muted);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

} // namespace asharia::editor
