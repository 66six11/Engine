#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace asharia::editor {

    struct EditorDirtyEntry {
        std::string stableId;
        std::string label;

        [[nodiscard]] friend bool operator==(const EditorDirtyEntry&,
                                             const EditorDirtyEntry&) = default;
    };

    struct EditorDirtySnapshot {
        std::uint64_t revision{};
        std::vector<EditorDirtyEntry> transientUi;
        std::vector<EditorDirtyEntry> documents;
        std::vector<EditorDirtyEntry> assetMetadata;
        std::size_t pendingReimportCount{};

        [[nodiscard]] bool hasTransientUiDirty() const noexcept;
        [[nodiscard]] bool hasDocumentDirty() const noexcept;
        [[nodiscard]] bool hasAssetMetadataDirty() const noexcept;
        [[nodiscard]] bool hasPendingReimport() const noexcept;
        [[nodiscard]] bool hasPersistentDirty() const noexcept;
        [[nodiscard]] bool clean() const noexcept;
        [[nodiscard]] std::size_t persistentDirtyCount() const noexcept;
    };

    class EditorDirtyState {
    public:
        [[nodiscard]] bool markTransientUiDirty(std::string stableId, std::string label = {});
        [[nodiscard]] bool markDocumentDirty(std::string stableId, std::string label = {});
        [[nodiscard]] bool markAssetMetadataDirty(std::string stableId, std::string label = {});

        [[nodiscard]] bool clearTransientUiDirty(std::string_view stableId);
        [[nodiscard]] bool clearDocumentDirty(std::string_view stableId);
        [[nodiscard]] bool clearAssetMetadataDirty(std::string_view stableId);
        [[nodiscard]] bool clearTransientUiDirty();
        [[nodiscard]] bool clearPersistentDirty();

        [[nodiscard]] bool setPendingReimportCount(std::size_t count);
        [[nodiscard]] bool clearPendingReimports();

        [[nodiscard]] const EditorDirtySnapshot& snapshot() const noexcept;

    private:
        [[nodiscard]] bool mark(std::vector<EditorDirtyEntry>& entries, std::string stableId,
                                std::string label);
        [[nodiscard]] bool clear(std::vector<EditorDirtyEntry>& entries, std::string_view stableId);
        void bumpRevision() noexcept;

        EditorDirtySnapshot snapshot_;
    };

} // namespace asharia::editor
