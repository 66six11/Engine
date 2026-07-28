#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "file_io_internal.hpp"

namespace asharia::core::detail {
    namespace {

        [[nodiscard]] Error posixFileError(std::string_view action,
                                           const std::filesystem::path& path, int errorCode) {
            return Error{ErrorDomain::Core, errorCode,
                         "Core atomic file " + std::string{action} + " failed for '" +
                             filePathToUtf8(path) + "' (POSIX error " + std::to_string(errorCode) +
                             ")."};
        }

        [[nodiscard]] Error posixExclusiveFileLockError(std::string_view action,
                                                        const std::filesystem::path& path,
                                                        int errorCode) {
            return Error{ErrorDomain::Core, errorCode,
                         "Core exclusive file lock " + std::string{action} + " failed for '" +
                             filePathToUtf8(path) + "' (POSIX error " + std::to_string(errorCode) +
                             ")."};
        }

        [[nodiscard]] Error posixStagedReplacementError(std::string_view action,
                                                        const std::filesystem::path& target,
                                                        const std::filesystem::path& staged,
                                                        const std::filesystem::path& backup,
                                                        int errorCode) {
            return Error{ErrorDomain::Core, errorCode,
                         "Core staged file replacement " + std::string{action} +
                             " failed target='" + filePathToUtf8(target) + "' staged='" +
                             filePathToUtf8(staged) + "' backup='" + filePathToUtf8(backup) +
                             "' (POSIX error " + std::to_string(errorCode) + ")."};
        }

        enum class PosixPathKind : std::uint8_t {
            Missing,
            RegularFile,
            Other,
        };

        [[nodiscard]] Result<PosixPathKind> inspectPosixPath(const std::filesystem::path& path,
                                                             std::string_view action) {
            struct stat status{};
            if (lstat(path.c_str(), &status) == 0) {
                return S_ISREG(status.st_mode) ? PosixPathKind::RegularFile : PosixPathKind::Other;
            }
            const int inspectionError = errno;
            if (inspectionError == ENOENT) {
                return PosixPathKind::Missing;
            }
            return std::unexpected{posixFileError(action, path, inspectionError)};
        }

        [[nodiscard]] StagedFileArtifactState
        observedArtifactState(PosixReplaceOperations& operations,
                              const std::filesystem::path& path) noexcept {
            const auto exists = operations.fileExists(path);
            if (!exists.has_value()) {
                return StagedFileArtifactState::Indeterminate;
            }
            return *exists ? StagedFileArtifactState::Present : StagedFileArtifactState::Absent;
        }

        [[nodiscard]] std::filesystem::path temporaryPathFor(const std::filesystem::path& target,
                                                             std::uint64_t uniqueValue) {
            const std::string temporaryName = target.filename().string() + ".tmp." +
                                              std::to_string(getpid()) + "." +
                                              std::to_string(uniqueValue);
            return target.parent_path() / temporaryName;
        }

        class OpenPosixTemporary final {
        public:
            OpenPosixTemporary(int descriptor, std::filesystem::path path)
                : descriptor_(descriptor), path_(std::move(path)) {}

            ~OpenPosixTemporary() {
                if (descriptor_ >= 0) {
                    close(descriptor_);
                    unlink(path_.c_str());
                }
            }

            OpenPosixTemporary(const OpenPosixTemporary&) = delete;
            OpenPosixTemporary& operator=(const OpenPosixTemporary&) = delete;
            OpenPosixTemporary(OpenPosixTemporary&&) = delete;
            OpenPosixTemporary& operator=(OpenPosixTemporary&&) = delete;

            [[nodiscard]] int descriptor() const noexcept {
                return descriptor_;
            }

            [[nodiscard]] const std::filesystem::path& path() const noexcept {
                return path_;
            }

            void release() noexcept {
                descriptor_ = -1;
            }

        private:
            int descriptor_{-1};
            std::filesystem::path path_;
        };

        class OpenPosixDescriptor final {
        public:
            explicit OpenPosixDescriptor(int descriptor) noexcept : descriptor_(descriptor) {}

