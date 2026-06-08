#pragma once

#include "asharia/core/result.hpp"

#include "editor_action.hpp"
#include "editor_app_config.hpp"
#include "editor_asset_catalog.hpp"
#include "editor_asset_icon.hpp"
#include "editor_asset_import_settings_command.hpp"
#include "editor_command.hpp"
#include "editor_event.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_panel.hpp"
#include "editor_selection.hpp"
#include "editor_settings.hpp"
#include "editor_tool.hpp"
#include "editor_tool_manager.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    struct EditorAppServices {
        explicit EditorAppServices(const EditorSettingsRunState& settingsRun);
        EditorAppServices(const EditorAppServices&) = delete;
        EditorAppServices& operator=(const EditorAppServices&) = delete;
        EditorAppServices(EditorAppServices&&) = delete;
        EditorAppServices& operator=(EditorAppServices&&) = delete;

        EditorEventQueue eventQueue;
        EditorSelectionSet selectionSet;
        EditorDiagnosticsLog diagnosticsLog;
        EditorFrameDebugger frameDebugger;
        EditorI18n i18n;
        EditorSettingsController settingsController;
        EditorWorkspaceController workspaceController;
        EditorPanelRegistry panelRegistry;
        EditorActionRegistry actionRegistry;
        EditorToolRegistry toolRegistry;
        EditorToolManager toolManager;
        EditorAssetCatalogStore assetCatalogStore;
        EditorAssetIconRegistry assetIconRegistry;
        EditorCommandHistory commandHistory;
        EditorAssetReimportRequestLog assetReimportRequests;
        EditorAssetReimportPendingState assetPendingReimports;
        EditorActionServices actionServices;
    };

    [[nodiscard]] asharia::VoidResult registerEditorAppServices(EditorAppServices& services);

} // namespace asharia::editor
