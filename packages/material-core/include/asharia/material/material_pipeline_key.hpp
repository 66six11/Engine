#pragma once

#include <cstdint>
#include <string>

#include "asharia/core/result.hpp"
#include "asharia/material/material_resource_signature.hpp"

namespace asharia::material {

    enum class MaterialPrimitiveTopology : std::uint32_t {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
    };

    enum class MaterialCullMode : std::uint32_t {
        None,
        Front,
        Back,
    };

    enum class MaterialFrontFace : std::uint32_t {
        CounterClockwise,
        Clockwise,
    };

    enum class MaterialCompareOp : std::uint32_t {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    enum class MaterialBlendMode : std::uint32_t {
        Disabled,
        Alpha,
        Additive,
    };

    struct MaterialShaderIdentity {
        std::string shaderProgram;
        std::uint64_t shaderHash{};
        std::uint64_t reflectionHash{};

        [[nodiscard]] friend bool operator==(const MaterialShaderIdentity&,
                                             const MaterialShaderIdentity&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !shaderProgram.empty() && shaderHash != 0 && reflectionHash != 0;
        }
    };

    struct MaterialRenderStateKey {
        MaterialPrimitiveTopology topology{MaterialPrimitiveTopology::TriangleList};
        MaterialCullMode cullMode{MaterialCullMode::Back};
        MaterialFrontFace frontFace{MaterialFrontFace::CounterClockwise};
        bool depthTestEnabled{true};
        bool depthWriteEnabled{true};
        MaterialCompareOp depthCompare{MaterialCompareOp::LessOrEqual};
        MaterialBlendMode colorBlend{MaterialBlendMode::Disabled};
        std::uint64_t vertexLayoutHash{};
        std::uint64_t colorFormatHash{};
        std::uint64_t depthFormatHash{};

        [[nodiscard]] friend bool operator==(MaterialRenderStateKey,
                                             MaterialRenderStateKey) = default;
    };

    struct MaterialPipelineKey {
        MaterialShaderIdentity shader;
        std::uint64_t resourceSignatureHash{};
        MaterialRenderStateKey renderState;

        [[nodiscard]] friend bool operator==(const MaterialPipelineKey&,
                                             const MaterialPipelineKey&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(shader) && resourceSignatureHash != 0 &&
                   renderState.vertexLayoutHash != 0 && renderState.colorFormatHash != 0;
        }
    };

    [[nodiscard]] VoidResult validateMaterialShaderIdentity(const MaterialShaderIdentity& shader);
    [[nodiscard]] VoidResult validateMaterialRenderStateKey(const MaterialRenderStateKey& state);
    [[nodiscard]] VoidResult validateMaterialPipelineKey(const MaterialPipelineKey& key);
    [[nodiscard]] Result<std::uint64_t> makeMaterialPipelineKeyHash(
        const MaterialPipelineKey& key);

} // namespace asharia::material
