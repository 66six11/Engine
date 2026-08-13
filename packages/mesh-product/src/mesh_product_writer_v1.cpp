#include "asharia/mesh_product/mesh_product_writer_v1.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asharia/core/file_io.hpp"

#include "mesh_product_internal.hpp"

namespace asharia::mesh {
    namespace {

        [[nodiscard]] Result<void> validateWriteLimits(const MeshProductBuildInputV1& product,
                                                       MeshProductWriteLimits limits) {
            if (!detail::validLimits(limits.maxProductBytes, limits.maxVertices, limits.maxIndices,
                                     limits.maxSubmeshes, limits.maxMaterialSlots)) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::InvalidLimits,
                    "writer limits must have maxProductBytes>=128 and all count limits>0.")};
            }
            if (product.vertices.size() > limits.maxVertices ||
                product.indices.size() > limits.maxIndices ||
                product.submeshes.size() > limits.maxSubmeshes ||
                product.materialSlots.size() > limits.maxMaterialSlots) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::CountLimitExceeded,
                    "build input counts exceed configured writer limits.")};
            }
            constexpr auto kMaximumU32 =
                static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)());
            if (product.vertices.size() > kMaximumU32 || product.indices.size() > kMaximumU32 ||
                product.submeshes.size() > kMaximumU32 ||
                product.materialSlots.size() > kMaximumU32) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::CountLimitExceeded,
                    "build input count cannot be represented by the v1 uint32 fields.")};
            }
            return {};
        }

    } // namespace

    Result<std::vector<std::byte>> writeMeshProductV1(const MeshProductBuildInputV1& product,
                                                      MeshProductWriteLimits limits) {
        try {
            if (auto limited = validateWriteLimits(product, limits); !limited) {
                return std::unexpected{std::move(limited.error())};
            }
            if (auto valid =
                    detail::validateMeshFacts(product.vertices, product.indices, product.submeshes,
                                              product.materialSlots, product.bounds);
                !valid) {
                return std::unexpected{std::move(valid.error())};
            }

            detail::DecodedHeaderV1 header{
                .vertexCount = static_cast<std::uint32_t>(product.vertices.size()),
                .indexCount = static_cast<std::uint32_t>(product.indices.size()),
                .submeshCount = static_cast<std::uint32_t>(product.submeshes.size()),
                .materialSlotCount = static_cast<std::uint32_t>(product.materialSlots.size()),
                .bounds = product.bounds,
            };
            auto byteCount =
                detail::encodedSize(header.vertexCount, header.indexCount, header.submeshCount,
                                    header.materialSlotCount, header);
            if (!byteCount) {
                return std::unexpected{std::move(byteCount.error())};
            }
            if (*byteCount > limits.maxProductBytes || *byteCount > SIZE_MAX) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::ByteBudgetExceeded,
                    "encoded bytes=" + std::to_string(*byteCount) + " exceed maxProductBytes=" +
                        std::to_string(limits.maxProductBytes) + " or the addressable size.")};
            }

            // Value initialization makes all alignment and reserved padding deterministic zeroes.
            std::vector<std::byte> bytes(static_cast<std::size_t>(*byteCount), std::byte{0});
            std::ranges::copy(detail::kMeshProductMagic, bytes.begin());
            detail::writeU32(bytes, 8U, kMeshProductFormatVersionV1);
            detail::writeU32(bytes, 12U, detail::kLittleEndianMarker);
            detail::writeU32(bytes, 16U, static_cast<std::uint32_t>(detail::kHeaderBytes));
            detail::writeU32(bytes, 20U, static_cast<std::uint32_t>(MeshVertexFormat::P3N3Uv2F32));
            detail::writeU32(bytes, 24U, kMeshVertexStrideP3N3Uv2F32);
            detail::writeU32(bytes, 32U, header.vertexCount);
            detail::writeU32(bytes, 36U, header.indexCount);
            detail::writeU32(bytes, 40U, header.submeshCount);
            detail::writeU32(bytes, 44U, header.materialSlotCount);
            detail::writeU64(bytes, 48U, header.vertexOffset);
            detail::writeU64(bytes, 56U, header.indexOffset);
            detail::writeU64(bytes, 64U, header.submeshOffset);
            detail::writeU64(bytes, 72U, header.materialSlotOffset);
            detail::writeU64(bytes, 80U, header.fileSize);
            detail::writeF32(bytes, 88U, header.bounds.minX);
            detail::writeF32(bytes, 92U, header.bounds.minY);
            detail::writeF32(bytes, 96U, header.bounds.minZ);
            detail::writeF32(bytes, 100U, header.bounds.maxX);
            detail::writeF32(bytes, 104U, header.bounds.maxY);
            detail::writeF32(bytes, 108U, header.bounds.maxZ);

            for (std::size_t index = 0U; index < product.vertices.size(); ++index) {
                const MeshVertexP3N3Uv2F32& vertex = product.vertices[index];
                const auto offset = static_cast<std::size_t>(header.vertexOffset +
                                                             (index * kMeshVertexStrideP3N3Uv2F32));
                detail::writeF32(bytes, offset, vertex.positionX);
                detail::writeF32(bytes, offset + 4U, vertex.positionY);
                detail::writeF32(bytes, offset + 8U, vertex.positionZ);
                detail::writeF32(bytes, offset + 12U, vertex.normalX);
                detail::writeF32(bytes, offset + 16U, vertex.normalY);
                detail::writeF32(bytes, offset + 20U, vertex.normalZ);
                detail::writeF32(bytes, offset + 24U, vertex.uvX);
                detail::writeF32(bytes, offset + 28U, vertex.uvY);
            }
            for (std::size_t index = 0U; index < product.indices.size(); ++index) {
                const auto offset = static_cast<std::size_t>(header.indexOffset + (index * 4U));
                detail::writeU32(bytes, offset, product.indices[index]);
            }
            for (std::size_t index = 0U; index < product.submeshes.size(); ++index) {
                const MeshSubmeshV1& submesh = product.submeshes[index];
                const auto offset = static_cast<std::size_t>(header.submeshOffset +
                                                             (index * detail::kSubmeshRecordBytes));
                detail::writeU32(bytes, offset, submesh.firstIndex);
                detail::writeU32(bytes, offset + 4U, submesh.indexCount);
                detail::writeU32(bytes, offset + 8U, submesh.materialSlot);
            }
            for (std::size_t index = 0U; index < product.materialSlots.size(); ++index) {
                const auto offset = static_cast<std::size_t>(
                    header.materialSlotOffset + (index * detail::kMaterialSlotRecordBytes));
                const asset::AssetGuid guid = product.materialSlots[index].materialAsset;
                std::size_t byteIndex = 0U;
                for (const std::uint8_t value : guid.bytes) {
                    bytes[offset + byteIndex] = static_cast<std::byte>(value);
                    ++byteIndex;
                }
            }

            return bytes;
        } catch (const std::bad_alloc&) {
            return std::unexpected{
                detail::meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                         "could not allocate the bounded encoded mesh product.")};
        } catch (const std::length_error&) {
            return std::unexpected{
                detail::meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                         "encoded mesh product exceeds container addressability.")};
        }
    }

    VoidResult writeMeshProductV1File(const std::filesystem::path& path,
                                      const MeshProductBuildInputV1& product,
                                      MeshProductWriteLimits limits) {
        auto bytes = writeMeshProductV1(product, limits);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }
        auto written = core::writeFileBytesAtomically(path, *bytes);
        if (!written) {
            return std::unexpected{detail::meshProductError(MeshProductErrorCode::FileWriteFailed,
                                                            "file write failed for '" +
                                                                detail::pathToUtf8(path) +
                                                                "': " + written.error().message)};
        }
        return {};
    }

} // namespace asharia::mesh
