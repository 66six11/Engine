#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "asharia/core/file_io.hpp"
#include "asharia/scene/scene_document.hpp"
#include "asharia/scene/scene_document_io.hpp"

namespace {

    class TestDirectory {
    public:
        TestDirectory() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path() /
                    ("asharia-scene-document-smoke-" + std::to_string(stamp));
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

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] bool expect(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
        }
        return condition;
    }

    [[nodiscard]] bool testMeshEntityCreation(asharia::scene::SceneDocument& document,
                                              asharia::scene::SceneObjectId objectId,
                                              asharia::asset::AssetGuid meshAsset,
                                              std::uint64_t expectedRevision) {
        if (auto invalidMesh =
                document.createMeshEntity(objectId, "Invalid Mesh", {}, expectedRevision);
            invalidMesh || document.snapshot().revision != expectedRevision) {
            std::cerr << "Invalid mesh creation changed the document.\n";
            return false;
        }
        if (auto created =
                document.createMeshEntity(objectId, "Mesh Entity", meshAsset, expectedRevision);
            !created) {
            std::cerr << created.error().message << '\n';
            return false;
        }

        const auto created = document.snapshot();
        if (!expect(created.revision == expectedRevision + 1U &&
                        created.data.entities.size() == 2U && created.runtimeEntities.size() == 2U,
                    "Mesh entity creation did not publish a complete snapshot.")) {
            return false;
        }
        const auto& persisted = created.data.entities.at(1U);
        const auto& runtime = created.runtimeEntities.at(1U);
        if (!persisted.mesh.has_value()) {
            std::cerr << "Mesh entity creation omitted its typed asset reference.\n";
            return false;
        }
        if (!expect(runtime.objectId == objectId && asharia::isValid(runtime.entity) &&
                        persisted.mesh->guid == meshAsset &&
                        persisted.mesh->expectedType == asharia::scene::kSceneMeshAssetType,
                    "Mesh entity creation did not publish its typed reference and runtime ID.")) {
            return false;
        }
        if (auto duplicate =
                document.createMeshEntity(objectId, "Duplicate", meshAsset, created.revision);
            duplicate || document.snapshot().revision != created.revision) {
            std::cerr << "Duplicate mesh creation changed the document.\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool testSchemaV2HardCut(const asharia::scene::SceneDocumentData& restored,
                                           asharia::scene::SceneId sceneId) {
        auto serialized = asharia::scene::writeSceneDocumentText(restored);
        if (!expect(serialized && serialized->find("\"schemaVersion\": 2") != std::string::npos &&
                        serialized->find("\"mesh\"") != std::string::npos &&
                        serialized->find("com.asharia.asset.Mesh") != std::string::npos,
                    "Current scene serialization did not emit the v2 mesh schema.")) {
            return false;
        }

        constexpr std::string_view kLegacyScene =
            R"({"schema":"com.asharia.scene","schemaVersion":1,"sceneId":"11111111-2222-3333-4444-555555555555","entities":[{"id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","name":"Legacy","transform":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]})";
        auto legacy = asharia::scene::readSceneDocumentText(kLegacyScene);
        if (!expect(!legacy &&
                        legacy.error().code ==
                            static_cast<int>(asharia::scene::SceneDocumentErrorCode::InvalidScene),
                    "Legacy v1 scene was not rejected by the v2-only reader.")) {
            return false;
        }

        TestDirectory legacyProjectRoot;
        const auto legacyPath = legacyProjectRoot.path() /
                                std::filesystem::path{asharia::scene::kDefaultSceneRelativePath};
        std::filesystem::create_directories(legacyPath.parent_path());
        auto legacyWritten = asharia::core::writeFileTextAtomically(legacyPath, kLegacyScene);
        auto legacyOpened =
            asharia::scene::SceneDocument::openOrCreateDefault(legacyProjectRoot.path(), sceneId);
        auto legacyAfter = asharia::core::readFileText(
            legacyPath, {.maxBytes = static_cast<std::uint64_t>(kLegacyScene.size())});
        if (!expect(
                legacyWritten && !legacyOpened &&
                    legacyOpened.error().code ==
                        static_cast<int>(asharia::scene::SceneDocumentErrorCode::InvalidScene) &&
                    legacyAfter && *legacyAfter == kLegacyScene,
                "Opening a legacy v1 scene did not fail closed without modifying the file.")) {
            return false;
        }

        auto wrongMeshType = asharia::scene::readSceneDocumentText(
            R"({"schema":"com.asharia.scene","schemaVersion":2,"sceneId":"11111111-2222-3333-4444-555555555555","entities":[{"id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","name":"Bad","transform":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"mesh":{"assetGuid":"7c9fe8ac-3c8b-4f66-9665-0af0fd7b693e","assetType":"com.asharia.asset.Texture2D"}}]})");
        auto invalidMeshGuid = asharia::scene::readSceneDocumentText(
            R"({"schema":"com.asharia.scene","schemaVersion":2,"sceneId":"11111111-2222-3333-4444-555555555555","entities":[{"id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","name":"Bad","transform":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"mesh":{"assetGuid":"00000000-0000-0000-0000-000000000000","assetType":"com.asharia.asset.Mesh"}}]})");
        if (!expect(!wrongMeshType && !invalidMeshGuid,
                    "Scene parser accepted an invalid typed mesh reference.")) {
            return false;
        }

        auto malformed = asharia::scene::readSceneDocumentText(
            R"({"schema":"com.asharia.scene","schemaVersion":2,"sceneId":"11111111-2222-3333-4444-555555555555","entities":[{"id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","name":"Bad","transform":{"position":[0,0,0],"rotation":[0,0,0,0],"scale":[1,1,1]}}]})");
        if (!expect(!malformed, "Scene parser accepted a non-unit rotation.")) {
            return false;
        }

        asharia::scene::SceneDocumentData oversized{.sceneId = sceneId};
        oversized.entities.resize(asharia::scene::kMaxSceneEntities + 1U);
        return expect(!asharia::scene::validateSceneDocumentData(oversized),
                      "Scene validation accepted more than the bounded entity count.");
    }

    [[nodiscard]] bool testTransformReceipts(asharia::scene::SceneDocument& document,
                                             asharia::scene::SceneObjectId objectId,
                                             const asharia::scene::SceneDocumentSnapshot& renamed,
                                             const asharia::TransformComponent& moved) {
        auto transformed = document.setEntityTransform(objectId, moved, renamed.revision);
        if (!transformed) {
            std::cerr << transformed.error().message << '\n';
            return false;
        }
        if (!expect(transformed->objectId == objectId && transformed->changed &&
                        transformed->before == renamed.data.entities.front().transform &&
                        transformed->after == moved &&
                        transformed->beforeRevision == renamed.revision &&
                        transformed->afterRevision == renamed.revision + 1U,
                    "Scene Transform edit did not return an authoritative receipt.")) {
            return false;
        }
        const auto edited = document.snapshot();
        auto unchanged = document.setEntityTransform(objectId, moved, edited.revision);
        return expect(unchanged && !unchanged->changed && unchanged->before == moved &&
                          unchanged->after == moved &&
                          unchanged->beforeRevision == edited.revision &&
                          unchanged->afterRevision == edited.revision &&
                          document.snapshot().revision == edited.revision,
                      "No-op Scene Transform edit did not preserve revision and values.");
    }

    [[nodiscard]] bool testExternalSaveChanges() {
        TestDirectory directory;
        auto document = asharia::scene::SceneDocument::openOrCreateDefault(
            directory.path(), asharia::scene::SceneId{.bytes = {3U}});
        if (!document) {
            return false;
        }
        auto external = document->snapshot().data;
        external.entities.push_back({.objectId = {.bytes = {4U}}, .name = "external"});
        if (!asharia::scene::writeSceneDocumentFile(document->path(), external) ||
            document->save(1U)) {
            return false;
        }
        auto fresh = asharia::scene::SceneDocument::openOrCreateDefault(directory.path(), {});
        if (!fresh || !fresh->save(1U)) {
            return false;
        }
        auto lockPath = fresh->path();
        lockPath += ".lock";
        auto held = asharia::core::tryAcquireExclusiveFileLock(lockPath);
        if (!held || !held->has_value() || fresh->save(1U)) {
            return false;
        }
        if (!(**held).release() || !fresh->save(1U)) {
            return false;
        }
        if (!asharia::core::writeFileTextAtomically(fresh->path(), "malformed") ||
            fresh->save(1U)) {
            return false;
        }
        auto after = asharia::core::readFileText(fresh->path(), {.maxBytes = 64U});
        return expect(after && *after == "malformed", "Save replaced malformed external bytes.");
    }

    [[nodiscard]] bool testMeshEdits() {
        TestDirectory directory;
        const asharia::scene::SceneObjectId objectId{.bytes = {2U}};
        auto document =
            asharia::scene::SceneDocument::openOrCreateDefault(directory.path(), {.bytes = {1U}});
        if (!document || !document->createEntity(objectId, "Mesh edits", 1U)) {
            return false;
        }
        const auto baseline = document->snapshot();
        const auto meshA = asharia::asset::makeAssetReference({.bytes = {3U}},
                                                              asharia::scene::kSceneMeshAssetType);
        const auto meshB = asharia::asset::makeAssetReference({.bytes = {4U}},
                                                              asharia::scene::kSceneMeshAssetType);
        auto attached = document->setEntityMesh(objectId, meshA, 2U);
        auto unchanged = document->setEntityMesh(objectId, meshA, 3U);
        if (!attached || !attached->changed || attached->before || attached->after != meshA ||
            attached->beforeRevision != 2U || attached->afterRevision != 3U || !unchanged ||
            unchanged->changed || unchanged->before != meshA || unchanged->after != meshA ||
            unchanged->afterRevision != 3U) {
            return expect(false, "Mesh attach or no-op receipt differed.");
        }
        const auto beforeFailure = document->snapshot();
        auto wrongType = meshB;
        wrongType.expectedType = asharia::asset::makeAssetTypeId("Wrong");
        const auto invalid = document->setEntityMesh(objectId, wrongType, 3U);
        const auto empty = document->setEntityMesh(objectId, asharia::asset::AssetReference{}, 3U);
        const auto missing = document->setEntityMesh({.bytes = {9U}}, meshB, 3U);
        const auto stale = document->setEntityMesh(objectId, meshB, 2U);
        const auto afterFailure = document->snapshot();
        if (invalid || empty || missing || stale || afterFailure.data != beforeFailure.data ||
            afterFailure.revision != beforeFailure.revision ||
            afterFailure.savedRevision != beforeFailure.savedRevision) {
            return expect(false, "Failed Mesh edits modified authoritative data.");
        }
        auto replaced = document->setEntityMesh(objectId, meshB, 3U);
        if (!replaced || replaced->before != meshA || replaced->after != meshB ||
            replaced->afterRevision != 4U) {
            return false;
        }
        // The receipt supports revision-checked inverse/reapply operations without owning history.
        auto inverse = document->setEntityMesh(objectId, replaced->before, 4U);
        auto reapplied = document->setEntityMesh(objectId, replaced->after, 5U);
        if (!inverse || !reapplied || !document->save(6U)) {
            return false;
        }
        auto reopened = asharia::scene::SceneDocument::openOrCreateDefault(directory.path(), {});
        if (!reopened || reopened->snapshot().data != document->snapshot().data) {
            return expect(false, "Replaced Mesh did not survive save/reopen.");
        }
        auto removed = document->setEntityMesh(objectId, std::nullopt, 6U);
        auto absent = document->setEntityMesh(objectId, std::nullopt, 7U);
        if (!removed || !removed->changed || removed->before != meshB || removed->after ||
            !absent || absent->changed || absent->afterRevision != 7U || !document->save(7U)) {
            return false;
        }
        auto removedOnDisk = asharia::scene::readSceneDocumentFile(document->path());
        const auto final = document->snapshot();
        return expect(removedOnDisk && *removedOnDisk == baseline.data &&
                          final.runtimeEntities == baseline.runtimeEntities &&
                          final.savedRevision == 7U,
                      "Mesh removal changed entity identity or failed persistence.");
    }

    [[nodiscard]] bool testConcurrentSave() {
        TestDirectory directory;
        const asharia::scene::SceneId sceneId{.bytes = {1U}};
        const asharia::scene::SceneObjectId objectId{.bytes = {2U}};
        auto first = asharia::scene::SceneDocument::openOrCreateDefault(directory.path(), sceneId);
        auto second = asharia::scene::SceneDocument::openOrCreateDefault(directory.path(), sceneId);
        if (!first || !second || !first->createEntity(objectId, "first", 1U) ||
            !second->createEntity(objectId, "second", 1U) || !first->save(2U)) {
            return false;
        }
        const auto conflict = second->save(2U);
        const auto disk = asharia::scene::readSceneDocumentFile(first->path());
        return expect(
            !conflict &&
                conflict.error().code ==
                    static_cast<int>(asharia::scene::SceneDocumentErrorCode::RevisionConflict) &&
                disk && *disk == first->snapshot().data && second->snapshot().savedRevision == 1U,
            "Stale scene save overwrote another writer or advanced its savepoint.");
    }

} // namespace

