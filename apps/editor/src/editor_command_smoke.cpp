#include "editor_command_smoke.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/core/error.hpp"
#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"

#include "editor_asset_import_settings_command.hpp"
#include "editor_command.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {

    namespace {

        class TestSetIntCommand final : public EditorCommand {
        public:
            TestSetIntCommand(int& target, int newValue)
                : target_(&target), newValue_(newValue), oldValue_(target) {}

            [[nodiscard]] std::string description() const override {
                return "SetInt " + std::to_string(oldValue_) + " -> " + std::to_string(newValue_);
            }

            [[nodiscard]] asharia::Result<void> execute() override {
                *target_ = newValue_;
                return {};
            }

            [[nodiscard]] asharia::Result<void> undo() override {
                *target_ = oldValue_;
                return {};
            }

        private:
            int* target_{};
            int newValue_{};
            int oldValue_{};
        };

        [[nodiscard]] asharia::Error commandSmokeError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        class TestControlledIntCommand final : public EditorCommand {
        public:
            struct Options {
                bool failExecute{false};
                bool failSecondExecute{false};
                bool failUndo{false};
            };

            TestControlledIntCommand(int& target, int newValue, Options options)
                : target_(&target), newValue_(newValue), oldValue_(target), options_(options) {}

            [[nodiscard]] std::string description() const override {
                return "ControlledInt " + std::to_string(oldValue_) + " -> " +
                       std::to_string(newValue_);
            }

            [[nodiscard]] asharia::Result<void> execute() override {
                ++executeCount_;
                if (options_.failExecute ||
                    (options_.failSecondExecute && executeCount_ > 1)) {
                    return std::unexpected{commandSmokeError("ControlledInt execute failed.")};
                }
                *target_ = newValue_;
                return {};
            }

            [[nodiscard]] asharia::Result<void> undo() override {
                if (options_.failUndo) {
                    return std::unexpected{commandSmokeError("ControlledInt undo failed.")};
                }
                *target_ = oldValue_;
                return {};
            }

        private:
            int* target_{};
            int newValue_{};
            int oldValue_{};
            Options options_{};
            int executeCount_{0};
        };

        [[nodiscard]] std::filesystem::path commandSmokeRoot() {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            return std::filesystem::temp_directory_path() /
                   ("AshariaEditorCommandSmoke-" + std::to_string(suffix));
        }

        [[nodiscard]] asharia::asset::AssetMetadataDocument
        textureMetadataDocument(std::string_view profile) {
            constexpr std::string_view kTextureGuidText = "91f37f8d-11fd-4b68-89cb-55332ce8620a";
            constexpr std::string_view kTextureTypeName = "com.asharia.asset.Texture2D";
            constexpr std::string_view kTextureImporterName =
                "com.asharia.importer.texture-placeholder";

            auto guid = asharia::asset::parseAssetGuid(kTextureGuidText);
            std::vector<asharia::asset::AssetImportSetting> settings{
                asharia::asset::AssetImportSetting{.key = "compression", .value = "auto"},
                asharia::asset::AssetImportSetting{
                    .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                    .value = std::string{profile},
                },
            };

            return asharia::asset::AssetMetadataDocument{
                .source =
                    asharia::asset::SourceAssetRecord{
                        .guid = guid ? *guid : asharia::asset::AssetGuid{},
                        .assetType = asharia::asset::makeAssetTypeId(kTextureTypeName),
                        .assetTypeName = std::string{kTextureTypeName},
                        .sourcePath = "Assets/Textures/profile.png",
                        .importerId = asharia::asset::makeImporterId(kTextureImporterName),
                        .importerName = std::string{kTextureImporterName},
                        .importerVersion = asharia::asset::ImporterVersion{1},
                        .sourceHash = 0xA11CE00000000001ULL,
                        .settingsHash = asharia::asset::hashAssetImportSettings(settings),
                    },
                .settings = std::move(settings),
            };
        }

        [[nodiscard]] bool writeTextureMetadata(const std::filesystem::path& metadataFile,
                                                std::string_view profile) {
            auto written = asharia::asset::writeAssetMetadataFile(metadataFile,
                                                                  textureMetadataDocument(profile));
            if (!written) {
                asharia::logError(written.error().message);
                return false;
            }
            return true;
        }

        [[nodiscard]] const asharia::asset::AssetImportSetting*
        findSetting(const asharia::asset::AssetMetadataDocument& document, std::string_view key) {
            const auto found = std::ranges::find_if(
                document.settings, [key](const asharia::asset::AssetImportSetting& setting) {
                    return setting.key == key;
                });
            return found == document.settings.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool expectTextureProfile(const std::filesystem::path& metadataFile,
                                                std::string_view expectedProfile,
                                                std::uint64_t expectedSettingsHash = 0U) {
            auto document = asharia::asset::readAssetMetadataFile(metadataFile);
            if (!document) {
                asharia::logError(document.error().message);
                return false;
            }

            const asharia::asset::AssetImportSetting* profileSetting =
                findSetting(*document, asharia::asset::kTextureImportProfileSettingKey);
            if (profileSetting == nullptr || profileSetting->value != expectedProfile) {
                asharia::logError("Editor command smoke: texture.profile did not match.");
                return false;
            }

            if (expectedSettingsHash != 0U &&
                document->source.settingsHash != expectedSettingsHash) {
                asharia::logError("Editor command smoke: settings hash did not match.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectReadableTextureProfile(const std::filesystem::path& metadataFile,
                                                        std::string_view expectedProfile) {
            auto profile = readEditorTextureProfileSetting(metadataFile);
            if (!profile || *profile != expectedProfile) {
                if (!profile) {
                    asharia::logError(profile.error().message);
                } else {
                    asharia::logError(
                        "Editor command smoke: readable texture.profile did not match.");
                }
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectReimportRequest(const EditorAssetReimportRequest& request,
                                                 std::string_view expectedSourcePath,
                                                 std::string_view expectedTargetProfile,
                                                 const std::filesystem::path& metadataFile) {
            if (!request.guid || request.sourcePath != expectedSourcePath ||
                request.targetProfile != expectedTargetProfile ||
                request.metadataFile != metadataFile || request.changedSettingKeys.size() != 1U ||
                request.changedSettingKeys.front() !=
                    asharia::asset::kTextureImportProfileSettingKey) {
                asharia::logError("Editor command smoke: invalid reimport request fact.");
                return false;
            }
            return true;
        }

        struct ImportSettingsSmokeContext {
            std::filesystem::path root;
            std::filesystem::path metadataFile;
            asharia::asset::AssetMetadataDocument originalDocument;
            asharia::asset::AssetMetadataDocument changedDocument;
            std::string originalText;
            EditorAssetReimportRequestLog reimportRequests;
        };

        [[nodiscard]] bool prepareImportSettingsSmokeContext(ImportSettingsSmokeContext& context) {
            context.root = commandSmokeRoot();
            context.metadataFile = context.root / "profile.png.ameta";
            std::error_code error;
            std::filesystem::remove_all(context.root, error);
            std::filesystem::create_directories(context.root, error);
            if (error || !writeTextureMetadata(context.metadataFile,
                                               asharia::asset::kTextureImportProfileTexture2D)) {
                asharia::logError(
                    "Editor command smoke: could not prepare import-settings fixture.");
                return false;
            }

            auto document = asharia::asset::readAssetMetadataFile(context.metadataFile);
            if (!document) {
                asharia::logError(document.error().message);
                return false;
            }
            auto text = asharia::asset::writeAssetMetadataText(*document);
            if (!text) {
                asharia::logError(text.error().message);
                return false;
            }
            context.originalDocument = std::move(*document);
            context.originalText = std::move(*text);
            return true;
        }

        [[nodiscard]] bool executeImportSettingsSmokeEdit(ImportSettingsSmokeContext& context,
                                                          EditorCommandHistory& history) {
            auto command = makeEditorTextureProfileEditCommand(
                context.metadataFile, "editor-debug", "SpriteSheet", context.reimportRequests);
            if (!command) {
                asharia::logError(command.error().message);
                return false;
            }

            EditorTransaction transaction;
            transaction.addCommand(std::move(*command));
            if (auto executed = transaction.executeAll(); !executed) {
                asharia::logError(executed.error().message);
                return false;
            }
            if (!expectTextureProfile(context.metadataFile,
                                      asharia::asset::kTextureImportProfileSpriteSheet) ||
                !expectReadableTextureProfile(context.metadataFile,
                                              asharia::asset::kTextureImportProfileSpriteSheet) ||
                context.reimportRequests.size() != 1U ||
                !expectReimportRequest(context.reimportRequests.requests().back(),
                                       "Assets/Textures/profile.png", "editor-debug",
                                       context.metadataFile)) {
                return false;
            }

            auto changedDocument = asharia::asset::readAssetMetadataFile(context.metadataFile);
            if (!changedDocument || changedDocument->source.settingsHash ==
                                        context.originalDocument.source.settingsHash) {
                asharia::logError("Editor command smoke: settings hash did not change.");
                return false;
            }
            context.changedDocument = std::move(*changedDocument);
            history.push(std::move(transaction));
            return true;
        }

        [[nodiscard]] bool undoImportSettingsSmokeEdit(ImportSettingsSmokeContext& context,
                                                       EditorCommandHistory& history) {
            if (auto undone = history.undo(); !undone) {
                asharia::logError(undone.error().message);
                return false;
            }
            auto restoredDocument = asharia::asset::readAssetMetadataFile(context.metadataFile);
            if (!restoredDocument) {
                asharia::logError(restoredDocument.error().message);
                return false;
            }
            auto undoText = asharia::asset::writeAssetMetadataText(*restoredDocument);
            if (!undoText || *undoText != context.originalText ||
                !expectTextureProfile(context.metadataFile,
                                      asharia::asset::kTextureImportProfileTexture2D,
                                      context.originalDocument.source.settingsHash) ||
                !expectReadableTextureProfile(context.metadataFile,
                                              asharia::asset::kTextureImportProfileTexture2D) ||
                context.reimportRequests.size() != 2U ||
                !expectReimportRequest(context.reimportRequests.requests().back(),
                                       "Assets/Textures/profile.png", "editor-debug",
                                       context.metadataFile)) {
                asharia::logError("Editor command smoke: undo did not restore metadata.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool redoImportSettingsSmokeEdit(ImportSettingsSmokeContext& context,
                                                       EditorCommandHistory& history) {
            if (auto redone = history.redo(); !redone) {
                asharia::logError(redone.error().message);
                return false;
            }
            if (!expectTextureProfile(context.metadataFile,
                                      asharia::asset::kTextureImportProfileSpriteSheet,
                                      context.changedDocument.source.settingsHash) ||
                !expectReadableTextureProfile(context.metadataFile,
                                              asharia::asset::kTextureImportProfileSpriteSheet) ||
                context.reimportRequests.size() != 3U ||
                !expectReimportRequest(context.reimportRequests.requests().back(),
                                       "Assets/Textures/profile.png", "editor-debug",
                                       context.metadataFile)) {
                asharia::logError("Editor command smoke: redo did not reapply metadata.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectNoopImportSettingsEdit(ImportSettingsSmokeContext& context) {
            const std::size_t requestCountBeforeNoop = context.reimportRequests.size();
            auto noopCommand = makeEditorTextureProfileEditCommand(
                context.metadataFile, "editor-debug",
                asharia::asset::kTextureImportProfileSpriteSheet, context.reimportRequests);
            if (!noopCommand || !(*noopCommand)->execute() ||
                context.reimportRequests.size() != requestCountBeforeNoop) {
                asharia::logError("Editor command smoke: no-op profile edit was not quiet.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectInvalidImportSettingsEdits(ImportSettingsSmokeContext& context) {
            const std::size_t requestCountBeforeInvalid = context.reimportRequests.size();
            auto beforeInvalidDocument =
                asharia::asset::readAssetMetadataFile(context.metadataFile);
            if (!beforeInvalidDocument) {
                asharia::logError(beforeInvalidDocument.error().message);
                return false;
            }
            auto beforeInvalid = asharia::asset::writeAssetMetadataText(*beforeInvalidDocument);
            auto invalidKeyCommand = makeEditorAssetImportSettingsEditCommand(
                EditorAssetImportSettingsEditRequest{
                    .metadataFile = context.metadataFile,
                    .targetProfile = "editor-debug",
                    .edits =
                        std::vector<EditorAssetImportSettingEdit>{
                            EditorAssetImportSettingEdit{.key = "texture.decode",
                                                         .value = "linear"},
                        },
                },
                context.reimportRequests);
            if (!invalidKeyCommand || (*invalidKeyCommand)->execute()) {
                asharia::logError(
                    "Editor command smoke: unsupported setting key should have failed.");
                return false;
            }
            auto invalidProfileCommand = makeEditorTextureProfileEditCommand(
                context.metadataFile, "editor-debug", "volume-texture", context.reimportRequests);
            if (!invalidProfileCommand || (*invalidProfileCommand)->execute()) {
                asharia::logError(
                    "Editor command smoke: unsupported texture profile should have failed.");
                return false;
            }
            auto afterInvalidDocument = asharia::asset::readAssetMetadataFile(context.metadataFile);
            if (!afterInvalidDocument) {
                asharia::logError(afterInvalidDocument.error().message);
                return false;
            }
            auto afterInvalid = asharia::asset::writeAssetMetadataText(*afterInvalidDocument);
            if (!beforeInvalid || !afterInvalid || *beforeInvalid != *afterInvalid ||
                context.reimportRequests.size() != requestCountBeforeInvalid) {
                asharia::logError(
                    "Editor command smoke: invalid edit corrupted metadata or recorded import.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectPendingReimportState(ImportSettingsSmokeContext& context) {
            EditorAssetReimportPendingState pending;
            pending.recordAll(context.reimportRequests.requests());
            if (pending.size() != 1U ||
                !pending.hasPending(context.originalDocument.source.guid, "editor-debug") ||
                pending.findPending(EditorAssetReimportSourceKey{
                    .guid = context.originalDocument.source.guid,
                    .sourcePath = "Assets/Textures/profile.png",
                    .targetProfile = "editor-debug",
                }) == nullptr) {
                asharia::logError(
                    "Editor command smoke: pending reimport state did not coalesce source.");
                return false;
            }

            const EditorAssetPendingReimport& pendingRequest = pending.pending().front();
            if (pendingRequest.requestCount != 3U ||
                !expectReimportRequest(pendingRequest.request, "Assets/Textures/profile.png",
                                       "editor-debug", context.metadataFile)) {
                asharia::logError(
                    "Editor command smoke: pending reimport state kept invalid request data.");
                return false;
            }

            pending.record(EditorAssetReimportRequest{
                .guid = context.originalDocument.source.guid,
                .sourcePath = "Assets/Textures/profile.png",
                .metadataFile = context.metadataFile,
                .targetProfile = "editor-debug",
                .changedSettingKeys = std::vector<std::string>{"compression"},
            });

            if (pending.size() != 1U || pending.pending().front().requestCount != 4U ||
                pending.pending().front().request.changedSettingKeys.size() != 2U) {
                asharia::logError(
                    "Editor command smoke: pending reimport state did not merge setting keys.");
                return false;
            }

            const std::vector<EditorAssetPendingReimportWorkItem> mergedWorkItems =
                pending.snapshotPendingWork();
            if (mergedWorkItems.size() != 1U ||
                mergedWorkItems.front().guid != context.originalDocument.source.guid ||
                mergedWorkItems.front().sourcePath != "Assets/Textures/profile.png" ||
                mergedWorkItems.front().metadataFile != context.metadataFile ||
                mergedWorkItems.front().targetProfile != "editor-debug" ||
                mergedWorkItems.front().requestCount != 4U ||
                mergedWorkItems.front().changedSettingKeys.size() != 2U ||
                mergedWorkItems.front().changedSettingKeys[0] != "compression" ||
                mergedWorkItems.front().changedSettingKeys[1] !=
                    asharia::asset::kTextureImportProfileSettingKey) {
                asharia::logError(
                    "Editor command smoke: pending reimport work snapshot missed merged data.");
                return false;
            }

            if (!pending.clearForSource(context.originalDocument.source.guid, "editor-debug") ||
                pending.size() != 0U ||
                pending.clearForSource(context.originalDocument.source.guid, "editor-debug")) {
                asharia::logError(
                    "Editor command smoke: pending reimport state clear was not deterministic.");
                return false;
            }

            pending.record(EditorAssetReimportRequest{
                .guid = asharia::asset::AssetGuid{},
                .sourcePath = "Assets/Textures/path-only.png",
                .metadataFile = context.metadataFile,
                .targetProfile = "editor-debug",
                .changedSettingKeys = std::vector<std::string>{std::string{
                    asharia::asset::kTextureImportProfileSettingKey}},
            });
            if (pending.size() != 1U ||
                !pending.hasPending(EditorAssetReimportSourceKey{
                    .guid = asharia::asset::AssetGuid{},
                    .sourcePath = "Assets/Textures/path-only.png",
                    .targetProfile = "editor-debug",
                }) ||
                pending.findPending(EditorAssetReimportSourceKey{
                    .guid = asharia::asset::AssetGuid{},
                    .sourcePath = "Assets/Textures/path-only.png",
                    .targetProfile = "editor-debug",
                }) == nullptr ||
                !pending.clearForSource(EditorAssetReimportSourceKey{
                    .guid = asharia::asset::AssetGuid{},
                    .sourcePath = "Assets/Textures/path-only.png",
                    .targetProfile = "editor-debug",
                }) ||
                pending.size() != 0U) {
                asharia::logError(
                    "Editor command smoke: pending reimport state missed sourcePath clear.");
                return false;
            }

            pending.record(EditorAssetReimportRequest{
                .guid = asharia::asset::AssetGuid{},
                .sourcePath = "Assets/Textures/z-last.png",
                .metadataFile = context.metadataFile,
                .targetProfile = "editor-debug",
                .changedSettingKeys = std::vector<std::string>{std::string{
                    asharia::asset::kTextureImportProfileSettingKey}},
            });
            pending.record(EditorAssetReimportRequest{
                .guid = context.originalDocument.source.guid,
                .sourcePath = "Assets/Textures/a-first.png",
                .metadataFile = context.metadataFile,
                .targetProfile = "editor-debug",
                .changedSettingKeys = std::vector<std::string>{"compression"},
            });
            const std::vector<EditorAssetPendingReimportWorkItem> sortedWorkItems =
                pending.snapshotPendingWork();
            if (sortedWorkItems.size() != 2U ||
                sortedWorkItems[0].sourcePath != "Assets/Textures/a-first.png" ||
                sortedWorkItems[0].changedSettingKeys.front() != "compression" ||
                sortedWorkItems[1].sourcePath != "Assets/Textures/z-last.png" ||
                sortedWorkItems[1].changedSettingKeys.front() !=
                    asharia::asset::kTextureImportProfileSettingKey) {
                asharia::logError(
                    "Editor command smoke: pending reimport work snapshot was not deterministic.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectExecuteFailureRollback() {
            int firstValue = 0;
            int secondValue = 0;
            auto transaction = EditorTransaction{};
            transaction.addCommand(std::make_unique<TestSetIntCommand>(firstValue, 1));
            transaction.addCommand(std::make_unique<TestControlledIntCommand>(
                secondValue, 2, TestControlledIntCommand::Options{.failExecute = true}));

            auto executed = transaction.executeAll();
            if (executed || firstValue != 0 || secondValue != 0) {
                asharia::logError(
                    "Editor command smoke: execute failure did not roll back transaction.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectUndoFailureRestoresTransactionAndHistory() {
            int firstValue = 0;
            int secondValue = 0;
            EditorCommandHistory history;
            auto transaction = EditorTransaction{};
            transaction.addCommand(std::make_unique<TestControlledIntCommand>(
                firstValue, 1, TestControlledIntCommand::Options{.failUndo = true}));
            transaction.addCommand(std::make_unique<TestSetIntCommand>(secondValue, 2));

            auto executed = transaction.executeAll();
            if (!executed || firstValue != 1 || secondValue != 2) {
                asharia::logError(
                    "Editor command smoke: undo failure fixture did not execute.");
                return false;
            }
            history.push(std::move(transaction));

            auto undone = history.undo();
            if (undone || firstValue != 1 || secondValue != 2 || history.undoDepth() != 1 ||
                history.redoDepth() != 0) {
                asharia::logError(
                    "Editor command smoke: undo failure dropped history or visible state.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool expectRedoFailurePreservesRedoTransaction() {
            int value = 0;
            EditorCommandHistory history;
            auto transaction = EditorTransaction{};
            transaction.addCommand(std::make_unique<TestControlledIntCommand>(
                value, 9, TestControlledIntCommand::Options{.failSecondExecute = true}));

            auto executed = transaction.executeAll();
            if (!executed || value != 9) {
                asharia::logError(
                    "Editor command smoke: redo failure fixture did not execute.");
                return false;
            }
            history.push(std::move(transaction));

            auto undone = history.undo();
            if (!undone || value != 0 || history.undoDepth() != 0 || history.redoDepth() != 1) {
                asharia::logError(
                    "Editor command smoke: redo failure fixture did not undo cleanly.");
                return false;
            }

            auto redone = history.redo();
            if (redone || value != 0 || history.undoDepth() != 0 || history.redoDepth() != 1) {
                asharia::logError(
                    "Editor command smoke: redo failure dropped redo history or mutated state.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateCommandFailureSmoke() {
            return expectExecuteFailureRollback() &&
                   expectUndoFailureRestoresTransactionAndHistory() &&
                   expectRedoFailurePreservesRedoTransaction();
        }

        [[nodiscard]] bool validateImportSettingsCommandSmoke() {
            ImportSettingsSmokeContext context;
            EditorCommandHistory history;
            const bool passed = prepareImportSettingsSmokeContext(context) &&
                                executeImportSettingsSmokeEdit(context, history) &&
                                undoImportSettingsSmokeEdit(context, history) &&
                                redoImportSettingsSmokeEdit(context, history) &&
                                expectNoopImportSettingsEdit(context) &&
                                expectInvalidImportSettingsEdits(context) &&
                                expectPendingReimportState(context);
            std::error_code error;
            std::filesystem::remove_all(context.root, error);
            return passed;
        }

    } // namespace

    bool validateEditorCommandSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        int testValue = 0;
        constexpr int kNewValue = 42;
        EditorCommandHistory history;
        {
            auto transaction = EditorTransaction{};
            transaction.addCommand(std::make_unique<TestSetIntCommand>(testValue, kNewValue));
            history.push(std::move(transaction));
        }
        if (history.undoDepth() != 1 || history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: invalid depth after push.");
            return false;
        }
        auto undoResult = history.undo();
        if (!undoResult || testValue != 0 || history.undoDepth() != 0 || history.redoDepth() != 1) {
            asharia::logError("Editor command smoke: undo did not restore value.");
            return false;
        }
        auto redoResult = history.redo();
        if (!redoResult || testValue != kNewValue || history.undoDepth() != 1 ||
            history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: redo did not reapply value.");
            return false;
        }
        auto emptyUndo = history.undo();
        auto doubleUndo = history.undo();
        if (doubleUndo) {
            asharia::logError("Editor command smoke: double undo should have failed.");
            return false;
        }
        static_cast<void>(emptyUndo);
        return validateCommandFailureSmoke() && validateImportSettingsCommandSmoke();
    }

} // namespace asharia::editor
