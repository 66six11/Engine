#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::project {

    inline constexpr std::string_view kAshariaProjectSchema = "com.asharia.project";
    inline constexpr std::uint32_t kAshariaProjectSchemaVersion = 1;
    inline constexpr std::string_view kDefaultAshariaProjectFileName = "asharia.project.json";

    struct ProjectId {
        std::array<std::uint8_t, 16> bytes{};

        [[nodiscard]] friend bool operator==(ProjectId, ProjectId) = default;
        [[nodiscard]] explicit operator bool() const noexcept;
    };

    struct AssetSourceRootDesc {
        std::string rootName;
        std::string directory;
        std::string sourcePathPrefix;

        [[nodiscard]] friend bool operator==(const AssetSourceRootDesc&,
                                             const AssetSourceRootDesc&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !rootName.empty() && !directory.empty() && !sourcePathPrefix.empty();
        }
    };

    struct AssetDiscoveryDesc {
        std::vector<std::string> ignoredDirectoryNames;

        [[nodiscard]] friend bool operator==(const AssetDiscoveryDesc&,
                                             const AssetDiscoveryDesc&) = default;
    };

    struct AshariaProjectDescriptor {
        std::string projectName;
        ProjectId projectId{};
        std::vector<AssetSourceRootDesc> assetSourceRoots;
        std::string assetCacheRoot;
        AssetDiscoveryDesc assetDiscovery;

        [[nodiscard]] friend bool operator==(const AshariaProjectDescriptor&,
                                             const AshariaProjectDescriptor&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !projectName.empty() && static_cast<bool>(projectId) &&
                   !assetSourceRoots.empty() && !assetCacheRoot.empty();
        }
    };

    [[nodiscard]] Result<ProjectId> parseProjectId(std::string_view text);
    [[nodiscard]] std::string formatProjectId(ProjectId projectId);

    [[nodiscard]] VoidResult validateProjectRelativePath(std::string_view path,
                                                         std::string_view context);
    [[nodiscard]] VoidResult
    validateAshariaProjectDescriptor(const AshariaProjectDescriptor& descriptor);

} // namespace asharia::project
