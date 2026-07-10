#include "editor_asset_import_settings_command.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/core/error.hpp"
#include "asharia/core/file_io.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] asharia::Error importSettingsCommandError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        constexpr std::uint64_t kMaxEditorMetadataBytes = 16ULL * 1024ULL * 1024ULL;

        [[nodiscard]] asharia::Result<std::string> readTextFile(const std::filesystem::path& path) {
            auto text = asharia::core::readFileText(path, {.maxBytes = kMaxEditorMetadataBytes});
            if (!text) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command could not read metadata file '" +
                    path.string() + "': " + text.error().message)};
            }
            return text;
        }

        [[nodiscard]] asharia::VoidResult writeTextFile(const std::filesystem::path& path,
                                                        std::string_view text) {
            auto written = asharia::core::writeFileTextAtomically(path, text);
            if (!written) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command could not commit metadata file '" +
                    path.string() + "': " + written.error().message)};
            }
            return {};
        }

        [[nodiscard]] bool isSupportedTextureProfile(std::string_view profile) noexcept {
            return profile == asharia::asset::kTextureImportProfileTexture2D ||
                   profile == asharia::asset::kTextureImportProfileSpriteSheet ||
                   profile == asharia::asset::kTextureImportProfileTextureCube ||
                   profile == asharia::asset::kTextureImportProfileSkybox;
        }

        [[nodiscard]] asharia::Result<EditorAssetImportSettingEdit>
        normalizeEdit(const EditorAssetImportSettingEdit& edit) {
            if (edit.key != asharia::asset::kTextureImportProfileSettingKey) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command only supports setting key '" +
                    std::string{asharia::asset::kTextureImportProfileSettingKey} + "', got '" +
                    edit.key + "'.")};
            }

            std::string normalized = asharia::asset::normalizeTextureImportProfileName(edit.value);
            if (normalized.empty() || !isSupportedTextureProfile(normalized)) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command does not support texture profile '" +
                    edit.value + "'.")};
            }

            return EditorAssetImportSettingEdit{.key = edit.key, .value = std::move(normalized)};
        }

        [[nodiscard]] asharia::Result<std::vector<EditorAssetImportSettingEdit>>
        normalizeEdits(std::span<const EditorAssetImportSettingEdit> edits) {
            std::vector<EditorAssetImportSettingEdit> normalized;
            normalized.reserve(edits.size());
            for (const EditorAssetImportSettingEdit& edit : edits) {
                if (std::ranges::any_of(normalized,
                                        [&edit](const EditorAssetImportSettingEdit& existing) {
                                            return existing.key == edit.key;
                                        })) {
                    return std::unexpected{importSettingsCommandError(
                        "Editor import settings command has duplicate setting key '" + edit.key +
                        "'.")};
                }

                auto normalizedEdit = normalizeEdit(edit);
                if (!normalizedEdit) {
                    return std::unexpected{std::move(normalizedEdit.error())};
                }
                normalized.push_back(std::move(*normalizedEdit));
            }
            return normalized;
        }

        [[nodiscard]] asharia::asset::AssetImportSetting*
        findSetting(std::vector<asharia::asset::AssetImportSetting>& settings,
                    std::string_view key) {
            const auto found = std::ranges::find_if(
                settings, [key](const asharia::asset::AssetImportSetting& setting) {
                    return setting.key == key;
                });
            return found == settings.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool applyEdits(asharia::asset::AssetMetadataDocument& document,
                                      std::span<const EditorAssetImportSettingEdit> edits,
                                      std::vector<std::string>& changedSettingKeys) {
            bool changed = false;
            for (const EditorAssetImportSettingEdit& edit : edits) {
                if (asharia::asset::AssetImportSetting* setting =
                        findSetting(document.settings, edit.key)) {
                    if (setting->value == edit.value) {
                        continue;
                    }
                    setting->value = edit.value;
                } else {
                    document.settings.push_back(asharia::asset::AssetImportSetting{
                        .key = edit.key,
                        .value = edit.value,
                    });
                }
                changedSettingKeys.push_back(edit.key);
                changed = true;
            }

            if (changed) {
                document.source.settingsHash =
                    asharia::asset::hashAssetImportSettings(document.settings);
            }
            return changed;
        }

        [[nodiscard]] asharia::VoidResult
        validateRequest(const EditorAssetImportSettingsEditRequest& request) {
            if (request.metadataFile.empty()) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command requires a metadata file.")};
            }
            if (request.targetProfile.empty()) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command requires a target profile.")};
            }
            if (request.edits.empty()) {
                return std::unexpected{importSettingsCommandError(
                    "Editor import settings command requires at least one setting edit.")};
            }
            return {};
        }

        class EditorAssetImportSettingsEditCommand final : public EditorCommand {
        public:
            EditorAssetImportSettingsEditCommand(EditorAssetImportSettingsEditRequest request,
                                                 EditorAssetReimportRequestLog& reimportRequests)
                : request_(std::move(request)), reimportRequests_(&reimportRequests) {}

            [[nodiscard]] std::string description() const override {
                return "Edit asset import settings " + request_.metadataFile.string();
            }

            [[nodiscard]] asharia::Result<void> execute() override {
                auto validRequest = validateRequest(request_);
                if (!validRequest) {
                    return std::unexpected{std::move(validRequest.error())};
                }

                auto normalizedEdits = normalizeEdits(request_.edits);
                if (!normalizedEdits) {
                    return std::unexpected{std::move(normalizedEdits.error())};
                }

                auto oldText = readTextFile(request_.metadataFile);
                if (!oldText) {
                    return std::unexpected{std::move(oldText.error())};
                }

                auto document = asharia::asset::readAssetMetadataText(*oldText);
                if (!document) {
                    return std::unexpected{std::move(document.error())};
                }

                std::vector<std::string> changedSettingKeys;
                if (!applyEdits(*document, *normalizedEdits, changedSettingKeys)) {
                    oldText_ = std::move(*oldText);
                    lastChangedSettingKeys_.clear();
                    executed_ = true;
                    return {};
                }

                auto newText = asharia::asset::writeAssetMetadataText(*document);
                if (!newText) {
                    return std::unexpected{std::move(newText.error())};
                }

                if (auto written = writeTextFile(request_.metadataFile, *newText); !written) {
                    return std::unexpected{std::move(written.error())};
                }

                oldText_ = std::move(*oldText);
                lastChangedSettingKeys_ = changedSettingKeys;
                executed_ = true;
                recordReimportRequest(*document, changedSettingKeys);
                return {};
            }

            [[nodiscard]] asharia::Result<void> undo() override {
                if (!executed_) {
                    return std::unexpected{importSettingsCommandError(
                        "Editor import settings command cannot undo before execute.")};
                }
                if (lastChangedSettingKeys_.empty()) {
                    return {};
                }

                auto oldDocument = asharia::asset::readAssetMetadataText(oldText_);
                if (!oldDocument) {
                    return std::unexpected{std::move(oldDocument.error())};
                }

                if (auto written = writeTextFile(request_.metadataFile, oldText_); !written) {
                    return std::unexpected{std::move(written.error())};
                }
                recordReimportRequest(*oldDocument, lastChangedSettingKeys_);
                return {};
            }

        private:
            void recordReimportRequest(const asharia::asset::AssetMetadataDocument& document,
                                       std::span<const std::string> changedSettingKeys) {
                if (reimportRequests_ == nullptr || changedSettingKeys.empty()) {
                    return;
                }

                reimportRequests_->record(EditorAssetReimportRequest{
                    .guid = document.source.guid,
                    .sourcePath = document.source.sourcePath,
                    .metadataFile = request_.metadataFile,
                    .targetProfile = request_.targetProfile,
                    .changedSettingKeys = std::vector<std::string>{changedSettingKeys.begin(),
                                                                   changedSettingKeys.end()},
                });
            }

            EditorAssetImportSettingsEditRequest request_;
            EditorAssetReimportRequestLog* reimportRequests_{};
            std::string oldText_;
            std::vector<std::string> lastChangedSettingKeys_;
            bool executed_{false};
        };

    } // namespace

    void EditorAssetReimportRequestLog::record(EditorAssetReimportRequest request) {
        requests_.push_back(std::move(request));
    }

    void EditorAssetReimportRequestLog::clear() {
        requests_.clear();
    }

    const std::vector<EditorAssetReimportRequest>&
    EditorAssetReimportRequestLog::requests() const noexcept {
        return requests_;
    }

    std::size_t EditorAssetReimportRequestLog::size() const noexcept {
        return requests_.size();
    }

    namespace {

        [[nodiscard]] bool
        samePendingReimportTarget(const EditorAssetReimportRequest& pending,
                                  const EditorAssetReimportRequest& request) noexcept {
            if (pending.targetProfile != request.targetProfile) {
                return false;
            }

            if (pending.guid && request.guid) {
                return pending.guid == request.guid;
            }

            return !pending.sourcePath.empty() && pending.sourcePath == request.sourcePath;
        }

        [[nodiscard]] bool
        pendingReimportMatchesSource(const EditorAssetReimportRequest& pending,
                                     EditorAssetReimportSourceKey sourceKey) noexcept {
            if (pending.targetProfile != sourceKey.targetProfile) {
                return false;
            }
            if (sourceKey.guid && pending.guid == sourceKey.guid) {
                return true;
            }
            return !sourceKey.sourcePath.empty() && pending.sourcePath == sourceKey.sourcePath;
        }

        void appendUniqueChangedSettingKeys(std::vector<std::string>& target,
                                            std::span<const std::string> keys) {
            for (const std::string& key : keys) {
                if (std::ranges::find(target, key) == target.end()) {
                    target.push_back(key);
                }
            }
        }

        [[nodiscard]] EditorAssetPendingReimportWorkItem
        makePendingWorkItem(const EditorAssetPendingReimport& pending) {
            std::vector<std::string> changedSettingKeys = pending.request.changedSettingKeys;
            std::ranges::sort(changedSettingKeys);
            return EditorAssetPendingReimportWorkItem{
                .guid = pending.request.guid,
                .sourcePath = pending.request.sourcePath,
                .metadataFile = pending.request.metadataFile,
                .targetProfile = pending.request.targetProfile,
                .changedSettingKeys = std::move(changedSettingKeys),
                .requestCount = pending.requestCount,
            };
        }

        [[nodiscard]] std::string guidSortKey(asharia::asset::AssetGuid guid) {
            return guid ? asharia::asset::formatAssetGuid(guid) : std::string{};
        }

        [[nodiscard]] bool pendingWorkItemLess(const EditorAssetPendingReimportWorkItem& left,
                                               const EditorAssetPendingReimportWorkItem& right) {
            if (left.targetProfile != right.targetProfile) {
                return left.targetProfile < right.targetProfile;
            }
            if (left.sourcePath != right.sourcePath) {
                return left.sourcePath < right.sourcePath;
            }
            if (const std::string leftGuid = guidSortKey(left.guid),
                rightGuid = guidSortKey(right.guid);
                leftGuid != rightGuid) {
                return leftGuid < rightGuid;
            }
            return left.metadataFile.string() < right.metadataFile.string();
        }

    } // namespace

    void EditorAssetReimportPendingState::record(EditorAssetReimportRequest request) {
        if ((!request.guid && request.sourcePath.empty()) || request.targetProfile.empty() ||
            request.changedSettingKeys.empty()) {
            return;
        }

        const auto found =
            std::ranges::find_if(pending_, [&request](const EditorAssetPendingReimport& pending) {
                return samePendingReimportTarget(pending.request, request);
            });
        if (found == pending_.end()) {
            pending_.push_back(
                EditorAssetPendingReimport{.request = std::move(request), .requestCount = 1U});
            return;
        }

        found->request.guid = request.guid;
        found->request.sourcePath = std::move(request.sourcePath);
        found->request.metadataFile = std::move(request.metadataFile);
        found->request.targetProfile = std::move(request.targetProfile);
        appendUniqueChangedSettingKeys(found->request.changedSettingKeys,
                                       request.changedSettingKeys);
        ++found->requestCount;
    }

    void EditorAssetReimportPendingState::recordAll(
        std::span<const EditorAssetReimportRequest> requests) {
        for (const EditorAssetReimportRequest& request : requests) {
            record(request);
        }
    }

    void EditorAssetReimportPendingState::clear() {
        pending_.clear();
    }

    bool EditorAssetReimportPendingState::clearForSource(asharia::asset::AssetGuid guid,
                                                         std::string_view targetProfile) {
        return clearForSource(EditorAssetReimportSourceKey{
            .guid = guid, .sourcePath = {}, .targetProfile = targetProfile});
    }

    bool EditorAssetReimportPendingState::clearForSource(EditorAssetReimportSourceKey sourceKey) {
        const auto oldSize = pending_.size();
        std::erase_if(pending_, [sourceKey](const EditorAssetPendingReimport& pending) {
            return pendingReimportMatchesSource(pending.request, sourceKey);
        });
        return pending_.size() != oldSize;
    }

    bool
    EditorAssetReimportPendingState::hasPending(asharia::asset::AssetGuid guid,
                                                std::string_view targetProfile) const noexcept {
        return hasPending(EditorAssetReimportSourceKey{
            .guid = guid, .sourcePath = {}, .targetProfile = targetProfile});
    }

    bool EditorAssetReimportPendingState::hasPending(
        EditorAssetReimportSourceKey sourceKey) const noexcept {
        return findPending(sourceKey) != nullptr;
    }

    const EditorAssetPendingReimport* EditorAssetReimportPendingState::findPending(
        EditorAssetReimportSourceKey sourceKey) const noexcept {
        const auto found =
            std::ranges::find_if(pending_, [sourceKey](const EditorAssetPendingReimport& item) {
                return pendingReimportMatchesSource(item.request, sourceKey);
            });
        return found == pending_.end() ? nullptr : &*found;
    }

    const std::vector<EditorAssetPendingReimport>&
    EditorAssetReimportPendingState::pending() const noexcept {
        return pending_;
    }

    std::vector<EditorAssetPendingReimportWorkItem>
    EditorAssetReimportPendingState::snapshotPendingWork() const {
        std::vector<EditorAssetPendingReimportWorkItem> workItems;
        workItems.reserve(pending_.size());
        for (const EditorAssetPendingReimport& pending : pending_) {
            workItems.push_back(makePendingWorkItem(pending));
        }
        std::ranges::sort(workItems, pendingWorkItemLess);
        return workItems;
    }

    std::size_t EditorAssetReimportPendingState::size() const noexcept {
        return pending_.size();
    }

    asharia::Result<std::unique_ptr<EditorCommand>>
    makeEditorAssetImportSettingsEditCommand(EditorAssetImportSettingsEditRequest request,
                                             EditorAssetReimportRequestLog& reimportRequests) {
        if (auto validRequest = validateRequest(request); !validRequest) {
            return std::unexpected{std::move(validRequest.error())};
        }
        return std::make_unique<EditorAssetImportSettingsEditCommand>(std::move(request),
                                                                      reimportRequests);
    }

    asharia::Result<std::unique_ptr<EditorCommand>> makeEditorTextureProfileEditCommand(
        const std::filesystem::path& metadataFile, std::string_view targetProfile,
        std::string_view textureProfile, EditorAssetReimportRequestLog& reimportRequests) {
        return makeEditorAssetImportSettingsEditCommand(
            EditorAssetImportSettingsEditRequest{
                .metadataFile = metadataFile,
                .targetProfile = std::string{targetProfile},
                .edits =
                    std::vector<EditorAssetImportSettingEdit>{
                        EditorAssetImportSettingEdit{
                            .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                            .value = std::string{textureProfile},
                        },
                    },
            },
            reimportRequests);
    }

    asharia::Result<std::string>
    readEditorTextureProfileSetting(const std::filesystem::path& metadataFile) {
        if (metadataFile.empty()) {
            return std::unexpected{importSettingsCommandError(
                "Editor import settings command requires a metadata file.")};
        }

        auto document = asharia::asset::readAssetMetadataFile(metadataFile);
        if (!document) {
            return std::unexpected{std::move(document.error())};
        }

        const asharia::asset::AssetImportSetting* profileSetting =
            findSetting(document->settings, asharia::asset::kTextureImportProfileSettingKey);
        if (profileSetting == nullptr || profileSetting->value.empty()) {
            return std::unexpected{importSettingsCommandError(
                "Editor import settings command could not find texture.profile in metadata file '" +
                metadataFile.string() + "'.")};
        }

        std::string normalized =
            asharia::asset::normalizeTextureImportProfileName(profileSetting->value);
        if (normalized.empty() || !isSupportedTextureProfile(normalized)) {
            return std::unexpected{importSettingsCommandError(
                "Editor import settings command does not support texture profile '" +
                profileSetting->value + "'.")};
        }
        return normalized;
    }

} // namespace asharia::editor
