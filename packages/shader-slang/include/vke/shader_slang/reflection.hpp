#pragma once

#include <cstdint>
#include <filesystem>
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

    struct ShaderReflection {
        std::string source;
        std::string entry;
        std::string stage;
        std::string profile;
        std::string target;
        std::vector<ShaderVertexInputReflection> vertexInputs;
        std::uint32_t descriptorBindingCount{};
        std::uint32_t pushConstantCount{};
    };

    [[nodiscard]] Result<ShaderReflection> readShaderReflection(const std::filesystem::path& path);

} // namespace vke
