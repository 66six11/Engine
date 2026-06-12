#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/shader_material_adapter/reflection_to_material_signature.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    asharia::ShaderDescriptorBindingReflection descriptor(
        std::string name,
        std::uint32_t set,
        std::uint32_t binding,
        std::string kind,
        std::uint32_t count,
        std::string stageVisibility) {
        return asharia::ShaderDescriptorBindingReflection{
            .name = std::move(name),
            .set = set,
            .binding = binding,
            .kind = std::move(kind),
            .count = count,
            .category = "descriptor",
            .stageVisibility = std::move(stageVisibility),
        };
    }

    asharia::ShaderResourceSignature basicShaderSignature() {
        asharia::ShaderResourceSignature signature{
            .descriptorBindings =
                {
                    descriptor("materialParams", 0, 0, "constantBuffer", 1,
                               "vertex|fragment"),
                    descriptor("baseColorTexture", 0, 1, "texture", 1, "fragment"),
                    descriptor("baseColorSampler", 0, 2, "sampler", 1, "fragment"),
                    descriptor("computePayload", 1, 0, "typedBuffer", 4, "compute"),
                },
            .pushConstants = {},
        };
        signature.descriptorBindingCount =
            static_cast<std::uint32_t>(signature.descriptorBindings.size());
        signature.pushConstantCount = 0;
        return signature;
    }

    bool expectInvalid(const asharia::ShaderResourceSignature& signature,
                       std::string_view expectedToken) {
        auto adapted = asharia::shader_material::makeReflectionMaterialSignature(signature);
        if (adapted) {
            logFailure("Reflection adapter smoke accepted an invalid shader signature.");
            return false;
        }
        if (adapted.error().domain != asharia::ErrorDomain::Material ||
            !messageContains(adapted.error().message, expectedToken)) {
            logFailure(std::string{"Reflection adapter smoke produced incomplete diagnostic: "} +
                       adapted.error().message);
            return false;
        }
        return true;
    }

    bool smokePositiveMapping() {
        auto adapted =
            asharia::shader_material::makeReflectionMaterialSignature(basicShaderSignature());
        if (!adapted) {
            logFailure(adapted.error().message);
            return false;
        }
        if (adapted->signatureHash == 0 || adapted->signature.bindings.size() != 4) {
            logFailure("Reflection adapter smoke produced incomplete material signature.");
            return false;
        }

        const auto& params = adapted->signature.bindings[0];
        const auto& texture = adapted->signature.bindings[1];
        const auto& sampler = adapted->signature.bindings[2];
        const auto& computePayload = adapted->signature.bindings[3];
        if (params.kind != asharia::material::MaterialResourceKind::UniformBuffer ||
            params.visibility != asharia::material::MaterialShaderVisibility::AllGraphics ||
            texture.kind != asharia::material::MaterialResourceKind::SampledImage ||
            sampler.kind != asharia::material::MaterialResourceKind::Sampler ||
            computePayload.kind != asharia::material::MaterialResourceKind::StorageBuffer ||
            computePayload.visibility != asharia::material::MaterialShaderVisibility::Compute ||
            computePayload.arrayCount != 4) {
            logFailure("Reflection adapter smoke mapped descriptor facts incorrectly.");
            return false;
        }

        auto reordered = basicShaderSignature();
        std::swap(reordered.descriptorBindings[0], reordered.descriptorBindings[3]);
        auto reorderedAdapted =
            asharia::shader_material::makeReflectionMaterialSignature(reordered);
        if (!reorderedAdapted || reorderedAdapted->signatureHash != adapted->signatureHash) {
            logFailure("Reflection adapter smoke produced order-dependent signature hashes.");
            return false;
        }

        auto combined = basicShaderSignature();
        combined.descriptorBindings = {
            descriptor("albedo", 0, 0, "combinedTextureSampler", 1, "fragment"),
        };
        combined.descriptorBindingCount = 1;
        auto combinedAdapted =
            asharia::shader_material::makeReflectionMaterialSignature(combined);
        if (!combinedAdapted ||
            combinedAdapted->signature.bindings.front().kind !=
                asharia::material::MaterialResourceKind::CombinedImageSampler) {
            logFailure("Reflection adapter smoke failed combined texture/sampler mapping.");
            return false;
        }

        auto allStages = basicShaderSignature();
        allStages.descriptorBindings = {
            descriptor("globalParams", 0, 0, "constantBuffer", 1,
                       "vertex|fragment|compute"),
        };
        allStages.descriptorBindingCount = 1;
        auto allStagesAdapted =
            asharia::shader_material::makeReflectionMaterialSignature(allStages);
        if (!allStagesAdapted ||
            allStagesAdapted->signature.bindings.front().visibility !=
                asharia::material::MaterialShaderVisibility::All) {
            logFailure("Reflection adapter smoke failed all-stage visibility mapping.");
            return false;
        }

        std::cout << "Reflection adapter signature hash: " << adapted->signatureHash << '\n';
        return true;
    }

    bool smokeNegativeDiagnostics() {
        auto unsupportedKind = basicShaderSignature();
        unsupportedKind.descriptorBindings[1].kind = "mutableTexture";

        auto invalidVisibility = basicShaderSignature();
        invalidVisibility.descriptorBindings[0].stageVisibility = "raygen";

        auto missingVisibility = basicShaderSignature();
        missingVisibility.descriptorBindings[0].stageVisibility.clear();

        auto partialGraphicsComputeVisibility = basicShaderSignature();
        partialGraphicsComputeVisibility.descriptorBindings[0].stageVisibility =
            "vertex|compute";

        auto zeroCount = basicShaderSignature();
        zeroCount.descriptorBindings[0].count = 0;

        auto duplicateCoordinate = basicShaderSignature();
        duplicateCoordinate.descriptorBindings[2].binding =
            duplicateCoordinate.descriptorBindings[1].binding;

        auto duplicateName = basicShaderSignature();
        duplicateName.descriptorBindings[2].name = duplicateName.descriptorBindings[1].name;

        auto countMismatch = basicShaderSignature();
        countMismatch.descriptorBindingCount += 1;

        asharia::ShaderResourceSignature empty{};

        asharia::ShaderResourceSignature pushConstantOnly{
            .descriptorBindings = {},
            .pushConstants =
                {
                    asharia::ShaderPushConstantReflection{
                        .name = "Params",
                        .offset = 0,
                        .size = 16,
                        .stageVisibility = "vertex",
                    },
                },
            .descriptorBindingCount = 0,
            .pushConstantCount = 1,
        };

        return expectInvalid(unsupportedKind, "unsupported descriptor kind") &&
               expectInvalid(invalidVisibility, "unsupported stage visibility") &&
               expectInvalid(missingVisibility, "requires stage visibility") &&
               expectInvalid(partialGraphicsComputeVisibility,
                             "partial graphics/compute mixed") &&
               expectInvalid(zeroCount, "invalid array count") &&
               expectInvalid(duplicateCoordinate, "duplicate binding coordinate") &&
               expectInvalid(duplicateName, "duplicate resource name") &&
               expectInvalid(countMismatch, "descriptor binding count mismatch") &&
               expectInvalid(empty, "at least one descriptor binding") &&
               expectInvalid(pushConstantOnly, "push-constant-only");
    }

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        if (!smokePositiveMapping() || !smokeNegativeDiagnostics()) {
            return EXIT_FAILURE;
        }
    } catch (const std::exception& exception) {
        logFailure(exception.what());
        return EXIT_FAILURE;
    } catch (...) {
        logFailure("Reflection adapter smoke caught an unknown exception.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
