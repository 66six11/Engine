#include "asharia/core/file_io.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "file_io_internal.hpp"

namespace asharia::core {
    namespace {

        [[nodiscard]] Error fileIoError(std::string_view action, const std::filesystem::path& path,
                                        std::string_view reason) {
            return Error{ErrorDomain::Core, 0,
                         "Core file " + std::string{action} + " failed for '" +
                             detail::filePathToUtf8(path) + "': " + std::string{reason} + "."};
        }

        [[nodiscard]] Error stagedReplacementContractError(const std::filesystem::path& target,
                                                           const std::filesystem::path& staged,
                                                           const std::filesystem::path& backup,
                                                           std::string_view reason) {
            return Error{ErrorDomain::Core, 0,
                         "Core staged file replacement rejected target='" +
                             detail::filePathToUtf8(target) + "' staged='" +
                             detail::filePathToUtf8(staged) + "' backup='" +
                             detail::filePathToUtf8(backup) + "': " + std::string{reason} + "."};
        }

        [[nodiscard]] Error stagedPreparationContractError(const std::filesystem::path& target,
                                                           const std::filesystem::path& staged,
                                                           std::string_view reason) {
            return Error{ErrorDomain::Core, 0,
                         "Core staged file preparation rejected target='" +
                             detail::filePathToUtf8(target) + "' staged='" +
                             detail::filePathToUtf8(staged) + "': " + std::string{reason} + "."};
        }

        [[nodiscard]] StagedFilePreparationOutcome
        stagedPreparationFailure(std::unique_ptr<detail::AtomicTemporaryFile>& stagedFile,
                                 const std::filesystem::path& staged,
                                 detail::AtomicFileBackend& backend, Error error) {
            stagedFile->releaseCleanupOwnership();
            stagedFile.reset();
            return {
                .stagedFileState = backend.inspectArtifactState(staged),
                .error = std::move(error),
            };
        }

    } // namespace

    namespace detail {

        std::string filePathToUtf8(const std::filesystem::path& path) {
            const std::u8string utf8 = path.u8string();
            std::string text;
            text.reserve(utf8.size());
            for (const char8_t character : utf8) {
                text.push_back(static_cast<char>(character));
            }
            return text;
        }

        ExclusiveFileLock
        ExclusiveFileLockFactory::create(std::unique_ptr<ExclusiveFileLockHandle> handle) noexcept {
            return ExclusiveFileLock{std::move(handle)};
        }

        Result<std::optional<ExclusiveFileLock>>
        tryAcquireExclusiveFileLockWithBackend(const std::filesystem::path& lockPath,
                                               ExclusiveFileLockBackend& backend) {
            if (lockPath.empty()) {
                return std::unexpected{
                    fileIoError("lock acquisition", lockPath, "lock path must be non-empty")};
            }

            auto handle = backend.tryAcquire(lockPath);
            if (!handle) {
                return std::unexpected{std::move(handle.error())};
            }
            if (*handle == nullptr) {
                return std::optional<ExclusiveFileLock>{};
            }
            return std::optional<ExclusiveFileLock>{
                ExclusiveFileLockFactory::create(std::move(*handle))};
        }

