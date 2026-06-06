#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::material {

    inline constexpr std::uint32_t kMaxMaterialDescriptorSets = 8;
    inline constexpr std::uint32_t kMaxMaterialBindingsPerSet = 1024;
    inline constexpr std::uint32_t kMaxMaterialBindingArrayCount = 4096;

    enum class MaterialResourceKind : std::uint32_t {
        UniformBuffer,
        StorageBuffer,
        SampledImage,
        Sampler,
        CombinedImageSampler,
    };

    enum class MaterialShaderVisibility : std::uint32_t {
        None = 0,
        Vertex = 1U << 0U,
        Fragment = 1U << 1U,
        Compute = 1U << 2U,
        AllGraphics = (1U << 0U) | (1U << 1U),
        All = (1U << 0U) | (1U << 1U) | (1U << 2U),
    };

    [[nodiscard]] constexpr MaterialShaderVisibility
    operator|(MaterialShaderVisibility lhs, MaterialShaderVisibility rhs) noexcept {
        return static_cast<MaterialShaderVisibility>(static_cast<std::uint32_t>(lhs) |
                                                     static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr MaterialShaderVisibility
    operator&(MaterialShaderVisibility lhs, MaterialShaderVisibility rhs) noexcept {
        return static_cast<MaterialShaderVisibility>(static_cast<std::uint32_t>(lhs) &
                                                     static_cast<std::uint32_t>(rhs));
    }

    struct MaterialResourceBinding {
        std::uint32_t set{};
        std::uint32_t binding{};
        std::string name;
        MaterialResourceKind kind{MaterialResourceKind::UniformBuffer};
        MaterialShaderVisibility visibility{MaterialShaderVisibility::None};
        std::uint32_t arrayCount{1};

        [[nodiscard]] friend bool operator==(const MaterialResourceBinding&,
                                             const MaterialResourceBinding&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !name.empty() && visibility != MaterialShaderVisibility::None &&
                   arrayCount != 0;
        }
    };

    struct MaterialResourceSignature {
        std::vector<MaterialResourceBinding> bindings;

        [[nodiscard]] friend bool operator==(const MaterialResourceSignature&,
                                             const MaterialResourceSignature&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !bindings.empty();
        }
    };

    [[nodiscard]] std::string_view materialResourceKindName(MaterialResourceKind kind) noexcept;
    [[nodiscard]] std::string materialShaderVisibilityName(MaterialShaderVisibility visibility);

    [[nodiscard]] VoidResult
    validateMaterialResourceSignature(const MaterialResourceSignature& signature);
    [[nodiscard]] Result<std::uint64_t>
    makeMaterialResourceSignatureHash(const MaterialResourceSignature& signature);
    [[nodiscard]] VoidResult validateMaterialSignatureCompatibility(
        const MaterialResourceSignature& materialSignature,
        const MaterialResourceSignature& shaderSignature);

} // namespace asharia::material