int main() noexcept {
    try {
        TestDirectory projectRoot;
        auto sceneId = asharia::scene::parseSceneId("11111111-2222-3333-4444-555555555555");
        auto objectId = asharia::scene::parseSceneObjectId("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        auto meshObjectId =
            asharia::scene::parseSceneObjectId("bbbbbbbb-cccc-dddd-eeee-ffffffffffff");
        auto meshAsset = asharia::asset::parseAssetGuid("7c9fe8ac-3c8b-4f66-9665-0af0fd7b693e");
        if (!sceneId || !objectId || !meshObjectId || !meshAsset) {
            std::cerr << "Scene smoke IDs did not parse.\n";
            return 1;
        }

        auto opened =
            asharia::scene::SceneDocument::openOrCreateDefault(projectRoot.path(), *sceneId);
        if (!opened) {
            std::cerr << opened.error().message << '\n';
            return 1;
        }
        const auto initial = opened->snapshot();
        auto initialFile =
            asharia::core::readFileText(opened->path(), {.maxBytes = 64ULL * 1024ULL});
        if (!expect(initial.revision == 1U && initial.savedRevision == 1U &&
                        initial.data.sceneId == *sceneId && initial.data.entities.empty() &&
                        std::filesystem::is_regular_file(opened->path()) && initialFile &&
                        initialFile->find("\"schemaVersion\": 2") != std::string::npos,
                    "Default scene did not open as a clean empty v2 document.")) {
            return 1;
        }

        if (auto created = opened->createEntity(*objectId, "Entity", initial.revision); !created) {
            std::cerr << created.error().message << '\n';
            return 1;
        }
        const auto created = opened->snapshot();
        if (!expect(created.revision == 2U && created.savedRevision == 1U &&
                        created.data.entities.size() == 1U,
                    "Entity creation did not advance the authoritative revision.")) {
            return 1;
        }
        if (auto stale = opened->setEntityName(*objectId, "Stale", initial.revision); stale) {
            std::cerr << "Stale scene mutation unexpectedly succeeded.\n";
            return 1;
        }

        if (auto renamed = opened->setEntityName(*objectId, "主角", created.revision); !renamed) {
            std::cerr << renamed.error().message << '\n';
            return 1;
        }
        auto renamed = opened->snapshot();
        const asharia::TransformComponent moved{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {},
            .scale = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
        };
        if (!testTransformReceipts(*opened, *objectId, renamed, moved)) {
            return 1;
        }
        const auto edited = opened->snapshot();
        if (!expect(edited.revision == 4U && edited.data.entities.front().name == "主角" &&
                        edited.data.entities.front().transform == moved &&
                        !edited.data.entities.front().mesh.has_value(),
                    "Scene name/Transform edits were not reflected by one snapshot.")) {
            return 1;
        }
        if (!testMeshEntityCreation(*opened, *meshObjectId, *meshAsset, edited.revision)) {
            return 1;
        }
        const auto meshCreated = opened->snapshot();

        if (auto saved = opened->save(meshCreated.revision); !saved) {
            std::cerr << saved.error().message << '\n';
            return 1;
        }
        const auto saved = opened->snapshot();
        if (!expect(saved.savedRevision == saved.revision,
                    "Scene save did not advance the savepoint.")) {
            return 1;
        }

        auto reopened = asharia::scene::SceneDocument::openOrCreateDefault(projectRoot.path(), {});
        if (!reopened) {
            std::cerr << reopened.error().message << '\n';
            return 1;
        }
        const auto restored = reopened->snapshot();
        if (!expect(restored.data == saved.data && restored.revision == 1U &&
                        restored.savedRevision == 1U &&
                        restored.runtimeEntities.size() == restored.data.entities.size() &&
                        restored.data.entities[1].mesh.has_value(),
                    "Saved scene data did not survive close/reopen.")) {
            return 1;
        }

        if (!testMeshEdits() || !testExternalSaveChanges() || !testConcurrentSave() ||
            !testSchemaV2HardCut(restored.data, *sceneId)) {
            return 1;
        }

        std::cout << "Scene document persistence smoke tests passed.\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
