#include "editor_dirty_state.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "editor_event.hpp"

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

    EditorDirtyState::EditorDirtyState(EditorEventQueue& eventQueue) : eventQueue_(&eventQueue) {}

    void EditorDirtyState::setEventQueue(EditorEventQueue* eventQueue) noexcept {
        eventQueue_ = eventQueue;
    }

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
        return mark(snapshot_.transientUi, "TransientUi", std::move(stableId), std::move(label));
    }

    bool EditorDirtyState::markDocumentDirty(std::string stableId, std::string label) {
        return mark(snapshot_.documents, "Document", std::move(stableId), std::move(label));
    }

    bool EditorDirtyState::markAssetMetadataDirty(std::string stableId, std::string label) {
        return mark(snapshot_.assetMetadata, "AssetMetadata", std::move(stableId),
                    std::move(label));
    }

    bool EditorDirtyState::clearTransientUiDirty(std::string_view stableId) {
        return clear(snapshot_.transientUi, "TransientUi", stableId);
    }

    bool EditorDirtyState::clearDocumentDirty(std::string_view stableId) {
        return clear(snapshot_.documents, "Document", stableId);
    }

    bool EditorDirtyState::clearAssetMetadataDirty(std::string_view stableId) {
        return clear(snapshot_.assetMetadata, "AssetMetadata", stableId);
    }

    bool EditorDirtyState::clearTransientUiDirty() {
        if (snapshot_.transientUi.empty()) {
            return false;
        }
        snapshot_.transientUi.clear();
        bumpRevision();
        emitChanged("TransientUi", "transient-ui", "clear");
        return true;
    }

    bool EditorDirtyState::clearPersistentDirty() {
        if (snapshot_.documents.empty() && snapshot_.assetMetadata.empty()) {
            return false;
        }
        snapshot_.documents.clear();
        snapshot_.assetMetadata.clear();
        bumpRevision();
        emitChanged("Persistent", "persistent", "clear");
        return true;
    }

    bool EditorDirtyState::setPendingReimportCount(std::size_t count) {
        if (snapshot_.pendingReimportCount == count) {
            return false;
        }
        snapshot_.pendingReimportCount = count;
        bumpRevision();
        emitChanged("PendingReimport", "pending-reimport", std::to_string(count));
        return true;
    }

    bool EditorDirtyState::clearPendingReimports() {
        return setPendingReimportCount(0U);
    }

    const EditorDirtySnapshot& EditorDirtyState::snapshot() const noexcept {
        return snapshot_;
    }

    bool EditorDirtyState::mark(std::vector<EditorDirtyEntry>& entries, std::string_view bucket,
                                std::string stableId, std::string label) {
        if (stableId.empty() || hasEntry(entries, stableId)) {
            return false;
        }
        std::string entryLabel = normalizedLabel(stableId, std::move(label));
        entries.push_back(EditorDirtyEntry{
            .stableId = std::move(stableId),
            .label = std::move(entryLabel),
        });
        bumpRevision();
        emitChanged(bucket, entries.back().stableId, entries.back().label);
        return true;
    }

    bool EditorDirtyState::clear(std::vector<EditorDirtyEntry>& entries, std::string_view bucket,
                                 std::string_view stableId) {
        const std::size_t oldSize = entries.size();
        std::erase_if(entries, [stableId](const EditorDirtyEntry& entry) {
            return entry.stableId == stableId;
        });
        if (entries.size() == oldSize) {
            return false;
        }
        bumpRevision();
        emitChanged(bucket, std::string{stableId}, "clear");
        return true;
    }

    void EditorDirtyState::emitChanged(std::string_view bucket, std::string subjectId,
                                       std::string message) {
        if (eventQueue_ == nullptr) {
            return;
        }
        eventQueue_->push(EditorEvent{
            .kind = EditorEventKind::DirtyStateChanged,
            .sourceId = EditorId{.value = std::string{kEditorDirtyStateOwnerId}},
            .metadata =
                EditorEventMetadata{
                    .revision = snapshot_.revision,
                    .subjectId = std::move(subjectId),
                    .label = std::string{bucket},
                    .message = std::move(message),
                    .severity = EditorEventSeverity::Info,
                    .outcome = EditorEventOutcome::Succeeded,
                },
        });
    }

    void EditorDirtyState::bumpRevision() noexcept {
        ++snapshot_.revision;
    }

} // namespace asharia::editor