            ~OpenPosixDescriptor() {
                if (descriptor_ >= 0) {
                    close(descriptor_);
                }
            }

            OpenPosixDescriptor(const OpenPosixDescriptor&) = delete;
            OpenPosixDescriptor& operator=(const OpenPosixDescriptor&) = delete;
            OpenPosixDescriptor(OpenPosixDescriptor&&) = delete;
            OpenPosixDescriptor& operator=(OpenPosixDescriptor&&) = delete;

            [[nodiscard]] int get() const noexcept {
                return descriptor_;
            }

            [[nodiscard]] int release() noexcept {
                return std::exchange(descriptor_, -1);
            }

        private:
            int descriptor_{-1};
        };

        class PosixExclusiveFileLockHandle final : public ExclusiveFileLockHandle {
        public:
            PosixExclusiveFileLockHandle(int descriptor, std::filesystem::path path)
                : descriptor_(descriptor), path_(std::move(path)) {}

            ~PosixExclusiveFileLockHandle() override {
                if (descriptor_ < 0) {
                    return;
                }
                if (locked_) {
                    flock(descriptor_, LOCK_UN);
                }
                close(descriptor_);
            }

            PosixExclusiveFileLockHandle(const PosixExclusiveFileLockHandle&) = delete;
            PosixExclusiveFileLockHandle& operator=(const PosixExclusiveFileLockHandle&) = delete;
            PosixExclusiveFileLockHandle(PosixExclusiveFileLockHandle&&) = delete;
            PosixExclusiveFileLockHandle& operator=(PosixExclusiveFileLockHandle&&) = delete;

            [[nodiscard]] bool ownsLock() const noexcept override {
                return locked_ && descriptor_ >= 0;
            }

            [[nodiscard]] VoidResult release() override {
                if (descriptor_ < 0) {
                    locked_ = false;
                    return {};
                }

                int unlockResult = -1;
                do {
                    unlockResult = flock(descriptor_, LOCK_UN);
                } while (unlockResult < 0 && errno == EINTR);
                const int unlockError = unlockResult < 0 ? errno : 0;
                if (unlockResult == 0) {
                    locked_ = false;
                }

                const int descriptor = std::exchange(descriptor_, -1);
                if (close(descriptor) < 0) {
                    locked_ = false;
                    return std::unexpected{posixExclusiveFileLockError("close", path_, errno)};
                }
                locked_ = false;

                if (unlockError != 0) {
                    return std::unexpected{
                        posixExclusiveFileLockError("unlock", path_, unlockError)};
                }
                return {};
            }

        private:
            int descriptor_{-1};
            std::filesystem::path path_;
            bool locked_{true};
        };

        class PosixExclusiveFileLockBackend final : public ExclusiveFileLockBackend {
        public:
            [[nodiscard]] Result<std::unique_ptr<ExclusiveFileLockHandle>>
            tryAcquire(const std::filesystem::path& lockPath) override {
                int flags = O_RDWR | O_CREAT;
#if defined(O_CLOEXEC)
                flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
                flags |= O_NOFOLLOW;
#endif
                const int descriptor = open(lockPath.c_str(), flags, 0600);
                if (descriptor < 0) {
                    return std::unexpected{
                        posixExclusiveFileLockError("sentinel open", lockPath, errno)};
                }
                OpenPosixDescriptor openDescriptor{descriptor};

                struct stat descriptorStatus{};
                if (fstat(openDescriptor.get(), &descriptorStatus) < 0) {
                    return std::unexpected{
                        posixExclusiveFileLockError("sentinel inspection", lockPath, errno)};
                }
                if (!S_ISREG(descriptorStatus.st_mode)) {
                    return std::unexpected{
                        posixExclusiveFileLockError("sentinel validation", lockPath, EINVAL)};
                }

                int lockResult = -1;
                do {
                    lockResult = flock(openDescriptor.get(), LOCK_EX | LOCK_NB);
                } while (lockResult < 0 && errno == EINTR);
                if (lockResult < 0) {
                    const int lockError = errno;
                    if (lockError == EWOULDBLOCK || lockError == EAGAIN) {
                        return std::unique_ptr<ExclusiveFileLockHandle>{};
                    }
                    return std::unexpected{
                        posixExclusiveFileLockError("acquisition", lockPath, lockError)};
                }

                struct stat pathStatus{};
                if (lstat(lockPath.c_str(), &pathStatus) < 0) {
                    return std::unexpected{
                        posixExclusiveFileLockError("sentinel path revalidation", lockPath, errno)};
                }
                if (!S_ISREG(pathStatus.st_mode) || pathStatus.st_dev != descriptorStatus.st_dev ||
                    pathStatus.st_ino != descriptorStatus.st_ino) {
                    return std::unexpected{posixExclusiveFileLockError(
                        "sentinel identity validation", lockPath, EAGAIN)};
                }

                std::unique_ptr<ExclusiveFileLockHandle> result =
                    std::make_unique<PosixExclusiveFileLockHandle>(openDescriptor.release(),
                                                                   lockPath);
                return result;
            }
        };

