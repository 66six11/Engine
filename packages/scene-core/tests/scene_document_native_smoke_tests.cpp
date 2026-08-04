#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "asharia/scene/scene_document_native_api.h"

namespace {

    constexpr std::string_view kSceneId = "11111111-2222-3333-4444-555555555555";
    constexpr std::string_view kObjectId = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    constexpr std::string_view kName = "Native \xE4\xB8\xBB\xE8\xA7\x92";

    class TestDirectory {
    public:
        TestDirectory() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path() /
                    ("asharia-scene-document-native-" + std::to_string(stamp));
            std::filesystem::create_directories(path_);
        }

        ~TestDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;
        TestDirectory(TestDirectory&&) = delete;
        TestDirectory& operator=(TestDirectory&&) = delete;

        [[nodiscard]] std::string utf8Path() const {
            const std::u8string value = path_.u8string();
            return std::string{value.begin(), value.end()};
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] AshariaSceneNativeAbiHeader header(std::size_t size) {
        return {.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION,
                .structSize = static_cast<std::uint32_t>(size)};
    }

    [[nodiscard]] AshariaSceneNativeStringView view(std::string_view value) {
        return {.data = value.data(), .byteLength = static_cast<std::uint64_t>(value.size())};
    }

    [[nodiscard]] bool expect(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
        }
        return condition;
    }

    [[nodiscard]] std::string spanText(const std::vector<std::byte>& buffer,
                                       AshariaSceneNativeTextSpan span) {
        if (span.offset > buffer.size() || span.byteLength > buffer.size() - span.offset) {
            return {};
        }
        const auto offset = static_cast<std::size_t>(span.offset);
        const auto length = static_cast<std::size_t>(span.byteLength);
        const std::span<const std::byte> bytes{buffer};
        std::string text(length, '\0');
        std::memcpy(text.data(), bytes.subspan(offset, length).data(), length);
        return text;
    }

    [[nodiscard]] bool snapshotDocument(AshariaSceneNativeDocumentHandle document,
                                        std::uint64_t expectedRevision,
                                        std::uint64_t expectedSavedRevision,
                                        std::string_view expectedName) {
        const AshariaSceneNativeDocumentRequest request{
            .header = header(sizeof(AshariaSceneNativeDocumentRequest)),
            .document = document,
        };
        AshariaSceneNativeDocumentSnapshotResult query{};
        if (!expect(asharia_scene_document_snapshot(&request, nullptr, 0U, &query) ==
                            AshariaSceneNativeStatus_BufferTooSmall &&
                        query.operationStatus == AshariaSceneNativeStatus_Success &&
                        query.requiredBufferSize != 0U,
                    "Document snapshot size query did not report the required response.")) {
            return false;
        }
        std::vector<std::byte> buffer(static_cast<std::size_t>(query.requiredBufferSize));
        AshariaSceneNativeDocumentSnapshotResult result{};
        if (!expect(asharia_scene_document_snapshot(&request, buffer.data(), buffer.size(),
                                                    &result) == AshariaSceneNativeStatus_Success &&
                        result.revision == expectedRevision &&
                        result.savedRevision == expectedSavedRevision &&
                        spanText(buffer, result.sceneIdUtf8) == kSceneId,
                    "Document snapshot metadata was not authoritative.")) {
            return false;
        }
        if (expectedName.empty()) {
            return expect(result.entityCount == 0U, "Empty scene snapshot contained an entity.");
        }
        if (!expect(result.entityCount == 1U &&
                        result.requiredBufferSize >=
                            sizeof(AshariaSceneNativeDocumentEntitySnapshot),
                    "Edited scene snapshot did not contain one entity.")) {
            return false;
        }
        AshariaSceneNativeDocumentEntitySnapshot entity{};
        const std::span<const std::byte> bytes{buffer};
        std::memcpy(
            &entity,
            bytes.subspan(static_cast<std::size_t>(result.entitiesOffset), sizeof(entity)).data(),
            sizeof(entity));
        return expect(spanText(buffer, entity.objectIdUtf8) == kObjectId &&
                          spanText(buffer, entity.nameUtf8) == expectedName &&
                          entity.transform.position.x == 4.0F &&
                          entity.transform.rotation.w == 1.0F && entity.transform.scale.z == 3.0F,
                      "Edited entity snapshot did not preserve ID, name, and Transform.");
    }

} // namespace

