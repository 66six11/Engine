#include "editor_inspector_smoke.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/scene/entity_id.hpp"

#include "editor_inspector_model.hpp"
#include "editor_selection.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {

    namespace {

        struct EditorInspectorSmokeRowId {
            std::string_view sectionId;
            std::string_view rowId;
        };

        [[nodiscard]] EditorSceneEntitySelectionId target(std::uint32_t index,
                                                          std::uint32_t generation = 1U) {
            return EditorSceneEntitySelectionId{
                .sceneId = std::string{kEditorInspectedWorldSceneId},
                .entity = asharia::EntityId{.index = index, .generation = generation},
            };
        }

        [[nodiscard]] EditorSelectionItem
        item(EditorSceneEntitySelectionId targetId,
             EditorSelectionTargetState state = EditorSelectionTargetState::Resolved,
             bool primary = false, std::string displayName = {}) {
            return EditorSelectionItem{
                .target = std::move(targetId),
                .state = state,
                .primary = primary,
                .displayName = std::move(displayName),
            };
        }

        [[nodiscard]] const EditorInspectorRow* requireRow(const EditorInspectorModel& model,
                                                           EditorInspectorSmokeRowId expected) {
            const EditorInspectorSection* section =
                findEditorInspectorSection(model, expected.sectionId);
            if (section == nullptr) {
                asharia::logError("Editor inspector smoke missed an expected section.");
                return nullptr;
            }
            const EditorInspectorRow* row = findEditorInspectorRow(*section, expected.rowId);
            if (row == nullptr) {
                asharia::logError("Editor inspector smoke missed an expected row.");
                return nullptr;
            }
            return row;
        }

        [[nodiscard]] bool expectValueKind(const EditorInspectorModel& model,
                                           EditorInspectorSmokeRowId expected,
                                           EditorInspectorValueKind kind) {
            const EditorInspectorRow* found = requireRow(model, expected);
            if (found == nullptr || found->value.kind != kind) {
                asharia::logError("Editor inspector smoke saw an unexpected value kind.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateEmptyModel() {
            EditorSelectionSnapshot selection{};
            const EditorInspectorModel model =
                buildEditorInspectorModel(EditorInspectorModelBuildInput{
                    .selection = selection, .undoDepth = 1U, .redoDepth = 2U});
            const EditorInspectorRow* undoRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "command-history",
                                                            .rowId = "command.undoDepth"});
            if (!model.empty() || !model.readOnly || model.selectionCount != 0U ||
                model.selectionRevision != 0U || model.hasMixedValues() || model.hasValidation() ||
                undoRow == nullptr || undoRow->value.text != "1" ||
                !expectValueKind(model,
                                 EditorInspectorSmokeRowId{.sectionId = "selection",
                                                           .rowId = "selection.target"},
                                 EditorInspectorValueKind::Empty)) {
                asharia::logError("Editor inspector smoke empty model contract failed.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateSingleReadOnlyModel() {
            const EditorSceneEntitySelectionId cameraTarget = target(1U);
            EditorSelectionSnapshot selection{
                .revision = 7U,
                .items = std::vector<EditorSelectionItem>{item(
                    cameraTarget, EditorSelectionTargetState::Resolved, true, "Camera")},
            };
            const EditorInspectorModel model =
                buildEditorInspectorModel(EditorInspectorModelBuildInput{.selection = selection});
            const EditorInspectorRow* targetRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "selection",
                                                            .rowId = "selection.target"});
            const EditorInspectorRow* primaryRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "selection",
                                                            .rowId = "selection.primary"});
            if (model.empty() || !model.readOnly || model.selectionCount != 1U ||
                model.selectionRevision != 7U || model.hasMixedValues() || model.hasValidation() ||
                targetRow == nullptr || primaryRow == nullptr || !targetRow->readOnly ||
                !primaryRow->readOnly || targetRow->value.kind != EditorInspectorValueKind::Text ||
                targetRow->value.text != editorSelectionTargetLabel(cameraTarget) ||
                primaryRow->value.text != "Camera") {
                asharia::logError("Editor inspector smoke single read-only model failed.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateMixedAndValidationModel() {
            EditorSelectionSnapshot selection{
                .revision = 9U,
                .items =
                    std::vector<EditorSelectionItem>{
                        item(target(1U), EditorSelectionTargetState::Resolved, true, "Camera"),
                        item(target(2U), EditorSelectionTargetState::Missing, false,
                             "Missing Light"),
                        item(target(3U), EditorSelectionTargetState::Stale, false, "Stale Mesh")},
            };
            const EditorInspectorModel model =
                buildEditorInspectorModel(EditorInspectorModelBuildInput{.selection = selection});
            const EditorInspectorRow* missingRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "selection",
                                                            .rowId = "selection.missing"});
            const EditorInspectorRow* staleRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "selection",
                                                            .rowId = "selection.stale"});
            if (model.empty() || model.selectionCount != 3U || !model.hasMixedValues() ||
                !model.hasValidation() || missingRow == nullptr || staleRow == nullptr ||
                missingRow->validation.empty() || staleRow->validation.empty() ||
                !expectValueKind(model,
                                 EditorInspectorSmokeRowId{.sectionId = "selection",
                                                           .rowId = "selection.target"},
                                 EditorInspectorValueKind::Mixed) ||
                !expectValueKind(
                    model,
                    EditorInspectorSmokeRowId{.sectionId = "selection", .rowId = "selection.state"},
                    EditorInspectorValueKind::Mixed)) {
                asharia::logError("Editor inspector smoke mixed/validation model failed.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateDirtySectionModel() {
            EditorSelectionSnapshot selection{};
            EditorDirtySnapshot dirty{
                .revision = 5U,
                .transientUi = std::vector<EditorDirtyEntry>{EditorDirtyEntry{.stableId = "layout",
                                                                              .label = "Layout"}},
                .documents = std::vector<EditorDirtyEntry>{EditorDirtyEntry{
                    .stableId = "scene:main", .label = "Main Scene"}},
                .assetMetadata = std::vector<EditorDirtyEntry>{EditorDirtyEntry{
                    .stableId = "asset:hero", .label = "Hero metadata"}},
                .pendingReimportCount = 3U,
            };
            const EditorInspectorModel model =
                buildEditorInspectorModel(EditorInspectorModelBuildInput{
                    .selection = selection,
                    .dirtySnapshot = &dirty,
                });
            const EditorInspectorRow* documentRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "dirty-state",
                                                            .rowId = "dirty.document"});
            const EditorInspectorRow* assetMetadataRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "dirty-state",
                                                            .rowId = "dirty.assetMetadata"});
            const EditorInspectorRow* pendingReimportRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "dirty-state",
                                                            .rowId = "dirty.pendingReimport"});
            const EditorInspectorRow* transientUiRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "dirty-state",
                                                            .rowId = "dirty.transientUi"});
            const EditorInspectorRow* revisionRow =
                requireRow(model, EditorInspectorSmokeRowId{.sectionId = "dirty-state",
                                                            .rowId = "dirty.revision"});
            if (documentRow == nullptr || assetMetadataRow == nullptr ||
                pendingReimportRow == nullptr || transientUiRow == nullptr ||
                revisionRow == nullptr || !documentRow->readOnly ||
                documentRow->value.text != "yes" || assetMetadataRow->value.text != "1" ||
                pendingReimportRow->value.text != "3" || transientUiRow->value.text != "1" ||
                revisionRow->value.text != "5") {
                asharia::logError("Editor inspector smoke dirty section model failed.");
                return false;
            }
            return true;
        }

    } // namespace

    bool validateEditorInspectorModelSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        return validateEmptyModel() && validateSingleReadOnlyModel() &&
               validateMixedAndValidationModel() && validateDirtySectionModel();
    }

} // namespace asharia::editor
