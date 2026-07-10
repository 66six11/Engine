#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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

    [[nodiscard]] Result<std::string_view>
    requirePresentAssetProductHeaderValue(const std::string& header, std::string_view key,
                                          std::string_view relativeProductPath);
    [[nodiscard]] Result<std::string_view>
    requirePresentAssetProductHeaderValue(std::string&& header, std::string_view key,
                                          std::string_view relativeProductPath) = delete;

} // namespace asharia::asset::detail
