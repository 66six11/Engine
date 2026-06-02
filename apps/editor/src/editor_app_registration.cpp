#include "editor_app_registration.hpp"

#include <memory>
#include <string>

#include "asharia/core/result.hpp"

#include "editor_action.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_panel.hpp"
#include "editor_tool.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "editor_workspace.hpp"
#include "panels/editor_settings_panel.hpp"
#include "panels/frame_debugger_panel.hpp"
#include "panels/log_panel.hpp"
#include "panels/render_graph_panel.hpp"
#include "panels/scene_view_panel.hpp"
#include "panels/ui_style_preview_panel.hpp"

namespace asharia::editor {

    [[nodiscard]] asharia::VoidResult registerEditorPanels(EditorPanelRegistry& panelRegistry) {
        auto sceneView =
            panelRegistry.registerPanel([] { return std::make_unique<SceneViewPanel>(); });
        if (!sceneView) {
            return std::unexpected{std::move(sceneView.error())};
        }

        auto renderGraph =
            panelRegistry.registerPanel([] { return std::make_unique<RenderGraphPanel>(); });
        if (!renderGraph) {
            return std::unexpected{std::move(renderGraph.error())};
        }

        auto frameDebugger =
            panelRegistry.registerPanel([] { return std::make_unique<FrameDebuggerPanel>(); });
        if (!frameDebugger) {
            return std::unexpected{std::move(frameDebugger.error())};
        }

        auto log = panelRegistry.registerPanel([] { return std::make_unique<LogPanel>(); });
        if (!log) {
            return std::unexpected{std::move(log.error())};
        }

        auto settings =
            panelRegistry.registerPanel([] { return std::make_unique<EditorSettingsPanel>(); });
        if (!settings) {
            return std::unexpected{std::move(settings.error())};
        }

        return panelRegistry.registerPanel([] { return std::make_unique<UiStylePreviewPanel>(); });
    }

