#include "asharia/material/material_pipeline_key.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::material {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

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

        [[nodiscard]] bool isKnownTopology(MaterialPrimitiveTopology topology) noexcept {
            switch (topology) {
            case MaterialPrimitiveTopology::TriangleList:
            case MaterialPrimitiveTopology::TriangleStrip:
            case MaterialPrimitiveTopology::LineList:
            case MaterialPrimitiveTopology::LineStrip:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool isKnownCullMode(MaterialCullMode cullMode) noexcept {
            switch (cullMode) {
            case MaterialCullMode::None:
            case MaterialCullMode::Front:
            case MaterialCullMode::Back:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool isKnownFrontFace(MaterialFrontFace frontFace) noexcept {
            switch (frontFace) {
            case MaterialFrontFace::CounterClockwise:
            case MaterialFrontFace::Clockwise:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool isKnownCompareOp(MaterialCompareOp compareOp) noexcept {
            switch (compareOp) {
            case MaterialCompareOp::Never:
            case MaterialCompareOp::Less:
            case MaterialCompareOp::Equal:
            case MaterialCompareOp::LessOrEqual:
            case MaterialCompareOp::Greater:
            case MaterialCompareOp::NotEqual:
            case MaterialCompareOp::GreaterOrEqual:
            case MaterialCompareOp::Always:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool isKnownBlendMode(MaterialBlendMode blendMode) noexcept {
            switch (blendMode) {
            case MaterialBlendMode::Disabled:
            case MaterialBlendMode::Alpha:
            case MaterialBlendMode::Additive:
                return true;
            }
            return false;
        }

    } // namespace

    VoidResult validateMaterialShaderIdentity(const MaterialShaderIdentity& shader) {
        if (shader.shaderProgram.empty()) {
            return std::unexpected{
                materialError("Material pipeline key shader program is missing.")};
        }

        if (shader.shaderHash == 0) {
            return std::unexpected{materialError("Material pipeline key shader \"" +
                                                 shader.shaderProgram +
                                                 "\" has an invalid shader hash.")};
        }

        if (shader.reflectionHash == 0) {
            return std::unexpected{materialError("Material pipeline key shader \"" +
                                                 shader.shaderProgram +
                                                 "\" has an invalid reflection hash.")};
        }

        return {};
    }

    VoidResult validateMaterialRenderStateKey(const MaterialRenderStateKey& state) {
        if (!isKnownTopology(state.topology)) {
            return std::unexpected{
                materialError("Material render state has an unknown primitive topology.")};
        }

        if (!isKnownCullMode(state.cullMode)) {
            return std::unexpected{
                materialError("Material render state has an unknown cull mode.")};
        }

        if (!isKnownFrontFace(state.frontFace)) {
            return std::unexpected{
                materialError("Material render state has an unknown front face.")};
        }

        if (!isKnownCompareOp(state.depthCompare)) {
            return std::unexpected{
                materialError("Material render state has an unknown depth compare op.")};
        }

        if (!isKnownBlendMode(state.colorBlend)) {
            return std::unexpected{
                materialError("Material render state has an unknown blend mode.")};
        }

        if (state.vertexLayoutHash == 0) {
            return std::unexpected{
                materialError("Material render state vertex layout hash is invalid.")};
        }

        if (state.colorFormatHash == 0) {
            return std::unexpected{
                materialError("Material render state color format hash is invalid.")};
        }

        if ((state.depthTestEnabled || state.depthWriteEnabled) && state.depthFormatHash == 0) {
            return std::unexpected{
                materialError("Material render state depth format hash is required when depth is "
                              "enabled.")};
        }

        return {};
    }

    VoidResult validateMaterialPipelineKey(const MaterialPipelineKey& key) {
        if (auto validShader = validateMaterialShaderIdentity(key.shader); !validShader) {
            return std::unexpected{std::move(validShader.error())};
        }

        if (key.resourceSignatureHash == 0) {
            return std::unexpected{
                materialError("Material pipeline key resource signature hash is invalid.")};
        }

        if (auto validState = validateMaterialRenderStateKey(key.renderState); !validState) {
            return std::unexpected{std::move(validState.error())};
        }

        return {};
    }

    Result<std::uint64_t> makeMaterialPipelineKeyHash(const MaterialPipelineKey& key) {
        if (auto validKey = validateMaterialPipelineKey(key); !validKey) {
            return std::unexpected{std::move(validKey.error())};
        }

        std::uint64_t hash = hashText(kFnv1a64Offset, key.shader.shaderProgram);
        hash = hashUint64(hash, key.shader.shaderHash);
        hash = hashUint64(hash, key.shader.reflectionHash);
        hash = hashUint64(hash, key.resourceSignatureHash);
        hash = hashUint64(hash, static_cast<std::uint64_t>(key.renderState.topology));
        hash = hashUint64(hash, static_cast<std::uint64_t>(key.renderState.cullMode));
        hash = hashUint64(hash, static_cast<std::uint64_t>(key.renderState.frontFace));
        hash = hashUint64(hash, key.renderState.depthTestEnabled ? 1U : 0U);
        hash = hashUint64(hash, key.renderState.depthWriteEnabled ? 1U : 0U);
        hash = hashUint64(hash, static_cast<std::uint64_t>(key.renderState.depthCompare));
        hash = hashUint64(hash, static_cast<std::uint64_t>(key.renderState.colorBlend));
        hash = hashUint64(hash, key.renderState.vertexLayoutHash);
        hash = hashUint64(hash, key.renderState.colorFormatHash);
        hash = hashUint64(hash, key.renderState.depthFormatHash);
        return hash;
    }

} // namespace asharia::material
