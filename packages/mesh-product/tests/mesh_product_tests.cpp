#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/mesh_product/mesh_product_v1.hpp"
#include "asharia/mesh_product/mesh_product_writer_v1.hpp"

namespace {

    constexpr std::size_t kVersionOffset = 8U;
    constexpr std::size_t kEndianOffset = 12U;
    constexpr std::size_t kHeaderSizeOffset = 16U;
    constexpr std::size_t kVertexFormatOffset = 20U;
    constexpr std::size_t kVertexStrideOffset = 24U;
    constexpr std::size_t kHeaderReservedOffset = 28U;
    constexpr std::size_t kVertexCountOffset = 32U;
    constexpr std::size_t kIndexCountOffset = 36U;
    constexpr std::size_t kVertexOffsetOffset = 48U;
    constexpr std::size_t kFileSizeOffset = 80U;
    constexpr std::size_t kBoundsOffset = 88U;

    [[nodiscard]] std::filesystem::path createUniqueTestDirectory() {
        constexpr std::uint32_t kMaximumAttempts = 128U;
        const auto root = std::filesystem::temp_directory_path();
        for (std::uint32_t attempt = 0U; attempt < kMaximumAttempts; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = root / ("asharia-mesh-product-tests-" + std::to_string(stamp) +
                                           "." + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                return candidate;
            }
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error{"Could not create test directory",
                                                        candidate, error};
            }
        }
        throw std::runtime_error{"Could not allocate a unique mesh-product test directory."};
    }

    class ScopedTestDirectory final {
    public:
        ScopedTestDirectory() : path_(createUniqueTestDirectory()) {}
        ~ScopedTestDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScopedTestDirectory(const ScopedTestDirectory&) = delete;
        ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;
        ScopedTestDirectory(ScopedTestDirectory&&) = delete;
        ScopedTestDirectory& operator=(ScopedTestDirectory&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] asharia::mesh::MeshProductBuildInputV1 makeMesh() {
        asharia::asset::AssetGuid material{};
        material.bytes[0] = 0x42U;
        return asharia::mesh::MeshProductBuildInputV1{
            .vertices =
                {
                    {.positionX = -1.0F,
                     .positionY = -2.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 0.0F,
                     .uvY = 0.0F},
                    {.positionX = 2.0F,
                     .positionY = -2.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 1.0F,
                     .uvY = 0.0F},
                    {.positionX = 2.0F,
                     .positionY = 3.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 1.0F,
                     .uvY = 1.0F},
                    {.positionX = -1.0F,
                     .positionY = 3.0F,
                     .positionZ = 0.0F,
                     .normalX = 0.0F,
                     .normalY = 0.0F,
                     .normalZ = 1.0F,
                     .uvX = 0.0F,
                     .uvY = 1.0F},
                },
            .indices = {0U, 1U, 2U, 0U, 2U, 3U},
            .submeshes =
                {
                    {.firstIndex = 0U, .indexCount = 3U, .materialSlot = 0U},
                    {.firstIndex = 3U, .indexCount = 3U, .materialSlot = 1U},
                },
            .materialSlots =
                {
                    {},
                    {.materialAsset = material},
                },
            .bounds = {.minX = -1.0F,
                       .minY = -2.0F,
                       .minZ = 0.0F,
                       .maxX = 2.0F,
                       .maxY = 3.0F,
                       .maxZ = 0.0F},
        };
    }

    void writeU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
        for (std::size_t index = 0U; index < 4U; ++index) {
            bytes.at(offset + index) = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
        }
    }

    void writeU64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
        writeU32(bytes, offset, static_cast<std::uint32_t>(value));
        writeU32(bytes, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
    }

    void writeF32(std::vector<std::byte>& bytes, std::size_t offset, float value) {
        writeU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::uint64_t readU64(std::span<const std::byte> bytes, std::size_t offset) {
        std::uint64_t value{};
        for (std::size_t index = 0U; index < 8U; ++index) {
            value |=
                static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                << (index * 8U);
        }
        return value;
    }

    [[nodiscard]] bool contains(std::string_view text, std::string_view token) {
        return text.find(token) != std::string_view::npos;
    }

    [[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
        const std::u8string utf8 = path.generic_u8string();
        std::string text;
        text.reserve(utf8.size());
        for (const char8_t character : utf8) {
            text.push_back(static_cast<char>(character));
        }
        return text;
    }

    [[nodiscard]] bool expectReadFailure(std::span<const std::byte> bytes,
                                         asharia::mesh::MeshProductErrorCode expectedCode,
                                         std::string_view expectedMessage,
                                         asharia::mesh::MeshProductReadLimits limits = {}) {
        auto read = asharia::mesh::readMeshProductV1(bytes, limits);
        if (read) {
            std::cerr << "Reader accepted invalid mesh product; expected "
                      << asharia::mesh::meshProductErrorCodeName(expectedCode) << ".\n";
            return false;
        }
        if (read.error().domain != asharia::ErrorDomain::Asset ||
            read.error().code != static_cast<int>(expectedCode) ||
            !contains(read.error().message, expectedMessage)) {
            std::cerr << "Reader diagnostic mismatch: " << read.error().message << '\n';
            return false;
        }
        return true;
    }

    [[nodiscard]] bool expectWriteFailure(const asharia::mesh::MeshProductBuildInputV1& input,
                                          asharia::mesh::MeshProductErrorCode expectedCode,
                                          std::string_view expectedMessage,
                                          asharia::mesh::MeshProductWriteLimits limits = {}) {
        auto written = asharia::mesh::writeMeshProductV1(input, limits);
        if (written) {
            std::cerr << "Writer accepted invalid mesh product; expected "
                      << asharia::mesh::meshProductErrorCodeName(expectedCode) << ".\n";
            return false;
        }
        if (written.error().code != static_cast<int>(expectedCode) ||
            !contains(written.error().message, expectedMessage)) {
            std::cerr << "Writer diagnostic mismatch: " << written.error().message << '\n';
            return false;
        }
        return true;
    }

    [[nodiscard]] bool roundTripAndFileTest() {
        const auto source = makeMesh();
        auto first = asharia::mesh::writeMeshProductV1(source);
        auto second = asharia::mesh::writeMeshProductV1(source);
        if (!first || !second || *first != *second) {
            std::cerr << "Writer was not byte deterministic.\n";
            return false;
        }
        auto read = asharia::mesh::readMeshProductV1(*first);
        if (!read || read->vertexFormat() != asharia::mesh::MeshVertexFormat::P3N3Uv2F32 ||
            read->vertices().size() != source.vertices.size() ||
            !std::ranges::equal(read->indices(), source.indices) ||
            !std::ranges::equal(read->submeshes(), source.submeshes) ||
            !std::ranges::equal(read->materialSlots(), source.materialSlots) ||
            read->bounds() != source.bounds) {
            std::cerr << (read ? "Span round-trip facts differed.\n" : read.error().message + "\n");
            return false;
        }

        const ScopedTestDirectory directory;
        const auto file = directory.path() / std::filesystem::path{u8"网格.amesh"};
        if (auto written = asharia::mesh::writeMeshProductV1File(file, source); !written) {
            std::cerr << written.error().message << '\n';
            return false;
        }
        auto fileRead = asharia::mesh::readMeshProductV1File(file);
        if (!fileRead || fileRead->vertices().size() != source.vertices.size()) {
            std::cerr << (fileRead ? "File round-trip facts differed.\n"
                                   : fileRead.error().message + "\n");
            return false;
        }
        const auto missingPath = directory.path() / std::filesystem::path{u8"不存在.amesh"};
        auto missing = asharia::mesh::readMeshProductV1File(missingPath);
        if (missing ||
            missing.error().code !=
                static_cast<int>(asharia::mesh::MeshProductErrorCode::FileReadFailed) ||
            !contains(missing.error().message, pathToUtf8(missingPath))) {
            std::cerr << "Missing file did not return a typed path-rich diagnostic.\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool writerNegativeTests() {
        auto empty = makeMesh();
        empty.vertices.clear();
        auto nonFinite = makeMesh();
        nonFinite.vertices[0].positionX = std::numeric_limits<float>::infinity();
        auto negativeZero = makeMesh();
        negativeZero.vertices[0].uvX = -0.0F;
        auto badBounds = makeMesh();
        badBounds.bounds.maxX = 1.0F;
        auto badIndex = makeMesh();
        badIndex.indices[2] = 99U;
        auto badSubmesh = makeMesh();
        badSubmesh.submeshes[1].firstIndex = 0U;
        auto badMaterial = makeMesh();
        badMaterial.submeshes[1].materialSlot = 2U;

        return expectWriteFailure(empty, asharia::mesh::MeshProductErrorCode::InvalidArgument,
                                  "no vertices") &&
               expectWriteFailure(nonFinite, asharia::mesh::MeshProductErrorCode::NonFiniteValue,
                                  "NaN or infinity") &&
               expectWriteFailure(negativeZero,
                                  asharia::mesh::MeshProductErrorCode::NonCanonicalEncoding,
                                  "negative zero") &&
               expectWriteFailure(badBounds, asharia::mesh::MeshProductErrorCode::InvalidBounds,
                                  "position-derived") &&
               expectWriteFailure(badIndex, asharia::mesh::MeshProductErrorCode::InvalidIndex,
                                  "outside vertexCount") &&
               expectWriteFailure(badSubmesh, asharia::mesh::MeshProductErrorCode::InvalidSubmesh,
                                  "contiguous") &&
               expectWriteFailure(badMaterial,
                                  asharia::mesh::MeshProductErrorCode::InvalidMaterialSlot,
                                  "outside materialSlotCount") &&
               expectWriteFailure(makeMesh(), asharia::mesh::MeshProductErrorCode::InvalidLimits,
                                  "writer limits", {.maxProductBytes = 0U}) &&
               expectWriteFailure(makeMesh(),
                                  asharia::mesh::MeshProductErrorCode::CountLimitExceeded,
                                  "counts exceed",
                                  {.maxProductBytes = 512U,
                                   .maxVertices = 3U,
                                   .maxIndices = 6U,
                                   .maxSubmeshes = 2U,
                                   .maxMaterialSlots = 2U}) &&
               expectWriteFailure(makeMesh(),
                                  asharia::mesh::MeshProductErrorCode::ByteBudgetExceeded,
                                  "encoded bytes", {.maxProductBytes = 128U});
    }

    [[nodiscard]] bool readerHeaderNegativeTests(const std::vector<std::byte>& canonical) {
        auto magic = canonical;
        magic[0] = std::byte{0};
        auto version = canonical;
        writeU32(version, kVersionOffset, 2U);
        auto endian = canonical;
        writeU32(endian, kEndianOffset, 0x0403'0201U);
        auto headerSize = canonical;
        writeU32(headerSize, kHeaderSizeOffset, 127U);
        auto vertexFormat = canonical;
        writeU32(vertexFormat, kVertexFormatOffset, 2U);
        auto vertexStride = canonical;
        writeU32(vertexStride, kVertexStrideOffset, 28U);
        auto headerReserved = canonical;
        writeU32(headerReserved, kHeaderReservedOffset, 1U);
        auto offset = canonical;
        writeU64(offset, kVertexOffsetOffset, 144U);
        auto fileSize = canonical;
        fileSize.push_back(std::byte{0});
        writeU64(fileSize, kFileSizeOffset, canonical.size());
        auto reserved = canonical;
        reserved[112U] = std::byte{1};

        const std::span<const std::byte> truncated{canonical.data(), 127U};
        return expectReadFailure({}, asharia::mesh::MeshProductErrorCode::Truncated, "truncated") &&
               expectReadFailure(truncated, asharia::mesh::MeshProductErrorCode::Truncated,
                                 "truncated") &&
               expectReadFailure(magic, asharia::mesh::MeshProductErrorCode::InvalidMagic,
                                 "magic") &&
               expectReadFailure(version, asharia::mesh::MeshProductErrorCode::UnsupportedVersion,
                                 "unsupported") &&
               expectReadFailure(endian, asharia::mesh::MeshProductErrorCode::UnsupportedEndianness,
                                 "little-endian") &&
               expectReadFailure(headerSize, asharia::mesh::MeshProductErrorCode::InvalidHeader,
                                 "header byte size") &&
               expectReadFailure(vertexFormat,
                                 asharia::mesh::MeshProductErrorCode::UnsupportedVertexFormat,
                                 "vertex format") &&
               expectReadFailure(vertexStride, asharia::mesh::MeshProductErrorCode::InvalidHeader,
                                 "vertex stride") &&
               expectReadFailure(headerReserved,
                                 asharia::mesh::MeshProductErrorCode::NonCanonicalEncoding,
                                 "byte 28") &&
               expectReadFailure(offset, asharia::mesh::MeshProductErrorCode::InvalidSectionLayout,
                                 "not canonical") &&
               expectReadFailure(fileSize, asharia::mesh::MeshProductErrorCode::Truncated,
                                 "does not equal") &&
               expectReadFailure(reserved,
                                 asharia::mesh::MeshProductErrorCode::NonCanonicalEncoding,
                                 "reserved header") &&
               expectReadFailure(canonical, asharia::mesh::MeshProductErrorCode::InvalidLimits,
                                 "reader limits", {.maxProductBytes = 0U}) &&
               expectReadFailure(canonical, asharia::mesh::MeshProductErrorCode::ByteBudgetExceeded,
                                 "exceeds maxProductBytes",
                                 {.maxProductBytes = canonical.size() - 1U,
                                  .maxVertices = 4U,
                                  .maxIndices = 6U,
                                  .maxSubmeshes = 2U,
                                  .maxMaterialSlots = 2U});
    }

    [[nodiscard]] bool readerPayloadNegativeTests(const std::vector<std::byte>& canonical) {
        const std::uint64_t vertexOffset = readU64(canonical, kVertexOffsetOffset);
        const std::uint64_t indexOffset = readU64(canonical, 56U);
        const std::uint64_t submeshOffset = readU64(canonical, 64U);

        auto hugeCount = canonical;
        writeU32(hugeCount, kVertexCountOffset, (std::numeric_limits<std::uint32_t>::max)());
        auto nonFinite = canonical;
        writeF32(nonFinite, static_cast<std::size_t>(vertexOffset),
                 std::numeric_limits<float>::quiet_NaN());
        auto negativeZero = canonical;
        writeF32(negativeZero, static_cast<std::size_t>(vertexOffset + 24U), -0.0F);
        auto bounds = canonical;
        writeF32(bounds, kBoundsOffset + 12U, 1.0F);
        auto index = canonical;
        writeU32(index, static_cast<std::size_t>(indexOffset), 4U);
        auto submesh = canonical;
        writeU32(submesh, static_cast<std::size_t>(submeshOffset + 16U), 0U);
        auto material = canonical;
        writeU32(material, static_cast<std::size_t>(submeshOffset + 16U + 8U), 2U);
        auto submeshReserved = canonical;
        writeU32(submeshReserved, static_cast<std::size_t>(submeshOffset + 12U), 1U);
        auto alignmentPadding = canonical;
        constexpr std::uint64_t kFixtureIndexCount = 6U;
        const std::uint64_t indexEnd = indexOffset + (kFixtureIndexCount * sizeof(std::uint32_t));
        alignmentPadding.at(static_cast<std::size_t>(indexEnd)) = std::byte{1};

        return expectReadFailure(hugeCount, asharia::mesh::MeshProductErrorCode::CountLimitExceeded,
                                 "configured limits") &&
               expectReadFailure(nonFinite, asharia::mesh::MeshProductErrorCode::NonFiniteValue,
                                 "NaN or infinity") &&
               expectReadFailure(negativeZero,
                                 asharia::mesh::MeshProductErrorCode::NonCanonicalEncoding,
                                 "negative zero") &&
               expectReadFailure(bounds, asharia::mesh::MeshProductErrorCode::InvalidBounds,
                                 "position-derived") &&
               expectReadFailure(index, asharia::mesh::MeshProductErrorCode::InvalidIndex,
                                 "outside vertexCount") &&
               expectReadFailure(submesh, asharia::mesh::MeshProductErrorCode::InvalidSubmesh,
                                 "contiguous") &&
               expectReadFailure(material, asharia::mesh::MeshProductErrorCode::InvalidMaterialSlot,
                                 "outside materialSlotCount") &&
               expectReadFailure(submeshReserved,
                                 asharia::mesh::MeshProductErrorCode::NonCanonicalEncoding,
                                 "reserved field") &&
               expectReadFailure(alignmentPadding,
                                 asharia::mesh::MeshProductErrorCode::NonCanonicalEncoding,
                                 "alignment padding");
    }

} // namespace

// Unexpected exceptions are reported by the test executable rather than escaping main.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        if (!roundTripAndFileTest() || !writerNegativeTests()) {
            return EXIT_FAILURE;
        }
        auto canonical = asharia::mesh::writeMeshProductV1(makeMesh());
        if (!canonical || !readerHeaderNegativeTests(*canonical) ||
            !readerPayloadNegativeTests(*canonical)) {
            if (!canonical) {
                std::cerr << canonical.error().message << '\n';
            }
            return EXIT_FAILURE;
        }
        std::cout << "Mesh Product v1 bytes: " << canonical->size() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Mesh Product tests threw: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Mesh Product tests caught an unknown exception.\n";
        return EXIT_FAILURE;
    }
}
