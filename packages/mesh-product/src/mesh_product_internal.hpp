#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/mesh_product/mesh_product_v1.hpp"

namespace asharia::mesh::detail {

    inline constexpr std::array<std::byte, 8> kMeshProductMagic{
        std::byte{'A'}, std::byte{'S'}, std::byte{'H'}, std::byte{'M'},
        std::byte{'E'}, std::byte{'S'}, std::byte{'H'}, std::byte{'1'},
    };
    inline constexpr std::uint32_t kLittleEndianMarker = 0x0102'0304U;
    inline constexpr std::uint64_t kHeaderBytes = 128U;
    inline constexpr std::uint64_t kSectionAlignment = 16U;
    inline constexpr std::uint64_t kSubmeshRecordBytes = 16U;
    inline constexpr std::uint64_t kMaterialSlotRecordBytes = 16U;

    static_assert(kHeaderBytes % kSectionAlignment == 0U);
    static_assert(kMeshVertexStrideP3N3Uv2F32 % kSectionAlignment == 0U);
    static_assert(kSubmeshRecordBytes % kSectionAlignment == 0U);
    static_assert(kMaterialSlotRecordBytes == asset::AssetGuid{}.bytes.size());

    struct DecodedHeaderV1 {
        std::uint32_t vertexCount{};
        std::uint32_t indexCount{};
        std::uint32_t submeshCount{};
        std::uint32_t materialSlotCount{};
        std::uint64_t vertexOffset{};
        std::uint64_t indexOffset{};
        std::uint64_t submeshOffset{};
        std::uint64_t materialSlotOffset{};
        std::uint64_t fileSize{};
        MeshAabbV1 bounds{};
    };

    [[nodiscard]] Error meshProductError(MeshProductErrorCode code, std::string message);
    [[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

    [[nodiscard]] bool checkedAdd(std::uint64_t left, std::uint64_t right,
                                  std::uint64_t& result) noexcept;
    [[nodiscard]] bool checkedMultiply(std::uint64_t left, std::uint64_t right,
                                       std::uint64_t& result) noexcept;
    [[nodiscard]] bool alignUp(std::uint64_t value, std::uint64_t alignment,
                               std::uint64_t& result) noexcept;

    [[nodiscard]] bool validLimits(std::uint64_t maxProductBytes, std::uint32_t maxVertices,
                                   std::uint32_t maxIndices, std::uint32_t maxSubmeshes,
                                   std::uint32_t maxMaterialSlots) noexcept;

    [[nodiscard]] bool isFinite(MeshAabbV1 bounds) noexcept;
    [[nodiscard]] bool hasOrderedBounds(MeshAabbV1 bounds) noexcept;
    [[nodiscard]] MeshAabbV1 computeBounds(std::span<const MeshVertexP3N3Uv2F32> vertices);
    [[nodiscard]] bool equalBounds(MeshAabbV1 left, MeshAabbV1 right) noexcept;

    [[nodiscard]] Result<void> validateMeshFacts(std::span<const MeshVertexP3N3Uv2F32> vertices,
                                                 std::span<const std::uint32_t> indices,
                                                 std::span<const MeshSubmeshV1> submeshes,
                                                 std::span<const MeshMaterialSlotV1> materialSlots,
                                                 MeshAabbV1 bounds);

    [[nodiscard]] Result<std::uint64_t>
    encodedSize(std::uint32_t vertexCount, std::uint32_t indexCount, std::uint32_t submeshCount,
                std::uint32_t materialSlotCount, DecodedHeaderV1& layout);

    [[nodiscard]] std::uint32_t readU32(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept;
    [[nodiscard]] std::uint64_t readU64(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept;
    [[nodiscard]] float readF32(std::span<const std::byte> bytes, std::size_t offset) noexcept;

    void writeU32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept;
    void writeU64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) noexcept;
    void writeF32(std::span<std::byte> bytes, std::size_t offset, float value) noexcept;

} // namespace asharia::mesh::detail
