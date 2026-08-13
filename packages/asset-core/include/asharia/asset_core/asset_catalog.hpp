#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/core/result.hpp"

namespace asharia::asset {

    enum class AssetCatalogRelocationPolicy {
        RejectPathChange,
        AllowPathChange,
    };

    class AssetCatalog {
    public:
        [[nodiscard]] VoidResult addSource(SourceAssetRecord record);
        [[nodiscard]] VoidResult updateSource(SourceAssetRecord record,
                                              AssetCatalogRelocationPolicy relocationPolicy =
                                                  AssetCatalogRelocationPolicy::RejectPathChange);
        [[nodiscard]] VoidResult removeSource(AssetGuid guid);

        [[nodiscard]] const SourceAssetRecord* findByGuid(AssetGuid guid) const noexcept;
        [[nodiscard]] const SourceAssetRecord*
        findBySourcePath(std::string_view sourcePath) const noexcept;
        [[nodiscard]] std::span<const SourceAssetRecord> sources() const noexcept;

    private:
        using GuidBytes = std::array<std::uint8_t, 16>;

        std::vector<SourceAssetRecord> sources_;
        std::map<GuidBytes, std::size_t> sourceIndicesByGuid_;
        std::map<std::string, std::size_t, std::less<>> sourceIndicesByPath_;
    };

} // namespace asharia::asset
