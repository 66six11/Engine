#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "asharia/core/result.hpp"

namespace asharia::asset::detail {

    struct AssetProductRecordLimitRequest {
        std::uint64_t count{};
        std::uint64_t hardLimit{};
        std::size_t headerLineCount{};
        std::size_t minimumLinesPerRecord{};
        std::string_view recordName;
        std::string_view relativeProductPath;
    };

    [[nodiscard]] Result<std::size_t>
    validateAssetProductRecordCount(const AssetProductRecordLimitRequest& request);

} // namespace asharia::asset::detail
