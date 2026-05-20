#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_type.hpp"

namespace asharia::asset {

    struct AssetProductKey {
        AssetGuid guid{};
        AssetTypeId assetType{};
        ImporterId importerId{};
        ImporterVersion importerVersion{};
        std::uint64_t sourceHash{};
        std::uint64_t settingsHash{};
        std::uint64_t dependencyHash{};
        std::uint64_t targetProfileHash{};

        [[nodiscard]] friend bool operator==(AssetProductKey, AssetProductKey) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(guid) && static_cast<bool>(assetType) &&
                   static_cast<bool>(importerId) && static_cast<bool>(importerVersion) &&
                   sourceHash != 0 && settingsHash != 0 && targetProfileHash != 0;
        }
    };

    struct AssetProductRecord {
        AssetProductKey key{};
        std::string relativeProductPath;
        std::uint64_t productSizeBytes{};
        std::uint64_t productHash{};

        [[nodiscard]] friend bool operator==(const AssetProductRecord&,
                                             const AssetProductRecord&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(key) && !relativeProductPath.empty() && productHash != 0;
        }
    };

    enum class AssetDependencyKind {
        SourceFile,
        AssetReference,
        ToolVersion,
        ImportSettings,
    };

    struct AssetDependency {
        AssetGuid owner{};
        AssetDependencyKind kind{AssetDependencyKind::SourceFile};
        AssetGuid asset{};
        std::string path;
        std::uint64_t hash{};

        [[nodiscard]] friend bool operator==(const AssetDependency&,
                                             const AssetDependency&) = default;
    };

    [[nodiscard]] std::uint64_t makeAssetTargetProfileHash(std::string_view targetProfile) noexcept;
    [[nodiscard]] std::uint64_t
    hashAssetDependencies(std::span<const AssetDependency> dependencies) noexcept;
    [[nodiscard]] AssetProductKey makeAssetProductKey(const SourceAssetRecord& source,
                                                      std::uint64_t dependencyHash,
                                                      std::uint64_t targetProfileHash) noexcept;
    [[nodiscard]] std::uint64_t hashAssetProductKey(const AssetProductKey& key) noexcept;

} // namespace asharia::asset
