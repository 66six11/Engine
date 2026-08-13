#include "asharia/mesh_product/mesh_product_v1.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asharia/core/file_io.hpp"

#include "mesh_product_internal.hpp"

namespace asharia::mesh {
    namespace {

        [[nodiscard]] Result<detail::DecodedHeaderV1>
        decodeAndValidateHeader(std::span<const std::byte> bytes, MeshProductReadLimits limits) {
            using namespace detail;

            if (!validLimits(limits.maxProductBytes, limits.maxVertices, limits.maxIndices,
                             limits.maxSubmeshes, limits.maxMaterialSlots)) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::InvalidLimits,
                    "reader limits must have maxProductBytes>=128 and all count limits>0.")};
            }
            if (bytes.size() > limits.maxProductBytes) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::ByteBudgetExceeded,
                    "byte size=" + std::to_string(bytes.size()) + " exceeds maxProductBytes=" +
                        std::to_string(limits.maxProductBytes) + ".")};
            }
            if (bytes.size() < kHeaderBytes) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::Truncated,
                    "is truncated: byte size=" + std::to_string(bytes.size()) +
                        ", required header bytes=" + std::to_string(kHeaderBytes) + ".")};
            }
            if (!std::equal(kMeshProductMagic.begin(), kMeshProductMagic.end(), bytes.begin())) {
                return std::unexpected{meshProductError(MeshProductErrorCode::InvalidMagic,
                                                        "has an invalid magic value.")};
            }
            const std::uint32_t version = readU32(bytes, 8U);
            if (version != kMeshProductFormatVersionV1) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::UnsupportedVersion,
                    "format version=" + std::to_string(version) + " is unsupported; expected 1.")};
            }
            const std::uint32_t endianness = readU32(bytes, 12U);
            if (endianness != kLittleEndianMarker) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::UnsupportedEndianness,
                    "endianness marker is unsupported; v1 requires little-endian bytes.")};
            }
            if (readU32(bytes, 16U) != kHeaderBytes) {
                return std::unexpected{meshProductError(MeshProductErrorCode::InvalidHeader,
                                                        "header byte size must equal 128.")};
            }
            if (readU32(bytes, 20U) != static_cast<std::uint32_t>(MeshVertexFormat::P3N3Uv2F32)) {
                return std::unexpected{
                    meshProductError(MeshProductErrorCode::UnsupportedVertexFormat,
                                     "vertex format is unsupported; v1 requires P3N3Uv2F32.")};
            }
            if (readU32(bytes, 24U) != kMeshVertexStrideP3N3Uv2F32) {
                return std::unexpected{meshProductError(MeshProductErrorCode::InvalidHeader,
                                                        "vertex stride must equal 32 bytes.")};
            }
            if (readU32(bytes, 28U) != 0U) {
                return std::unexpected{
                    meshProductError(MeshProductErrorCode::NonCanonicalEncoding,
                                     "reserved header field at byte 28 must be zero.")};
            }

            DecodedHeaderV1 header{
                .vertexCount = readU32(bytes, 32U),
                .indexCount = readU32(bytes, 36U),
                .submeshCount = readU32(bytes, 40U),
                .materialSlotCount = readU32(bytes, 44U),
                .vertexOffset = readU64(bytes, 48U),
                .indexOffset = readU64(bytes, 56U),
                .submeshOffset = readU64(bytes, 64U),
                .materialSlotOffset = readU64(bytes, 72U),
                .fileSize = readU64(bytes, 80U),
                .bounds =
                    MeshAabbV1{
                        .minX = readF32(bytes, 88U),
                        .minY = readF32(bytes, 92U),
                        .minZ = readF32(bytes, 96U),
                        .maxX = readF32(bytes, 100U),
                        .maxY = readF32(bytes, 104U),
                        .maxZ = readF32(bytes, 108U),
                    },
            };

            if (header.vertexCount == 0U || header.indexCount == 0U || header.submeshCount == 0U ||
                header.materialSlotCount == 0U) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::InvalidHeader,
                    "vertex, index, submesh, and material-slot counts must be non-zero.")};
            }
            if (header.vertexCount > limits.maxVertices || header.indexCount > limits.maxIndices ||
                header.submeshCount > limits.maxSubmeshes ||
                header.materialSlotCount > limits.maxMaterialSlots) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::CountLimitExceeded,
                    "declared counts exceed configured limits: vertices=" +
                        std::to_string(header.vertexCount) + "/" +
                        std::to_string(limits.maxVertices) +
                        ", indices=" + std::to_string(header.indexCount) + "/" +
                        std::to_string(limits.maxIndices) +
                        ", submeshes=" + std::to_string(header.submeshCount) + "/" +
                        std::to_string(limits.maxSubmeshes) +
                        ", materialSlots=" + std::to_string(header.materialSlotCount) + "/" +
                        std::to_string(limits.maxMaterialSlots) + ".")};
            }

            DecodedHeaderV1 canonical{};
            auto expectedSize =
                encodedSize(header.vertexCount, header.indexCount, header.submeshCount,
                            header.materialSlotCount, canonical);
            if (!expectedSize) {
                return std::unexpected{std::move(expectedSize.error())};
            }
            if (*expectedSize > limits.maxProductBytes) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::ByteBudgetExceeded,
                    "declared sections require bytes=" + std::to_string(*expectedSize) +
                        " exceeding maxProductBytes=" + std::to_string(limits.maxProductBytes) +
                        ".")};
            }
            if (header.vertexOffset != canonical.vertexOffset ||
                header.indexOffset != canonical.indexOffset ||
                header.submeshOffset != canonical.submeshOffset ||
                header.materialSlotOffset != canonical.materialSlotOffset ||
                header.fileSize != canonical.fileSize) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::InvalidSectionLayout,
                    "section offsets or declared file size are not canonical for the counts.")};
            }
            if (header.fileSize != bytes.size()) {
                return std::unexpected{meshProductError(
                    MeshProductErrorCode::Truncated,
                    "declared fileSize=" + std::to_string(header.fileSize) +
                        " does not equal observed bytes=" + std::to_string(bytes.size()) + ".")};
            }
            for (std::size_t offset = 112U; offset < kHeaderBytes; ++offset) {
                if (bytes[offset] != std::byte{0}) {
                    return std::unexpected{
                        meshProductError(MeshProductErrorCode::NonCanonicalEncoding,
                                         "reserved header bytes 112..127 must be zero.")};
                }
            }
            return header;
        }

        [[nodiscard]] Result<void> requireZeroRange(std::span<const std::byte> bytes,
                                                    std::uint64_t begin, std::uint64_t end,
                                                    std::string_view name) {
            for (std::uint64_t offset = begin; offset < end; ++offset) {
                if (bytes[static_cast<std::size_t>(offset)] != std::byte{0}) {
                    return std::unexpected{detail::meshProductError(
                        MeshProductErrorCode::NonCanonicalEncoding,
                        std::string{name} + " alignment padding must be zero.")};
                }
            }
            return {};
        }

        [[nodiscard]] MeshVertexP3N3Uv2F32 decodeVertex(std::span<const std::byte> bytes,
                                                        std::uint64_t vertexOffset,
                                                        std::uint32_t index) noexcept {
            const auto offset = static_cast<std::size_t>(
                vertexOffset + (static_cast<std::uint64_t>(index) * kMeshVertexStrideP3N3Uv2F32));
            return MeshVertexP3N3Uv2F32{
                .positionX = detail::readF32(bytes, offset),
                .positionY = detail::readF32(bytes, offset + 4U),
                .positionZ = detail::readF32(bytes, offset + 8U),
                .normalX = detail::readF32(bytes, offset + 12U),
                .normalY = detail::readF32(bytes, offset + 16U),
                .normalZ = detail::readF32(bytes, offset + 20U),
                .uvX = detail::readF32(bytes, offset + 24U),
                .uvY = detail::readF32(bytes, offset + 28U),
            };
        }

        [[nodiscard]] Result<void>
        preflightVerticesAndBounds(std::span<const std::byte> bytes,
                                   const detail::DecodedHeaderV1& header) {
            MeshAabbV1 computed{};
            for (std::uint32_t index = 0U; index < header.vertexCount; ++index) {
                const MeshVertexP3N3Uv2F32 vertex = decodeVertex(bytes, header.vertexOffset, index);
                const std::array values{vertex.positionX, vertex.positionY, vertex.positionZ,
                                        vertex.normalX,   vertex.normalY,   vertex.normalZ,
                                        vertex.uvX,       vertex.uvY};
                for (const float value : values) {
                    if (!std::isfinite(value)) {
                        return std::unexpected{detail::meshProductError(
                            MeshProductErrorCode::NonFiniteValue,
                            "vertex " + std::to_string(index) + " contains NaN or infinity.")};
                    }
                    if (value == 0.0F && std::signbit(value)) {
                        return std::unexpected{detail::meshProductError(
                            MeshProductErrorCode::NonCanonicalEncoding,
                            "vertex " + std::to_string(index) + " contains negative zero.")};
                    }
                }
                if (index == 0U) {
                    computed = {.minX = vertex.positionX,
                                .minY = vertex.positionY,
                                .minZ = vertex.positionZ,
                                .maxX = vertex.positionX,
                                .maxY = vertex.positionY,
                                .maxZ = vertex.positionZ};
                } else {
                    computed.minX = (std::min)(computed.minX, vertex.positionX);
                    computed.minY = (std::min)(computed.minY, vertex.positionY);
                    computed.minZ = (std::min)(computed.minZ, vertex.positionZ);
                    computed.maxX = (std::max)(computed.maxX, vertex.positionX);
                    computed.maxY = (std::max)(computed.maxY, vertex.positionY);
                    computed.maxZ = (std::max)(computed.maxZ, vertex.positionZ);
                }
            }

            if (!detail::isFinite(header.bounds)) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::NonFiniteValue, "bounds contain NaN or infinity.")};
            }
            const std::array boundValues{header.bounds.minX, header.bounds.minY,
                                         header.bounds.minZ, header.bounds.maxX,
                                         header.bounds.maxY, header.bounds.maxZ};
            for (const float value : boundValues) {
                if (value == 0.0F && std::signbit(value)) {
                    return std::unexpected{
                        detail::meshProductError(MeshProductErrorCode::NonCanonicalEncoding,
                                                 "bounds contain negative zero.")};
                }
            }
            if (!detail::hasOrderedBounds(header.bounds)) {
                return std::unexpected{detail::meshProductError(MeshProductErrorCode::InvalidBounds,
                                                                "bounds minimum exceeds maximum.")};
            }
            if (!detail::equalBounds(header.bounds, computed)) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::InvalidBounds,
                    "bounds do not exactly match the position-derived local AABB.")};
            }
            return {};
        }

        [[nodiscard]] Result<void> preflightIndices(std::span<const std::byte> bytes,
                                                    const detail::DecodedHeaderV1& header) {
            for (std::uint32_t index = 0U; index < header.indexCount; ++index) {
                const auto offset = static_cast<std::size_t>(
                    header.indexOffset + (static_cast<std::uint64_t>(index) * 4U));
                const std::uint32_t vertexIndex = detail::readU32(bytes, offset);
                if (vertexIndex >= header.vertexCount) {
                    return std::unexpected{detail::meshProductError(
                        MeshProductErrorCode::InvalidIndex,
                        "index " + std::to_string(index) + " references vertex " +
                            std::to_string(vertexIndex) +
                            " outside vertexCount=" + std::to_string(header.vertexCount) + ".")};
                }
            }
            return {};
        }

        [[nodiscard]] Result<void> preflightSubmeshes(std::span<const std::byte> bytes,
                                                      const detail::DecodedHeaderV1& header) {
            std::uint64_t expectedFirstIndex = 0U;
            for (std::uint32_t index = 0U; index < header.submeshCount; ++index) {
                const auto offset = static_cast<std::size_t>(
                    header.submeshOffset +
                    (static_cast<std::uint64_t>(index) * detail::kSubmeshRecordBytes));
                const MeshSubmeshV1 submesh{
                    .firstIndex = detail::readU32(bytes, offset),
                    .indexCount = detail::readU32(bytes, offset + 4U),
                    .materialSlot = detail::readU32(bytes, offset + 8U),
                };
                if (detail::readU32(bytes, offset + 12U) != 0U) {
                    return std::unexpected{detail::meshProductError(
                        MeshProductErrorCode::NonCanonicalEncoding,
                        "submesh " + std::to_string(index) + " reserved field must be zero.")};
                }
                if (submesh.firstIndex != expectedFirstIndex || submesh.indexCount == 0U ||
                    submesh.indexCount % 3U != 0U) {
                    return std::unexpected{
                        detail::meshProductError(MeshProductErrorCode::InvalidSubmesh,
                                                 "submesh " + std::to_string(index) +
                                                     " must be a non-empty triangle range "
                                                     "contiguous with its predecessor.")};
                }
                if (submesh.materialSlot >= header.materialSlotCount) {
                    return std::unexpected{detail::meshProductError(
                        MeshProductErrorCode::InvalidMaterialSlot,
                        "submesh " + std::to_string(index) + " references material slot " +
                            std::to_string(submesh.materialSlot) + " outside materialSlotCount=" +
                            std::to_string(header.materialSlotCount) + ".")};
                }
                if (!detail::checkedAdd(expectedFirstIndex, submesh.indexCount,
                                        expectedFirstIndex) ||
                    expectedFirstIndex > header.indexCount) {
                    return std::unexpected{detail::meshProductError(
                        MeshProductErrorCode::InvalidSubmesh,
                        "submesh " + std::to_string(index) + " exceeds the index buffer.")};
                }
            }
            if (expectedFirstIndex != header.indexCount) {
                return std::unexpected{
                    detail::meshProductError(MeshProductErrorCode::InvalidSubmesh,
                                             "submeshes do not cover the complete index buffer.")};
            }
            return {};
        }

        [[nodiscard]] Result<void> preflightPayload(std::span<const std::byte> bytes,
                                                    const detail::DecodedHeaderV1& header) {
            if (auto valid = preflightVerticesAndBounds(bytes, header); !valid) {
                return valid;
            }
            if (auto valid = preflightIndices(bytes, header); !valid) {
                return valid;
            }
            return preflightSubmeshes(bytes, header);
        }

    } // namespace

    MeshProductV1::MeshProductV1(std::vector<MeshVertexP3N3Uv2F32> vertices,
                                 std::vector<std::uint32_t> indices,
                                 std::vector<MeshSubmeshV1> submeshes,
                                 std::vector<MeshMaterialSlotV1> materialSlots, MeshAabbV1 bounds)
        : vertices_(std::move(vertices)), indices_(std::move(indices)),
          submeshes_(std::move(submeshes)), materialSlots_(std::move(materialSlots)),
          bounds_(bounds) {}

    // The member keeps product queries uniform even though v1 has one fixed layout.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    MeshVertexFormat MeshProductV1::vertexFormat() const noexcept {
        return MeshVertexFormat::P3N3Uv2F32;
    }

    MeshAabbV1 MeshProductV1::bounds() const noexcept {
        return bounds_;
    }

    std::span<const MeshVertexP3N3Uv2F32> MeshProductV1::vertices() const noexcept {
        return vertices_;
    }

    std::span<const std::uint32_t> MeshProductV1::indices() const noexcept {
        return indices_;
    }

    std::span<const MeshSubmeshV1> MeshProductV1::submeshes() const noexcept {
        return submeshes_;
    }

    std::span<const MeshMaterialSlotV1> MeshProductV1::materialSlots() const noexcept {
        return materialSlots_;
    }

    Result<MeshProductV1> readMeshProductV1(std::span<const std::byte> bytes,
                                            MeshProductReadLimits limits) {
        try {
            auto header = decodeAndValidateHeader(bytes, limits);
            if (!header) {
                return std::unexpected{std::move(header.error())};
            }

            std::uint64_t vertexEnd{};
            std::uint64_t indexEnd{};
            std::uint64_t submeshEnd{};
            if (!detail::checkedAdd(header->vertexOffset,
                                    static_cast<std::uint64_t>(header->vertexCount) *
                                        kMeshVertexStrideP3N3Uv2F32,
                                    vertexEnd) ||
                !detail::checkedAdd(header->indexOffset,
                                    static_cast<std::uint64_t>(header->indexCount) * 4U,
                                    indexEnd) ||
                !detail::checkedAdd(header->submeshOffset,
                                    static_cast<std::uint64_t>(header->submeshCount) *
                                        detail::kSubmeshRecordBytes,
                                    submeshEnd)) {
                return std::unexpected{detail::meshProductError(
                    MeshProductErrorCode::InvalidSectionLayout, "section end overflowed.")};
            }
            if (auto zero = requireZeroRange(bytes, vertexEnd, header->indexOffset, "vertex");
                !zero) {
                return std::unexpected{std::move(zero.error())};
            }
            if (auto zero = requireZeroRange(bytes, indexEnd, header->submeshOffset, "index");
                !zero) {
                return std::unexpected{std::move(zero.error())};
            }
            if (auto zero =
                    requireZeroRange(bytes, submeshEnd, header->materialSlotOffset, "submesh");
                !zero) {
                return std::unexpected{std::move(zero.error())};
            }
            // Scan all typed payload facts and ranges before any count-sized allocation.
            if (auto valid = preflightPayload(bytes, *header); !valid) {
                return std::unexpected{std::move(valid.error())};
            }

            std::vector<MeshVertexP3N3Uv2F32> vertices;
            vertices.reserve(header->vertexCount);
            for (std::uint32_t index = 0U; index < header->vertexCount; ++index) {
                vertices.push_back(decodeVertex(bytes, header->vertexOffset, index));
            }

            std::vector<std::uint32_t> indices;
            indices.reserve(header->indexCount);
            for (std::uint32_t index = 0U; index < header->indexCount; ++index) {
                const auto offset = static_cast<std::size_t>(
                    header->indexOffset + (static_cast<std::uint64_t>(index) * 4U));
                indices.push_back(detail::readU32(bytes, offset));
            }

            std::vector<MeshSubmeshV1> submeshes;
            submeshes.reserve(header->submeshCount);
            for (std::uint32_t index = 0U; index < header->submeshCount; ++index) {
                const auto offset = static_cast<std::size_t>(
                    header->submeshOffset +
                    (static_cast<std::uint64_t>(index) * detail::kSubmeshRecordBytes));
                submeshes.push_back(MeshSubmeshV1{
                    .firstIndex = detail::readU32(bytes, offset),
                    .indexCount = detail::readU32(bytes, offset + 4U),
                    .materialSlot = detail::readU32(bytes, offset + 8U),
                });
            }

            std::vector<MeshMaterialSlotV1> materialSlots;
            materialSlots.reserve(header->materialSlotCount);
            for (std::uint32_t index = 0U; index < header->materialSlotCount; ++index) {
                const auto offset = static_cast<std::size_t>(
                    header->materialSlotOffset +
                    (static_cast<std::uint64_t>(index) * detail::kMaterialSlotRecordBytes));
                asset::AssetGuid guid{};
                std::size_t byteIndex = 0U;
                for (std::uint8_t& value : guid.bytes) {
                    value = std::to_integer<std::uint8_t>(bytes[offset + byteIndex]);
                    ++byteIndex;
                }
                materialSlots.push_back(MeshMaterialSlotV1{.materialAsset = guid});
            }

            return MeshProductV1{std::move(vertices), std::move(indices), std::move(submeshes),
                                 std::move(materialSlots), header->bounds};
        } catch (const std::bad_alloc&) {
            return std::unexpected{
                detail::meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                         "could not allocate the bounded decoded mesh payload.")};
        } catch (const std::length_error&) {
            return std::unexpected{
                detail::meshProductError(MeshProductErrorCode::ByteBudgetExceeded,
                                         "decoded mesh payload exceeds container addressability.")};
        }
    }

    Result<MeshProductV1> readMeshProductV1File(const std::filesystem::path& path,
                                                MeshProductReadLimits limits) {
        if (!detail::validLimits(limits.maxProductBytes, limits.maxVertices, limits.maxIndices,
                                 limits.maxSubmeshes, limits.maxMaterialSlots)) {
            return std::unexpected{detail::meshProductError(
                MeshProductErrorCode::InvalidLimits,
                "reader limits must have maxProductBytes>=128 and all count limits>0.")};
        }
        auto bytes = core::readFileBytes(path, {.maxBytes = limits.maxProductBytes});
        if (!bytes) {
            return std::unexpected{detail::meshProductError(MeshProductErrorCode::FileReadFailed,
                                                            "file read failed for '" +
                                                                detail::pathToUtf8(path) +
                                                                "': " + bytes.error().message)};
        }
        auto product = readMeshProductV1(*bytes, limits);
        if (!product) {
            product.error().message += " File='" + detail::pathToUtf8(path) + "'.";
        }
        return product;
    }

    const char* meshProductErrorCodeName(MeshProductErrorCode code) noexcept {
        switch (code) {
        case MeshProductErrorCode::InvalidArgument:
            return "invalid-argument";
        case MeshProductErrorCode::InvalidLimits:
            return "invalid-limits";
        case MeshProductErrorCode::ByteBudgetExceeded:
            return "byte-budget-exceeded";
        case MeshProductErrorCode::FileReadFailed:
            return "file-read-failed";
        case MeshProductErrorCode::FileWriteFailed:
            return "file-write-failed";
        case MeshProductErrorCode::Truncated:
            return "truncated";
        case MeshProductErrorCode::InvalidMagic:
            return "invalid-magic";
        case MeshProductErrorCode::UnsupportedVersion:
            return "unsupported-version";
        case MeshProductErrorCode::UnsupportedEndianness:
            return "unsupported-endianness";
        case MeshProductErrorCode::UnsupportedVertexFormat:
            return "unsupported-vertex-format";
        case MeshProductErrorCode::InvalidHeader:
            return "invalid-header";
        case MeshProductErrorCode::InvalidSectionLayout:
            return "invalid-section-layout";
        case MeshProductErrorCode::CountLimitExceeded:
            return "count-limit-exceeded";
        case MeshProductErrorCode::NonFiniteValue:
            return "non-finite-value";
        case MeshProductErrorCode::InvalidBounds:
            return "invalid-bounds";
        case MeshProductErrorCode::InvalidIndex:
            return "invalid-index";
        case MeshProductErrorCode::InvalidSubmesh:
            return "invalid-submesh";
        case MeshProductErrorCode::InvalidMaterialSlot:
            return "invalid-material-slot";
        case MeshProductErrorCode::NonCanonicalEncoding:
            return "non-canonical-encoding";
        }
        return "unknown";
    }

} // namespace asharia::mesh
