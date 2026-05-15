#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_type.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    inline constexpr std::string_view kAssetMetadataSchema = "com.asharia.asset.metadata";
    inline constexpr std::uint32_t kAssetMetadataVersion = 1;

    struct ImporterId {
        std::uint64_t value{};

        [[nodiscard]] friend bool operator==(ImporterId, ImporterId) = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return value != 0;
        }
    };

    struct ImporterVersion {
        std::uint32_t value{};

        [[nodiscard]] friend bool operator==(ImporterVersion, ImporterVersion) = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return value != 0;
        }
    };

    struct AssetImportSetting {
        std::string key;
        std::string value;

        [[nodiscard]] friend bool operator==(const AssetImportSetting&,
                                             const AssetImportSetting&) = default;
    };

    struct SourceAssetRecord {
        AssetGuid guid{};
        AssetTypeId assetType{};
        std::string assetTypeName;
        std::string sourcePath;
        ImporterId importerId{};
        std::string importerName;
        ImporterVersion importerVersion{};
        std::uint64_t sourceHash{};
        std::uint64_t settingsHash{};

        [[nodiscard]] friend bool operator==(const SourceAssetRecord&,
                                             const SourceAssetRecord&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(guid) && static_cast<bool>(assetType) &&
                   !assetTypeName.empty() && !sourcePath.empty() && static_cast<bool>(importerId) &&
                   !importerName.empty() && static_cast<bool>(importerVersion);
        }
    };

    [[nodiscard]] ImporterId makeImporterId(std::string_view importerName) noexcept;
    [[nodiscard]] std::uint64_t
    hashAssetImportSettings(std::span<const AssetImportSetting> settings) noexcept;

    [[nodiscard]] VoidResult validateSourceAssetRecord(const SourceAssetRecord& record);
    [[nodiscard]] VoidResult validateSourceAssetRecords(std::span<const SourceAssetRecord> records);

} // namespace asharia::asset
