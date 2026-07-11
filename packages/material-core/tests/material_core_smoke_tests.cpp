#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/material/material_pipeline_key.hpp"
#include "asharia/material/material_resource_signature.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    asharia::material::MaterialResourceBinding textureBinding() {
        return asharia::material::MaterialResourceBinding{
            .set = 0,
            .binding = 1,
            .name = "baseColorTexture",
            .kind = asharia::material::MaterialResourceKind::SampledImage,
            .visibility = asharia::material::MaterialShaderVisibility::Fragment,
            .arrayCount = 1,
        };
    }

    asharia::material::MaterialResourceBinding samplerBinding() {
        return asharia::material::MaterialResourceBinding{
            .set = 0,
            .binding = 2,
            .name = "baseColorSampler",
            .kind = asharia::material::MaterialResourceKind::Sampler,
            .visibility = asharia::material::MaterialShaderVisibility::Fragment,
            .arrayCount = 1,
        };
    }

    asharia::material::MaterialResourceBinding paramsBinding() {
        return asharia::material::MaterialResourceBinding{
            .set = 0,
            .binding = 0,
            .name = "materialParams",
            .kind = asharia::material::MaterialResourceKind::UniformBuffer,
            .visibility = asharia::material::MaterialShaderVisibility::AllGraphics,
            .arrayCount = 1,
        };
    }

    asharia::material::MaterialResourceSignature basicSignature() {
        return asharia::material::MaterialResourceSignature{
            .bindings = {paramsBinding(), textureBinding(), samplerBinding()},
        };
    }

    bool expectInvalidSignature(const asharia::material::MaterialResourceSignature& signature,
                                std::string_view expected) {
        auto validated = asharia::material::validateMaterialResourceSignature(signature);
        if (validated) {
            logFailure("Material signature smoke accepted an invalid signature.");
            return false;
        }

        if (validated.error().domain != asharia::ErrorDomain::Material ||
            !messageContains(validated.error().message, expected)) {
            logFailure(std::string{"Material signature smoke produced an incomplete diagnostic: "} +
                       validated.error().message);
            return false;
        }

        return true;
    }

    bool smokeResourceSignatureValidationAndHash() {
        asharia::material::MaterialResourceSignature signature = basicSignature();
        auto hash = asharia::material::makeMaterialResourceSignatureHash(signature);
        if (!hash || *hash == 0) {
            logFailure("Material signature smoke failed to hash a valid signature.");
            return false;
        }

        asharia::material::MaterialResourceSignature reordered{
            .bindings = {samplerBinding(), paramsBinding(), textureBinding()},
        };
        auto reorderedHash = asharia::material::makeMaterialResourceSignatureHash(reordered);
        if (!reorderedHash || *reorderedHash != *hash) {
            logFailure("Material signature smoke saw order-dependent signature hashing.");
            return false;
        }

        auto duplicateCoordinate = basicSignature();
        duplicateCoordinate.bindings[2].binding = duplicateCoordinate.bindings[1].binding;
        if (!expectInvalidSignature({}, "must not be empty") ||
            !expectInvalidSignature(
                asharia::material::MaterialResourceSignature{
                    .bindings = {asharia::material::MaterialResourceBinding{}},
                },
                "name is missing") ||
            !expectInvalidSignature(
                asharia::material::MaterialResourceSignature{
                    .bindings = {asharia::material::MaterialResourceBinding{
                        .set = asharia::material::kMaxMaterialDescriptorSets,
                        .binding = 0,
                        .name = "tooHighSet",
                        .kind = asharia::material::MaterialResourceKind::UniformBuffer,
                        .visibility = asharia::material::MaterialShaderVisibility::Vertex,
                        .arrayCount = 1,
                    }},
                },
                "descriptor set range") ||
            !expectInvalidSignature(duplicateCoordinate, "duplicate binding coordinate")) {
            return false;
        }

        auto duplicateName = basicSignature();
        duplicateName.bindings[2].name = duplicateName.bindings[1].name;
        if (!expectInvalidSignature(duplicateName, "duplicate resource name")) {
            return false;
        }

        auto invalidKind = basicSignature();
        // The analyzer flags out-of-range enum casts; this test deliberately crosses that
        // boundary to exercise the public validator's unknown-kind diagnostic.
        invalidKind.bindings[0].kind =
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            static_cast<asharia::material::MaterialResourceKind>(999U);
        auto invalidVisibility = basicSignature();
        // The analyzer flags out-of-range enum casts; this test deliberately constructs an
        // invalid visibility bit so the validator's rejection path remains covered.
        invalidVisibility.bindings[0].visibility =
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            static_cast<asharia::material::MaterialShaderVisibility>(1U << 8U);
        auto invalidCount = basicSignature();
        invalidCount.bindings[0].arrayCount = 0;
        if (!expectInvalidSignature(invalidKind, "unknown resource kind") ||
            !expectInvalidSignature(invalidVisibility, "invalid shader visibility") ||
            !expectInvalidSignature(invalidCount, "invalid array count")) {
            return false;
        }

        std::cout << "Material signature hash: " << *hash << '\n';
        return true;
    }

    bool expectIncompatible(const asharia::material::MaterialResourceSignature& materialSignature,
                            const asharia::material::MaterialResourceSignature& shaderSignature,
                            std::string_view expected) {
        auto compatible = asharia::material::validateMaterialSignatureCompatibility(
            materialSignature, shaderSignature);
        if (compatible) {
            logFailure("Material compatibility smoke accepted an incompatible signature pair.");
            return false;
        }

        if (compatible.error().domain != asharia::ErrorDomain::Material ||
            !messageContains(compatible.error().message, expected)) {
            logFailure(
                std::string{"Material compatibility smoke produced an incomplete diagnostic: "} +
                compatible.error().message);
            return false;
        }

        return true;
    }

    bool smokeSignatureCompatibility() {
        if (!asharia::material::validateMaterialSignatureCompatibility(basicSignature(),
                                                                       basicSignature())) {
            logFailure("Material compatibility smoke rejected matching signatures.");
            return false;
        }

        auto missing = basicSignature();
        missing.bindings.pop_back();

        auto kindMismatch = basicSignature();
        kindMismatch.bindings[1].kind =
            asharia::material::MaterialResourceKind::CombinedImageSampler;

        auto nameMismatch = basicSignature();
        nameMismatch.bindings[0].name = "wrongParams";

        auto visibilityMismatch = basicSignature();
        visibilityMismatch.bindings[0].visibility =
            asharia::material::MaterialShaderVisibility::Vertex;

        auto extra = basicSignature();
        extra.bindings.push_back(asharia::material::MaterialResourceBinding{
            .set = 1,
            .binding = 0,
            .name = "unusedTexture",
            .kind = asharia::material::MaterialResourceKind::SampledImage,
            .visibility = asharia::material::MaterialShaderVisibility::Fragment,
            .arrayCount = 1,
        });

        if (!expectIncompatible(missing, basicSignature(), "missing this binding") ||
            !expectIncompatible(kindMismatch, basicSignature(), "expected sampled") ||
            !expectIncompatible(nameMismatch, basicSignature(), "expected name") ||
            !expectIncompatible(visibilityMismatch, basicSignature(), "expected visibility") ||
            !expectIncompatible(extra, basicSignature(), "extra resource")) {
            return false;
        }

        std::cout << "Material signature compatibility diagnostics validated.\n";
        return true;
    }

    asharia::material::MaterialPipelineKey basicPipelineKey(std::uint64_t signatureHash) {
        return asharia::material::MaterialPipelineKey{
            .shader =
                asharia::material::MaterialShaderIdentity{
                    .shaderProgram = "builtin.forward.material",
                    .shaderHash = 0x1010U,
                    .reflectionHash = 0x2020U,
                },
            .resourceSignatureHash = signatureHash,
            .renderState =
                asharia::material::MaterialRenderStateKey{
                    .topology = asharia::material::MaterialPrimitiveTopology::TriangleList,
                    .cullMode = asharia::material::MaterialCullMode::Back,
                    .frontFace = asharia::material::MaterialFrontFace::CounterClockwise,
                    .depthTestEnabled = true,
                    .depthWriteEnabled = true,
                    .depthCompare = asharia::material::MaterialCompareOp::LessOrEqual,
                    .colorBlend = asharia::material::MaterialBlendMode::Disabled,
                    .vertexLayoutHash = 0x3030U,
                    .colorFormatHash = 0x4040U,
                    .depthFormatHash = 0x5050U,
                },
        };
    }

    bool expectInvalidPipeline(const asharia::material::MaterialPipelineKey& key,
                               std::string_view expected) {
        auto hash = asharia::material::makeMaterialPipelineKeyHash(key);
        if (hash) {
            logFailure("Material pipeline key smoke accepted an invalid key.");
            return false;
        }

        if (hash.error().domain != asharia::ErrorDomain::Material ||
            !messageContains(hash.error().message, expected)) {
            logFailure(
                std::string{"Material pipeline key smoke produced an incomplete diagnostic: "} +
                hash.error().message);
            return false;
        }

        return true;
    }

    bool smokePipelineKey() {
        auto signatureHash = asharia::material::makeMaterialResourceSignatureHash(basicSignature());
        if (!signatureHash) {
            logFailure(signatureHash.error().message);
            return false;
        }

        auto key = basicPipelineKey(*signatureHash);
        auto keyHash = asharia::material::makeMaterialPipelineKeyHash(key);
        if (!keyHash || *keyHash == 0) {
            logFailure("Material pipeline key smoke failed to hash a valid key.");
            return false;
        }

        auto changedShader = key;
        changedShader.shader.reflectionHash = 0x2021U;
        auto changedHash = asharia::material::makeMaterialPipelineKeyHash(changedShader);
        if (!changedHash || *changedHash == *keyHash) {
            logFailure("Material pipeline key smoke did not include shader identity.");
            return false;
        }

        auto changedState = key;
        changedState.renderState.colorBlend = asharia::material::MaterialBlendMode::Alpha;
        auto changedStateHash = asharia::material::makeMaterialPipelineKeyHash(changedState);
        if (!changedStateHash || *changedStateHash == *keyHash) {
            logFailure("Material pipeline key smoke did not include render state.");
            return false;
        }

        auto missingShader = key;
        missingShader.shader.shaderProgram.clear();
        auto missingSignature = key;
        missingSignature.resourceSignatureHash = 0;
        auto invalidTopology = key;
        // The analyzer flags out-of-range enum casts; this test deliberately constructs an
        // unknown topology to verify fail-early validation.
        invalidTopology.renderState.topology =
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            static_cast<asharia::material::MaterialPrimitiveTopology>(999U);
        auto missingDepthFormat = key;
        missingDepthFormat.renderState.depthFormatHash = 0;
        auto missingColorFormat = key;
        missingColorFormat.renderState.colorFormatHash = 0;

        if (!expectInvalidPipeline(missingShader, "shader program is missing") ||
            !expectInvalidPipeline(missingSignature, "resource signature hash is invalid") ||
            !expectInvalidPipeline(invalidTopology, "unknown primitive topology") ||
            !expectInvalidPipeline(missingDepthFormat, "depth format hash is required") ||
            !expectInvalidPipeline(missingColorFormat, "color format hash is invalid")) {
            return false;
        }

        std::cout << "Material pipeline key hash: " << *keyHash << '\n';
        return true;
    }

} // namespace

int main() {
    try {
        if (!smokeResourceSignatureValidationAndHash() || !smokeSignatureCompatibility() ||
            !smokePipelineKey()) {
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    } catch (...) {
        return EXIT_FAILURE;
    }
}
