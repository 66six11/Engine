#include "editor_dirty_state.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace asharia::editor {

    namespace {

        [[nodiscard]] bool hasEntry(const std::vector<EditorDirtyEntry>& entries,
                                    std::string_view stableId) {
            return std::ranges::any_of(entries, [stableId](const EditorDirtyEntry& entry) {
                return entry.stableId == stableId;
            });
        }

        [[nodiscard]] std::string normalizedLabel(std::string_view stableId, std::string label) {
            if (!label.empty()) {
                return label;
            }
            return std::string{stableId};
        }

    } // namespace

    bool EditorDirtySnapshot::hasTransientUiDirty() const noexcept {
        return !transientUi.empty();
    }

    bool EditorDirtySnapshot::hasDocumentDirty() const noexcept {
        return !documents.empty();
    }

    bool EditorDirtySnapshot::hasAssetMetadataDirty() const noexcept {
        return !assetMetadata.empty();
    }

    bool EditorDirtySnapshot::hasPendingReimport() const noexcept {
        return pendingReimportCount > 0U;
    }

    bool EditorDirtySnapshot::hasPersistentDirty() const noexcept {
        return hasDocumentDirty() || hasAssetMetadataDirty();
    }

    bool EditorDirtySnapshot::clean() const noexcept {
        return !hasTransientUiDirty() && !hasPersistentDirty() && !hasPendingReimport();
    }

    std::size_t EditorDirtySnapshot::persistentDirtyCount() const noexcept {
        return documents.size() + assetMetadata.size();
    }

    bool EditorDirtyState::markTransientUiDirty(std::string stableId, std::string label) {
        return mark(snapshot_.transientUi, std::move(stableId), std::move(label));
    }

    bool EditorDirtyState::markDocumentDirty(std::string stableId, std::string label) {
        return mark(snapshot_.documents, std::move(stableId), std::move(label));
    }

    bool EditorDirtyState::markAssetMetadataDirty(std::string stableId, std::string label) {
        return mark(snapshot_.assetMetadata, std::move(stableId), std::move(label));
    }

    bool EditorDirtyState::clearTransientUiDirty(std::string_view stableId) {
        return clear(snapshot_.transientUi, stableId);
    }

    bool EditorDirtyState::clearDocumentDirty(std::string_view stableId) {
        return clear(snapshot_.documents, stableId);
    }

    bool EditorDirtyState::clearAssetMetadataDirty(std::string_view stableId) {
        return clear(snapshot_.assetMetadata, stableId);
    }

    bool EditorDirtyState::clearTransientUiDirty() {
        if (snapshot_.transientUi.empty()) {
            return false;
        }
        snapshot_.transientUi.clear();
        bumpRevision();
        return true;
    }

    bool EditorDirtyState::clearPersistentDirty() {
        if (snapshot_.documents.empty() && snapshot_.assetMetadata.empty()) {
            return false;
        }
        snapshot_.documents.clear();
        snapshot_.assetMetadata.clear();
        bumpRevision();
        return true;
    }

    bool EditorDirtyState::setPendingReimportCount(std::size_t count) {
        if (snapshot_.pendingReimportCount == count) {
            return false;
        }
        snapshot_.pendingReimportCount = count;
        bumpRevision();
        return true;
    }

    bool EditorDirtyState::clearPendingReimports() {
        return setPendingReimportCount(0U);
    }

    const EditorDirtySnapshot& EditorDirtyState::snapshot() const noexcept {
        return snapshot_;
    }

    bool EditorDirtyState::mark(std::vector<EditorDirtyEntry>& entries, std::string stableId,
                                std::string label) {
        if (stableId.empty() || hasEntry(entries, stableId)) {
            return false;
        }
        std::string entryLabel = normalizedLabel(stableId, std::move(label));
        entries.push_back(EditorDirtyEntry{
            .stableId = std::move(stableId),
            .label = std::move(entryLabel),
        });
        bumpRevision();
        return true;
    }

    bool EditorDirtyState::clear(std::vector<EditorDirtyEntry>& entries,
                                 std::string_view stableId) {
        const std::size_t oldSize = entries.size();
        std::erase_if(entries, [stableId](const EditorDirtyEntry& entry) {
            return entry.stableId == stableId;
        });
        if (entries.size() == oldSize) {
            return false;
        }
        bumpRevision();
        return true;
    }

    void EditorDirtyState::bumpRevision() noexcept {
        ++snapshot_.revision;
    }

} // namespace asharia::editor