        class PosixAtomicTemporaryFile final : public AtomicTemporaryFile {
        public:
            PosixAtomicTemporaryFile(int descriptor, std::filesystem::path path,
                                     std::string operationName)
                : descriptor_(descriptor), path_(std::move(path)),
                  operationName_(std::move(operationName)) {}

            ~PosixAtomicTemporaryFile() override {
                if (descriptor_ >= 0) {
                    close(descriptor_);
                }
                if (!released_) {
                    unlink(path_.c_str());
                }
            }

            PosixAtomicTemporaryFile(const PosixAtomicTemporaryFile&) = delete;
            PosixAtomicTemporaryFile& operator=(const PosixAtomicTemporaryFile&) = delete;
            PosixAtomicTemporaryFile(PosixAtomicTemporaryFile&&) = delete;
            PosixAtomicTemporaryFile& operator=(PosixAtomicTemporaryFile&&) = delete;

            [[nodiscard]] Result<std::size_t> write(std::span<const std::byte> bytes) override {
                const auto chunkSize = std::min<std::size_t>(
                    bytes.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
                ssize_t written = -1;
                do {
                    written = ::write(descriptor_, bytes.data(), chunkSize);
                } while (written < 0 && errno == EINTR);

                if (written < 0) {
                    return std::unexpected{posixFileError(operationName_ + " write", path_, errno)};
                }
                if (written == 0 && !bytes.empty()) {
                    return std::unexpected{posixFileError(operationName_ + " write", path_, EIO)};
                }
                return static_cast<std::size_t>(written);
            }

            [[nodiscard]] VoidResult flush() override {
                int flushResult = -1;
                do {
                    flushResult = fsync(descriptor_);
                } while (flushResult < 0 && errno == EINTR);
                if (flushResult < 0) {
                    return std::unexpected{posixFileError(operationName_ + " flush", path_, errno)};
                }
                return {};
            }

            [[nodiscard]] VoidResult close() override {
                const int descriptor = std::exchange(descriptor_, -1);
                if (::close(descriptor) < 0) {
                    return std::unexpected{posixFileError(operationName_ + " close", path_, errno)};
                }
                return {};
            }

            [[nodiscard]] const std::filesystem::path& path() const noexcept override {
                return path_;
            }

            void releaseCleanupOwnership() noexcept override {
                released_ = true;
            }

        private:
            int descriptor_{-1};
            std::filesystem::path path_;
            std::string operationName_;
            bool released_{};
        };

        class SystemPosixReplaceOperations final : public PosixReplaceOperations {
        public:
            [[nodiscard]] int createLink(const std::filesystem::path& existing,
                                         const std::filesystem::path& linkPath) override {
                if (link(existing.c_str(), linkPath.c_str()) < 0) {
                    return errno;
                }
                return 0;
            }

            [[nodiscard]] int renameFile(const std::filesystem::path& source,
                                         const std::filesystem::path& target) override {
                if (rename(source.c_str(), target.c_str()) < 0) {
                    return errno;
                }
                return 0;
            }

