#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/mesh_product/mesh_product_v1.hpp"

namespace asharia::mesh {

    struct MeshProductBuildInputV1 {
        std::vector<MeshVertexP3N3Uv2F32> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<MeshSubmeshV1> submeshes;
        std::vector<MeshMaterialSlotV1> materialSlots;
        MeshAabbV1 bounds{};

        [[nodiscard]] friend bool operator==(const MeshProductBuildInputV1&,
                                             const MeshProductBuildInputV1&) = default;
    };

    struct MeshProductWriteLimits {
        std::uint64_t maxProductBytes{512ULL * 1024ULL * 1024ULL};
        std::uint32_t maxVertices{8U * 1024U * 1024U};
        std::uint32_t maxIndices{24U * 1024U * 1024U};
        std::uint32_t maxSubmeshes{65'536U};
        std::uint32_t maxMaterialSlots{65'536U};

        [[nodiscard]] friend bool operator==(MeshProductWriteLimits,
                                             MeshProductWriteLimits) = default;
    };

    [[nodiscard]] Result<std::vector<std::byte>>
    writeMeshProductV1(const MeshProductBuildInputV1& product, MeshProductWriteLimits limits = {});

    [[nodiscard]] VoidResult writeMeshProductV1File(const std::filesystem::path& path,
                                                    const MeshProductBuildInputV1& product,
                                                    MeshProductWriteLimits limits = {});

} // namespace asharia::mesh
