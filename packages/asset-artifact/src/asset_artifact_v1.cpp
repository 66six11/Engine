#include "asharia/asset_artifact/asset_artifact_v1.hpp"

#include <cctype>
#include <expected>
#include <string>
#include <utility>

#include "asharia/core/file_io.hpp"

namespace asharia::asset {
    namespace {

        [[nodiscard]] Error artifactError(AssetArtifactErrorCode code, std::string message) {
            return Error{ErrorDomain::Asset, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] std::string artifactLabel(std::string_view relativePath) {
            return "Asset artifact relativePath=\"" + std::string{relativePath} + "\"";
        }

        [[nodiscard]] bool isAsciiAlpha(char value) noexcept {
            const auto character = static_cast<unsigned char>(value);
            return std::isalpha(character) != 0;
        }

        [[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
            std::u8string utf8;
            utf8.reserve(text.size());
            for (const char value : text) {
                utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
            }
            return std::filesystem::path{utf8};
        }

    } // namespace

    VoidResult validateAssetArtifactRelativePathV1(std::string_view relativePath) {
        auto invalid = [relativePath](std::string reason) {
            return artifactError(AssetArtifactErrorCode::InvalidPath,
                                 artifactLabel(relativePath) + " is invalid: " + std::move(reason) +
                                     ".");
        };

        if (relativePath.empty()) {
            return std::unexpected{invalid("path is missing")};
        }
        if (relativePath.find('\\') != std::string_view::npos) {
            return std::unexpected{invalid("path must use '/' separators")};
        }
        if (relativePath.front() == '/') {
            return std::unexpected{invalid("path must be artifact-root-relative")};
        }
        if (relativePath.size() >= 2U && isAsciiAlpha(relativePath[0]) && relativePath[1] == ':') {
            return std::unexpected{invalid("path must not use a drive prefix")};
        }

        std::size_t segmentStart = 0U;
        while (segmentStart <= relativePath.size()) {
            const std::size_t segmentEnd = relativePath.find('/', segmentStart);
            const std::size_t end =
                segmentEnd == std::string_view::npos ? relativePath.size() : segmentEnd;
            const std::string_view segment = relativePath.substr(segmentStart, end - segmentStart);
            if (segment.empty()) {
                return std::unexpected{invalid("path contains an empty segment")};
            }
            if (segment == "." || segment == "..") {
                return std::unexpected{invalid("path contains a traversal segment")};
            }
            if (segmentEnd == std::string_view::npos) {
                break;
            }
            segmentStart = segmentEnd + 1U;
        }

        return {};
    }

    std::uint64_t hashAssetArtifactBytesV1(std::span<const std::byte> bytes) noexcept {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const std::byte value : bytes) {
            hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(value));
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    Result<AssetArtifactLocatorV1> makeAssetArtifactLocatorV1(const AssetProductRecord& product) {
        if (!product.key || product.relativeProductPath.empty() || product.productHash == 0U) {
            return std::unexpected{
                artifactError(AssetArtifactErrorCode::InvalidLocator,
                              artifactLabel(product.relativeProductPath) +
                                  " cannot form a locator from an invalid product record.")};
        }
        if (auto validPath = validateAssetArtifactRelativePathV1(product.relativeProductPath);
            !validPath) {
            return std::unexpected{std::move(validPath.error())};
        }

        return AssetArtifactLocatorV1{.relativePath = product.relativeProductPath,
                                      .expectedBytes = product.productSizeBytes,
                                      .expectedHash = product.productHash};
    }

    Result<VerifiedAssetArtifactV1>
    readVerifiedAssetArtifactV1(const std::filesystem::path& artifactRoot,
                                const AssetArtifactLocatorV1& locator,
                                AssetArtifactReadLimits limits) {
        if (artifactRoot.empty()) {
            return std::unexpected{artifactError(AssetArtifactErrorCode::InvalidLocator,
                                                 artifactLabel(locator.relativePath) +
                                                     " rejected an empty artifact root.")};
        }
        if (!locator || locator.expectedHash == 0U) {
            return std::unexpected{artifactError(AssetArtifactErrorCode::InvalidLocator,
                                                 artifactLabel(locator.relativePath) +
                                                     " rejected an invalid locator.")};
        }
        if (auto validPath = validateAssetArtifactRelativePathV1(locator.relativePath);
            !validPath) {
            return std::unexpected{std::move(validPath.error())};
        }
        if (limits.maxBytes == 0U) {
            return std::unexpected{artifactError(AssetArtifactErrorCode::InvalidLimits,
                                                 artifactLabel(locator.relativePath) +
                                                     " rejected a zero byte read limit.")};
        }
        if (locator.expectedBytes > limits.maxBytes) {
            return std::unexpected{
                artifactError(AssetArtifactErrorCode::ByteBudgetExceeded,
                              artifactLabel(locator.relativePath) +
                                  " expectedBytes=" + std::to_string(locator.expectedBytes) +
                                  " exceeds maxBytes=" + std::to_string(limits.maxBytes) + ".")};
        }

        auto bytes = core::readFileBytes(artifactRoot / pathFromUtf8(locator.relativePath),
                                         core::FileReadLimits{.maxBytes = limits.maxBytes});
        if (!bytes) {
            return std::unexpected{
                artifactError(AssetArtifactErrorCode::FileReadFailed,
                              artifactLabel(locator.relativePath) +
                                  " could not be read within the configured byte limit.")};
        }
        if (bytes->size() != locator.expectedBytes) {
            return std::unexpected{
                artifactError(AssetArtifactErrorCode::SizeMismatch,
                              artifactLabel(locator.relativePath) +
                                  " expectedBytes=" + std::to_string(locator.expectedBytes) +
                                  " actualBytes=" + std::to_string(bytes->size()) + ".")};
        }

        const std::uint64_t actualHash = hashAssetArtifactBytesV1(*bytes);
        if (actualHash != locator.expectedHash) {
            return std::unexpected{
                artifactError(AssetArtifactErrorCode::HashMismatch,
                              artifactLabel(locator.relativePath) +
                                  " expectedHash=" + std::to_string(locator.expectedHash) +
                                  " actualHash=" + std::to_string(actualHash) + ".")};
        }

        return VerifiedAssetArtifactV1{.locator = locator, .bytes = std::move(*bytes)};
    }

    const char* assetArtifactErrorCodeName(AssetArtifactErrorCode code) noexcept {
        switch (code) {
        case AssetArtifactErrorCode::InvalidPath:
            return "InvalidPath";
        case AssetArtifactErrorCode::InvalidLocator:
            return "InvalidLocator";
        case AssetArtifactErrorCode::InvalidLimits:
            return "InvalidLimits";
        case AssetArtifactErrorCode::ByteBudgetExceeded:
            return "ByteBudgetExceeded";
        case AssetArtifactErrorCode::FileReadFailed:
            return "FileReadFailed";
        case AssetArtifactErrorCode::SizeMismatch:
            return "SizeMismatch";
        case AssetArtifactErrorCode::HashMismatch:
            return "HashMismatch";
        }
        return "Unknown";
    }

} // namespace asharia::asset
