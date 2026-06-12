#include "asharia/shader_material_adapter/reflection_to_material_signature.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::shader_material {
    namespace {

        [[nodiscard]] Error adapterError(std::string message) {
            return Error{ErrorDomain::Material, 2, std::move(message)};
        }

        [[nodiscard]] std::string bindingContext(
            const ShaderDescriptorBindingReflection& binding) {
            return "shader descriptor \"" + binding.name + "\" at set " +
                   std::to_string(binding.set) + ", binding " +
                   std::to_string(binding.binding);
        }

        [[nodiscard]] Result<material::MaterialResourceKind>
        materialResourceKind(const ShaderDescriptorBindingReflection& binding) {
            if (binding.kind == "constantBuffer") {
                return material::MaterialResourceKind::UniformBuffer;
            }
            if (binding.kind == "texture") {
                return material::MaterialResourceKind::SampledImage;
            }
            if (binding.kind == "sampler") {
                return material::MaterialResourceKind::Sampler;
            }
            if (binding.kind == "combinedTextureSampler") {
                return material::MaterialResourceKind::CombinedImageSampler;
            }
            if (binding.kind == "typedBuffer" || binding.kind == "mutableTypedBuffer" ||
                binding.kind == "rawBuffer" || binding.kind == "mutableRawBuffer") {
                return material::MaterialResourceKind::StorageBuffer;
            }

            return std::unexpected{adapterError(
                "Shader material adapter cannot map unsupported descriptor kind \"" +
                binding.kind + "\" for " + bindingContext(binding) + ".")};
        }

        [[nodiscard]] Result<material::MaterialShaderVisibility>
        materialShaderVisibility(const ShaderDescriptorBindingReflection& binding) {
            bool vertex{};
            bool fragment{};
            bool compute{};
            std::size_t begin = 0;
            while (begin <= binding.stageVisibility.size()) {
                const std::size_t end = binding.stageVisibility.find('|', begin);
                const std::size_t tokenEnd =
                    end == std::string::npos ? binding.stageVisibility.size() : end;
                const std::string_view token =
                    std::string_view{binding.stageVisibility}.substr(begin, tokenEnd - begin);

                if (token == "vertex") {
                    vertex = true;
                } else if (token == "fragment") {
                    fragment = true;
                } else if (token == "compute") {
                    compute = true;
                } else if (!token.empty()) {
                    return std::unexpected{adapterError(
                        "Shader material adapter found unsupported stage visibility \"" +
                        std::string{token} + "\" for " + bindingContext(binding) + ".")};
                }

                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }

            if (!vertex && !fragment && !compute) {
                return std::unexpected{adapterError(
                    "Shader material adapter requires stage visibility for " +
                    bindingContext(binding) + ".")};
            }
            if (vertex && fragment && compute) {
                return material::MaterialShaderVisibility::All;
            }
            if (vertex && fragment) {
                return material::MaterialShaderVisibility::AllGraphics;
            }
            if (compute && (vertex || fragment)) {
                return std::unexpected{adapterError(
                    "Shader material adapter does not support partial graphics/compute mixed "
                    "stage visibility for " +
                    bindingContext(binding) + ".")};
            }
            if (vertex) {
                return material::MaterialShaderVisibility::Vertex;
            }
            if (fragment) {
                return material::MaterialShaderVisibility::Fragment;
            }

            return material::MaterialShaderVisibility::Compute;
        }

        [[nodiscard]] Result<material::MaterialResourceBinding>
        materialBinding(const ShaderDescriptorBindingReflection& binding) {
            auto kind = materialResourceKind(binding);
            if (!kind) {
                return std::unexpected{std::move(kind.error())};
            }
            auto visibility = materialShaderVisibility(binding);
            if (!visibility) {
                return std::unexpected{std::move(visibility.error())};
            }

            return material::MaterialResourceBinding{
                .set = binding.set,
                .binding = binding.binding,
                .name = binding.name,
                .kind = *kind,
                .visibility = *visibility,
                .arrayCount = binding.count,
            };
        }

        [[nodiscard]] VoidResult validateSignatureCounters(
            const ShaderResourceSignature& shaderSignature) {
            if (shaderSignature.descriptorBindingCount !=
                shaderSignature.descriptorBindings.size()) {
                return std::unexpected{adapterError(
                    "Shader material adapter descriptor binding count mismatch: field says " +
                    std::to_string(shaderSignature.descriptorBindingCount) + " but vector has " +
                    std::to_string(shaderSignature.descriptorBindings.size()) + ".")};
            }
            if (shaderSignature.pushConstantCount != shaderSignature.pushConstants.size()) {
                return std::unexpected{adapterError(
                    "Shader material adapter push constant count mismatch: field says " +
                    std::to_string(shaderSignature.pushConstantCount) + " but vector has " +
                    std::to_string(shaderSignature.pushConstants.size()) + ".")};
            }
            return {};
        }

    } // namespace

    Result<material::MaterialResourceSignature>
    makeMaterialResourceSignature(const ShaderResourceSignature& shaderSignature) {
        if (auto counters = validateSignatureCounters(shaderSignature); !counters) {
            return std::unexpected{std::move(counters.error())};
        }
        if (shaderSignature.descriptorBindings.empty()) {
            if (!shaderSignature.pushConstants.empty()) {
                return std::unexpected{adapterError(
                    "Shader material adapter cannot build a material resource signature from a "
                    "push-constant-only shader signature.")};
            }
            return std::unexpected{adapterError(
                "Shader material adapter requires at least one descriptor binding.")};
        }

        material::MaterialResourceSignature materialSignature;
        materialSignature.bindings.reserve(shaderSignature.descriptorBindings.size());
        for (const ShaderDescriptorBindingReflection& shaderBinding :
             shaderSignature.descriptorBindings) {
            auto binding = materialBinding(shaderBinding);
            if (!binding) {
                return std::unexpected{std::move(binding.error())};
            }
            materialSignature.bindings.push_back(std::move(*binding));
        }

        if (auto validSignature =
                material::validateMaterialResourceSignature(materialSignature);
            !validSignature) {
            return std::unexpected{std::move(validSignature.error())};
        }

        return materialSignature;
    }

    Result<ReflectionMaterialSignature>
    makeReflectionMaterialSignature(const ShaderResourceSignature& shaderSignature) {
        auto signature = makeMaterialResourceSignature(shaderSignature);
        if (!signature) {
            return std::unexpected{std::move(signature.error())};
        }

        auto hash = material::makeMaterialResourceSignatureHash(*signature);
        if (!hash) {
            return std::unexpected{std::move(hash.error())};
        }

        return ReflectionMaterialSignature{
            .signature = std::move(*signature),
            .signatureHash = *hash,
        };
    }

} // namespace asharia::shader_material
