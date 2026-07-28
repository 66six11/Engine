#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/core/file_io.hpp"

namespace asharia::core::detail {

    [[nodiscard]] std::string filePathToUtf8(const std::filesystem::path& path);

    class ExclusiveFileLockHandle {
    public:
        virtual ~ExclusiveFileLockHandle() = default;

        [[nodiscard]] virtual bool ownsLock() const noexcept = 0;
        [[nodiscard]] virtual VoidResult release() = 0;
    };

    class ExclusiveFileLockBackend {
    public:
        virtual ~ExclusiveFileLockBackend() = default;

        // A null handle is ordinary lock contention; an Error is an acquisition failure.
        [[nodiscard]] virtual Result<std::unique_ptr<ExclusiveFileLockHandle>>
        tryAcquire(const std::filesystem::path& lockPath) = 0;
    };

    struct ExclusiveFileLockFactory {
        [[nodiscard]] static ExclusiveFileLock
        create(std::unique_ptr<ExclusiveFileLockHandle> handle) noexcept;
    };

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

        [[nodiscard]] virtual Result<std::unique_ptr<AtomicTemporaryFile>>
        createStaged(const std::filesystem::path& target, const std::filesystem::path& staged) = 0;

        [[nodiscard]] virtual StagedFileArtifactState
        inspectArtifactState(const std::filesystem::path& path) noexcept = 0;

        [[nodiscard]] virtual AtomicReplaceOutcome replace(const std::filesystem::path& temporary,
                                                           const std::filesystem::path& target) = 0;

        [[nodiscard]] virtual StagedFileReplacementOutcome
        replaceStaged(const std::filesystem::path& target, const std::filesystem::path& staged,
                      const std::filesystem::path& backup) = 0;
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
        [[nodiscard]] virtual std::optional<bool>
        fileExists(const std::filesystem::path& path) noexcept = 0;
        virtual void reportWarning(std::string_view warning) noexcept = 0;
    };

    [[nodiscard]] AtomicReplaceOutcome replaceExistingWindowsFileWithRecovery(
        const std::filesystem::path& target, const std::filesystem::path& replacement,
        const std::filesystem::path& backup, WindowsReplaceOperations& operations);

    [[nodiscard]] bool isWindowsRegularFileAttributes(std::uint32_t attributes) noexcept;

    [[nodiscard]] StagedFileReplacementOutcome replaceExistingWindowsStagedFileWithRecovery(
        const std::filesystem::path& target, const std::filesystem::path& staged,
        const std::filesystem::path& backup, WindowsReplaceOperations& operations);
#else
    class PosixReplaceOperations {
    public:
        virtual ~PosixReplaceOperations() = default;

        [[nodiscard]] virtual int createLink(const std::filesystem::path& existing,
                                             const std::filesystem::path& linkPath) = 0;
        [[nodiscard]] virtual int renameFile(const std::filesystem::path& source,
                                             const std::filesystem::path& target) = 0;
        [[nodiscard]] virtual std::optional<bool>
        fileExists(const std::filesystem::path& path) noexcept = 0;
    };

    [[nodiscard]] StagedFileReplacementOutcome replaceExistingPosixStagedFile(
        const std::filesystem::path& target, const std::filesystem::path& staged,
        const std::filesystem::path& backup, PosixReplaceOperations& operations);
#endif

    [[nodiscard]] Result<std::vector<std::byte>>
    readBoundedStream(std::istream& stream, std::uint64_t measuredBytes, FileReadLimits limits,
                      const std::filesystem::path& path);

    [[nodiscard]] VoidResult
    writeFileBytesAtomicallyWithBackend(const std::filesystem::path& target,
                                        std::span<const std::byte> bytes,
                                        AtomicFileWriteOptions options, AtomicFileBackend& backend);

    [[nodiscard]] StagedFilePreparationOutcome
    prepareStagedFileBytesWithBackend(const std::filesystem::path& target,
                                      const std::filesystem::path& staged,
                                      std::span<const std::byte> bytes, AtomicFileBackend& backend);

    [[nodiscard]] StagedFileReplacementOutcome replaceFileFromStagedWithBackend(
        const std::filesystem::path& target, const std::filesystem::path& staged,
        const std::filesystem::path& backup, AtomicFileBackend& backend);

    [[nodiscard]] Result<std::optional<ExclusiveFileLock>>
    tryAcquireExclusiveFileLockWithBackend(const std::filesystem::path& lockPath,
                                           ExclusiveFileLockBackend& backend);

    [[nodiscard]] AtomicFileBackend& atomicFileBackend();
    [[nodiscard]] ExclusiveFileLockBackend& exclusiveFileLockBackend();

} // namespace asharia::core::detail
