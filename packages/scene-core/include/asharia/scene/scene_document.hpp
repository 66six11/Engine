#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_reference.hpp"
#include "asharia/core/result.hpp"
#include "asharia/scene/entity_id.hpp"
#include "asharia/scene/transform.hpp"
#include "asharia/scene/world.hpp"

namespace asharia::scene {

    inline constexpr std::string_view kAshariaSceneSchema = "com.asharia.scene";
    inline constexpr std::uint32_t kAshariaSceneSchemaVersion = 2;
    inline constexpr std::string_view kSceneMeshAssetTypeName = "com.asharia.asset.Mesh";
    inline constexpr asset::AssetTypeId kSceneMeshAssetType =
        asset::makeAssetTypeId(kSceneMeshAssetTypeName);
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
        InvalidAssetReference = 7,
        RevisionExhausted = 8,
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
        std::optional<asset::AssetReference> mesh;

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
        struct RuntimeEntityBinding {
            SceneObjectId objectId{};
            EntityId entity{};

            [[nodiscard]] friend bool operator==(RuntimeEntityBinding,
                                                 RuntimeEntityBinding) = default;
        };

        SceneDocumentData data;
        std::vector<RuntimeEntityBinding> runtimeEntities;
        std::uint64_t revision{};
        std::uint64_t savedRevision{};
    };

    struct SceneEntityTransformReceipt {
        SceneObjectId objectId{};
        bool changed{};
        TransformComponent before{};
        TransformComponent after{};
        std::uint64_t beforeRevision{};
        std::uint64_t afterRevision{};

        [[nodiscard]] friend bool operator==(const SceneEntityTransformReceipt&,
                                             const SceneEntityTransformReceipt&) = default;
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
        [[nodiscard]] VoidResult createMeshEntity(SceneObjectId objectId, std::string_view name,
                                                  asset::AssetGuid meshAsset,
                                                  std::uint64_t expectedRevision);
        [[nodiscard]] VoidResult setEntityName(SceneObjectId objectId, std::string_view name,
                                               std::uint64_t expectedRevision);
        [[nodiscard]] Result<SceneEntityTransformReceipt>
        setEntityTransform(SceneObjectId objectId, const TransformComponent& transform,
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
        [[nodiscard]] VoidResult createPreparedEntity(SceneEntityData prepared,
                                                      std::uint64_t expectedRevision);
        [[nodiscard]] VoidResult requireRevision(std::uint64_t expectedRevision) const;
        void advanceRevision() noexcept;

        std::filesystem::path path_;
        SceneDocumentData data_;
        SceneDocumentData savedData_;
        World world_;
        std::vector<RuntimeEntity> runtimeEntities_;
        std::uint64_t revision_{kInitialSceneDocumentRevision};
        std::uint64_t savedRevision_{kInitialSceneDocumentRevision};
    };

} // namespace asharia::scene
