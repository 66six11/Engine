#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vke/core/result.hpp"

namespace vke {

    struct ShaderVertexInputReflection {
        std::string name;
        std::uint32_t location{};
        std::string semantic;
        std::uint32_t semanticIndex{};
        std::string type;
        std::string scalarType;
        std::uint32_t rowCount{};
        std::uint32_t columnCount{};
    };

    struct ShaderDescriptorBindingReflection {
        std::string name;
        std::uint32_t set{};
        std::uint32_t binding{};
        std::string kind;
        std::uint32_t count{};
        std::string category;
        std::string stageVisibility;
    };

    struct ShaderPushConstantReflection {
        std::string name;
        std::uint32_t offset{};
        std::uint32_t size{};
        std::string stageVisibility;
    };

    struct ShaderReflection {
        std::string source;
        std::string entry;
        std::string stage;
        std::string profile;
        std::string target;
        std::vector<ShaderVertexInputReflection> vertexInputs;
        std::vector<ShaderDescriptorBindingReflection> descriptorBindings;
        std::vector<ShaderPushConstantReflection> pushConstants;
        std::uint32_t descriptorBindingCount{};
        std::uint32_t pushConstantCount{};
    };

    struct ShaderResourceSignature {
        std::vector<ShaderDescriptorBindingReflection> descriptorBindings;
        std::vector<ShaderPushConstantReflection> pushConstants;
        std::uint32_t descriptorBindingCount{};
        std::uint32_t pushConstantCount{};
    };

    [[nodiscard]] Result<ShaderReflection> readShaderReflection(const std::filesystem::path& path);
    [[nodiscard]] ShaderResourceSignature
    shaderResourceSignature(std::span<const ShaderReflection> shaders);

} // namespace vke
