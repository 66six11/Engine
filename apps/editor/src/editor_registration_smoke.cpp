#include "editor_registration_smoke.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include "asharia/core/log.hpp"

#include "editor_action.hpp"
#include "editor_event.hpp"
#include "editor_extension.hpp"
#include "editor_i18n.hpp"
#include "editor_panel.hpp"
#include "editor_settings.hpp"
#include "editor_shortcut_smoke.hpp"
#include "editor_smoke.hpp"
#include "editor_tool.hpp"
#include "editor_tool_manager.hpp"
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

        [[nodiscard]] bool closeFloat(float lhs, float rhs) {
            return std::fabs(lhs - rhs) < 0.0001F;
        }

        [[nodiscard]] bool sameWorldGridSettings(EditorViewportWorldGridSettings lhs,
                                                 EditorViewportWorldGridSettings rhs) {
            return closeFloat(lhs.planeY, rhs.planeY) &&
                   closeFloat(lhs.minorSpacing, rhs.minorSpacing) &&
                   closeFloat(lhs.majorSpacing, rhs.majorSpacing) &&
                   closeFloat(lhs.fadeStart, rhs.fadeStart) &&
                   closeFloat(lhs.fadeEnd, rhs.fadeEnd) && closeFloat(lhs.opacity, rhs.opacity);
        }

        [[nodiscard]] EditorToolDesc smokeToolDesc(std::string toolId, std::string title) {
            return EditorToolDesc{
                .id = EditorId{.value = std::move(toolId)},
                .title = std::move(title),
                .titleKey = {},
                .category = EditorToolCategory::Core,
                .panels = {},
                .actions = {},
                .viewportOverlays = {},
            };
        }

        [[nodiscard]] bool validateEditorSettingsSmoke(EditorRunMode mode,
                                                       EditorSettingsController& settings,
                                                       EditorI18n& i18n) {
            if (!isEditorSmokeMode(mode)) {
                return true;
            }

            const EditorLocale initialLocale = settings.settings().locale;
            const EditorUiThemeId initialTheme = settings.settings().theme;
            const EditorViewportWorldGridSettings initialSceneGrid = settings.settings().sceneGrid;
            if (!sameWorldGridSettings(initialSceneGrid, defaultEditorSceneGridSettings())) {
                asharia::logError(
                    "Editor settings smoke did not use the default Scene View grid settings.");
                return false;
            }
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

            const EditorViewportWorldGridSettings changedSceneGrid{
                .planeY = 0.25F,
                .minorSpacing = 2.0F,
                .majorSpacing = 20.0F,
                .fadeStart = 12.0F,
                .fadeEnd = 96.0F,
                .opacity = 0.6F,
            };
            if (auto changed = settings.setSceneGrid(changedSceneGrid); !changed) {
                asharia::logError(changed.error().message);
                return false;
            }
            if (!sameWorldGridSettings(settings.settings().sceneGrid, changedSceneGrid)) {
                asharia::logError("Editor settings smoke did not apply the Scene View grid "
                                  "settings.");
                return false;
            }

            loaded = loadEditorSettings(settings.settingsPath(), initialLocale);
            if (!loaded) {
                asharia::logError(loaded.error().message);
                return false;
            }
            if (!sameWorldGridSettings(loaded->sceneGrid, changedSceneGrid)) {
                asharia::logError("Editor settings smoke did not persist the Scene View grid "
                                  "settings.");
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
            if (auto restored = settings.setSceneGrid(initialSceneGrid); !restored) {
                asharia::logError(restored.error().message);
                return false;
            }
            if (!sameWorldGridSettings(settings.settings().sceneGrid, initialSceneGrid)) {
                asharia::logError(
                    "Editor settings smoke did not restore the initial Scene View grid settings.");
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

        [[nodiscard]] bool validateEditorExtensionRegistrySmoke() {
            EditorExtensionRegistry extensionRegistry;
            if (extensionRegistry.registerOrReplaceExtension(EditorExtensionManifest{
                    .id = EditorId{.value = "extension.invalid-empty-name"},
                    .displayName = {},
                    .tools = {},
                })) {
                asharia::logError(
                    "Editor extension registry smoke accepted an empty display name.");
                return false;
            }

            auto first = extensionRegistry.registerOrReplaceExtension(EditorExtensionManifest{
                .id = EditorId{.value = "extension.smoke"},
                .displayName = "Smoke Extension",
                .tools = {smokeToolDesc("tool.smoke-a", "Smoke A")},
            });
            if (!first) {
                asharia::logError(first.error().message);
                return false;
            }
            if (extensionRegistry.extensionCount() != 1U ||
                extensionRegistry.toolContributionCount() != 1U ||
                extensionRegistry.findExtension("extension.smoke") == nullptr) {
                asharia::logError("Editor extension registry smoke missed initial registration.");
                return false;
            }

            auto replaced = extensionRegistry.registerOrReplaceExtension(EditorExtensionManifest{
                .id = EditorId{.value = "extension.smoke"},
                .displayName = "Smoke Extension Reloaded",
                .tools =
                    {
                        smokeToolDesc("tool.smoke-a", "Smoke A"),
                        smokeToolDesc("tool.smoke-b", "Smoke B"),
                    },
            });
            if (!replaced) {
                asharia::logError(replaced.error().message);
                return false;
            }
            const EditorExtensionManifest* reloaded =
                extensionRegistry.findExtension("extension.smoke");
            if (extensionRegistry.extensionCount() != 1U ||
                extensionRegistry.toolContributionCount() != 2U || reloaded == nullptr ||
                reloaded->displayName != "Smoke Extension Reloaded") {
                asharia::logError("Editor extension registry smoke did not replace by id.");
                return false;
            }

            if (extensionRegistry.registerOrReplaceExtension(EditorExtensionManifest{
                    .id = EditorId{.value = "extension.smoke-duplicate"},
                    .displayName = "Smoke Duplicate",
                    .tools =
                        {
                            smokeToolDesc("tool.duplicate", "Duplicate A"),
                            smokeToolDesc("tool.duplicate", "Duplicate B"),
                        },
                })) {
                asharia::logError("Editor extension registry smoke accepted duplicate tool ids.");
                return false;
            }
            if (extensionRegistry.extensionCount() != 1U ||
                extensionRegistry.toolContributionCount() != 2U) {
                asharia::logError(
                    "Editor extension registry smoke changed state after a failed reload.");
                return false;
            }

            EditorToolRegistry toolRegistry;
            auto tools = registerEditorExtensionTools(extensionRegistry, toolRegistry);
            if (!tools) {
                asharia::logError(tools.error().message);
                return false;
            }
            if (toolRegistry.toolCount() != 2U ||
                toolRegistry.findTool("tool.smoke-a") == nullptr ||
                toolRegistry.findTool("tool.smoke-b") == nullptr) {
                asharia::logError("Editor extension registry smoke did not publish tools.");
                return false;
            }

            auto conflict = extensionRegistry.registerOrReplaceExtension(EditorExtensionManifest{
                .id = EditorId{.value = "extension.smoke-conflict"},
                .displayName = "Smoke Conflict",
                .tools = {smokeToolDesc("tool.smoke-a", "Smoke A Conflict")},
            });
            if (!conflict) {
                asharia::logError(conflict.error().message);
                return false;
            }
            if (registerEditorExtensionTools(extensionRegistry, toolRegistry)) {
                asharia::logError("Editor extension registry smoke published duplicate tool ids.");
                return false;
            }
            if (toolRegistry.toolCount() != 2U ||
                toolRegistry.findTool("tool.smoke-a") == nullptr ||
                toolRegistry.findTool("tool.smoke-b") == nullptr) {
                asharia::logError(
                    "Editor extension registry smoke did not preserve the tool facade.");
                return false;
            }

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
            bool sawSceneGridDefault = false;
            bool sawUnexpectedWorldGridDefault = false;
            bool sawSceneGizmo = false;
            bool sawSceneSelectionOutline = false;
            toolRegistry.visitViewportOverlays(
                "scene-view", [&](const EditorToolDesc& tool,
                                  const EditorToolViewportOverlayContribution& overlay) {
                    static_cast<void>(tool);
                    ++sceneOverlayCount;
                    if (overlay.overlayId == kEditorSceneGridOverlayId) {
                        sawSceneGrid = true;
                        sawSceneGridDefault =
                            overlay.worldGrid.has_value() &&
                            sameWorldGridSettings(*overlay.worldGrid,
                                                  defaultEditorSceneGridSettings());
                    } else if (overlay.worldGrid.has_value()) {
                        sawUnexpectedWorldGridDefault = true;
                    }
                    sawSceneGizmo =
                        sawSceneGizmo || overlay.overlayId == kEditorSceneTransformGizmoOverlayId;
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
                gameOverlayCount != 0 || !sawSceneGrid || !sawSceneGridDefault ||
                sawUnexpectedWorldGridDefault || !sawSceneGizmo || !sawSceneSelectionOutline) {
                asharia::logError("Editor tool registry smoke missed a viewport overlay query.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validateToolManagerSmoke(const EditorToolRegistry& toolRegistry,
                                                    const EditorToolManager& appToolManager,
                                                    EditorWorkspaceController& workspace) {
            constexpr std::string_view kSceneViewportId = "scene-view";
            constexpr std::string_view kSceneToolId = "tool.scene-view";
            constexpr std::string_view kFrameDebuggerToolId = "tool.frame-debugger";

            if (appToolManager.trackedToolCount() != toolRegistry.toolCount() ||
                appToolManager.lifecycleState(kSceneToolId) !=
                    EditorToolLifecycleState::Available ||
                !appToolManager.activeToolForViewport(kSceneViewportId).empty()) {
                asharia::logError("Editor tool manager smoke found invalid app tool state.");
                return false;
            }

            EditorToolManager toolManager;
            if (auto synced = toolManager.syncTools(toolRegistry); !synced) {
                asharia::logError(synced.error().message);
                return false;
            }
            if (toolManager.trackedToolCount() != toolRegistry.toolCount() ||
                toolManager.lifecycleState("tool.missing") !=
                    EditorToolLifecycleState::Unregistered) {
                asharia::logError("Editor tool manager smoke did not sync registered tools.");
                return false;
            }
            if (toolManager.beginActivateToolForViewport(
                    {.viewportId = kSceneViewportId, .toolId = "tool.missing"})) {
                asharia::logError("Editor tool manager smoke activated an unknown tool.");
                return false;
            }

            if (auto started = toolManager.beginActivateToolForViewport(
                    {.viewportId = kSceneViewportId, .toolId = kSceneToolId});
                !started) {
                asharia::logError(started.error().message);
                return false;
            }
            if (toolManager.lifecycleState(kSceneToolId) != EditorToolLifecycleState::Activating ||
                toolManager.activeToolForViewport(kSceneViewportId) != kSceneToolId ||
                toolManager.activeViewportCount() != 1U) {
                asharia::logError("Editor tool manager smoke missed activating state.");
                return false;
            }
            if (auto completed = toolManager.completeToolActivation(
                    {.viewportId = kSceneViewportId, .toolId = kSceneToolId});
                !completed) {
                asharia::logError(completed.error().message);
                return false;
            }
            if (toolManager.lifecycleState(kSceneToolId) != EditorToolLifecycleState::Active ||
                !toolManager.isToolActiveForViewport(
                    {.viewportId = kSceneViewportId, .toolId = kSceneToolId})) {
                asharia::logError("Editor tool manager smoke missed active state.");
                return false;
            }
            if (toolManager.completeToolActivation(
                    {.viewportId = kSceneViewportId, .toolId = kSceneToolId})) {
                asharia::logError("Editor tool manager smoke completed activation twice.");
                return false;
            }
            if (toolManager.completeToolDeactivation(
                    {.viewportId = kSceneViewportId, .toolId = kSceneToolId})) {
                asharia::logError("Editor tool manager smoke deactivated without suspending.");
                return false;
            }

            workspace.requestLayoutReset();
            if (!toolManager.isToolActiveForViewport(
                    {.viewportId = kSceneViewportId, .toolId = kSceneToolId})) {
                asharia::logError(
                    "Editor tool manager smoke lost active tool during layout reset.");
                return false;
            }

            if (auto switched = toolManager.activateToolForViewport(
                    {.viewportId = kSceneViewportId, .toolId = kFrameDebuggerToolId});
                !switched) {
                asharia::logError(switched.error().message);
                return false;
            }
            if (toolManager.lifecycleState(kSceneToolId) != EditorToolLifecycleState::Inactive ||
                toolManager.lifecycleState(kFrameDebuggerToolId) !=
                    EditorToolLifecycleState::Active ||
                toolManager.activeToolForViewport(kSceneViewportId) != kFrameDebuggerToolId ||
                toolManager.activeViewportCount() != 1U) {
                asharia::logError("Editor tool manager smoke allowed multiple active tools.");
                return false;
            }

            if (auto started = toolManager.beginDeactivateToolForViewport(
                    {.viewportId = kSceneViewportId, .toolId = kFrameDebuggerToolId});
                !started) {
                asharia::logError(started.error().message);
                return false;
            }
            if (toolManager.lifecycleState(kFrameDebuggerToolId) !=
                EditorToolLifecycleState::Suspending) {
                asharia::logError("Editor tool manager smoke missed suspending state.");
                return false;
            }
            if (auto completed = toolManager.completeToolDeactivation(
                    {.viewportId = kSceneViewportId, .toolId = kFrameDebuggerToolId});
                !completed) {
                asharia::logError(completed.error().message);
                return false;
            }
            if (toolManager.lifecycleState(kFrameDebuggerToolId) !=
                    EditorToolLifecycleState::Inactive ||
                !toolManager.activeToolForViewport(kSceneViewportId).empty() ||
                toolManager.activeViewportCount() != 0U) {
                asharia::logError("Editor tool manager smoke missed inactive state.");
                return false;
            }

            return true;
        }

    } // namespace

    bool validateEditorRegistrationSmoke(EditorRunMode mode, EditorActionRegistry& actionRegistry,
                                         EditorActionServices& actionServices,
                                         EditorSettingsController& settings, EditorI18n& i18n,
                                         const EditorToolRegistry& toolRegistry,
                                         const EditorToolManager& toolManager) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        return validatePanelRegistrySmoke(actionServices.panels) &&
               validateActionRegistrySmoke(actionRegistry, actionServices) &&
               validateEditorExtensionRegistrySmoke() &&
               validateToolRegistrySmoke(toolRegistry, actionRegistry, actionServices.panels) &&
               validateToolManagerSmoke(toolRegistry, toolManager, actionServices.workspace) &&
               validateEditorSettingsSmoke(mode, settings, i18n) &&
               validateShortcutRouterSmoke(actionRegistry, actionServices);
    }
} // namespace asharia::editor