            [[nodiscard]] std::optional<bool>
            fileExists(const std::filesystem::path& path) noexcept override {
                struct stat status{};
                if (lstat(path.c_str(), &status) == 0) {
                    return true;
                }
                return errno == ENOENT ? std::optional<bool>{false} : std::nullopt;
            }
        };

        class PosixAtomicFileBackend final : public AtomicFileBackend {
        public:
            Result<std::unique_ptr<AtomicTemporaryFile>>
            createUniqueTemporary(const std::filesystem::path& target) override {
                constexpr std::uint32_t kMaximumCreateAttempts = 128U;
                mode_t temporaryMode = 0666;
                bool copyTargetMode = false;
                struct stat targetStatus{};
                if (stat(target.c_str(), &targetStatus) == 0) {
                    temporaryMode = targetStatus.st_mode & 07777;
                    copyTargetMode = true;
                } else if (errno != ENOENT) {
                    return std::unexpected{posixFileError("target inspection", target, errno)};
                }

                int descriptor = -1;
                std::filesystem::path temporary;

                for (std::uint32_t attempt = 0U; attempt < kMaximumCreateAttempts; ++attempt) {
                    temporary = temporaryPathFor(target, nextTemporaryId_.fetch_add(1U));
                    descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
                    if (descriptor >= 0) {
                        break;
                    }
                    if (errno != EEXIST) {
                        return std::unexpected{posixFileError("temporary creation", target, errno)};
                    }
                }

                if (descriptor < 0) {
                    return std::unexpected{posixFileError("temporary creation", target, EEXIST)};
                }

                OpenPosixTemporary openTemporary{descriptor, std::move(temporary)};

                if (copyTargetMode && fchmod(openTemporary.descriptor(), temporaryMode) < 0) {
                    return std::unexpected{
                        posixFileError("permission copy", openTemporary.path(), errno)};
                }

                std::unique_ptr<AtomicTemporaryFile> result =
                    std::make_unique<PosixAtomicTemporaryFile>(openTemporary.descriptor(),
                                                               openTemporary.path(), "temporary");
                openTemporary.release();
                return result;
            }

            Result<std::unique_ptr<AtomicTemporaryFile>>
            createStaged(const std::filesystem::path& target,
                         const std::filesystem::path& staged) override {
                struct stat targetStatus{};
                if (lstat(target.c_str(), &targetStatus) < 0) {
                    return std::unexpected{
                        posixFileError("staged target inspection", target, errno)};
                }
                if (!S_ISREG(targetStatus.st_mode)) {
                    return std::unexpected{
                        posixFileError("staged target validation", target, EINVAL)};
                }

                int flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
                flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
                flags |= O_NOFOLLOW;
#endif
                const int descriptor = open(staged.c_str(), flags, 0600);
                if (descriptor < 0) {
                    return std::unexpected{posixFileError("staged creation", staged, errno)};
                }

                OpenPosixTemporary openStaged{descriptor, staged};
                const mode_t targetMode = targetStatus.st_mode & 0777;
                if (fchmod(openStaged.descriptor(), targetMode) < 0) {
                    return std::unexpected{posixFileError("staged permission copy", staged, errno)};
                }

                std::unique_ptr<AtomicTemporaryFile> result =
                    std::make_unique<PosixAtomicTemporaryFile>(openStaged.descriptor(),
                                                               openStaged.path(), "staged");
                openStaged.release();
                return result;
            }

            [[nodiscard]] StagedFileArtifactState
            inspectArtifactState(const std::filesystem::path& path) noexcept override {
                struct stat status{};
                if (lstat(path.c_str(), &status) == 0) {
                    return StagedFileArtifactState::Present;
                }
                return errno == ENOENT ? StagedFileArtifactState::Absent
                                       : StagedFileArtifactState::Indeterminate;
            }

            AtomicReplaceOutcome replace(const std::filesystem::path& temporary,
                                         const std::filesystem::path& target) override {
                if (rename(temporary.c_str(), target.c_str()) < 0) {
                    auto error = posixFileError("replacement", target, errno);
                    error.message += " commitPointReached=false.";
                    return {.commitState = AtomicReplaceCommitState::NotReached,
                            .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                            .error = std::move(error)};
                }
                return {.commitState = AtomicReplaceCommitState::Committed,
                        .temporaryDisposition = AtomicTemporaryDisposition::Preserve,
                        .error = std::nullopt};
            }

