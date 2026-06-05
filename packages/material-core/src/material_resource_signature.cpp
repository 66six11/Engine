#include "asharia/material/material_resource_signature.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asharia::material {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
        constexpr std::uint32_t kKnownVisibilityMask =
            static_cast<std::uint32_t>(MaterialShaderVisibility::All);

        [[nodiscard]] Error materialError(std::string message) {
            return Error{ErrorDomain::Material, 1, std::move(message)};
        }

        [[nodiscard]] constexpr std::uint64_t hashByte(std::uint64_t hash,
                                                       std::uint8_t byte) noexcept {
            hash ^= byte;
            hash *= kFnv1a64Prime;
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashUint64(std::uint64_t hash,
                                                         std::uint64_t value) noexcept {
            for (std::uint32_t shift = 0; shift < 64; shift += 8) {
                hash = hashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashText(std::uint64_t hash,
                                                       std::string_view text) noexcept {
            hash = hashUint64(hash, text.size());
            for (const char character : text) {
                hash = hashByte(hash, static_cast<unsigned char>(character));
            }
            return hash;
        }

        [[nodiscard]] bool bindingCoordinateLess(const MaterialResourceBinding& lhs,
                                                 const MaterialResourceBinding& rhs) noexcept {
            if (lhs.set != rhs.set) {
                return lhs.set < rhs.set;
            }
            if (lhs.binding != rhs.binding) {
                return lhs.binding < rhs.binding;
            }
            return lhs.name < rhs.name;
        }

        [[nodiscard]] std::vector<MaterialResourceBinding>
        sortedBindings(std::span<const MaterialResourceBinding> bindings) {
            std::vector<MaterialResourceBinding> sorted{bindings.begin(), bindings.end()};
            std::ranges::sort(sorted, bindingCoordinateLess);
            return sorted;
        }

        [[nodiscard]] bool isKnownResourceKind(MaterialResourceKind kind) noexcept {
            switch (kind) {
            case MaterialResourceKind::UniformBuffer:
            case MaterialResourceKind::StorageBuffer:
            case MaterialResourceKind::SampledImage:
            case MaterialResourceKind::Sampler:
            case MaterialResourceKind::CombinedImageSampler:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool isKnownVisibility(MaterialShaderVisibility visibility) noexcept {
            const auto mask = static_cast<std::uint32_t>(visibility);
            return mask != 0 && (mask & ~kKnownVisibilityMask) == 0;
        }

        [[nodiscard]] VoidResult validateBindingName(std::string_view name,
                                                     std::size_t index) {
            if (name.empty()) {
                return std::unexpected{materialError("Material resource signature binding[" +
                                                     std::to_string(index) +
                                                     "].name is missing.")};
            }

            if (name.find('/') != std::string_view::npos ||
                name.find('\\') != std::string_view::npos) {
                return std::unexpected{materialError("Material resource signature binding \"" +
                                                     std::string{name} +
                                                     "\" must not contain path separators.")};
            }

            return {};
        }

        [[nodiscard]] VoidResult validateBinding(const MaterialResourceBinding& binding,
                                                 std::size_t index) {
            if (binding.set >= kMaxMaterialDescriptorSets) {
                return std::unexpected{materialError("Material resource signature binding[" +
                                                     std::to_string(index) +
                                                     "].set exceeds the supported descriptor set "
                                                     "range.")};
            }

            if (binding.binding >= kMaxMaterialBindingsPerSet) {
                return std::unexpected{materialError("Material resource signature binding[" +
                                                     std::to_string(index) +
                                                     "].binding exceeds the supported binding "
                                                     "range.")};
            }

            if (auto validName = validateBindingName(binding.name, index); !validName) {
                return std::unexpected{std::move(validName.error())};
            }

            if (!isKnownResourceKind(binding.kind)) {
                return std::unexpected{materialError("Material resource signature binding \"" +
                                                     binding.name +
                                                     "\" has an unknown resource kind.")};
            }

            if (!isKnownVisibility(binding.visibility)) {
                return std::unexpected{materialError("Material resource signature binding \"" +
                                                     binding.name +
                                                     "\" has invalid shader visibility.")};
            }

            if (binding.arrayCount == 0 ||
                binding.arrayCount > kMaxMaterialBindingArrayCount) {
                return std::unexpected{materialError("Material resource signature binding \"" +
                                                     binding.name +
                                                     "\" has invalid array count.")};
            }

            return {};
        }

        [[nodiscard]] const MaterialResourceBinding*
        findBindingByCoordinate(std::span<const MaterialResourceBinding> bindings,
                                const MaterialResourceBinding& needle) noexcept {
            for (const MaterialResourceBinding& binding : bindings) {
                if (binding.set == needle.set && binding.binding == needle.binding) {
                    return &binding;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::string coordinateText(const MaterialResourceBinding& binding) {
            return "set " + std::to_string(binding.set) + ", binding " +
                   std::to_string(binding.binding);
        }

        [[nodiscard]] Error mismatchError(std::string message,
                                          const MaterialResourceBinding& shaderBinding) {
            return materialError("Material signature mismatch for shader resource \"" +
                                 shaderBinding.name + "\" at " + coordinateText(shaderBinding) +
                                 ": " + std::move(message));
        }

    } // namespace

    std::string_view materialResourceKindName(MaterialResourceKind kind) noexcept {
        switch (kind) {
        case MaterialResourceKind::UniformBuffer:
            return "uniform-buffer";
        case MaterialResourceKind::StorageBuffer:
            return "storage-buffer";
        case MaterialResourceKind::SampledImage:
            return "sampled-image";
        case MaterialResourceKind::Sampler:
            return "sampler";
        case MaterialResourceKind::CombinedImageSampler:
            return "combined-image-sampler";
        }
        return "unknown";
    }

    std::string materialShaderVisibilityName(MaterialShaderVisibility visibility) {
        if (!isKnownVisibility(visibility)) {
            return "invalid";
        }

        if (visibility == MaterialShaderVisibility::All) {
            return "all";
        }
        if (visibility == MaterialShaderVisibility::AllGraphics) {
            return "graphics";
        }

        std::string text;
        const auto append = [&text](std::string_view token) {
            if (!text.empty()) {
                text += "|";
            }
            text += token;
        };

        if ((visibility & MaterialShaderVisibility::Vertex) != MaterialShaderVisibility::None) {
            append("vertex");
        }
        if ((visibility & MaterialShaderVisibility::Fragment) != MaterialShaderVisibility::None) {
            append("fragment");
        }
        if ((visibility & MaterialShaderVisibility::Compute) != MaterialShaderVisibility::None) {
            append("compute");
        }

        return text;
    }

    VoidResult validateMaterialResourceSignature(const MaterialResourceSignature& signature) {
        if (signature.bindings.empty()) {
            return std::unexpected{
                materialError("Material resource signature must not be empty.")};
        }

        for (std::size_t index = 0; index < signature.bindings.size(); ++index) {
            const MaterialResourceBinding& binding = signature.bindings[index];
            if (auto validBinding = validateBinding(binding, index); !validBinding) {
                return std::unexpected{std::move(validBinding.error())};
            }

            for (std::size_t otherIndex = index + 1; otherIndex < signature.bindings.size();
                 ++otherIndex) {
                const MaterialResourceBinding& other = signature.bindings[otherIndex];
                if (binding.set == other.set && binding.binding == other.binding) {
                    return std::unexpected{
                        materialError("Material resource signature duplicate binding coordinate "
                                      "for \"" +
                                      binding.name + "\" and \"" + other.name + "\" at " +
                                      coordinateText(binding) + ".")};
                }
                if (binding.name == other.name) {
                    return std::unexpected{
                        materialError("Material resource signature duplicate resource name \"" +
                                      binding.name + "\".")};
                }
            }
        }

        return {};
    }

    Result<std::uint64_t>
    makeMaterialResourceSignatureHash(const MaterialResourceSignature& signature) {
        if (auto validSignature = validateMaterialResourceSignature(signature); !validSignature) {
            return std::unexpected{std::move(validSignature.error())};
        }

        std::uint64_t hash = hashUint64(kFnv1a64Offset, signature.bindings.size());
        for (const MaterialResourceBinding& binding : sortedBindings(signature.bindings)) {
            hash = hashUint64(hash, binding.set);
            hash = hashUint64(hash, binding.binding);
            hash = hashText(hash, binding.name);
            hash = hashUint64(hash, static_cast<std::uint64_t>(binding.kind));
            hash = hashUint64(hash, static_cast<std::uint64_t>(binding.visibility));
            hash = hashUint64(hash, binding.arrayCount);
        }

        return hash;
    }

    VoidResult validateMaterialSignatureCompatibility(
        const MaterialResourceSignature& materialSignature,
        const MaterialResourceSignature& shaderSignature) {
        if (auto validMaterial = validateMaterialResourceSignature(materialSignature);
            !validMaterial) {
            return std::unexpected{std::move(validMaterial.error())};
        }
        if (auto validShader = validateMaterialResourceSignature(shaderSignature); !validShader) {
            return std::unexpected{std::move(validShader.error())};
        }

        const std::vector<MaterialResourceBinding> shaderBindings =
            sortedBindings(shaderSignature.bindings);
        const std::vector<MaterialResourceBinding> materialBindings =
            sortedBindings(materialSignature.bindings);

        for (const MaterialResourceBinding& shaderBinding : shaderBindings) {
            const MaterialResourceBinding* materialBinding =
                findBindingByCoordinate(materialBindings, shaderBinding);
            if (materialBinding == nullptr) {
                return std::unexpected{
                    mismatchError("material signature is missing this binding", shaderBinding)};
            }

            if (materialBinding->name != shaderBinding.name) {
                return std::unexpected{mismatchError(
                    "expected name \"" + shaderBinding.name + "\" but material uses \"" +
                        materialBinding->name + "\"",
                    shaderBinding)};
            }

            if (materialBinding->kind != shaderBinding.kind) {
                return std::unexpected{mismatchError(
                    "expected " + std::string{materialResourceKindName(shaderBinding.kind)} +
                        " but material uses " +
                        std::string{materialResourceKindName(materialBinding->kind)},
                    shaderBinding)};
            }

            if (materialBinding->visibility != shaderBinding.visibility) {
                return std::unexpected{mismatchError(
                    "expected visibility " +
                        materialShaderVisibilityName(shaderBinding.visibility) +
                        " but material uses " +
                        materialShaderVisibilityName(materialBinding->visibility),
                    shaderBinding)};
            }

            if (materialBinding->arrayCount != shaderBinding.arrayCount) {
                return std::unexpected{mismatchError(
                    "expected array count " + std::to_string(shaderBinding.arrayCount) +
                        " but material uses " + std::to_string(materialBinding->arrayCount),
                    shaderBinding)};
            }
        }

        for (const MaterialResourceBinding& materialBinding : materialBindings) {
            if (findBindingByCoordinate(shaderBindings, materialBinding) == nullptr) {
                return std::unexpected{
                    materialError("Material signature contains extra resource \"" +
                                  materialBinding.name + "\" at " +
                                  coordinateText(materialBinding) +
                                  " that is not present in the shader signature.")};
            }
        }

        return {};
    }

} // namespace asharia::material
