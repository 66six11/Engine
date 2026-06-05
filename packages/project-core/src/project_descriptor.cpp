#include "asharia/project/project_descriptor.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::project {
    namespace {

        constexpr std::size_t kUuidTextLength = 36;
        constexpr std::array<std::size_t, 4> kHyphenPositions{8, 13, 18, 23};

        [[nodiscard]] bool isHyphenPosition(std::size_t index) noexcept {
            return std::ranges::find(kHyphenPositions, index) != kHyphenPositions.end();
        }

        [[nodiscard]] bool isAsciiAlpha(char character) noexcept {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        [[nodiscard]] int hexValue(char character) noexcept {
            if (character >= '0' && character <= '9') {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f') {
                return 10 + character - 'a';
            }
            if (character >= 'A' && character <= 'F') {
                return 10 + character - 'A';
            }
            return -1;
        }

        [[nodiscard]] Error projectError(std::string message) {
            return Error{ErrorDomain::Project, 1, std::move(message)};
        }

        [[nodiscard]] Error projectIdError(std::string_view text, std::string_view reason) {
            return projectError("Invalid Asharia project id \"" + std::string{text} +
                                "\": " + std::string{reason});
        }

        [[nodiscard]] bool isValidSinglePathSegment(std::string_view text) noexcept {
            return !text.empty() && text != "." && text != ".." &&
                   text.find('/') == std::string_view::npos &&
                   text.find('\\') == std::string_view::npos;
        }

        [[nodiscard]] VoidResult validateRootName(std::string_view rootName, std::size_t index) {
            if (isValidSinglePathSegment(rootName)) {
                return {};
            }

            return std::unexpected{
                projectError("Asharia project assetSourceRoots[" + std::to_string(index) +
                             "].rootName must be a non-empty single path segment.")};
        }

        [[nodiscard]] VoidResult validateIgnoredDirectoryName(std::string_view name,
                                                              std::size_t index) {
            if (isValidSinglePathSegment(name)) {
                return {};
            }

            return std::unexpected{
                projectError("Asharia project assetDiscovery.ignoredDirectories[" +
                             std::to_string(index) + "] must be a non-empty single path segment.")};
        }

        [[nodiscard]] VoidResult validateAssetSourceRoot(const AssetSourceRootDesc& root,
                                                         std::size_t index) {
            if (auto validName = validateRootName(root.rootName, index); !validName) {
                return std::unexpected{std::move(validName.error())};
            }

            if (auto validDirectory = validateProjectRelativePath(
                    root.directory, "assetSourceRoots[" + std::to_string(index) + "].directory");
                !validDirectory) {
                return std::unexpected{std::move(validDirectory.error())};
            }

            if (auto validPrefix = validateProjectRelativePath(
                    root.sourcePathPrefix,
                    "assetSourceRoots[" + std::to_string(index) + "].sourcePathPrefix");
                !validPrefix) {
                return std::unexpected{std::move(validPrefix.error())};
            }

            return {};
        }

    } // namespace

    ProjectId::operator bool() const noexcept {
        return std::ranges::any_of(bytes, [](std::uint8_t byte) { return byte != 0; });
    }

    Result<ProjectId> parseProjectId(std::string_view text) {
        if (text.size() != kUuidTextLength) {
            return std::unexpected{projectIdError(text, "expected 36 characters")};
        }

        ProjectId projectId{};
        auto byte = projectId.bytes.begin();
        bool highNibble = true;
        for (std::size_t index = 0; index < text.size(); ++index) {
            const char character = text[index];
            if (isHyphenPosition(index)) {
                if (character != '-') {
                    return std::unexpected{projectIdError(text, "expected hyphen separators")};
                }
                continue;
            }

            const int value = hexValue(character);
            if (value < 0) {
                return std::unexpected{projectIdError(text, "expected hexadecimal digits")};
            }

            if (highNibble) {
                *byte = static_cast<std::uint8_t>(value << 4);
            } else {
                *byte = static_cast<std::uint8_t>(*byte | value);
                ++byte;
            }
            highNibble = !highNibble;
        }

        if (!projectId) {
            return std::unexpected{projectIdError(text, "zero project id is invalid")};
        }

        return projectId;
    }

    std::string formatProjectId(ProjectId projectId) {
        constexpr std::string_view kHexDigits = "0123456789abcdef";

        std::string text;
        text.reserve(kUuidTextLength);
        std::size_t byteIndex = 0;
        for (const std::uint8_t byte : projectId.bytes) {
            if (byteIndex == 4 || byteIndex == 6 || byteIndex == 8 || byteIndex == 10) {
                text.push_back('-');
            }

            text.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
            text.push_back(kHexDigits[byte & 0x0FU]);
            ++byteIndex;
        }

        return text;
    }

    VoidResult validateProjectRelativePath(std::string_view path, std::string_view context) {
        if (path.empty()) {
            return std::unexpected{
                projectError("Asharia project " + std::string{context} + " is missing.")};
        }

        if (path.find('\\') != std::string_view::npos) {
            return std::unexpected{projectError("Asharia project " + std::string{context} +
                                                " must use '/' separators.")};
        }

        if (path.front() == '/') {
            return std::unexpected{projectError("Asharia project " + std::string{context} +
                                                " must be project-relative.")};
        }

        if (path.size() >= 2 && isAsciiAlpha(path[0]) && path[1] == ':') {
            return std::unexpected{projectError("Asharia project " + std::string{context} +
                                                " must not use a drive prefix.")};
        }

        std::size_t segmentStart = 0;
        while (segmentStart <= path.size()) {
            const std::size_t segmentEnd = path.find('/', segmentStart);
            const std::size_t clampedEnd =
                segmentEnd == std::string_view::npos ? path.size() : segmentEnd;
            const std::string_view segment = path.substr(segmentStart, clampedEnd - segmentStart);

            if (segment.empty()) {
                return std::unexpected{projectError("Asharia project " + std::string{context} +
                                                    " contains an empty path segment.")};
            }

            if (segment == "." || segment == "..") {
                return std::unexpected{projectError("Asharia project " + std::string{context} +
                                                    " must not contain '.' or '..' segments.")};
            }

            if (segmentEnd == std::string_view::npos) {
                break;
            }
            segmentStart = segmentEnd + 1;
        }

        return {};
    }

    VoidResult validateAshariaProjectDescriptor(const AshariaProjectDescriptor& descriptor) {
        if (descriptor.projectName.empty()) {
            return std::unexpected{projectError("Asharia project name is missing.")};
        }

        if (!descriptor.projectId) {
            return std::unexpected{projectError("Asharia project id is invalid.")};
        }

        if (descriptor.assetSourceRoots.empty()) {
            return std::unexpected{
                projectError("Asharia project assetSourceRoots must not be empty.")};
        }

        for (std::size_t index = 0; index < descriptor.assetSourceRoots.size(); ++index) {
            const AssetSourceRootDesc& root = descriptor.assetSourceRoots[index];
            if (auto validRoot = validateAssetSourceRoot(root, index); !validRoot) {
                return std::unexpected{std::move(validRoot.error())};
            }

            for (std::size_t otherIndex = index + 1;
                 otherIndex < descriptor.assetSourceRoots.size(); ++otherIndex) {
                const AssetSourceRootDesc& other = descriptor.assetSourceRoots[otherIndex];
                if (root.rootName == other.rootName) {
                    return std::unexpected{
                        projectError("Asharia project assetSourceRoots duplicate rootName \"" +
                                     root.rootName + "\".")};
                }
                if (root.directory == other.directory) {
                    return std::unexpected{
                        projectError("Asharia project assetSourceRoots duplicate directory \"" +
                                     root.directory + "\".")};
                }
                if (root.sourcePathPrefix == other.sourcePathPrefix) {
                    return std::unexpected{projectError(
                        "Asharia project assetSourceRoots duplicate sourcePathPrefix \"" +
                        root.sourcePathPrefix + "\".")};
                }
            }
        }

        if (auto validCacheRoot =
                validateProjectRelativePath(descriptor.assetCacheRoot, "assetCacheRoot");
            !validCacheRoot) {
            return std::unexpected{std::move(validCacheRoot.error())};
        }

        for (std::size_t index = 0; index < descriptor.assetDiscovery.ignoredDirectoryNames.size();
             ++index) {
            const std::string& name = descriptor.assetDiscovery.ignoredDirectoryNames[index];
            if (auto validName = validateIgnoredDirectoryName(name, index); !validName) {
                return std::unexpected{std::move(validName.error())};
            }

            for (std::size_t otherIndex = index + 1;
                 otherIndex < descriptor.assetDiscovery.ignoredDirectoryNames.size();
                 ++otherIndex) {
                if (name == descriptor.assetDiscovery.ignoredDirectoryNames[otherIndex]) {
                    return std::unexpected{projectError(
                        "Asharia project assetDiscovery.ignoredDirectories duplicate name \"" +
                        name + "\".")};
                }
            }
        }

        return {};
    }

} // namespace asharia::project
