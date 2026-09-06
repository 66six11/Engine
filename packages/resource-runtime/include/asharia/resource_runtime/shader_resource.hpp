#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asharia/asset_core/asset_product.hpp"
#include "asharia/shader_slang/reflection.hpp"

namespace asharia::resource {
    enum class ShaderResourceError : int {
        InvalidRequest = 1,
        IdentityMismatch,
        EntryUnavailable,
        InvalidCompiledEntry,
        BudgetExceeded,
    };

    struct ShaderResourceReadLimits {
        std::uint64_t maxProductBytes{64ULL * 1024ULL * 1024ULL};
        std::uint64_t maxSpirvBytes{4ULL * 1024ULL * 1024ULL};
        std::uint64_t maxReflectionBytes{4ULL * 1024ULL * 1024ULL};
        std::uint32_t maxEntries{64};
    };

    // The host selects a manifest record; runtime checks it against the authored request.
    // IO may run on a worker. This operation has no mutable store or GPU state to publish.
    struct ShaderResourceReadRequest {
        asset::AssetGuid guid;
        asset::AssetProductRecord product;
        std::filesystem::path artifactRoot;
        std::string targetProfile;
        std::string stableTypeId;
        std::string passName;
        std::string stage;
        std::string shaderProfile;
    };

    struct ShaderResourceData {
        asset::AssetProductKey productKey;
        std::uint64_t productHash{};
        std::string stableTypeId;
        std::string passName;
        std::vector<std::uint32_t> spirv;
        ShaderReflection reflection;
    };

    // Consumes the trusted cook pipeline's validated bytecode/reflection pair. Does not compile,
    // execute spirv-val, read source files or write temporary reflection files.
    [[nodiscard]] Result<ShaderResourceData>
    readShaderResource(const ShaderResourceReadRequest& request,
                       ShaderResourceReadLimits limits = {});
} // namespace asharia::resource
