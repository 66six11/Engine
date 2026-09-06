#include "asharia/scene_rendering/scene_mesh_extraction.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::scene_rendering {
    namespace {

        constexpr float kUnitQuaternionTolerance = 1.0e-3F;

        [[nodiscard]] bool finite(Vec3 value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool validSceneObjectId(scene::SceneObjectId objectId) noexcept {
            return std::ranges::any_of(objectId.bytes,
                                       [](std::uint8_t byte) { return byte != 0U; });
        }

        [[nodiscard]] bool validTransform(const TransformComponent& transform) noexcept {
            if (!finite(transform.position) || !finite(transform.scale) ||
                !std::isfinite(transform.rotation.x) || !std::isfinite(transform.rotation.y) ||
                !std::isfinite(transform.rotation.z) || !std::isfinite(transform.rotation.w)) {
                return false;
            }

            const float lengthSquared = (transform.rotation.x * transform.rotation.x) +
                                        (transform.rotation.y * transform.rotation.y) +
                                        (transform.rotation.z * transform.rotation.z) +
                                        (transform.rotation.w * transform.rotation.w);
            return std::fabs(lengthSquared - 1.0F) <= kUnitQuaternionTolerance;
        }

        [[nodiscard]] bool validDrawItem(const BasicDrawItem& item) noexcept {
            return item.instanceCount != 0U && item.vertexCount == 0U && item.indexCount != 0U;
        }

        [[nodiscard]] bool validBinding(const SceneMeshProductBinding& binding) noexcept {
            return binding.productHash != 0U && binding.productGeneration != 0U &&
                   static_cast<bool>(binding.meshResource) && !binding.sections.empty() &&
                   std::ranges::all_of(binding.sections,
                                       [](const SceneMeshSectionBinding& section) {
                                           return static_cast<bool>(section.materialResource) &&
                                                  validDrawItem(section.drawItem);
                                       });
        }

        [[nodiscard]] const SceneMeshProductBinding*
        findExactBinding(const SceneMeshInstance& instance,
                         std::span<const SceneMeshProductBinding> bindings) noexcept {
            const auto binding = std::ranges::find_if(
                bindings, [&instance](const SceneMeshProductBinding& candidate) {
                    return candidate.asset == instance.mesh;
                });
            return binding == bindings.end() ? nullptr : std::to_address(binding);
        }

        [[nodiscard]] bool
        hasAssetGuidBinding(const SceneMeshInstance& instance,
                            std::span<const SceneMeshProductBinding> bindings) noexcept {
            return std::ranges::any_of(bindings,
                                       [&instance](const SceneMeshProductBinding& binding) {
                                           return binding.asset.guid == instance.mesh.guid;
                                       });
        }

        void appendSectionDraws(std::vector<BasicDrawListItem>& drawItems,
                                const SceneMeshInstance& instance,
                                const SceneMeshProductBinding& binding) {
            const auto modelMatrix = makeSceneMeshModelMatrix(instance.transform);
            for (const SceneMeshSectionBinding& section : binding.sections) {
                drawItems.push_back(BasicDrawListItem{
                    .drawItem = section.drawItem,
                    .modelMatrix = modelMatrix,
                    .context =
                        BasicDrawPacketContext{
                            .sourceObject =
                                BasicDrawSourceId{
                                    .index = instance.entity.index,
                                    .generation = instance.entity.generation,
                                },
                            .meshResource = binding.meshResource,
                            .materialResource = section.materialResource,
                            .meshRevision = binding.productGeneration,
                        },
                });
            }
        }

    } // namespace

    std::uint64_t SceneMeshExtraction::revision() const noexcept {
        return revision_;
    }

    std::span<const BasicDrawListItem> SceneMeshExtraction::drawItems() const noexcept {
        return drawItems_;
    }

    std::span<const SceneMeshExtractionDiagnostic>
    SceneMeshExtraction::diagnostics() const noexcept {
        return diagnostics_;
    }

    BasicTransformMatrix3D makeSceneMeshModelMatrix(const TransformComponent& transform) noexcept {
        const float rotationX = transform.rotation.x;
        const float rotationY = transform.rotation.y;
        const float rotationZ = transform.rotation.z;
        const float rotationW = transform.rotation.w;
        const float rotationXX = rotationX * rotationX;
        const float rotationYY = rotationY * rotationY;
        const float rotationZZ = rotationZ * rotationZ;
        const float rotationXY = rotationX * rotationY;
        const float rotationXZ = rotationX * rotationZ;
        const float rotationYZ = rotationY * rotationZ;
        const float rotationWX = rotationW * rotationX;
        const float rotationWY = rotationW * rotationY;
        const float rotationWZ = rotationW * rotationZ;

        return BasicTransformMatrix3D{
            (1.0F - (2.0F * (rotationYY + rotationZZ))) * transform.scale.x,
            (2.0F * (rotationXY - rotationWZ)) * transform.scale.y,
            (2.0F * (rotationXZ + rotationWY)) * transform.scale.z,
            transform.position.x,
            (2.0F * (rotationXY + rotationWZ)) * transform.scale.x,
            (1.0F - (2.0F * (rotationXX + rotationZZ))) * transform.scale.y,
            (2.0F * (rotationYZ - rotationWX)) * transform.scale.z,
            transform.position.y,
            (2.0F * (rotationXZ - rotationWY)) * transform.scale.x,
            (2.0F * (rotationYZ + rotationWX)) * transform.scale.y,
            (1.0F - (2.0F * (rotationXX + rotationYY))) * transform.scale.z,
            transform.position.z,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
        };
    }

    SceneMeshExtraction extractSceneMeshDrawList(const SceneMeshExtractionInput& input) {
        SceneMeshExtraction extraction;
        extraction.revision_ = input.revision;
        extraction.drawItems_.reserve(input.instances.size());
        const auto appendDiagnostic = [&extraction](SceneMeshExtractionDiagnosticCode code,
                                                    const SceneMeshInstance& instance,
                                                    std::string message) {
            extraction.diagnostics_.push_back(SceneMeshExtractionDiagnostic{
                .code = code,
                .revision = extraction.revision_,
                .objectId = instance.objectId,
                .asset = instance.mesh.guid,
                .message = std::move(message),
            });
        };

        for (const SceneMeshInstance& instance : input.instances) {
            if (!validSceneObjectId(instance.objectId)) {
                appendDiagnostic(SceneMeshExtractionDiagnosticCode::InvalidSceneObject, instance,
                                 "Scene mesh extraction rejected an invalid scene object.");
                continue;
            }
            if (!isValid(instance.entity)) {
                appendDiagnostic(SceneMeshExtractionDiagnosticCode::InvalidRuntimeEntity, instance,
                                 "Scene mesh extraction rejected an invalid runtime entity.");
                continue;
            }
            if (!validTransform(instance.transform)) {
                appendDiagnostic(SceneMeshExtractionDiagnosticCode::InvalidTransform, instance,
                                 "Scene mesh extraction rejected an invalid Transform.");
                continue;
            }
            if (!static_cast<bool>(instance.mesh) ||
                instance.mesh.expectedType != scene::kSceneMeshAssetType) {
                appendDiagnostic(
                    SceneMeshExtractionDiagnosticCode::InvalidMeshReference, instance,
                    "Scene mesh extraction rejected a mesh reference with the wrong kind.");
                continue;
            }

            const SceneMeshProductBinding* binding =
                findExactBinding(instance, input.productBindings);
            if (binding == nullptr) {
                const SceneMeshExtractionDiagnosticCode code =
                    hasAssetGuidBinding(instance, input.productBindings)
                        ? SceneMeshExtractionDiagnosticCode::WrongProductBindingKind
                        : SceneMeshExtractionDiagnosticCode::MissingProductBinding;
                appendDiagnostic(
                    code, instance,
                    code == SceneMeshExtractionDiagnosticCode::WrongProductBindingKind
                        ? "Scene mesh extraction found a product binding with the wrong asset kind."
                        : "Scene mesh extraction found no product binding for the mesh asset.");
                continue;
            }
            if (std::ranges::count(input.productBindings, instance.mesh,
                                   &SceneMeshProductBinding::asset) != 1) {
                appendDiagnostic(SceneMeshExtractionDiagnosticCode::InvalidProductBinding, instance,
                                 "Scene mesh extraction rejected duplicate mesh product bindings.");
                continue;
            }
            if (binding->state != SceneMeshProductState::Ready) {
                appendDiagnostic(SceneMeshExtractionDiagnosticCode::StaleProductBinding, instance,
                                 "Scene mesh extraction rejected a stale mesh product binding.");
                continue;
            }
            if (!validBinding(*binding)) {
                appendDiagnostic(
                    SceneMeshExtractionDiagnosticCode::InvalidProductBinding, instance,
                    "Scene mesh extraction rejected an incomplete mesh product binding.");
                continue;
            }

            appendSectionDraws(extraction.drawItems_, instance, *binding);
        }

        return extraction;
    }

} // namespace asharia::scene_rendering
