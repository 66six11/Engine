#include "asharia/asset_pipeline/asset_tool_fingerprint.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "asharia/core/error.hpp"

#include "asset_tool_fingerprint_internal.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
        constexpr std::uint64_t kMaximumToolBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::size_t kToolReadBufferBytes = std::size_t{1024U} * 1024U;

        enum class AssetToolFingerprintErrorCode : std::uint8_t {
            InvalidInput = 1,
            FileInspectionFailed,
            FileTooLarge,
            FileOpenFailed,
            FileReadFailed,
        };

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string utf8 = path.generic_u8string();
            return std::string{utf8.begin(), utf8.end()};
        }

        [[nodiscard]] Error makeFingerprintError(AssetToolFingerprintErrorCode code,
                                                 std::string message) {
            return Error{ErrorDomain::Asset, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] constexpr std::uint64_t hashByte(std::uint64_t hash,
                                                       std::uint8_t byte) noexcept {
            hash ^= byte;
            hash *= kFnv1a64Prime;
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashUint64(std::uint64_t hash,
                                                         std::uint64_t value) noexcept {
            for (std::size_t index = 0; index < sizeof(value); ++index) {
                hash = hashByte(hash, static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashText(std::uint64_t hash,
                                                       std::string_view text) noexcept {
            hash = hashUint64(hash, text.size());
            for (const char character : text) {
                hash = hashByte(hash, static_cast<std::uint8_t>(character));
            }
            return hash;
        }

        [[nodiscard]] std::string lowercaseAscii(std::string text) {
            for (char& character : text) {
                if (character >= 'A' && character <= 'Z') {
                    character = static_cast<char>(character - 'A' + 'a');
                }
            }
            return text;
        }

        [[nodiscard]] Result<AssetToolFingerprint>
        fingerprintStream(std::istream& stream, std::uint64_t measuredSize,
                          std::string_view filename, std::string_view logicalToolName,
                          const detail::AssetToolFingerprintStreamLimits& limits) {
            if (logicalToolName.empty()) {
                return std::unexpected{makeFingerprintError(
                    AssetToolFingerprintErrorCode::InvalidInput,
                    "Asset tool fingerprint requires a non-empty logical tool name.")};
            }
            if (filename.empty()) {
                return std::unexpected{makeFingerprintError(
                    AssetToolFingerprintErrorCode::InvalidInput,
                    "Asset tool fingerprint requires a non-empty executable filename.")};
            }
            if (limits.maxBytes == 0U || limits.bufferBytes == 0U ||
                limits.bufferBytes >
                    static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) {
                return std::unexpected{
                    makeFingerprintError(AssetToolFingerprintErrorCode::InvalidInput,
                                         "Asset tool fingerprint stream limits are invalid.")};
            }
            if (measuredSize > limits.maxBytes) {
                return std::unexpected{makeFingerprintError(
                    AssetToolFingerprintErrorCode::FileTooLarge,
                    "Asset tool executable exceeds the fingerprint byte limit.")};
            }

            std::vector<char> buffer(limits.bufferBytes);
            std::uint64_t contentHash = kFnv1a64Offset;
            std::uint64_t totalBytes = 0U;
            while (true) {
                const std::uint64_t remaining = limits.maxBytes - totalBytes;
                const std::uint64_t probeBytes =
                    remaining == (std::numeric_limits<std::uint64_t>::max)() ? remaining
                                                                             : remaining + 1U;
                const auto requestBytes = static_cast<std::size_t>(
                    std::min<std::uint64_t>(limits.bufferBytes, probeBytes));
                stream.read(buffer.data(), static_cast<std::streamsize>(requestBytes));
                const std::streamsize extracted = stream.gcount();
                if (extracted < 0) {
                    return std::unexpected{makeFingerprintError(
                        AssetToolFingerprintErrorCode::FileReadFailed,
                        "Asset tool executable returned an invalid read count.")};
                }
                const auto extractedBytes = static_cast<std::uint64_t>(extracted);
                if (extractedBytes > remaining) {
                    return std::unexpected{
                        makeFingerprintError(AssetToolFingerprintErrorCode::FileTooLarge,
                                             "Asset tool executable grew beyond the fingerprint "
                                             "byte limit while being read.")};
                }
                for (std::streamsize index = 0; index < extracted; ++index) {
                    contentHash = hashByte(
                        contentHash,
                        static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]));
                }
                totalBytes += extractedBytes;

                if (stream.bad()) {
                    return std::unexpected{
                        makeFingerprintError(AssetToolFingerprintErrorCode::FileReadFailed,
                                             "Failed while reading asset tool executable bytes.")};
                }
                if (stream.eof()) {
                    break;
                }
                if (stream.fail() || extracted == 0) {
                    return std::unexpected{makeFingerprintError(
                        AssetToolFingerprintErrorCode::FileReadFailed,
                        "Asset tool executable read stopped before end of file.")};
                }
            }

            const std::string normalizedLogicalName = lowercaseAscii(std::string{logicalToolName});
            const std::string normalizedFilename = lowercaseAscii(std::string{filename});
            std::uint64_t versionHash = hashText(kFnv1a64Offset, "asset-tool-version-v1");
            versionHash = hashText(versionHash, normalizedLogicalName);
            versionHash = hashText(versionHash, normalizedFilename);
            versionHash = hashUint64(versionHash, totalBytes);
            versionHash = hashUint64(versionHash, contentHash);
            return AssetToolFingerprint{
                .fileSize = totalBytes,
                .contentHash = contentHash,
                .versionHash = versionHash,
            };
        }

    } // namespace

    Result<AssetToolFingerprint> fingerprintAssetTool(const std::filesystem::path& executable,
                                                      std::string_view logicalToolName) {
        std::error_code regularFileError;
        // MSVC's filesystem bitmask enum intentionally represents flag combinations that the
        // optional analyzer models as out-of-range enum values.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        if (!std::filesystem::is_regular_file(executable, regularFileError)) {
            return std::unexpected{makeFingerprintError(
                AssetToolFingerprintErrorCode::FileInspectionFailed,
                "Asset tool executable is not a readable regular file: '" + pathText(executable) +
                    "'." + (regularFileError ? " " + regularFileError.message() : ""))};
        }
        std::error_code sizeError;
        // The same MSVC STL analyzer false positive applies to the file-size query.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        const std::uintmax_t measuredSize = std::filesystem::file_size(executable, sizeError);
        if (sizeError || measuredSize > (std::numeric_limits<std::uint64_t>::max)()) {
            return std::unexpected{makeFingerprintError(
                AssetToolFingerprintErrorCode::FileInspectionFailed,
                "Could not measure asset tool executable '" + pathText(executable) + "'." +
                    (sizeError ? " " + sizeError.message() : ""))};
        }
        if (measuredSize > kMaximumToolBytes) {
            return std::unexpected{makeFingerprintError(
                AssetToolFingerprintErrorCode::FileTooLarge,
                "Asset tool executable exceeds the 2 GiB fingerprint byte limit: '" +
                    pathText(executable) + "'.")};
        }

        std::ifstream stream{executable, std::ios::binary};
        if (!stream.is_open()) {
            return std::unexpected{
                makeFingerprintError(AssetToolFingerprintErrorCode::FileOpenFailed,
                                     "Could not open asset tool executable for fingerprinting: '" +
                                         pathText(executable) + "'.")};
        }
        const std::u8string filenameUtf8 = executable.filename().generic_u8string();
        const std::string filename{filenameUtf8.begin(), filenameUtf8.end()};
        return fingerprintStream(stream, static_cast<std::uint64_t>(measuredSize), filename,
                                 logicalToolName,
                                 detail::AssetToolFingerprintStreamLimits{
                                     .maxBytes = kMaximumToolBytes,
                                     .bufferBytes = kToolReadBufferBytes,
                                 });
    }

    namespace detail {

        Result<AssetToolFingerprint> fingerprintAssetToolStreamForTesting(
            std::istream& stream, std::uint64_t measuredSize, std::string_view filename,
            std::string_view logicalToolName, const AssetToolFingerprintStreamLimits& limits) {
            return fingerprintStream(stream, measuredSize, filename, logicalToolName, limits);
        }

    } // namespace detail

} // namespace asharia::asset
