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
        if (!expect(initial.revision == 1U && initial.savedRevision == 1U && !initial.dirty() &&
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
        if (!expect(created.revision == 2U && created.savedRevision == 1U && created.dirty() &&
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
        if (auto transformed = opened->setEntityTransform(*objectId, moved, renamed.revision);
            !transformed) {
            std::cerr << transformed.error().message << '\n';
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
        if (!expect(!saved.dirty() && saved.savedRevision == saved.revision,
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
                        restored.savedRevision == 1U && !restored.dirty() &&
                        restored.runtimeEntities.size() == restored.data.entities.size() &&
                        restored.data.entities[1].mesh.has_value(),
                    "Saved scene data did not survive close/reopen.")) {
            return 1;
        }

        if (!testSchemaV2HardCut(restored.data, *sceneId)) {
            return 1;
        }

        std::cout << "Scene document persistence smoke tests passed.\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