    [[nodiscard]] asharia::VoidResult registerEditorActions(EditorActionRegistry& actionRegistry) {
        auto newScene = actionRegistry.registerAction(EditorActionDesc{
            .id = EditorId{.value = "file.new-scene"},
            .menuPath = "File",
            .label = "New Scene",
            .labelKey = "action.file.newScene",
            .shortcut = "Ctrl+N",
            .enabled = false,
        });
        if (!newScene) {
            return std::unexpected{std::move(newScene.error())};
        }

        auto openScene = actionRegistry.registerAction(EditorActionDesc{
            .id = EditorId{.value = "file.open"},
            .menuPath = "File",
            .label = "Open...",
            .labelKey = "action.file.open",
            .shortcut = "Ctrl+O",
            .enabled = false,
        });
        if (!openScene) {
            return std::unexpected{std::move(openScene.error())};
        }

        auto exit = actionRegistry.registerAction(EditorActionDesc{
            .id = EditorId{.value = "file.exit"},
            .menuPath = "File",
            .label = "Exit",
            .labelKey = "action.file.exit",
            .shortcut = "Alt+F4",
            .enabled = false,
        });
        if (!exit) {
            return std::unexpected{std::move(exit.error())};
        }

        auto sceneView = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.scene-view"},
                .menuPath = "View",
                .label = "Scene View",
                .labelKey = "action.view.sceneView",
                .shortcut = "Ctrl+1",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.panels.focusPanel("scene-view"));
            });
        if (!sceneView) {
            return std::unexpected{std::move(sceneView.error())};
        }

        auto renderGraph = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.render-graph"},
                .menuPath = "View",
                .label = "Live RG View",
                .labelKey = "action.view.renderGraph",
                .shortcut = "Ctrl+3",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.panels.focusPanel("render-graph"));
            });
        if (!renderGraph) {
            return std::unexpected{std::move(renderGraph.error())};
        }

        auto frameDebugger = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.frame-debugger"},
                .menuPath = "View",
                .label = "Frame Debugger",
                .labelKey = "action.view.frameDebugger",
                .shortcut = "Ctrl+4",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.panels.focusPanel("frame-debugger"));
            });
        if (!frameDebugger) {
            return std::unexpected{std::move(frameDebugger.error())};
        }

        auto log = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.log"},
                .menuPath = "View",
                .label = "Log",
                .labelKey = "action.view.log",
                .shortcut = "Ctrl+2",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.panels.focusPanel("log"));
            });
        if (!log) {
            return std::unexpected{std::move(log.error())};
        }

        auto uiStylePreview = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.ui-style-preview"},
                .menuPath = "View",
                .label = "UI Style Preview",
                .labelKey = "action.view.uiStylePreview",
                .shortcut = "Ctrl+5",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.panels.focusPanel("ui-style-preview"));
            });
        if (!uiStylePreview) {
            return std::unexpected{std::move(uiStylePreview.error())};
        }

        auto editorSettings = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.editor-settings"},
                .menuPath = "View",
                .label = "Editor Settings",
                .labelKey = "action.view.editorSettings",
                .shortcut = {},
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.panels.focusPanel("editor-settings"));
            });
        if (!editorSettings) {
            return std::unexpected{std::move(editorSettings.error())};
        }

        auto resetLayout = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "view.reset-layout"},
                .menuPath = "View",
                .label = "Reset Layout",
                .labelKey = "action.view.resetLayout",
                .shortcut = {},
                .enabled = true,
            },
            [](EditorActionContext& context) { context.workspace.requestLayoutReset(); });
        if (!resetLayout) {
            return std::unexpected{std::move(resetLayout.error())};
        }

        auto captureFrame = actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "debug.capture-frame"},
                .menuPath = "Debug",
                .label = "Capture Frame",
                .labelKey = "action.debug.captureFrame",
                .shortcut = "F8",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.frameDebugger.requestCapture());
            });
        if (!captureFrame) {
            return std::unexpected{std::move(captureFrame.error())};
        }

        return actionRegistry.registerAction(
            EditorActionDesc{
                .id = EditorId{.value = "debug.resume-frame"},
                .menuPath = "Debug",
                .label = "Resume",
                .labelKey = "action.debug.resumeFrame",
                .shortcut = "Shift+F8",
                .enabled = true,
            },
            [](EditorActionContext& context) {
                static_cast<void>(context.frameDebugger.requestResume());
            });
    }

    [[nodiscard]] asharia::VoidResult registerEditorTools(EditorToolRegistry& toolRegistry) {
        auto sceneView = toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.scene-view"},
            .title = "Scene View",
            .titleKey = "tool.sceneView",
            .category = EditorToolCategory::Viewport,
            .panels = {EditorToolPanelContribution{.panelId = "scene-view"}},
            .actions = {EditorToolActionContribution{
                .actionId = "view.scene-view",
                .toolbarSlot = EditorToolbarSlot::View,
            }},
            .viewportOverlays =
                {
                    EditorToolViewportOverlayContribution{
                        .overlayId = std::string{kEditorSceneGridOverlayId},
                        .viewportId = "scene-view",
                    },
                    EditorToolViewportOverlayContribution{
                        .overlayId = std::string{kEditorSceneTransformGizmoOverlayId},
                        .viewportId = "scene-view",
                    },
                    EditorToolViewportOverlayContribution{
                        .overlayId = std::string{kEditorSceneSelectionOutlineOverlayId},
                        .viewportId = "scene-view",
                    },
                },
        });
        if (!sceneView) {
            return std::unexpected{std::move(sceneView.error())};
        }

        auto renderGraph = toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.render-graph"},
            .title = "RenderGraph Diagnostics",
            .titleKey = "tool.renderGraph",
            .category = EditorToolCategory::Diagnostics,
            .panels = {EditorToolPanelContribution{.panelId = "render-graph"}},
            .actions = {EditorToolActionContribution{
                .actionId = "view.render-graph",
                .toolbarSlot = EditorToolbarSlot::View,
            }},
            .viewportOverlays = {},
        });
        if (!renderGraph) {
            return std::unexpected{std::move(renderGraph.error())};
        }

        auto frameDebugger = toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.frame-debugger"},
            .title = "Frame Debugger",
            .titleKey = "tool.frameDebugger",
            .category = EditorToolCategory::Diagnostics,
            .panels = {EditorToolPanelContribution{.panelId = "frame-debugger"}},
            .actions =
                {
                    EditorToolActionContribution{
                        .actionId = "debug.capture-frame",
                        .toolbarSlot = EditorToolbarSlot::Debug,
                    },
                    EditorToolActionContribution{
                        .actionId = "debug.resume-frame",
                        .toolbarSlot = EditorToolbarSlot::Debug,
                    },
                    EditorToolActionContribution{
                        .actionId = "view.frame-debugger",
                        .toolbarSlot = EditorToolbarSlot::View,
                    },
                },
            .viewportOverlays = {},
        });
        if (!frameDebugger) {
            return std::unexpected{std::move(frameDebugger.error())};
        }

        auto log = toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.log"},
            .title = "Log",
            .titleKey = "tool.log",
            .category = EditorToolCategory::Diagnostics,
            .panels = {EditorToolPanelContribution{.panelId = "log"}},
            .actions = {EditorToolActionContribution{
                .actionId = "view.log",
                .toolbarSlot = EditorToolbarSlot::View,
            }},
            .viewportOverlays = {},
        });
        if (!log) {
            return std::unexpected{std::move(log.error())};
        }

        auto style = toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.ui-style-preview"},
            .title = "UI Style Preview",
            .titleKey = "tool.uiStylePreview",
            .category = EditorToolCategory::Styling,
            .panels = {EditorToolPanelContribution{.panelId = "ui-style-preview"}},
            .actions = {EditorToolActionContribution{
                .actionId = "view.ui-style-preview",
                .toolbarSlot = EditorToolbarSlot::Utility,
            }},
            .viewportOverlays = {},
        });
        if (!style) {
            return std::unexpected{std::move(style.error())};
        }

        auto settings = toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.editor-settings"},
            .title = "Editor Settings",
            .titleKey = "tool.editorSettings",
            .category = EditorToolCategory::Settings,
            .panels = {EditorToolPanelContribution{.panelId = "editor-settings"}},
            .actions = {EditorToolActionContribution{
                .actionId = "view.editor-settings",
                .toolbarSlot = EditorToolbarSlot::Utility,
            }},
            .viewportOverlays = {},
        });
        if (!settings) {
            return std::unexpected{std::move(settings.error())};
        }

        return toolRegistry.registerTool(EditorToolDesc{
            .id = EditorId{.value = "tool.workspace-layout"},
            .title = "Workspace Layout",
            .titleKey = "tool.workspaceLayout",
            .category = EditorToolCategory::Core,
            .panels = {},
            .actions = {EditorToolActionContribution{
                .actionId = "view.reset-layout",
            }},
            .viewportOverlays = {},
        });
    }

    [[nodiscard]] asharia::VoidResult
    registerEditorAppRegistries(EditorPanelRegistry& panelRegistry,
                                EditorActionRegistry& actionRegistry,
                                EditorToolRegistry& toolRegistry,
                                EditorEventQueue& eventQueue) {
        panelRegistry.setEventQueue(&eventQueue);

        auto panels = registerEditorPanels(panelRegistry);
        if (!panels) {
            return std::unexpected{std::move(panels.error())};
        }

        auto actions = registerEditorActions(actionRegistry);
        if (!actions) {
            return std::unexpected{std::move(actions.error())};
        }

        return registerEditorTools(toolRegistry);
    }

} // namespace asharia::editor
