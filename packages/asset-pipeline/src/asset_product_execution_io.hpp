#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::asset::detail {

    inline constexpr std::uint64_t kMaxShaderToolDiagnosticBytes = 16ULL * 1024ULL * 1024ULL;
    inline constexpr std::uint64_t kMaxShaderToolBinaryBytes = 512ULL * 1024ULL * 1024ULL;

    [[nodiscard]] Result<std::string>
    readShaderToolDiagnostic(const std::filesystem::path& path,
                             std::uint64_t maxBytes = kMaxShaderToolDiagnosticBytes);

    [[nodiscard]] Result<std::vector<std::uint8_t>>
    readShaderToolBinary(const std::filesystem::path& path, std::string_view outputKind,
                         std::uint64_t maxBytes = kMaxShaderToolBinaryBytes);

    [[nodiscard]] VoidResult writeGeneratedSlangSource(const std::filesystem::path& path,
                                                       std::string_view text);

} // namespace asharia::asset::detail