        Result<std::vector<std::byte>> readBoundedStream(std::istream& stream,
                                                         std::uint64_t measuredBytes,
                                                         FileReadLimits limits,
                                                         const std::filesystem::path& path) {
            if (limits.maxBytes == 0U) {
                return std::unexpected{fileIoError("read", path,
                                                   "invalid configured read limit maxBytes=0; "
                                                   "maxBytes must be greater than zero")};
            }
            if (measuredBytes > limits.maxBytes) {
                return std::unexpected{fileIoError(
                    "read", path,
                    "observedBytes=" + std::to_string(measuredBytes) +
                        " exceeds configured maxBytes=" + std::to_string(limits.maxBytes))};
            }
            if (measuredBytes > static_cast<std::uint64_t>(SIZE_MAX)) {
                return std::unexpected{
                    fileIoError("read", path,
                                "observedBytes=" + std::to_string(measuredBytes) +
                                    " exceeds addressableBytes=" + std::to_string(SIZE_MAX) +
                                    " with maxBytes=" + std::to_string(limits.maxBytes))};
            }
            if (measuredBytes >
                static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
                return std::unexpected{
                    fileIoError("read", path,
                                "observedBytes=" + std::to_string(measuredBytes) +
                                    " exceeds streamReadableBytes=" +
                                    std::to_string(std::numeric_limits<std::streamsize>::max()) +
                                    " with maxBytes=" + std::to_string(limits.maxBytes))};
            }

            const auto measuredSize = static_cast<std::size_t>(measuredBytes);
            const auto readSize = static_cast<std::streamsize>(measuredBytes);
            std::vector<std::byte> bytes(measuredSize);
            if (readSize > 0) {
                // std::istream exposes only a char buffer API; char may alias std::byte storage.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                stream.read(reinterpret_cast<char*>(bytes.data()), readSize);
                if (stream.gcount() != readSize) {
                    return std::unexpected{
                        fileIoError("read", path, "file became shorter while it was being read")};
                }
            }

            char extraByte{};
            stream.read(&extraByte, 1);
            if (stream.gcount() != 0) {
                return std::unexpected{
                    fileIoError("read", path,
                                "file grew while it was being read: measuredBytes=" +
                                    std::to_string(measuredBytes) +
                                    " observedBytesAtLeast=" + std::to_string(measuredBytes + 1U) +
                                    " maxBytes=" + std::to_string(limits.maxBytes))};
            }
            if (stream.bad()) {
                return std::unexpected{
                    fileIoError("read", path, "stream failed while checking the file size")};
            }

            return bytes;
        }

        VoidResult writeFileBytesAtomicallyWithBackend(const std::filesystem::path& target,
                                                       std::span<const std::byte> bytes,
                                                       AtomicFileWriteOptions options,
                                                       AtomicFileBackend& backend) {
            auto temporary = backend.createUniqueTemporary(target);
            if (!temporary) {
                return std::unexpected{std::move(temporary.error())};
            }

            std::size_t offset = 0U;
            while (offset < bytes.size()) {
                const std::size_t remainingBytes = bytes.size() - offset;
                auto written = (*temporary)->write(bytes.subspan(offset, remainingBytes));
                if (!written) {
                    return std::unexpected{std::move(written.error())};
                }
                if (*written == 0U) {
                    return std::unexpected{
                        fileIoError("write", target, "temporary write made no progress")};
                }
                if (*written > remainingBytes) {
                    return std::unexpected{
                        fileIoError("write", target,
                                    "backend returned invalid progress reportedBytes=" +
                                        std::to_string(*written) +
                                        " remainingBytes=" + std::to_string(remainingBytes))};
                }
                offset += *written;
            }

            if (options.flushFileBuffers) {
                auto flushed = (*temporary)->flush();
                if (!flushed) {
                    return std::unexpected{std::move(flushed.error())};
                }
            }

            auto closed = (*temporary)->close();
            if (!closed) {
                return std::unexpected{std::move(closed.error())};
            }

            auto replacement = backend.replace((*temporary)->path(), target);
            if (replacement.temporaryDisposition == AtomicTemporaryDisposition::Preserve) {
                (*temporary)->releaseCleanupOwnership();
            }
            if (replacement.error.has_value()) {
                return std::unexpected{std::move(*replacement.error)};
            }
            return {};
        }

