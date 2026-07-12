#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <string_view>

#include "asharia/core/file_io.hpp"

namespace asharia::core::detail {

    enum class AtomicReplaceCommitState : std::uint8_t {
        NotReached,
        Committed,
        Indeterminate,
    };

    enum class AtomicTemporaryDisposition : std::uint8_t {
        Cleanup,
        Preserve,
    };

    struct AtomicReplaceOutcome {
        AtomicReplaceCommitState commitState{AtomicReplaceCommitState::NotReached};
        AtomicTemporaryDisposition temporaryDisposition{AtomicTemporaryDisposition::Cleanup};
        std::optional<Error> error;
    };

    class AtomicTemporaryFile {
    public:
        virtual ~AtomicTemporaryFile() = default;

        [[nodiscard]] virtual Result<std::size_t> write(std::span<const std::byte> bytes) = 0;
        [[nodiscard]] virtual VoidResult flush() = 0;
        [[nodiscard]] virtual VoidResult close() = 0;
        [[nodiscard]] virtual const std::filesystem::path& path() const noexcept = 0;
        virtual void releaseCleanupOwnership() noexcept = 0;
    };

    class AtomicFileBackend {
    public:
        virtual ~AtomicFileBackend() = default;

        [[nodiscard]] virtual Result<std::unique_ptr<AtomicTemporaryFile>>
        createUniqueTemporary(const std::filesystem::path& target) = 0;

        [[nodiscard]] virtual AtomicReplaceOutcome replace(const std::filesystem::path& temporary,
                                                           const std::filesystem::path& target) = 0;
    };

#if defined(_WIN32)
    class WindowsReplaceOperations {
    public:
        virtual ~WindowsReplaceOperations() = default;

        [[nodiscard]] virtual std::uint32_t replaceFile(const std::filesystem::path& target,
                                                        const std::filesystem::path& replacement,
                                                        const std::filesystem::path& backup) = 0;
        [[nodiscard]] virtual std::uint32_t moveFile(const std::filesystem::path& source,
                                                     const std::filesystem::path& target) = 0;
        [[nodiscard]] virtual std::uint32_t deleteFile(const std::filesystem::path& path) = 0;
        virtual void reportWarning(std::string_view warning) noexcept = 0;
    };

    [[nodiscard]] AtomicReplaceOutcome replaceExistingWindowsFileWithRecovery(
        const std::filesystem::path& target, const std::filesystem::path& replacement,
        const std::filesystem::path& backup, WindowsReplaceOperations& operations);
#endif

    [[nodiscard]] Result<std::vector<std::byte>>
    readBoundedStream(std::istream& stream, std::uint64_t measuredBytes, FileReadLimits limits,
                      const std::filesystem::path& path);

    [[nodiscard]] VoidResult
    writeFileBytesAtomicallyWithBackend(const std::filesystem::path& target,
                                        std::span<const std::byte> bytes,
                                        AtomicFileWriteOptions options, AtomicFileBackend& backend);

    [[nodiscard]] AtomicFileBackend& atomicFileBackend();

} // namespace asharia::core::detail
