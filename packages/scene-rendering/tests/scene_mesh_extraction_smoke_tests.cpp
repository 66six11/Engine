#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "asharia/scene_rendering/scene_mesh_extraction.hpp"

namespace {

    [[nodiscard]] bool expect(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << message << '\n';
        }
        return condition;
    }

    [[nodiscard]] bool matricesNear(const asharia::BasicTransformMatrix3D& lhs,
                                    const asharia::BasicTransformMatrix3D& rhs,
                                    float tolerance = 1.0e-4F) {
        for (std::size_t index = 0; index < lhs.size(); ++index) {
            if (std::fabs(lhs.at(index) - rhs.at(index)) > tolerance) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::array<float, 3>
    transformPoint(const asharia::BasicTransformMatrix3D& matrix,
                   std::array<float, 3> point) {
        return {
            (matrix[0] * point[0]) + (matrix[1] * point[1]) +
                (matrix[2] * point[2]) + matrix[3],
            (matrix[4] * point[0]) + (matrix[5] * point[1]) +
                (matrix[6] * point[2]) + matrix[7],
            (matrix[8] * point[0]) + (matrix[9] * point[1]) +
                (matrix[10] * point[2]) + matrix[11],
        };
    }

    [[nodiscard]] asharia::scene::SceneObjectId sceneObjectId() {
        return asharia::scene::SceneObjectId{.bytes = {0xAAU}};
    }

    [[nodiscard]] asharia::asset::AssetGuid assetGuid() {
        return asharia::asset::AssetGuid{.bytes = {0x7CU}};
    }

    [[nodiscard]] asharia::scene_rendering::SceneMeshInstance validInstance() {
        return asharia::scene_rendering::SceneMeshInstance{
            .objectId = sceneObjectId(),
            .entity = {.index = 17U, .generation = 3U},
            .transform = {
                .position = {.x = 2.0F, .y = 3.0F, .z = 4.0F},
                .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
                .scale = {.x = 2.0F, .y = 3.0F, .z = 4.0F},
            },
            .mesh = asharia::asset::makeAssetReference(assetGuid(),
                                                        asharia::scene::kSceneMeshAssetType),
        };
    }

    [[nodiscard]] asharia::scene_rendering::SceneMeshProductBinding validBinding() {
        return asharia::scene_rendering::SceneMeshProductBinding{
            .asset = asharia::asset::makeAssetReference(assetGuid(),
                                                         asharia::scene::kSceneMeshAssetType),
            .state = asharia::scene_rendering::SceneMeshProductState::Ready,
            .productHash = 0x0EB29D6DE539D278ULL,
            .productGeneration = 1U,
            .meshResource = asharia::kBasicValidationMeshResourceKey,
            .materialResource = asharia::kBasicDefaultUnlitMaterialResourceKey,
            .drawItem = asharia::basicValidationMeshDrawItem(),
        };
    }

    [[nodiscard]] bool expectDiagnostic(
        const asharia::scene_rendering::SceneMeshExtraction& extraction,
        asharia::scene_rendering::SceneMeshExtractionDiagnosticCode expectedCode,
        std::uint64_t revision) {
        const auto diagnostics = extraction.diagnostics();
        return expect(diagnostics.size() == 1U && diagnostics.front().code == expectedCode &&
                          diagnostics.front().revision == revision &&
                          diagnostics.front().objectId == sceneObjectId() &&
                          diagnostics.front().asset == assetGuid(),
                      "Extraction diagnostic did not preserve context.");
    }

    [[nodiscard]] bool expectModelMatrixContracts() {
        const asharia::TransformComponent transformed{
            .position = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
            .rotation = {.x = 0.0F, .y = std::sqrt(0.5F), .z = 0.0F, .w = std::sqrt(0.5F)},
            .scale = {.x = 2.0F, .y = 3.0F, .z = 4.0F},
        };
        const asharia::BasicTransformMatrix3D expectedMatrix{
            0.0F, 0.0F, 4.0F, 10.0F, 0.0F, 3.0F, 0.0F, 20.0F,
            -2.0F, 0.0F, 0.0F, 30.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        };
        const auto matrix = asharia::scene_rendering::makeSceneMeshModelMatrix(transformed);
        const auto transformedPoint = transformPoint(matrix, {1.0F, 2.0F, 3.0F});
        if (!expect(matricesNear(matrix, expectedMatrix) &&
                        std::fabs(transformedPoint[0] - 22.0F) < 1.0e-4F &&
                        std::fabs(transformedPoint[1] - 26.0F) < 1.0e-4F &&
                        std::fabs(transformedPoint[2] - 28.0F) < 1.0e-4F,
                    "T*R*S matrix did not preserve row-major composite point semantics.")) {
            return false;
        }

        asharia::TransformComponent negatedQuaternion = transformed;
        negatedQuaternion.rotation.x = -negatedQuaternion.rotation.x;
        negatedQuaternion.rotation.y = -negatedQuaternion.rotation.y;
        negatedQuaternion.rotation.z = -negatedQuaternion.rotation.z;
        negatedQuaternion.rotation.w = -negatedQuaternion.rotation.w;
        if (!expect(matricesNear(
                        asharia::scene_rendering::makeSceneMeshModelMatrix(negatedQuaternion),
                        matrix),
                    "Equivalent q and -q rotations produced different model matrices.")) {
            return false;
        }

        const asharia::TransformComponent reflectedAndCollapsed{
            .position = {},
            .rotation = {.w = 1.0F},
            .scale = {.x = -2.0F, .y = 0.0F, .z = 4.0F},
        };
        const auto reflectedMatrix =
            asharia::scene_rendering::makeSceneMeshModelMatrix(reflectedAndCollapsed);
        bool reflectedMatrixIsFinite = true;
        for (const float element : reflectedMatrix) {
            reflectedMatrixIsFinite = reflectedMatrixIsFinite && std::isfinite(element);
        }
        const auto reflectedPoint = transformPoint(reflectedMatrix, {1.0F, 2.0F, 3.0F});
        return expect(reflectedMatrixIsFinite &&
                          std::fabs(reflectedPoint[0] + 2.0F) < 1.0e-4F &&
                          std::fabs(reflectedPoint[1]) < 1.0e-4F &&
                          std::fabs(reflectedPoint[2] - 12.0F) < 1.0e-4F,
                      "Finite negative and zero scales did not remain explicit in the model matrix.");
    }

} // namespace

int main() noexcept {
    try {
        constexpr std::uint64_t kRevision = 42U;
        const asharia::scene_rendering::SceneMeshInstance instance = validInstance();
        const asharia::scene_rendering::SceneMeshProductBinding binding = validBinding();

        const asharia::scene_rendering::SceneMeshExtraction empty =
            asharia::scene_rendering::extractSceneMeshDrawList({.revision = kRevision});
        if (!expect(empty.revision() == kRevision && empty.drawItems().empty() &&
                        empty.diagnostics().empty(),
                    "Empty scene extraction did not remain empty.")) {
            return 1;
        }

        const auto extracted = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision, .instances = {&instance, 1U}, .productBindings = {&binding, 1U}});
        const auto items = extracted.drawItems();
        if (!expect(extracted.revision() == kRevision && items.size() == 1U &&
                        extracted.diagnostics().empty() &&
                        items.front().drawItem.indexCount == 72U &&
                        items.front().context.sourceObject.index == instance.entity.index &&
                        items.front().context.sourceObject.generation == instance.entity.generation &&
                        items.front().context.meshResource == asharia::kBasicValidationMeshResourceKey &&
                        items.front().context.materialResource ==
                            asharia::kBasicDefaultUnlitMaterialResourceKey &&
                        items.front().modelMatrix[0] == 2.0F && items.front().modelMatrix[5] == 3.0F &&
                        items.front().modelMatrix[10] == 4.0F && items.front().modelMatrix[3] == 2.0F &&
                        items.front().modelMatrix[7] == 3.0F && items.front().modelMatrix[11] == 4.0F,
                    "Ready validation mesh did not produce the expected row-major T*R*S packet.")) {
            return 1;
        }

        auto stale = binding;
        stale.state = asharia::scene_rendering::SceneMeshProductState::Stale;
        const auto staleResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision, .instances = {&instance, 1U}, .productBindings = {&stale, 1U}});
        if (!expect(staleResult.drawItems().empty(), "Stale product emitted a fallback draw.") ||
            !expectDiagnostic(staleResult,
                              asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::StaleProductBinding,
                              kRevision)) {
            return 1;
        }

        const auto missingResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision, .instances = {&instance, 1U}});
        if (!expect(missingResult.drawItems().empty(), "Missing product emitted a fallback draw.") ||
            !expectDiagnostic(missingResult,
                              asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::MissingProductBinding,
                              kRevision)) {
            return 1;
        }

        auto wrongKind = binding;
        wrongKind.asset.expectedType = asharia::asset::makeAssetTypeId("com.asharia.asset.Texture");
        const auto wrongKindResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision,
             .instances = {&instance, 1U},
             .productBindings = {&wrongKind, 1U}});
        if (!expect(wrongKindResult.drawItems().empty(), "Wrong-kind product emitted a fallback draw.") ||
            !expectDiagnostic(
                wrongKindResult,
                asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::WrongProductBindingKind,
                kRevision)) {
            return 1;
        }

        auto invalid = binding;
        invalid.productGeneration = 0U;
        const auto invalidResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision, .instances = {&instance, 1U}, .productBindings = {&invalid, 1U}});
        if (!expect(invalidResult.drawItems().empty(), "Invalid product emitted a fallback draw.") ||
            !expectDiagnostic(invalidResult,
                              asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::InvalidProductBinding,
                              kRevision)) {
            return 1;
        }

        auto vertexOnly = binding;
        vertexOnly.drawItem.vertexCount = 14U;
        vertexOnly.drawItem.indexCount = 0U;
        const auto vertexOnlyResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision,
             .instances = {&instance, 1U},
             .productBindings = {&vertexOnly, 1U}});
        if (!expect(vertexOnlyResult.drawItems().empty(),
                    "Vertex-only product escaped the indexed scene-mesh contract.") ||
            !expectDiagnostic(
                vertexOnlyResult,
                asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::InvalidProductBinding,
                kRevision)) {
            return 1;
        }

        auto mixedDraw = binding;
        mixedDraw.drawItem.vertexCount = 14U;
        const auto mixedDrawResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision,
             .instances = {&instance, 1U},
             .productBindings = {&mixedDraw, 1U}});
        if (!expect(mixedDrawResult.drawItems().empty(),
                    "Mixed vertex/index product escaped the indexed scene-mesh contract.") ||
            !expectDiagnostic(
                mixedDrawResult,
                asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::InvalidProductBinding,
                kRevision)) {
            return 1;
        }

        auto invalidInstance = instance;
        invalidInstance.objectId.bytes[0] = 0xABU;
        auto invalidAsset = assetGuid();
        invalidAsset.bytes[0] = 0x7DU;
        invalidInstance.mesh = asharia::asset::makeAssetReference(
            invalidAsset,
            asharia::scene::kSceneMeshAssetType);
        auto invalidMixedBinding = mixedDraw;
        invalidMixedBinding.asset = invalidInstance.mesh;
        const std::array mixedInstances{instance, invalidInstance};
        const std::array mixedBindings{binding, invalidMixedBinding};
        const auto partialResult = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision,
             .instances = mixedInstances,
             .productBindings = mixedBindings});
        if (!expect(partialResult.drawItems().size() == 1U,
                    "One invalid mesh product suppressed an independent valid draw.") ||
            !expect(partialResult.diagnostics().size() == 1U &&
                        partialResult.diagnostics().front().code ==
                            asharia::scene_rendering::SceneMeshExtractionDiagnosticCode::
                                InvalidProductBinding &&
                        partialResult.diagnostics().front().objectId == invalidInstance.objectId &&
                        partialResult.diagnostics().front().asset == invalidAsset,
                    "Per-item invalid product diagnostic lost its scene context.")) {
            return 1;
        }

        const auto replacement = asharia::scene_rendering::extractSceneMeshDrawList(
            {.revision = kRevision + 1U,
             .instances = {&instance, 1U},
             .productBindings = {&binding, 1U}});
        if (!expect(replacement.revision() == kRevision + 1U && replacement.drawItems().size() == 1U &&
                        replacement.drawItems().data() != extracted.drawItems().data(),
                    "Replacement revision shared a previous draw-list allocation.")) {
            return 1;
        }

        if (!expectModelMatrixContracts()) {
            return 1;
        }

        std::cout << "Scene mesh extraction smoke tests passed.\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
