#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia {

    struct ShaderReflectionFileOptions {
        static constexpr std::uint64_t kDefaultMaxBytes = 64ULL * 1024ULL * 1024ULL;
        std::uint64_t maxBytes{kDefaultMaxBytes};
    };

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

    struct ShaderParameterMemberReflection {
        std::string name;
        std::string scalarType;
        std::uint32_t componentCount{};
        std::uint32_t offset{};
        std::uint32_t size{};
        friend bool operator==(const ShaderParameterMemberReflection&,
                               const ShaderParameterMemberReflection&) = default;
    };

    struct ShaderParameterBlockReflection {
        std::uint32_t size{};
        std::vector<ShaderParameterMemberReflection> members;
        friend bool operator==(const ShaderParameterBlockReflection&,
                               const ShaderParameterBlockReflection&) = default;
    };

    struct ShaderDescriptorBindingReflection {
        std::string name;
        std::uint32_t set{};
        std::uint32_t binding{};
        std::string kind;
        std::uint32_t count{};
        std::string category;
        std::string stageVisibility;
        std::optional<ShaderParameterBlockReflection> parameterBlock;
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
        std::optional<Error> error;
    };

    // Parses an already loaded product payload without filesystem IO.
    [[nodiscard]] Result<ShaderReflection>
    parseShaderReflectionJson(std::string_view json, ShaderReflectionFileOptions limits = {});

    [[nodiscard]] Result<ShaderReflection>
    readShaderReflection(const std::filesystem::path& path,
                         ShaderReflectionFileOptions options = {});
    [[nodiscard]] Result<ShaderResourceSignature>
    mergeShaderResourceSignature(std::span<const ShaderReflection> shaders);
    [[nodiscard]] ShaderResourceSignature
    shaderResourceSignature(std::span<const ShaderReflection> shaders);

} // namespace asharia
