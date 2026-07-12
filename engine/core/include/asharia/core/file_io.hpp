#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::core {

    struct FileReadLimits {
        std::uint64_t maxBytes{};
    };

    struct AtomicFileWriteOptions {
        bool flushFileBuffers{true};
    };

    [[nodiscard]] Result<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path,
                                                               FileReadLimits limits);

    [[nodiscard]] Result<std::string> readFileText(const std::filesystem::path& path,
                                                   FileReadLimits limits);

    [[nodiscard]] VoidResult writeFileBytesAtomically(const std::filesystem::path& path,
                                                      std::span<const std::byte> bytes,
                                                      AtomicFileWriteOptions options = {});

    [[nodiscard]] VoidResult writeFileTextAtomically(const std::filesystem::path& path,
                                                     std::string_view text,
                                                     AtomicFileWriteOptions options = {});

} // namespace asharia::core