        StagedFilePreparationOutcome prepareStagedFileBytesWithBackend(
            const std::filesystem::path& target, const std::filesystem::path& staged,
            std::span<const std::byte> bytes, AtomicFileBackend& backend) {
            if (target.empty() || staged.empty()) {
                return {
                    .stagedFileState = StagedFileArtifactState::Indeterminate,
                    .error = stagedPreparationContractError(
                        target, staged, "target and staged paths must be non-empty"),
                };
            }

            const auto normalizedTarget = target.lexically_normal();
            const auto normalizedStaged = staged.lexically_normal();
            if (normalizedTarget == normalizedStaged) {
                return {
                    .stagedFileState = StagedFileArtifactState::Indeterminate,
                    .error = stagedPreparationContractError(
                        target, staged, "target and staged paths must identify distinct files"),
                };
            }
            if (normalizedTarget.parent_path() != normalizedStaged.parent_path()) {
                return {
                    .stagedFileState = StagedFileArtifactState::Indeterminate,
                    .error = stagedPreparationContractError(
                        target, staged, "target and staged files must share one directory"),
                };
            }

            auto stagedFile = backend.createStaged(target, staged);
            if (!stagedFile) {
                return {
                    .stagedFileState = backend.inspectArtifactState(staged),
                    .error = std::move(stagedFile.error()),
                };
            }
            (*stagedFile)->releaseCleanupOwnership();

            std::size_t offset = 0U;
            while (offset < bytes.size()) {
                const std::size_t remainingBytes = bytes.size() - offset;
                auto written = (*stagedFile)->write(bytes.subspan(offset, remainingBytes));
                if (!written) {
                    return stagedPreparationFailure(*stagedFile, staged, backend,
                                                    std::move(written.error()));
                }
                if (*written == 0U) {
                    return stagedPreparationFailure(
                        *stagedFile, staged, backend,
                        fileIoError("stage", staged, "staged write made no progress"));
                }
                if (*written > remainingBytes) {
                    return stagedPreparationFailure(
                        *stagedFile, staged, backend,
                        fileIoError("stage", staged,
                                    "backend returned invalid progress reportedBytes=" +
                                        std::to_string(*written) +
                                        " remainingBytes=" + std::to_string(remainingBytes)));
                }
                offset += *written;
            }

            auto flushed = (*stagedFile)->flush();
            if (!flushed) {
                return stagedPreparationFailure(*stagedFile, staged, backend,
                                                std::move(flushed.error()));
            }

            auto closed = (*stagedFile)->close();
            if (!closed) {
                return stagedPreparationFailure(*stagedFile, staged, backend,
                                                std::move(closed.error()));
            }

            const auto artifactState = backend.inspectArtifactState(staged);
            if (artifactState != StagedFileArtifactState::Present) {
                return {
                    .stagedFileState = artifactState,
                    .error =
                        fileIoError("stage", staged, "staged artifact was not present after close"),
                };
            }
            return {
                .stagedFileState = StagedFileArtifactState::Present,
                .error = std::nullopt,
            };
        }

        StagedFileReplacementOutcome replaceFileFromStagedWithBackend(
            const std::filesystem::path& target, const std::filesystem::path& staged,
            const std::filesystem::path& backup, AtomicFileBackend& backend) {
            if (target.empty() || staged.empty() || backup.empty()) {
                return {
                    .commitState = StagedFileCommitState::NotCommitted,
                    .stagedFileState = StagedFileArtifactState::Indeterminate,
                    .backupFileState = StagedFileArtifactState::Indeterminate,
                    .error = stagedReplacementContractError(
                        target, staged, backup,
                        "target, staged, and backup paths must be non-empty"),
                };
            }

            const auto normalizedTarget = target.lexically_normal();
            const auto normalizedStaged = staged.lexically_normal();
            const auto normalizedBackup = backup.lexically_normal();
            if (normalizedTarget == normalizedStaged || normalizedTarget == normalizedBackup ||
                normalizedStaged == normalizedBackup) {
                return {
                    .commitState = StagedFileCommitState::NotCommitted,
                    .stagedFileState = StagedFileArtifactState::Indeterminate,
                    .backupFileState = StagedFileArtifactState::Indeterminate,
                    .error = stagedReplacementContractError(
                        target, staged, backup,
                        "target, staged, and backup paths must identify distinct files"),
                };
            }

            return backend.replaceStaged(target, staged, backup);
        }

    } // namespace detail

