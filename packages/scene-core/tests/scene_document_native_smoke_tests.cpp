#include <array>
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
    constexpr std::string_view kMeshObjectId = "bbbbbbbb-cccc-dddd-eeee-ffffffffffff";
    constexpr std::string_view kMeshAssetGuid = "7c9fe8ac-3c8b-4f66-9665-0af0fd7b693e";
    constexpr std::string_view kName = "Native \xE4\xB8\xBB\xE8\xA7\x92";
    constexpr AshariaSceneNativeTransform kIdentityTransform{
        .position = {0.0F, 0.0F, 0.0F},
        .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
        .scale = {1.0F, 1.0F, 1.0F},
    };
    constexpr AshariaSceneNativeTransform kY45Transform{
        .position = {0.0F, 0.0F, 0.0F},
        .rotation = {0.0F, 0.38268343F, 0.0F, 0.92387953F},
        .scale = {1.0F, 1.0F, 1.0F},
    };
    constexpr AshariaSceneNativeTransform kY46Transform{
        .position = {0.0F, 0.0F, 0.0F},
        .rotation = {0.0F, 0.39073113F, 0.0F, 0.92050487F},
        .scale = {1.0F, 1.0F, 1.0F},
    };
    constexpr AshariaSceneNativeTransform kYPointOneTransform{
        .position = {0.0F, 0.0F, 0.0F},
        .rotation = {0.0F, 0.0008726645F, 0.0F, 0.99999964F},
        .scale = {1.0F, 1.0F, 1.0F},
    };
    constexpr AshariaSceneNativeTransform kNegativeYPointOneTransform{
        .position = {0.0F, 0.0F, 0.0F},
        .rotation = {-0.0F, -0.0008726645F, -0.0F, -0.99999964F},
        .scale = {1.0F, 1.0F, 1.0F},
    };

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
        return {.abiVersion = ASHARIA_SCENE_DOCUMENT_NATIVE_ABI_VERSION,
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

    [[nodiscard]] constexpr bool equal(AshariaSceneNativeTransform left,
                                       AshariaSceneNativeTransform right) noexcept {
        return left.position.x == right.position.x && left.position.y == right.position.y &&
               left.position.z == right.position.z && left.rotation.x == right.rotation.x &&
               left.rotation.y == right.rotation.y && left.rotation.z == right.rotation.z &&
               left.rotation.w == right.rotation.w && left.scale.x == right.scale.x &&
               left.scale.y == right.scale.y && left.scale.z == right.scale.z;
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
                                        std::string_view expectedName,
                                        const AshariaSceneNativeTransform& expectedTransform,
                                        bool expectedMesh = false) {
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
        const std::uint64_t expectedEntityCount = expectedMesh ? 2U : 1U;
        if (!expect(result.entityCount == expectedEntityCount &&
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
        if (!expect(
                spanText(buffer, entity.objectIdUtf8) == kObjectId &&
                    spanText(buffer, entity.nameUtf8) == expectedName &&
                    equal(entity.transform, expectedTransform) && entity.runtimeEntity.index != 0U &&
                    entity.runtimeEntity.generation != 0U &&
                    entity.meshAssetGuidUtf8.byteLength == 0U,
                "Edited entity snapshot did not preserve ID, runtime ID, name, and Transform.")) {
            return false;
        }
        if (!expectedMesh) {
            return true;
        }
        AshariaSceneNativeDocumentEntitySnapshot meshEntity{};
        std::memcpy(
            &meshEntity,
            bytes.subspan(sizeof(AshariaSceneNativeDocumentEntitySnapshot), sizeof(meshEntity))
                .data(),
            sizeof(meshEntity));
        return expect(spanText(buffer, meshEntity.objectIdUtf8) == kMeshObjectId &&
                          spanText(buffer, meshEntity.nameUtf8) == "Mesh Entity" &&
                          spanText(buffer, meshEntity.meshAssetGuidUtf8) == kMeshAssetGuid &&
                          meshEntity.runtimeEntity.index != 0U &&
                          meshEntity.runtimeEntity.generation != 0U &&
                          (meshEntity.runtimeEntity.index != entity.runtimeEntity.index ||
                           meshEntity.runtimeEntity.generation != entity.runtimeEntity.generation),
                      "Mesh entity snapshot did not preserve its runtime and asset identities.");
    }

    [[nodiscard]] bool snapshotEmptyDocument(AshariaSceneNativeDocumentHandle document,
                                             std::uint64_t expectedRevision,
                                             std::uint64_t expectedSavedRevision) {
        return snapshotDocument(document, expectedRevision, expectedSavedRevision, {},
                                kIdentityTransform);
    }

    [[nodiscard]] bool rejectSupersededDocumentAbi(
        const AshariaSceneNativeDocumentOpenDefaultRequest& openRequest) {
        auto previous = openRequest;
        previous.header.abiVersion = ASHARIA_SCENE_DOCUMENT_NATIVE_ABI_VERSION - 1U;
        AshariaSceneNativeDocumentHandle document{};
        AshariaSceneNativeDocumentOperationResult result{};
        return expect(asharia_scene_document_open_default(&previous, &document, nullptr, 0U,
                                                          &result) ==
                              AshariaSceneNativeStatus_UnsupportedAbi &&
                          document.index == 0U && document.generation == 0U,
                      "Document ABI v3 accepted the superseded Document ABI v2 request.");
    }

    [[nodiscard]] bool testTransformReceipts(
        AshariaSceneNativeDocumentHandle document, std::vector<std::byte>& errorBuffer) {
        const AshariaSceneNativeDocumentSetEntityTransformRequest request{
            .header = header(sizeof(AshariaSceneNativeDocumentSetEntityTransformRequest)),
            .document = document,
            .expectedRevision = 3U,
            .objectIdUtf8 = view(kObjectId),
            .transform = kY45Transform,
        };
        AshariaSceneNativeDocumentTransformOperationResult transformed{};
        constexpr std::array<std::uint8_t, 16> kObjectIdBytes{
            0xaaU, 0xaaU, 0xaaU, 0xaaU, 0xbbU, 0xbbU, 0xccU, 0xccU,
            0xddU, 0xddU, 0xeeU, 0xeeU, 0xeeU, 0xeeU, 0xeeU, 0xeeU};
        if (!expect(asharia_scene_document_set_entity_transform(
                        &request, errorBuffer.data(), errorBuffer.size(), &transformed) ==
                            AshariaSceneNativeStatus_Success &&
                        transformed.revision == 4U && transformed.changed == 1U &&
                        transformed.beforeRevision == 3U && transformed.afterRevision == 4U &&
                        std::memcmp(transformed.objectId.bytes, kObjectIdBytes.data(),
                                    kObjectIdBytes.size()) == 0 &&
                        equal(transformed.beforeTransform, kIdentityTransform) &&
                        equal(transformed.afterTransform, request.transform) &&
                        snapshotDocument(document, 4U, 1U, kName,
                                         transformed.afterTransform),
                    "Native identity-to-Y45 Transform edit did not return its authoritative "
                    "receipt and snapshot.")) {
            return false;
        }

        auto y46Request = request;
        y46Request.expectedRevision = 4U;
        y46Request.transform = kY46Transform;
        AshariaSceneNativeDocumentTransformOperationResult y46{};
        if (!expect(asharia_scene_document_set_entity_transform(
                        &y46Request, errorBuffer.data(), errorBuffer.size(), &y46) ==
                            AshariaSceneNativeStatus_Success &&
                        y46.revision == 5U && y46.changed == 1U &&
                        y46.beforeRevision == 4U && y46.afterRevision == 5U &&
                        equal(y46.beforeTransform, request.transform) &&
                        equal(y46.afterTransform, y46Request.transform) &&
                        snapshotDocument(document, 5U, 1U, kName, y46.afterTransform),
                    "Native Y45-to-Y46 rotation edit did not return its authoritative receipt "
                    "and snapshot.")) {
            return false;
        }

        auto pointOneRequest = y46Request;
        pointOneRequest.expectedRevision = 5U;
        pointOneRequest.transform = kYPointOneTransform;
        AshariaSceneNativeDocumentTransformOperationResult pointOne{};
        if (!expect(asharia_scene_document_set_entity_transform(
                        &pointOneRequest, errorBuffer.data(), errorBuffer.size(), &pointOne) ==
                            AshariaSceneNativeStatus_Success &&
                        pointOne.revision == 6U && pointOne.changed == 1U &&
                        pointOne.beforeRevision == 5U && pointOne.afterRevision == 6U &&
                        equal(pointOne.beforeTransform, y46Request.transform) &&
                        equal(pointOne.afterTransform, pointOneRequest.transform) &&
                        snapshotDocument(document, 6U, 1U, kName, pointOne.afterTransform),
                    "Native Y46-to-Y0.1 rotation edit did not return its authoritative receipt "
                    "and snapshot.")) {
            return false;
        }

        auto negativeRequest = pointOneRequest;
        negativeRequest.expectedRevision = 6U;
        negativeRequest.transform = kNegativeYPointOneTransform;
        AshariaSceneNativeDocumentTransformOperationResult negative{};
        if (!expect(asharia_scene_document_set_entity_transform(
                        &negativeRequest, errorBuffer.data(), errorBuffer.size(), &negative) ==
                            AshariaSceneNativeStatus_Success &&
                        negative.revision == 7U && negative.changed == 1U &&
                        negative.beforeRevision == 6U && negative.afterRevision == 7U &&
                        equal(negative.beforeTransform, pointOneRequest.transform) &&
                        equal(negative.afterTransform, negativeRequest.transform) &&
                        snapshotDocument(document, 7U, 1U, kName, negative.afterTransform),
                    "Native q-to-negative-q authored edit did not preserve exact Transform "
                    "semantics.")) {
            return false;
        }

        auto staleRequest = request;
        staleRequest.expectedRevision = 3U;
        AshariaSceneNativeDocumentTransformOperationResult stale{};
        if (!expect(asharia_scene_document_set_entity_transform(
                        &staleRequest, errorBuffer.data(), errorBuffer.size(), &stale) ==
                            AshariaSceneNativeStatus_RevisionConflict &&
                        stale.revision == 7U && stale.savedRevision == 1U && stale.changed == 0U &&
                        stale.beforeRevision == 0U && stale.afterRevision == 0U &&
                        stale.messageUtf8.byteLength != 0U,
                    "Failed native Transform edit exposed typed receipt state.")) {
            return false;
        }

        auto unchangedRequest = negativeRequest;
        unchangedRequest.expectedRevision = 7U;
        AshariaSceneNativeDocumentTransformOperationResult unchanged{};
        return expect(asharia_scene_document_set_entity_transform(
                          &unchangedRequest, errorBuffer.data(), errorBuffer.size(), &unchanged) ==
                          AshariaSceneNativeStatus_Success &&
                          unchanged.changed == 0U && unchanged.revision == 7U &&
                          unchanged.beforeRevision == 7U && unchanged.afterRevision == 7U &&
                          equal(unchanged.beforeTransform, negativeRequest.transform) &&
                          equal(unchanged.afterTransform, negativeRequest.transform) &&
                          snapshotDocument(document, 7U, 1U, kName,
                                           unchanged.afterTransform),
                      "Native no-op Transform receipt did not preserve its revision and values.");
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
        auto legacyAbiRequest = openRequest;
        legacyAbiRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION;
        AshariaSceneNativeDocumentHandle legacyAbiDocument{};
        AshariaSceneNativeDocumentOperationResult legacyAbiResult{};
        if (!expect(asharia_scene_document_open_default(&legacyAbiRequest, &legacyAbiDocument,
                                                        nullptr, 0U, &legacyAbiResult) ==
                            AshariaSceneNativeStatus_UnsupportedAbi &&
                        legacyAbiDocument.index == 0U && legacyAbiDocument.generation == 0U,
                    "Document ABI v3 accepted a World ABI v1 request.")) {
            return 1;
        }
        if (!rejectSupersededDocumentAbi(openRequest)) {
            return 1;
        }
        AshariaSceneNativeDocumentHandle document{};
        AshariaSceneNativeDocumentOperationResult opened{};
        if (!expect(
                asharia_scene_document_open_default(&openRequest, &document, nullptr, 0U,
                                                    &opened) == AshariaSceneNativeStatus_Success &&
                    document.index != 0U && document.generation != 0U && opened.revision == 1U &&
                    opened.savedRevision == 1U && snapshotEmptyDocument(document, 1U, 1U),
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

        if (!testTransformReceipts(document, errorBuffer)) {
            return 1;
        }

        const AshariaSceneNativeDocumentCreateMeshEntityRequest meshRequest{
            .header = header(sizeof(AshariaSceneNativeDocumentCreateMeshEntityRequest)),
            .document = document,
            .expectedRevision = 7U,
            .objectIdUtf8 = view(kMeshObjectId),
            .nameUtf8 = view("Mesh Entity"),
            .meshAssetGuidUtf8 = view(kMeshAssetGuid),
        };
        AshariaSceneNativeDocumentOperationResult meshCreated{};
        if (!expect(asharia_scene_document_create_mesh_entity(&meshRequest, errorBuffer.data(),
                                                              errorBuffer.size(), &meshCreated) ==
                            AshariaSceneNativeStatus_Success &&
                        meshCreated.revision == 8U &&
                        snapshotDocument(document, 8U, 1U, kName,
                                         kNegativeYPointOneTransform, true),
                    "Native mesh entity creation did not publish the typed snapshot.")) {
            return 1;
        }

        auto invalidMeshRequest = meshRequest;
        invalidMeshRequest.expectedRevision = 8U;
        invalidMeshRequest.meshAssetGuidUtf8 = view("00000000-0000-0000-0000-000000000000");
        AshariaSceneNativeDocumentOperationResult invalidMesh{};
        if (!expect(asharia_scene_document_create_mesh_entity(
                        &invalidMeshRequest, errorBuffer.data(), errorBuffer.size(),
                        &invalidMesh) == AshariaSceneNativeStatus_InvalidAssetReference &&
                        snapshotDocument(document, 8U, 1U, kName,
                                         kNegativeYPointOneTransform, true),
                    "Invalid native mesh reference changed the authoritative revision.")) {
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
            .expectedRevision = 8U,
        };
        AshariaSceneNativeDocumentOperationResult saved{};
        if (!expect(asharia_scene_document_save(&saveRequest, errorBuffer.data(),
                                                errorBuffer.size(),
                                                &saved) == AshariaSceneNativeStatus_Success &&
                        saved.savedRevision == 8U,
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
                        snapshotDocument(document, 1U, 1U, kName,
                                         kNegativeYPointOneTransform, true) &&
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
