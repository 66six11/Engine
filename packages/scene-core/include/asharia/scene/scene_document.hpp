#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/scene/entity_id.hpp"
#include "asharia/scene/transform.hpp"
#include "asharia/scene/world.hpp"

namespace asharia::scene {

    inline constexpr std::string_view kAshariaSceneSchema = "com.asharia.scene";
    inline constexpr std::uint32_t kAshariaSceneSchemaVersion = 1;
    inline constexpr std::string_view kDefaultSceneRelativePath =
        "Assets/Scenes/Default.asharia.scene.json";
    inline constexpr std::uint64_t kInitialSceneDocumentRevision = 1;
    inline constexpr std::size_t kMaxSceneEntities = 10'000;
    inline constexpr std::size_t kMaxSceneEntityNameUtf8Bytes = 4096;

    enum class SceneDocumentErrorCode : int {
        InvalidScene = 1,
        SceneIo = 2,
        RevisionConflict = 3,
        DuplicateObjectId = 4,
        InvalidObjectId = 5,
        InvalidTransform = 6,
    };

    struct SceneId {
        std::array<std::uint8_t, 16> bytes{};

        [[nodiscard]] friend bool operator==(SceneId, SceneId) = default;
        [[nodiscard]] explicit operator bool() const noexcept;
    };

    struct SceneObjectId {
        std::array<std::uint8_t, 16> bytes{};

        [[nodiscard]] friend bool operator==(SceneObjectId, SceneObjectId) = default;
        [[nodiscard]] explicit operator bool() const noexcept;
    };

    struct SceneEntityData {
        SceneObjectId objectId{};
        std::string name;
        TransformComponent transform{};

        [[nodiscard]] friend bool operator==(const SceneEntityData&,
                                             const SceneEntityData&) = default;
    };

    struct SceneDocumentData {
        SceneId sceneId{};
        std::vector<SceneEntityData> entities;

        [[nodiscard]] friend bool operator==(const SceneDocumentData&,
                                             const SceneDocumentData&) = default;
    };

    struct SceneDocumentSnapshot {
        SceneDocumentData data;
        std::uint64_t revision{};
        std::uint64_t savedRevision{};

        [[nodiscard]] bool dirty() const noexcept {
            return revision != savedRevision;
        }
    };

    [[nodiscard]] Result<SceneId> parseSceneId(std::string_view text);
    [[nodiscard]] std::string formatSceneId(SceneId id);
    [[nodiscard]] Result<SceneObjectId> parseSceneObjectId(std::string_view text);
    [[nodiscard]] std::string formatSceneObjectId(SceneObjectId id);
    [[nodiscard]] VoidResult validateSceneDocumentData(const SceneDocumentData& data);

    class SceneDocument {
    public:
        [[nodiscard]] static Result<SceneDocument>
        openOrCreateDefault(const std::filesystem::path& projectRoot, SceneId newSceneId);

        [[nodiscard]] const std::filesystem::path& path() const noexcept;
        [[nodiscard]] SceneDocumentSnapshot snapshot() const;

        [[nodiscard]] VoidResult createEntity(SceneObjectId objectId, std::string_view name,
                                              std::uint64_t expectedRevision);
        [[nodiscard]] VoidResult setEntityName(SceneObjectId objectId, std::string_view name,
                                               std::uint64_t expectedRevision);
        [[nodiscard]] VoidResult setEntityTransform(SceneObjectId objectId,
                                                    const TransformComponent& transform,
                                                    std::uint64_t expectedRevision);
        [[nodiscard]] VoidResult save(std::uint64_t expectedRevision);

    private:
        struct RuntimeEntity {
            SceneObjectId objectId{};
            EntityId entity{};
        };

        SceneDocument(std::filesystem::path path, SceneDocumentData data, World world,
                      std::vector<RuntimeEntity> runtimeEntities);

        [[nodiscard]] RuntimeEntity* findRuntimeEntity(SceneObjectId objectId) noexcept;
        [[nodiscard]] const RuntimeEntity* findRuntimeEntity(SceneObjectId objectId) const noexcept;
        [[nodiscard]] VoidResult requireRevision(std::uint64_t expectedRevision) const;
        void advanceRevision() noexcept;

        std::filesystem::path path_;
        SceneDocumentData data_;
        World world_;
        std::vector<RuntimeEntity> runtimeEntities_;
        std::uint64_t revision_{kInitialSceneDocumentRevision};
        std::uint64_t savedRevision_{kInitialSceneDocumentRevision};
    };

} // namespace asharia::scene