            StagedFileReplacementOutcome
            replaceStaged(const std::filesystem::path& target, const std::filesystem::path& staged,
                          const std::filesystem::path& backup) override {
                const auto stagedKind = inspectPosixPath(staged, "staged inspection");
                if (!stagedKind) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Indeterminate,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = posixStagedReplacementError("staged inspection", target, staged,
                                                             backup, stagedKind.error().code),
                    };
                }
                if (*stagedKind == PosixPathKind::Missing) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Absent,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = posixStagedReplacementError("staged inspection", target, staged,
                                                             backup, ENOENT),
                    };
                }
                if (*stagedKind != PosixPathKind::RegularFile) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = posixStagedReplacementError("staged regular-file validation",
                                                             target, staged, backup, EINVAL),
                    };
                }

                const auto backupKind = inspectPosixPath(backup, "backup inspection");
                if (!backupKind) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = posixStagedReplacementError("backup inspection", target, staged,
                                                             backup, backupKind.error().code),
                    };
                }
                if (*backupKind != PosixPathKind::Missing) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = observedArtifactState(replaceOperations_, staged),
                        .backupFileState = observedArtifactState(replaceOperations_, backup),
                        .error = posixStagedReplacementError("backup absence validation", target,
                                                             staged, backup, EEXIST),
                    };
                }

                const auto targetKind = inspectPosixPath(target, "target inspection");
                if (!targetKind) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Absent,
                        .error = posixStagedReplacementError("target inspection", target, staged,
                                                             backup, targetKind.error().code),
                    };
                }
                if (*targetKind == PosixPathKind::Other) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Absent,
                        .error = posixStagedReplacementError("target regular-file validation",
                                                             target, staged, backup, EINVAL),
                    };
                }

                if (*targetKind == PosixPathKind::Missing) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = observedArtifactState(replaceOperations_, staged),
                        .backupFileState = observedArtifactState(replaceOperations_, backup),
                        .error = posixStagedReplacementError("target presence validation", target,
                                                             staged, backup, ENOENT),
                    };
                }

                return replaceExistingPosixStagedFile(target, staged, backup, replaceOperations_);
            }

        private:
            std::atomic<std::uint64_t> nextTemporaryId_{1U};
            SystemPosixReplaceOperations replaceOperations_;
        };

    } // namespace

    StagedFileReplacementOutcome replaceExistingPosixStagedFile(
        const std::filesystem::path& target, const std::filesystem::path& staged,
        const std::filesystem::path& backup, PosixReplaceOperations& operations) {
        const int linkError = operations.createLink(target, backup);
        if (linkError != 0) {
            return {
                .commitState = StagedFileCommitState::NotCommitted,
                .stagedFileState = observedArtifactState(operations, staged),
                .backupFileState = observedArtifactState(operations, backup),
                .error = posixStagedReplacementError("backup creation", target, staged, backup,
                                                     linkError),
            };
        }

        const int replaceError = operations.renameFile(staged, target);
        if (replaceError != 0) {
            return {
                .commitState = StagedFileCommitState::NotCommitted,
                .stagedFileState = observedArtifactState(operations, staged),
                .backupFileState = observedArtifactState(operations, backup),
                .error = posixStagedReplacementError("replacement", target, staged, backup,
                                                     replaceError),
            };
        }

        return {
            .commitState = StagedFileCommitState::Committed,
            .stagedFileState = observedArtifactState(operations, staged),
            .backupFileState = observedArtifactState(operations, backup),
            .error = std::nullopt,
        };
    }

    AtomicFileBackend& atomicFileBackend() {
        static PosixAtomicFileBackend backend;
        return backend;
    }

    ExclusiveFileLockBackend& exclusiveFileLockBackend() {
        static PosixExclusiveFileLockBackend backend;
        return backend;
    }

} // namespace asharia::core::detail
