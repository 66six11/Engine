#include "editor_dirty_state_smoke.hpp"

#include <cstdint>

#include "asharia/core/log.hpp"

#include "editor_dirty_state.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] bool validateTransientSeparation(EditorDirtyState& dirty) {
            if (!dirty.markTransientUiDirty("layout", "Layout") ||
                !dirty.snapshot().hasTransientUiDirty() || dirty.snapshot().hasPersistentDirty() ||
                dirty.snapshot().persistentDirtyCount() != 0U) {
                asharia::logError("Editor dirty state smoke mixed transient and persistent dirty.");
                return false;
            }
            if (dirty.markTransientUiDirty("layout", "Duplicate") ||
                dirty.snapshot().transientUi.size() != 1U) {
                asharia::logError("Editor dirty state smoke accepted duplicate transient dirty.");
                return false;
            }
            if (!dirty.clearTransientUiDirty("layout") || dirty.snapshot().hasTransientUiDirty()) {
                asharia::logError("Editor dirty state smoke failed to clear transient dirty.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validatePersistentBuckets(EditorDirtyState& dirty) {
            if (!dirty.markDocumentDirty("scene:main", "Main Scene") ||
                !dirty.markAssetMetadataDirty("asset:hero", "hero.png metadata") ||
                !dirty.snapshot().hasDocumentDirty() || !dirty.snapshot().hasAssetMetadataDirty() ||
                !dirty.snapshot().hasPersistentDirty() ||
                dirty.snapshot().persistentDirtyCount() != 2U) {
                asharia::logError("Editor dirty state smoke persistent buckets failed.");
                return false;
            }
            if (dirty.markDocumentDirty("scene:main", "Duplicate") ||
                dirty.snapshot().documents.size() != 1U) {
                asharia::logError("Editor dirty state smoke accepted duplicate document dirty.");
                return false;
            }
            if (!dirty.clearDocumentDirty("scene:main") || dirty.snapshot().hasDocumentDirty() ||
                !dirty.snapshot().hasAssetMetadataDirty()) {
                asharia::logError("Editor dirty state smoke document clear crossed buckets.");
                return false;
            }
            if (!dirty.clearPersistentDirty() || dirty.snapshot().hasPersistentDirty()) {
                asharia::logError("Editor dirty state smoke failed to clear persistent dirty.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validatePendingReimport(EditorDirtyState& dirty) {
            const std::uint64_t revisionBefore = dirty.snapshot().revision;
            if (!dirty.setPendingReimportCount(2U) || !dirty.snapshot().hasPendingReimport() ||
                dirty.snapshot().pendingReimportCount != 2U ||
                dirty.snapshot().hasPersistentDirty()) {
                asharia::logError("Editor dirty state smoke pending reimport bucket failed.");
                return false;
            }
            if (dirty.setPendingReimportCount(2U) ||
                dirty.snapshot().revision != revisionBefore + 1U) {
                asharia::logError("Editor dirty state smoke emitted no-op pending revision.");
                return false;
            }
            if (!dirty.clearPendingReimports() || dirty.snapshot().hasPendingReimport()) {
                asharia::logError("Editor dirty state smoke failed to clear pending reimport.");
                return false;
            }
            return true;
        }

    } // namespace

    bool validateEditorDirtyStateSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        EditorDirtyState dirty;
        if (!dirty.snapshot().clean() || dirty.snapshot().revision != 0U) {
            asharia::logError("Editor dirty state smoke did not start clean.");
            return false;
        }
        return validateTransientSeparation(dirty) && validatePersistentBuckets(dirty) &&
               validatePendingReimport(dirty) && dirty.snapshot().clean();
    }

} // namespace asharia::editor
