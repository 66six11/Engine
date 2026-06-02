#include "editor_registration_smoke.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

#include "asharia/core/log.hpp"

#include "editor_action.hpp"
#include "editor_event.hpp"
#include "editor_i18n.hpp"
#include "editor_panel.hpp"
#include "editor_settings.hpp"
#include "editor_shortcut_smoke.hpp"
#include "editor_smoke.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorLocale alternateLocale(EditorLocale locale) {
            return locale == EditorLocale::ZhHans ? EditorLocale::EnUs : EditorLocale::ZhHans;
        }

        [[nodiscard]] EditorUiThemeId alternateTheme(EditorUiThemeId theme) {
            return theme == EditorUiThemeId::WarmGraphiteAmber ? EditorUiThemeId::ClassicBlueGray
                                                               : EditorUiThemeId::WarmGraphiteAmber;
        }

        [[nodiscard]] bool validateEditorSettingsSmoke(EditorRunMode mode,
                                                       EditorSettingsController& settings,
                                                       EditorI18n& i18n) {
            if (!isEditorSmokeMode(mode)) {
                return true;
            }

            const EditorLocale initialLocale = settings.settings().locale;
            const EditorUiThemeId initialTheme = settings.settings().theme;
            const EditorLocale changedLocale = alternateLocale(initialLocale);
            if (auto changed = settings.setLocale(changedLocale); !changed) {
                asharia::logError(changed.error().message);
                return false;
            }
            if (settings.settings().locale != changedLocale || i18n.locale() != changedLocale) {
                asharia::logError("Editor settings smoke did not apply the selected locale.");
                return false;
            }

            auto loaded = loadEditorSettings(settings.settingsPath(), initialLocale);
            if (!loaded) {
                asharia::logError(loaded.error().message);
                return false;
            }
            if (loaded->locale != changedLocale) {
                asharia::logError("Editor settings smoke did not persist the selected locale.");
                return false;
            }

            const EditorUiThemeId changedTheme = alternateTheme(initialTheme);
            if (auto changed = settings.setTheme(changedTheme); !changed) {
                asharia::logError(changed.error().message);
                return false;
            }
            if (settings.settings().theme != changedTheme ||
                currentEditorUiThemeId() != changedTheme) {
                asharia::logError("Editor settings smoke did not apply the selected theme.");
                return false;
            }

            loaded = loadEditorSettings(settings.settingsPath(), initialLocale);
            if (!loaded) {
                asharia::logError(loaded.error().message);
                return false;
            }
            if (loaded->theme != changedTheme) {
                asharia::logError("Editor settings smoke did not persist the selected theme.");
                return false;
            }

            if (auto restored = settings.setLocale(initialLocale); !restored) {
                asharia::logError(restored.error().message);
                return false;
            }
            if (settings.settings().locale != initialLocale || i18n.locale() != initialLocale) {
                asharia::logError("Editor settings smoke did not restore the initial locale.");
                return false;
            }
            if (auto restored = settings.setTheme(initialTheme); !restored) {
                asharia::logError(restored.error().message);
                return false;
            }
            if (settings.settings().theme != initialTheme ||
                currentEditorUiThemeId() != initialTheme) {
                asharia::logError("Editor settings smoke did not restore the initial theme.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validatePanelRegistrySmoke(EditorPanelRegistry& panelRegistry) {
            constexpr std::size_t kExpectedPanelCount = 6;
            constexpr std::size_t kExpectedOpenPanelCount = 4;

            if (!panelRegistry.closePanel("log") || !panelRegistry.focusPanel("log")) {
                asharia::logError(
                    "Editor panel registry smoke could not close and reopen Log panel.");
                return false;
            }
            if (panelRegistry.panelCount() != kExpectedPanelCount ||
                panelRegistry.openPanelCount() != kExpectedOpenPanelCount ||
                !panelRegistry.isOpen("log") || panelRegistry.isOpen("ui-style-preview") ||
                panelRegistry.isOpen("editor-settings")) {
                asharia::logError(
                    "Editor panel registry smoke detected invalid singleton panel state.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validateActionRegistrySmoke(EditorActionRegistry& actionRegistry,
                                                       EditorActionServices& actionServices) {
            constexpr std::size_t kExpectedActionCount = 12;
            constexpr std::size_t kExpectedEnabledActionCount = 9;

            if (actionRegistry.actionCount() != kExpectedActionCount ||
                actionRegistry.enabledActionCount() != kExpectedEnabledActionCount) {
                asharia::logError("Editor action registry smoke detected invalid action counts.");
                return false;
            }
            actionServices.eventQueue.clear();
            if (actionRegistry.invoke("file.open", makeEditorActionInvokeContext(actionServices)) ||
                actionRegistry.invokeCount("file.open") != 0 ||
                !actionServices.eventQueue.empty()) {
                asharia::logError("Editor action registry smoke invoked a disabled action.");
                return false;
            }
            if (!actionServices.panels.closePanel("log") ||
                !actionRegistry.invoke("view.log", makeEditorActionInvokeContext(actionServices)) ||
                !actionServices.panels.isOpen("log") ||
                actionRegistry.invokeCount("view.log") != 1) {
                asharia::logError("Editor action registry smoke failed to route View/Log action.");
                return false;
            }
            const std::span<const EditorEvent> events = actionServices.eventQueue.events();
            const bool closedLog = std::ranges::any_of(events, [](const EditorEvent& event) {
                return event.kind == EditorEventKind::PanelClosed && event.sourceId.value == "log";
            });
            const bool invokedLogAction = std::ranges::any_of(events, [](const EditorEvent& event) {
                return event.kind == EditorEventKind::ActionInvoked &&
                       event.sourceId.value == "view.log";
            });
            const bool openedLog = std::ranges::any_of(events, [](const EditorEvent& event) {
                return event.kind == EditorEventKind::PanelOpened && event.sourceId.value == "log";
            });
            if (!closedLog || !invokedLogAction || !openedLog) {
                asharia::logError(
                    "Editor event queue smoke missed action or panel lifecycle events.");
                return false;
            }
            actionServices.eventQueue.clear();

            if (!actionRegistry.invoke("view.ui-style-preview",
                                       makeEditorActionInvokeContext(actionServices)) ||
                !actionServices.panels.isOpen("ui-style-preview") ||
                actionRegistry.invokeCount("view.ui-style-preview") != 1 ||
                !actionServices.panels.closePanel("ui-style-preview")) {
                asharia::logError("Editor action registry smoke failed to route UI Style Preview.");
                return false;
            }
            actionServices.eventQueue.clear();

            if (!actionRegistry.invoke("view.editor-settings",
                                       makeEditorActionInvokeContext(actionServices)) ||
                !actionServices.panels.isOpen("editor-settings") ||
                actionRegistry.invokeCount("view.editor-settings") != 1 ||
                !actionServices.panels.closePanel("editor-settings")) {
                asharia::logError("Editor action registry smoke failed to route Editor Settings.");
                return false;
            }
            actionServices.eventQueue.clear();

            if (!actionRegistry.invoke("view.reset-layout",
                                       makeEditorActionInvokeContext(actionServices)) ||
                actionRegistry.invokeCount("view.reset-layout") != 1 ||
                actionServices.workspace.layoutResetRequestCount() != 1) {
                asharia::logError("Editor action registry smoke failed to request layout reset.");
                return false;
            }
            actionServices.eventQueue.clear();

            return true;
        }

        [[nodiscard]] bool validateToolRegistrySmoke(const EditorToolRegistry& toolRegistry,
                                                     const EditorActionRegistry& actionRegistry,
                                                     const EditorPanelRegistry& panelRegistry) {
            constexpr std::size_t kExpectedToolCount = 7;
            constexpr std::size_t kExpectedPanelContributions = 6;
            constexpr std::size_t kExpectedActionContributions = 9;
            constexpr std::size_t kExpectedToolbarActionContributions = 8;
            constexpr std::size_t kExpectedViewportOverlayContributions = 3;

            if (toolRegistry.toolCount() != kExpectedToolCount ||
                toolRegistry.panelContributionCount() != kExpectedPanelContributions ||
                toolRegistry.actionContributionCount() != kExpectedActionContributions ||
                toolRegistry.toolbarActionContributionCount() !=
                    kExpectedToolbarActionContributions ||
                toolRegistry.viewportOverlayContributionCount() !=
                    kExpectedViewportOverlayContributions) {
                asharia::logError(
                    "Editor tool registry smoke detected invalid contribution counts.");
                return false;
            }

            bool referencesValid = true;
            toolRegistry.visitTools([&](const EditorToolDesc& tool) {
                for (const EditorToolPanelContribution& panel : tool.panels) {
                    referencesValid =
                        referencesValid && panelRegistry.findPanelDesc(panel.panelId) != nullptr;
                }
                for (const EditorToolActionContribution& action : tool.actions) {
                    referencesValid =
                        referencesValid && actionRegistry.findAction(action.actionId) != nullptr;
                }
                for (const EditorToolViewportOverlayContribution& overlay : tool.viewportOverlays) {
                    referencesValid = referencesValid && overlay.viewportId == "scene-view";
                }
            });
            if (!referencesValid) {
                asharia::logError(
                    "Editor tool registry smoke found an invalid contribution target.");
                return false;
            }

            bool sawDebugToolbarAction = false;
            bool sawViewToolbarAction = false;
            bool sawUtilityToolbarAction = false;
            toolRegistry.visitToolbarActions(
                EditorToolbarSlot::Debug,
                [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                    static_cast<void>(tool);
                    sawDebugToolbarAction =
                        sawDebugToolbarAction || action.actionId == "debug.capture-frame";
                });
            toolRegistry.visitToolbarActions(
                EditorToolbarSlot::View,
                [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                    static_cast<void>(tool);
                    sawViewToolbarAction =
                        sawViewToolbarAction || action.actionId == "view.scene-view";
                });
            toolRegistry.visitToolbarActions(
                EditorToolbarSlot::Utility,
                [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                    static_cast<void>(tool);
                    sawUtilityToolbarAction =
                        sawUtilityToolbarAction || action.actionId == "view.editor-settings";
                });
            if (!sawDebugToolbarAction || !sawViewToolbarAction || !sawUtilityToolbarAction) {
                asharia::logError("Editor tool registry smoke missed a toolbar contribution slot.");
                return false;
            }

            std::size_t sceneOverlayCount = 0;
            bool sawSceneGrid = false;
            bool sawSceneGizmo = false;
            bool sawSceneSelectionOutline = false;
            toolRegistry.visitViewportOverlays(
                "scene-view", [&](const EditorToolDesc& tool,
                                  const EditorToolViewportOverlayContribution& overlay) {
                    static_cast<void>(tool);
                    ++sceneOverlayCount;
                    sawSceneGrid =
                        sawSceneGrid || overlay.overlayId == kEditorSceneGridOverlayId;
                    sawSceneGizmo = sawSceneGizmo ||
                                    overlay.overlayId == kEditorSceneTransformGizmoOverlayId;
                    sawSceneSelectionOutline =
                        sawSceneSelectionOutline ||
                        overlay.overlayId == kEditorSceneSelectionOutlineOverlayId;
                });
            std::size_t gameOverlayCount = 0;
            toolRegistry.visitViewportOverlays(
                "game-view", [&](const EditorToolDesc& tool,
                                 const EditorToolViewportOverlayContribution& overlay) {
                    static_cast<void>(tool);
                    static_cast<void>(overlay);
                    ++gameOverlayCount;
                });
            if (sceneOverlayCount != kExpectedViewportOverlayContributions ||
                gameOverlayCount != 0 || !sawSceneGrid || !sawSceneGizmo ||
                !sawSceneSelectionOutline) {
                asharia::logError("Editor tool registry smoke missed a viewport overlay query.");
                return false;
            }

            return true;
        }

    } // namespace

    bool validateEditorRegistrationSmoke(EditorRunMode mode, EditorActionRegistry& actionRegistry,
                                         EditorActionServices& actionServices,
                                         EditorSettingsController& settings, EditorI18n& i18n,
                                         const EditorToolRegistry& toolRegistry) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        return validatePanelRegistrySmoke(actionServices.panels) &&
               validateActionRegistrySmoke(actionRegistry, actionServices) &&
               validateToolRegistrySmoke(toolRegistry, actionRegistry, actionServices.panels) &&
               validateEditorSettingsSmoke(mode, settings, i18n) &&
               validateShortcutRouterSmoke(actionRegistry, actionServices);
    }
} // namespace asharia::editor
