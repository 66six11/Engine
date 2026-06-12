#include "editor_smoke_validation.hpp"

#include "editor_asset_catalog_smoke.hpp"
#include "editor_command_smoke.hpp"
#include "editor_dirty_state_smoke.hpp"
#include "editor_inspector_smoke.hpp"
#include "editor_registration_smoke.hpp"
#include "editor_selection_smoke.hpp"
#include "editor_startup_smoke.hpp"
#include "editor_state_event_smoke.hpp"
#include "editor_viewport_tool_state_smoke.hpp"

namespace asharia::editor {
    [[nodiscard]] bool validateEditorStartupGates(
        EditorRunMode mode, const ImGuiRuntime& imgui, EditorLocale locale, EditorUiThemeId theme,
        EditorActionRegistry& actionRegistry, EditorActionServices& actionServices,
        EditorSettingsController& settings, EditorI18n& i18n,
        const EditorToolRegistry& toolRegistry, const EditorToolManager& toolManager) {
        return validateEditorStartupSmoke(mode, imgui, locale, theme) &&
               validateEditorAssetCatalogSnapshotSmoke(mode) &&
               validateEditorRegistrationSmoke(mode, actionRegistry, actionServices, settings, i18n,
                                               toolRegistry, toolManager) &&
               validateEditorCommandSmoke(mode) && validateEditorSelectionSmoke(mode) &&
               validateEditorInspectorModelSmoke(mode) && validateEditorDirtyStateSmoke(mode) &&
               validateEditorStateEventSmoke(mode) && validateEditorViewportToolStateSmoke(mode);
    }
} // namespace asharia::editor
