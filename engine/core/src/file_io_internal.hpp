#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>

#include "asharia/core/file_io.hpp"

namespace asharia::core::detail {

    class AtomicTemporaryFile {
    public:
        virtual ~AtomicTemporaryFile() = default;

        [[nodiscard]] virtual Result<std::size_t> write(std::span<const std::byte> bytes) = 0;
        [[nodiscard]] virtual VoidResult flush() = 0;
        [[nodiscard]] virtual VoidResult close() = 0;
        [[nodiscard]] virtual const std::filesystem::path& path() const noexcept = 0;
        virtual void releaseAfterReplace() noexcept = 0;
    };

    class AtomicFileBackend {
    public:
        virtual ~AtomicFileBackend() = default;

        [[nodiscard]] virtual Result<std::unique_ptr<AtomicTemporaryFile>>
        createUniqueTemporary(const std::filesystem::path& target) = 0;

        [[nodiscard]] virtual VoidResult replace(const std::filesystem::path& temporary,
                                                 const std::filesystem::path& target) = 0;
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
