#include "mesh_product_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace asharia::mesh::detail {

    Error meshProductError(MeshProductErrorCode code, std::string message) {
        return Error{ErrorDomain::Asset, static_cast<int>(code),
                     "Mesh Product v1 " + std::move(message)};
    }

    std::string pathToUtf8(const std::filesystem::path& path) {
        const std::u8string utf8 = path.generic_u8string();
        std::string text;
        text.reserve(utf8.size());
        for (const char8_t character : utf8) {
            text.push_back(static_cast<char>(character));
        }
        return text;
    }

    bool checkedAdd(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
        if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
            return false;
        }
        result = left + right;
        return true;
    }

    bool checkedMultiply(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
        if (left != 0U && right > (std::numeric_limits<std::uint64_t>::max)() / left) {
            return false;
        }
        result = left * right;
        return true;
    }

    bool alignUp(std::uint64_t value, std::uint64_t alignment, std::uint64_t& result) noexcept {
        if (alignment == 0U) {
            return false;
        }
        const std::uint64_t remainder = value % alignment;
        if (remainder == 0U) {
            result = value;
            return true;
        }
        return checkedAdd(value, alignment - remainder, result);
    }

    bool validLimits(std::uint64_t maxProductBytes, std::uint32_t maxVertices,
                     std::uint32_t maxIndices, std::uint32_t maxSubmeshes,
                     std::uint32_t maxMaterialSlots) noexcept {
        return maxProductBytes >= kHeaderBytes && maxVertices > 0U && maxIndices > 0U &&
               maxSubmeshes > 0U && maxMaterialSlots > 0U;
    }

    bool isFinite(MeshAabbV1 bounds) noexcept {
        return std::isfinite(bounds.minX) && std::isfinite(bounds.minY) &&
               std::isfinite(bounds.minZ) && std::isfinite(bounds.maxX) &&
               std::isfinite(bounds.maxY) && std::isfinite(bounds.maxZ);
    }

    bool hasOrderedBounds(MeshAabbV1 bounds) noexcept {
        return bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY &&
               bounds.minZ <= bounds.maxZ;
    }

    MeshAabbV1 computeBounds(std::span<const MeshVertexP3N3Uv2F32> vertices) {
        MeshAabbV1 result{
            .minX = vertices.front().positionX,
            .minY = vertices.front().positionY,
            .minZ = vertices.front().positionZ,
            .maxX = vertices.front().positionX,
            .maxY = vertices.front().positionY,
            .maxZ = vertices.front().positionZ,
        };
        for (const MeshVertexP3N3Uv2F32& vertex : vertices.subspan(1U)) {
            result.minX = (std::min)(result.minX, vertex.positionX);
            result.minY = (std::min)(result.minY, vertex.positionY);
            result.minZ = (std::min)(result.minZ, vertex.positionZ);
            result.maxX = (std::max)(result.maxX, vertex.positionX);
            result.maxY = (std::max)(result.maxY, vertex.positionY);
            result.maxZ = (std::max)(result.maxZ, vertex.positionZ);
        }
        return result;
    }

    bool equalBounds(MeshAabbV1 left, MeshAabbV1 right) noexcept {
        return std::bit_cast<std::uint32_t>(left.minX) ==
                   std::bit_cast<std::uint32_t>(right.minX) &&
               std::bit_cast<std::uint32_t>(left.minY) ==
                   std::bit_cast<std::uint32_t>(right.minY) &&
               std::bit_cast<std::uint32_t>(left.minZ) ==
                   std::bit_cast<std::uint32_t>(right.minZ) &&
               std::bit_cast<std::uint32_t>(left.maxX) ==
                   std::bit_cast<std::uint32_t>(right.maxX) &&
               std::bit_cast<std::uint32_t>(left.maxY) ==
                   std::bit_cast<std::uint32_t>(right.maxY) &&
               std::bit_cast<std::uint32_t>(left.maxZ) == std::bit_cast<std::uint32_t>(right.maxZ);
    }

    namespace {

        [[nodiscard]] Result<void>
        validateVertices(std::span<const MeshVertexP3N3Uv2F32> vertices) {
            for (std::size_t index = 0U; index < vertices.size(); ++index) {
                const MeshVertexP3N3Uv2F32& vertex = vertices[index];
                const std::array values{vertex.positionX, vertex.positionY, vertex.positionZ,
                                        vertex.normalX,   vertex.normalY,   vertex.normalZ,
                                        vertex.uvX,       vertex.uvY};
                for (const float value : values) {
                    if (!std::isfinite(value)) {
                        return std::unexpected{meshProductError(
                            MeshProductErrorCode::NonFiniteValue,
                            "vertex " + std::to_string(index) + " contains NaN or infinity.")};
                    }
                    if (value == 0.0F && std::signbit(value)) {
                        return std::unexpected{meshProductError(
                            MeshProductErrorCode::NonCanonicalEncoding,
                            "vertex " + std::to_string(index) + " contains negative zero.")};
                    }
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> validateBounds(std::span<const MeshVertexP3N3Uv2F32> vertices,
                                                  MeshAabbV1 bounds) {
            if (!isFinite(bounds)) {
                return std::unexpected{meshProductError(MeshProductErrorCode::NonFiniteValue,
                                                        "bounds contain NaN or infinity.")};
            }
            const std::array boundValues{bounds.minX, bounds.minY, bounds.minZ,
                                         bounds.maxX, bounds.maxY, bounds.maxZ};
            for (const float value : boundValues) {
                if (value == 0.0F && std::signbit(value)) {
                    return std::unexpected{
                        meshProductError(MeshProductErrorCode::NonCanonicalEncoding,
                                         "bounds contain negative zero.")};
                }
            }
            if (!hasOrderedBounds(bounds)) {
                return std::unexpected{meshProductError(MeshProductErrorCode::InvalidBounds,
                                                        "bounds minimum exceeds maximum.")};
            }
            if (!equalBounds(bounds, computeBounds(vertices))) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::InvalidBounds,
                    "bounds do not exactly match the position-derived local AABB.")};
            }
            return {};
        }

        [[nodiscard]] Result<void> validateIndices(std::span<const std::uint32_t> indices,
                                                   std::size_t vertexCount) {
            for (std::size_t index = 0U; index < indices.size(); ++index) {
                if (indices[index] >= vertexCount) {
                    return std::unexpected{meshProductError(
                        MeshProductErrorCode::InvalidIndex,
                        "index " + std::to_string(index) + " references vertex " +
                            std::to_string(indices[index]) +
                            " outside vertexCount=" + std::to_string(vertexCount) + ".")};
                }
            }
            return {};
        }

        struct SubmeshValidationCounts {
            std::size_t indexCount{};
            std::size_t materialSlotCount{};
        };

        [[nodiscard]] Result<void> validateSubmeshes(std::span<const MeshSubmeshV1> submeshes,
                                                     SubmeshValidationCounts counts) {
            std::uint64_t expectedFirstIndex = 0U;
            for (std::size_t index = 0U; index < submeshes.size(); ++index) {
                const MeshSubmeshV1& submesh = submeshes[index];
                if (submesh.firstIndex != expectedFirstIndex || submesh.indexCount == 0U ||
                    submesh.indexCount % 3U != 0U) {
                    return std::unexpected{
                        meshProductError(MeshProductErrorCode::InvalidSubmesh,
                                         "submesh " + std::to_string(index) +
                                             " must be a non-empty triangle range contiguous with "
                                             "its predecessor.")};
                }
                if (submesh.materialSlot >= counts.materialSlotCount) {
                    return std::unexpected{meshProductError(
                        MeshProductErrorCode::InvalidMaterialSlot,
                        "submesh " + std::to_string(index) + " references material slot " +
                            std::to_string(submesh.materialSlot) + " outside materialSlotCount=" +
                            std::to_string(counts.materialSlotCount) + ".")};
                }
                if (!checkedAdd(expectedFirstIndex, submesh.indexCount, expectedFirstIndex) ||
                    expectedFirstIndex > counts.indexCount) {
                    return std::unexpected{meshProductError(MeshProductErrorCode::InvalidSubmesh,
                                                            "submesh " + std::to_string(index) +
                                                                " exceeds the index buffer.")};
                }
            }
            if (expectedFirstIndex != counts.indexCount) {
                return std::unexpected{
                    meshProductError(MeshProductErrorCode::InvalidSubmesh,
                                     "submeshes do not cover the complete index buffer.")};
            }
            return {};
        }

    } // namespace

    Result<void> validateMeshFacts(std::span<const MeshVertexP3N3Uv2F32> vertices,
                                   std::span<const std::uint32_t> indices,
                                   std::span<const MeshSubmeshV1> submeshes,
                                   std::span<const MeshMaterialSlotV1> materialSlots,
                                   MeshAabbV1 bounds) {
        if (vertices.empty()) {
            return std::unexpected{
                meshProductError(MeshProductErrorCode::InvalidArgument, "has no vertices.")};
        }
        if (indices.empty() || indices.size() % 3U != 0U) {
            return std::unexpected{meshProductError(
                MeshProductErrorCode::InvalidIndex,
                "index count must be non-zero and divisible by three for triangle lists.")};
        }
        if (submeshes.empty()) {
            return std::unexpected{
                meshProductError(MeshProductErrorCode::InvalidSubmesh, "has no submeshes.")};
        }
        if (materialSlots.empty()) {
            return std::unexpected{meshProductError(MeshProductErrorCode::InvalidMaterialSlot,
                                                    "has no material slots.")};
        }

        if (auto valid = validateVertices(vertices); !valid) {
            return valid;
        }
        if (auto valid = validateBounds(vertices, bounds); !valid) {
            return valid;
        }
        if (auto valid = validateIndices(indices, vertices.size()); !valid) {
            return valid;
        }
        return validateSubmeshes(
            submeshes, {.indexCount = indices.size(), .materialSlotCount = materialSlots.size()});
    }

    Result<std::uint64_t> encodedSize(std::uint32_t vertexCount, std::uint32_t indexCount,
                                      std::uint32_t submeshCount, std::uint32_t materialSlotCount,
                                      DecodedHeaderV1& layout) {
        std::uint64_t cursor = kHeaderBytes;
        layout.vertexOffset = cursor;

        std::uint64_t sectionBytes{};
        if (!checkedMultiply(vertexCount, kMeshVertexStrideP3N3Uv2F32, sectionBytes) ||
            !checkedAdd(cursor, sectionBytes, cursor) ||
            !alignUp(cursor, kSectionAlignment, cursor)) {
            return std::unexpected{meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                                    "vertex section size overflowed.")};
        }
        layout.indexOffset = cursor;
        if (!checkedMultiply(indexCount, sizeof(std::uint32_t), sectionBytes) ||
            !checkedAdd(cursor, sectionBytes, cursor) ||
            !alignUp(cursor, kSectionAlignment, cursor)) {
            return std::unexpected{meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                                    "index section size overflowed.")};
        }
        layout.submeshOffset = cursor;
        if (!checkedMultiply(submeshCount, kSubmeshRecordBytes, sectionBytes) ||
            !checkedAdd(cursor, sectionBytes, cursor) ||
            !alignUp(cursor, kSectionAlignment, cursor)) {
            return std::unexpected{meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                                    "submesh section size overflowed.")};
        }
        layout.materialSlotOffset = cursor;
        if (!checkedMultiply(materialSlotCount, kMaterialSlotRecordBytes, sectionBytes) ||
            !checkedAdd(cursor, sectionBytes, cursor)) {
            return std::unexpected{meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                                    "material section size overflowed.")};
        }
        layout.fileSize = cursor;
        return cursor;
    }

    std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
        return std::to_integer<std::uint32_t>(bytes[offset]) |
               (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
               (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
               (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }

    std::uint64_t readU64(std::span<const std::byte> bytes, std::size_t offset) noexcept {
        const std::uint64_t low = readU32(bytes, offset);
        const std::uint64_t high = readU32(bytes, offset + 4U);
        return low | (high << 32U);
    }

    float readF32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
        return std::bit_cast<float>(readU32(bytes, offset));
    }

    void writeU32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept {
        for (std::size_t byteIndex = 0U; byteIndex < 4U; ++byteIndex) {
            bytes[offset + byteIndex] = static_cast<std::byte>((value >> (byteIndex * 8U)) & 0xFFU);
        }
    }

    void writeU64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) noexcept {
        writeU32(bytes, offset, static_cast<std::uint32_t>(value));
        writeU32(bytes, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
    }

    void writeF32(std::span<std::byte> bytes, std::size_t offset, float value) noexcept {
        writeU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
    }

} // namespace asharia::mesh::detail