    ExclusiveFileLock::ExclusiveFileLock(
        std::unique_ptr<detail::ExclusiveFileLockHandle> handle) noexcept
        : handle_(std::move(handle)) {}

    ExclusiveFileLock::~ExclusiveFileLock() = default;

    ExclusiveFileLock::ExclusiveFileLock(ExclusiveFileLock&& other) noexcept = default;

    ExclusiveFileLock& ExclusiveFileLock::operator=(ExclusiveFileLock&& other) noexcept = default;

    bool ExclusiveFileLock::ownsLock() const noexcept {
        return handle_ != nullptr && handle_->ownsLock();
    }

    VoidResult ExclusiveFileLock::release() {
        if (handle_ == nullptr) {
            return {};
        }

        auto released = handle_->release();
        if (released) {
            handle_.reset();
        }
        return released;
    }

    Result<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path,
                                                 FileReadLimits limits) {
        if (limits.maxBytes == 0U) {
            return std::unexpected{fileIoError(
                "read", path,
                "invalid configured read limit maxBytes=0; maxBytes must be greater than zero")};
        }

        std::ifstream stream{path, std::ios::binary | std::ios::ate};
        if (!stream) {
            return std::unexpected{fileIoError("read", path, "could not open file")};
        }

        const std::streampos endPosition = stream.tellg();
        if (endPosition < 0) {
            return std::unexpected{fileIoError("read", path, "could not measure file")};
        }

        stream.seekg(0, std::ios::beg);
        if (!stream) {
            return std::unexpected{fileIoError("read", path, "could not seek to file start")};
        }

        return detail::readBoundedStream(stream, static_cast<std::uint64_t>(endPosition), limits,
                                         path);
    }

    Result<std::string> readFileText(const std::filesystem::path& path, FileReadLimits limits) {
        auto bytes = readFileBytes(path, limits);
        if (!bytes) {
            return std::unexpected{std::move(bytes.error())};
        }

        std::string text(bytes->size(), '\0');
        if (!bytes->empty()) {
            std::memcpy(text.data(), bytes->data(), bytes->size());
        }
        return text;
    }

    VoidResult writeFileBytesAtomically(const std::filesystem::path& path,
                                        std::span<const std::byte> bytes,
                                        AtomicFileWriteOptions options) {
        return detail::writeFileBytesAtomicallyWithBackend(path, bytes, options,
                                                           detail::atomicFileBackend());
    }

    VoidResult writeFileTextAtomically(const std::filesystem::path& path, std::string_view text,
                                       AtomicFileWriteOptions options) {
        const auto characters = std::span<const char>{text.data(), text.size()};
        return writeFileBytesAtomically(path, std::as_bytes(characters), options);
    }

    Result<std::optional<ExclusiveFileLock>>
    tryAcquireExclusiveFileLock(const std::filesystem::path& lockPath) {
        return detail::tryAcquireExclusiveFileLockWithBackend(lockPath,
                                                              detail::exclusiveFileLockBackend());
    }

    StagedFilePreparationOutcome prepareStagedFileBytes(const std::filesystem::path& target,
                                                        const std::filesystem::path& staged,
                                                        std::span<const std::byte> bytes) {
        return detail::prepareStagedFileBytesWithBackend(target, staged, bytes,
                                                         detail::atomicFileBackend());
    }

    StagedFilePreparationOutcome prepareStagedFileText(const std::filesystem::path& target,
                                                       const std::filesystem::path& staged,
                                                       std::string_view text) {
        const auto characters = std::span<const char>{text.data(), text.size()};
        return prepareStagedFileBytes(target, staged, std::as_bytes(characters));
    }

    StagedFileReplacementOutcome replaceFileFromStaged(const std::filesystem::path& target,
                                                       const std::filesystem::path& staged,
                                                       const std::filesystem::path& backup) {
        return detail::replaceFileFromStagedWithBackend(target, staged, backup,
                                                        detail::atomicFileBackend());
    }

} // namespace asharia::core