int main() noexcept {
    try {
        TestDirectory projectRoot;
        const std::string projectPath = projectRoot.utf8Path();
        const AshariaSceneNativeDocumentOpenDefaultRequest openRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentOpenDefaultRequest)),
            .projectRootUtf8 = view(projectPath),
            .newSceneIdUtf8 = view(kSceneId),
        };
        AshariaSceneNativeDocumentHandle document{};
        AshariaSceneNativeDocumentOperationResult opened{};
        if (!expect(
                asharia_scene_document_open_default(&openRequest, &document, nullptr, 0U,
                                                    &opened) == AshariaSceneNativeStatus_Success &&
                    document.index != 0U && document.generation != 0U && opened.revision == 1U &&
                    opened.savedRevision == 1U && snapshotDocument(document, 1U, 1U, {}),
                "Default native scene document did not open cleanly.")) {
            return 1;
        }

        std::vector<std::byte> errorBuffer(4096U);
        const AshariaSceneNativeDocumentCreateEntityRequest createRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentCreateEntityRequest)),
            .document = document,
            .expectedRevision = 1U,
            .objectIdUtf8 = view(kObjectId),
            .nameUtf8 = view("Entity"),
        };
        AshariaSceneNativeDocumentOperationResult created{};
        if (!expect(asharia_scene_document_create_entity(&createRequest, errorBuffer.data(),
                                                         errorBuffer.size(), &created) ==
                            AshariaSceneNativeStatus_Success &&
                        created.revision == 2U && created.savedRevision == 1U,
                    "Native scene entity creation did not advance the revision.")) {
            return 1;
        }

        const AshariaSceneNativeDocumentSetEntityNameRequest staleNameRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentSetEntityNameRequest)),
            .document = document,
            .expectedRevision = 1U,
            .objectIdUtf8 = view(kObjectId),
            .nameUtf8 = view(kName),
        };
        AshariaSceneNativeDocumentOperationResult stale{};
        if (!expect(asharia_scene_document_set_entity_name(&staleNameRequest, errorBuffer.data(),
                                                           errorBuffer.size(), &stale) ==
                            AshariaSceneNativeStatus_RevisionConflict &&
                        stale.operationStatus == AshariaSceneNativeStatus_RevisionConflict &&
                        stale.revision == 2U && stale.messageUtf8.byteLength != 0U,
                    "Stale native scene mutation was not rejected with current revision data.")) {
            return 1;
        }

        auto nameRequest = staleNameRequest;
        nameRequest.expectedRevision = 2U;
        AshariaSceneNativeDocumentOperationResult renamed{};
        if (!expect(asharia_scene_document_set_entity_name(&nameRequest, errorBuffer.data(),
                                                           errorBuffer.size(), &renamed) ==
                            AshariaSceneNativeStatus_Success &&
                        renamed.revision == 3U,
                    "Native scene rename did not accept valid UTF-8.")) {
            return 1;
        }

        const AshariaSceneNativeDocumentSetEntityTransformRequest transformRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentSetEntityTransformRequest)),
            .document = document,
            .expectedRevision = 3U,
            .objectIdUtf8 = view(kObjectId),
            .transform = {.position = {4.0F, 5.0F, 6.0F},
                          .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
                          .scale = {1.0F, 2.0F, 3.0F}},
        };
        AshariaSceneNativeDocumentOperationResult transformed{};
        if (!expect(asharia_scene_document_set_entity_transform(
                        &transformRequest, errorBuffer.data(), errorBuffer.size(), &transformed) ==
                            AshariaSceneNativeStatus_Success &&
                        transformed.revision == 4U && snapshotDocument(document, 4U, 1U, kName),
                    "Native scene Transform edit was not reflected by snapshot.")) {
            return 1;
        }

        AshariaSceneNativeStatus wrongThreadStatus = AshariaSceneNativeStatus_InternalError;
        std::thread wrongThread{[&] {
            const AshariaSceneNativeDocumentRequest request{
                .header = header(sizeof(AshariaSceneNativeDocumentRequest)),
                .document = document,
            };
            AshariaSceneNativeDocumentSnapshotResult result{};
            wrongThreadStatus = asharia_scene_document_snapshot(&request, errorBuffer.data(),
                                                                errorBuffer.size(), &result);
        }};
        wrongThread.join();
        if (!expect(wrongThreadStatus == AshariaSceneNativeStatus_WrongThread,
                    "Native document accepted a call from a non-owner thread.")) {
            return 1;
        }

        const AshariaSceneNativeDocumentSaveRequest saveRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentSaveRequest)),
            .document = document,
            .expectedRevision = 4U,
        };
        AshariaSceneNativeDocumentOperationResult saved{};
        if (!expect(asharia_scene_document_save(&saveRequest, errorBuffer.data(),
                                                errorBuffer.size(),
                                                &saved) == AshariaSceneNativeStatus_Success &&
                        saved.savedRevision == 4U,
                    "Native document save did not advance the savepoint.")) {
            return 1;
        }

        AshariaSceneNativeDocumentHandle staleHandle = document;
        if (!expect(asharia_scene_document_close(&document) == AshariaSceneNativeStatus_Success &&
                        document.index == 0U && document.generation == 0U,
                    "Native document close did not clear its handle.")) {
            return 1;
        }
        const AshariaSceneNativeDocumentRequest staleRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentRequest)),
            .document = staleHandle,
        };
        AshariaSceneNativeDocumentSnapshotResult staleSnapshot{};
        if (!expect(asharia_scene_document_snapshot(&staleRequest, errorBuffer.data(),
                                                    errorBuffer.size(), &staleSnapshot) ==
                        AshariaSceneNativeStatus_StaleHandle,
                    "Closed native document handle was not rejected as stale.")) {
            return 1;
        }

        auto reopenRequest = openRequest;
        reopenRequest.newSceneIdUtf8 = {};
        AshariaSceneNativeDocumentOperationResult reopened{};
        if (!expect(asharia_scene_document_open_default(&reopenRequest, &document, nullptr, 0U,
                                                        &reopened) ==
                            AshariaSceneNativeStatus_Success &&
                        document.generation != staleHandle.generation &&
                        snapshotDocument(document, 1U, 1U, kName) &&
                        asharia_scene_document_close(&document) == AshariaSceneNativeStatus_Success,
                    "Saved native scene did not survive close and reopen.")) {
            return 1;
        }

        std::cout << "Scene document native smoke tests passed.\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
