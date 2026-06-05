#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace asharia::asset_processor {

    [[nodiscard]] std::string pathText(const std::filesystem::path& path);
    [[nodiscard]] std::string escaped(std::string_view text);
    [[nodiscard]] std::string quoteText(std::string_view text);
    [[nodiscard]] std::string quotePath(const std::filesystem::path& path);
    [[nodiscard]] std::string formatHash64(std::uint64_t value);

} // namespace asharia::asset_processor
