#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

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

} // namespace

int main() noexcept {
    try {
        TestDirectory projectRoot;
        auto sceneId = asharia::scene::parseSceneId("11111111-2222-3333-4444-555555555555");
        auto objectId = asharia::scene::parseSceneObjectId("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        if (!sceneId || !objectId) {
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
        if (!expect(initial.revision == 1U && initial.savedRevision == 1U && !initial.dirty() &&
                        initial.data.sceneId == *sceneId && initial.data.entities.empty() &&
                        std::filesystem::is_regular_file(opened->path()),
                    "Default scene did not open as a clean empty document.")) {
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
                        edited.data.entities.front().transform == moved,
                    "Scene name/Transform edits were not reflected by one snapshot.")) {
            return 1;
        }

        if (auto saved = opened->save(edited.revision); !saved) {
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
                        restored.savedRevision == 1U && !restored.dirty(),
                    "Saved scene data did not survive close/reopen.")) {
            return 1;
        }

        auto malformed = asharia::scene::readSceneDocumentText(
            R"({"schema":"com.asharia.scene","schemaVersion":1,"sceneId":"11111111-2222-3333-4444-555555555555","entities":[{"id":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","name":"Bad","transform":{"position":[0,0,0],"rotation":[0,0,0,0],"scale":[1,1,1]}}]})");
        if (!expect(!malformed, "Scene parser accepted a non-unit rotation.")) {
            return 1;
        }

        asharia::scene::SceneDocumentData oversized{.sceneId = *sceneId};
        oversized.entities.resize(asharia::scene::kMaxSceneEntities + 1U);
        if (!expect(!asharia::scene::validateSceneDocumentData(oversized),
                    "Scene validation accepted more than the bounded entity count.")) {
            return 1;
        }

        std::cout << "Scene document persistence smoke tests passed.\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
