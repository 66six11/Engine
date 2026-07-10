#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>

#include "asharia/core/file_io.hpp"

namespace asharia::core::detail {

    class AtomicFileBackend {
    public:
        virtual ~AtomicFileBackend() = default;

        [[nodiscard]] virtual Result<std::filesystem::path>
        writeUniqueTemporary(const std::filesystem::path& target, std::span<const std::byte> bytes,
                             AtomicFileWriteOptions options) = 0;

        [[nodiscard]] virtual VoidResult replace(const std::filesystem::path& temporary,
                                                 const std::filesystem::path& target) = 0;

        virtual void removeTemporary(const std::filesystem::path& temporary) noexcept = 0;
    };

    [[nodiscard]] Result<std::vector<std::byte>>
    readBoundedStream(std::istream& stream, std::uint64_t measuredBytes, FileReadLimits limits,
                      const std::filesystem::path& path);

    [[nodiscard]] VoidResult
    writeFileBytesAtomicallyWithBackend(const std::filesystem::path& target,
                                        std::span<const std::byte> bytes,
                                        AtomicFileWriteOptions options, AtomicFileBackend& backend);

    [[nodiscard]] AtomicFileBackend& atomicFileBackend();

} // namespace asharia::core::detail
