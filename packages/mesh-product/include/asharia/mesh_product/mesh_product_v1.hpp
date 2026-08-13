#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/core/result.hpp"

namespace asharia::mesh {

    inline constexpr std::uint32_t kMeshProductFormatVersionV1 = 1U;
    inline constexpr std::uint32_t kMeshVertexStrideP3N3Uv2F32 = 32U;

    enum class MeshVertexFormat : std::uint32_t {
        P3N3Uv2F32 = 1U,
    };

    enum class MeshProductErrorCode : int {
        InvalidArgument = 1,
        InvalidLimits = 2,
        ByteBudgetExceeded = 3,
        FileReadFailed = 4,
        FileWriteFailed = 5,
        Truncated = 6,
        InvalidMagic = 7,
        UnsupportedVersion = 8,
        UnsupportedEndianness = 9,
        UnsupportedVertexFormat = 10,
        InvalidHeader = 11,
        InvalidSectionLayout = 12,
        CountLimitExceeded = 13,
        NonFiniteValue = 14,
        InvalidBounds = 15,
        InvalidIndex = 16,
        InvalidSubmesh = 17,
        InvalidMaterialSlot = 18,
        NonCanonicalEncoding = 19,
    };

    struct MeshVertexP3N3Uv2F32 {
        float positionX{};
        float positionY{};
        float positionZ{};
        float normalX{};
        float normalY{};
        float normalZ{};
        float uvX{};
        float uvY{};

        [[nodiscard]] friend bool operator==(MeshVertexP3N3Uv2F32, MeshVertexP3N3Uv2F32) = default;
    };

    struct MeshAabbV1 {
        float minX{};
        float minY{};
        float minZ{};
        float maxX{};
        float maxY{};
        float maxZ{};

        [[nodiscard]] friend bool operator==(MeshAabbV1, MeshAabbV1) = default;
    };

    struct MeshSubmeshV1 {
        std::uint32_t firstIndex{};
        std::uint32_t indexCount{};
        std::uint32_t materialSlot{};

        [[nodiscard]] friend bool operator==(MeshSubmeshV1, MeshSubmeshV1) = default;
    };

    // A zero GUID is an explicitly unbound material slot. Slot order is part of the product.
    struct MeshMaterialSlotV1 {
        asset::AssetGuid materialAsset{};

        [[nodiscard]] friend bool operator==(MeshMaterialSlotV1, MeshMaterialSlotV1) = default;
    };

    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    static_assert(std::numeric_limits<float>::digits == 24);
    static_assert(std::is_standard_layout_v<MeshVertexP3N3Uv2F32>);
    static_assert(std::is_trivially_copyable_v<MeshVertexP3N3Uv2F32>);
    static_assert(sizeof(MeshVertexP3N3Uv2F32) == kMeshVertexStrideP3N3Uv2F32);
    static_assert(offsetof(MeshVertexP3N3Uv2F32, positionX) == 0U);
    static_assert(offsetof(MeshVertexP3N3Uv2F32, normalX) == 12U);
    static_assert(offsetof(MeshVertexP3N3Uv2F32, uvX) == 24U);
    static_assert(asset::AssetGuid{}.bytes.size() == 16U);

    struct MeshProductReadLimits {
        std::uint64_t maxProductBytes{512ULL * 1024ULL * 1024ULL};
        std::uint32_t maxVertices{8U * 1024U * 1024U};
        std::uint32_t maxIndices{24U * 1024U * 1024U};
        std::uint32_t maxSubmeshes{65'536U};
        std::uint32_t maxMaterialSlots{65'536U};

        [[nodiscard]] friend bool operator==(MeshProductReadLimits,
                                             MeshProductReadLimits) = default;
    };

    class MeshProductV1 final {
    public:
        MeshProductV1(const MeshProductV1&) = default;
        MeshProductV1& operator=(const MeshProductV1&) = default;
        MeshProductV1(MeshProductV1&&) noexcept = default;
        MeshProductV1& operator=(MeshProductV1&&) noexcept = default;
        ~MeshProductV1() = default;

        [[nodiscard]] MeshVertexFormat vertexFormat() const noexcept;
        [[nodiscard]] MeshAabbV1 bounds() const noexcept;
        [[nodiscard]] std::span<const MeshVertexP3N3Uv2F32> vertices() const noexcept;
        [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
        [[nodiscard]] std::span<const MeshSubmeshV1> submeshes() const noexcept;
        [[nodiscard]] std::span<const MeshMaterialSlotV1> materialSlots() const noexcept;

    private:
        friend Result<MeshProductV1> readMeshProductV1(std::span<const std::byte>,
                                                       MeshProductReadLimits);

        MeshProductV1(std::vector<MeshVertexP3N3Uv2F32> vertices,
                      std::vector<std::uint32_t> indices, std::vector<MeshSubmeshV1> submeshes,
                      std::vector<MeshMaterialSlotV1> materialSlots, MeshAabbV1 bounds);

        std::vector<MeshVertexP3N3Uv2F32> vertices_;
        std::vector<std::uint32_t> indices_;
        std::vector<MeshSubmeshV1> submeshes_;
        std::vector<MeshMaterialSlotV1> materialSlots_;
        MeshAabbV1 bounds_{};
    };

    [[nodiscard]] Result<MeshProductV1> readMeshProductV1(std::span<const std::byte> bytes,
                                                          MeshProductReadLimits limits = {});

    [[nodiscard]] Result<MeshProductV1> readMeshProductV1File(const std::filesystem::path& path,
                                                              MeshProductReadLimits limits = {});

    [[nodiscard]] const char* meshProductErrorCodeName(MeshProductErrorCode code) noexcept;

} // namespace asharia::mesh
