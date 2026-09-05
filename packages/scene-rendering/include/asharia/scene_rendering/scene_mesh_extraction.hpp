#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "asharia/asset_core/asset_reference.hpp"
#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/scene/entity_id.hpp"
#include "asharia/scene/scene_document.hpp"
#include "asharia/scene/transform.hpp"

namespace asharia::scene_rendering {

    enum class SceneMeshProductState : std::uint8_t {
        Ready,
        Stale,
    };

    struct SceneMeshInstance {
        scene::SceneObjectId objectId{};
        EntityId entity{};
        TransformComponent transform{};
        asset::AssetReference mesh{};
    };

    struct SceneMeshSectionBinding {
        std::uint32_t materialSlot{};
        BasicDrawResourceKey materialResource{};
        BasicDrawItem drawItem{};
    };

    struct SceneMeshProductBinding {
        asset::AssetReference asset{};
        SceneMeshProductState state{SceneMeshProductState::Stale};
        std::uint64_t productHash{};
        std::uint64_t productGeneration{};
        BasicDrawResourceKey meshResource{};
        std::vector<SceneMeshSectionBinding> sections;
    };

    struct SceneMeshExtractionInput {
        std::uint64_t revision{};
        std::span<const SceneMeshInstance> instances{};
        std::span<const SceneMeshProductBinding> productBindings{};
    };

    enum class SceneMeshExtractionDiagnosticCode : std::uint8_t {
        InvalidSceneObject,
        InvalidRuntimeEntity,
        InvalidTransform,
        InvalidMeshReference,
        MissingProductBinding,
        WrongProductBindingKind,
        StaleProductBinding,
        InvalidProductBinding,
    };

    struct SceneMeshExtractionDiagnostic {
        SceneMeshExtractionDiagnosticCode code{};
        std::uint64_t revision{};
        scene::SceneObjectId objectId{};
        asset::AssetGuid asset{};
        std::string message;
    };

    class SceneMeshExtraction final {
    public:
        [[nodiscard]] std::uint64_t revision() const noexcept;
        [[nodiscard]] std::span<const BasicDrawListItem> drawItems() const noexcept;
        [[nodiscard]] std::span<const SceneMeshExtractionDiagnostic> diagnostics() const noexcept;

    private:
        friend SceneMeshExtraction extractSceneMeshDrawList(const SceneMeshExtractionInput& input);

        std::uint64_t revision_{};
        std::vector<BasicDrawListItem> drawItems_;
        std::vector<SceneMeshExtractionDiagnostic> diagnostics_;
    };

    [[nodiscard]] BasicTransformMatrix3D
    makeSceneMeshModelMatrix(const TransformComponent& transform) noexcept;

    [[nodiscard]] SceneMeshExtraction
    extractSceneMeshDrawList(const SceneMeshExtractionInput& input);

} // namespace asharia::scene_rendering
