#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/core/result.hpp"

#include "editor_command.hpp"

namespace asharia::editor {

    struct EditorAssetImportSettingEdit {
        std::string key;
        std::string value;
    };

    struct EditorAssetImportSettingsEditRequest {
        std::filesystem::path metadataFile;
        std::string targetProfile;
        std::vector<EditorAssetImportSettingEdit> edits;

        [[nodiscard]] explicit operator bool() const noexcept {
            return !metadataFile.empty() && !targetProfile.empty() && !edits.empty();
        }
    };

    struct EditorAssetReimportRequest {
        asharia::asset::AssetGuid guid{};
        std::string sourcePath;
        std::filesystem::path metadataFile;
        std::string targetProfile;
        std::vector<std::string> changedSettingKeys;
    };

    class EditorAssetReimportRequestLog {
    public:
        void record(EditorAssetReimportRequest request);
        void clear();

        [[nodiscard]] const std::vector<EditorAssetReimportRequest>& requests() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;

    private:
        std::vector<EditorAssetReimportRequest> requests_;
    };

    struct EditorAssetPendingReimport {
        EditorAssetReimportRequest request;
        std::size_t requestCount{1};
    };

    struct EditorAssetPendingReimportWorkItem {
        asharia::asset::AssetGuid guid{};
        std::string sourcePath;
        std::filesystem::path metadataFile;
        std::string targetProfile;
        std::vector<std::string> changedSettingKeys;
        std::size_t requestCount{1};
    };

    struct EditorAssetReimportSourceKey {
        asharia::asset::AssetGuid guid{};
        std::string_view sourcePath;
        std::string_view targetProfile;
    };

    class EditorAssetReimportPendingState {
    public:
        void record(EditorAssetReimportRequest request);
        void recordAll(std::span<const EditorAssetReimportRequest> requests);
        void clear();
        [[nodiscard]] bool clearForSource(asharia::asset::AssetGuid guid,
                                          std::string_view targetProfile);
        [[nodiscard]] bool clearForSource(EditorAssetReimportSourceKey sourceKey);

        [[nodiscard]] bool hasPending(asharia::asset::AssetGuid guid,
                                      std::string_view targetProfile) const noexcept;
        [[nodiscard]] bool hasPending(EditorAssetReimportSourceKey sourceKey) const noexcept;
        [[nodiscard]] const EditorAssetPendingReimport*
        findPending(EditorAssetReimportSourceKey sourceKey) const noexcept;
        [[nodiscard]] const std::vector<EditorAssetPendingReimport>& pending() const noexcept;
        [[nodiscard]] std::vector<EditorAssetPendingReimportWorkItem> snapshotPendingWork() const;
        [[nodiscard]] std::size_t size() const noexcept;

    private:
        std::vector<EditorAssetPendingReimport> pending_;
    };

    [[nodiscard]] asharia::Result<std::unique_ptr<EditorCommand>>
    makeEditorAssetImportSettingsEditCommand(EditorAssetImportSettingsEditRequest request,
                                             EditorAssetReimportRequestLog& reimportRequests);

    [[nodiscard]] asharia::Result<std::unique_ptr<EditorCommand>>
    makeEditorTextureProfileEditCommand(const std::filesystem::path& metadataFile,
                                        std::string_view targetProfile,
                                        std::string_view textureProfile,
                                        EditorAssetReimportRequestLog& reimportRequests);

    [[nodiscard]] asharia::Result<std::string>
    readEditorTextureProfileSetting(const std::filesystem::path& metadataFile);

} // namespace asharia::editor
