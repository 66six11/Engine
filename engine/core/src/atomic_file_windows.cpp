#include "asharia/core/log.hpp"

#include "file_io_internal.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

namespace asharia::core::detail {
    namespace {

        [[nodiscard]] Error windowsFileError(std::string_view action,
                                             const std::filesystem::path& path, DWORD errorCode) {
            return Error{ErrorDomain::Core, static_cast<int>(errorCode),
                         "Core atomic file " + std::string{action} + " failed for '" +
                             filePathToUtf8(path) + "' (Windows error " +
                             std::to_string(errorCode) + ")."};
        }

        [[nodiscard]] Error windowsExclusiveFileLockError(std::string_view action,
                                                          const std::filesystem::path& path,
                                                          DWORD errorCode) {
            return Error{ErrorDomain::Core, static_cast<int>(errorCode),
                         "Core exclusive file lock " + std::string{action} + " failed for '" +
                             filePathToUtf8(path) + "' (Windows error " +
                             std::to_string(errorCode) + ")."};
        }

        [[nodiscard]] std::filesystem::path temporaryPathFor(const std::filesystem::path& target,
                                                             std::uint64_t uniqueValue) {
            const std::wstring temporaryName = target.filename().wstring() + L".tmp." +
                                               std::to_wstring(GetCurrentProcessId()) + L"." +
                                               std::to_wstring(uniqueValue);
            return target.parent_path() / temporaryName;
        }

        [[nodiscard]] std::filesystem::path backupPathFor(const std::filesystem::path& replacement,
                                                          std::uint64_t uniqueValue) {
            const std::wstring backupName = replacement.filename().wstring() + L".backup." +
                                            std::to_wstring(GetCurrentProcessId()) + L"." +
                                            std::to_wstring(uniqueValue);
            return replacement.parent_path() / backupName;
        }

        [[nodiscard]] Error windowsReplacementError(DWORD errorCode, std::string_view commitState,
                                                    std::string_view recovery,
                                                    const std::filesystem::path& target,
                                                    const std::filesystem::path& replacement,
                                                    const std::filesystem::path& backup,
                                                    DWORD recoveryError = ERROR_SUCCESS) {
            std::string message = "Core atomic file replacement failed commitPointReached=" +
                                  std::string{commitState} + " recovery=" + std::string{recovery} +
                                  " target='" + filePathToUtf8(target) + "' replacement='" +
                                  filePathToUtf8(replacement) + "' backup='" +
                                  filePathToUtf8(backup) + "' Windows error " +
                                  std::to_string(errorCode);
            if (recoveryError != ERROR_SUCCESS) {
                message += " recoveryError=" + std::to_string(recoveryError);
            }
            message += ".";
            return Error{ErrorDomain::Core, static_cast<int>(errorCode), std::move(message)};
        }

        [[nodiscard]] StagedFileArtifactState
        observedArtifactState(WindowsReplaceOperations& operations,
                              const std::filesystem::path& path) noexcept {
            const auto exists = operations.fileExists(path);
            if (!exists.has_value()) {
                return StagedFileArtifactState::Indeterminate;
            }
            return *exists ? StagedFileArtifactState::Present : StagedFileArtifactState::Absent;
        }

        class SystemWindowsReplaceOperations final : public WindowsReplaceOperations {
        public:
            [[nodiscard]] std::uint32_t replaceFile(const std::filesystem::path& target,
                                                    const std::filesystem::path& replacement,
                                                    const std::filesystem::path& backup) override {
                if (ReplaceFileW(target.c_str(), replacement.c_str(), backup.c_str(), 0, nullptr,
                                 nullptr) == FALSE) {
                    return GetLastError();
                }
                return ERROR_SUCCESS;
            }

            [[nodiscard]] std::uint32_t moveFile(const std::filesystem::path& source,
                                                 const std::filesystem::path& target) override {
                if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE) {
                    return GetLastError();
                }
                return ERROR_SUCCESS;
            }

            [[nodiscard]] std::uint32_t deleteFile(const std::filesystem::path& path) override {
                if (DeleteFileW(path.c_str()) == FALSE) {
                    return GetLastError();
                }
                return ERROR_SUCCESS;
            }

            [[nodiscard]] std::optional<bool>
            fileExists(const std::filesystem::path& path) noexcept override {
                const DWORD attributes = GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES) {
                    return true;
                }
                const DWORD inspectionError = GetLastError();
                if (inspectionError == ERROR_FILE_NOT_FOUND ||
                    inspectionError == ERROR_PATH_NOT_FOUND) {
                    return false;
                }
                return std::nullopt;
            }

            void reportWarning(std::string_view warning) noexcept override {
                try {
                    logWarning(warning);
                } catch (...) {
                    OutputDebugStringA(
                        "Core atomic replacement warning delivery failed after commit.\n");
                }
            }
        };

        class OpenWindowsTemporary final {
        public:
            OpenWindowsTemporary(HANDLE handle, std::filesystem::path path)
                : handle_(handle), path_(std::move(path)) {}

            ~OpenWindowsTemporary() {
                if (handle_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle_);
                    DeleteFileW(path_.c_str());
                }
            }

            OpenWindowsTemporary(const OpenWindowsTemporary&) = delete;
            OpenWindowsTemporary& operator=(const OpenWindowsTemporary&) = delete;
            OpenWindowsTemporary(OpenWindowsTemporary&&) = delete;
            OpenWindowsTemporary& operator=(OpenWindowsTemporary&&) = delete;

            [[nodiscard]] HANDLE handle() const noexcept {
                return handle_;
            }

            [[nodiscard]] const std::filesystem::path& path() const noexcept {
                return path_;
            }

            void release() noexcept {
                handle_ = INVALID_HANDLE_VALUE;
            }

        private:
            HANDLE handle_{INVALID_HANDLE_VALUE};
            std::filesystem::path path_;
        };

        class OpenWindowsHandle final {
        public:
            explicit OpenWindowsHandle(HANDLE handle) noexcept : handle_(handle) {}

            ~OpenWindowsHandle() {
                if (handle_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle_);
                }
            }

            OpenWindowsHandle(const OpenWindowsHandle&) = delete;
            OpenWindowsHandle& operator=(const OpenWindowsHandle&) = delete;
            OpenWindowsHandle(OpenWindowsHandle&&) = delete;
            OpenWindowsHandle& operator=(OpenWindowsHandle&&) = delete;

            [[nodiscard]] HANDLE get() const noexcept {
                return handle_;
            }

            [[nodiscard]] HANDLE release() noexcept {
                const HANDLE handle = handle_;
                handle_ = INVALID_HANDLE_VALUE;
                return handle;
            }

        private:
            HANDLE handle_{INVALID_HANDLE_VALUE};
        };

        class WindowsExclusiveFileLockHandle final : public ExclusiveFileLockHandle {
        public:
            WindowsExclusiveFileLockHandle(HANDLE handle, std::filesystem::path path)
                : handle_(handle), path_(std::move(path)) {}

            ~WindowsExclusiveFileLockHandle() override {
                if (handle_ == INVALID_HANDLE_VALUE) {
                    return;
                }
                if (locked_) {
                    UnlockFileEx(handle_, 0U, 1U, 0U, &lockRange_);
                }
                CloseHandle(handle_);
            }

            WindowsExclusiveFileLockHandle(const WindowsExclusiveFileLockHandle&) = delete;
            WindowsExclusiveFileLockHandle&
            operator=(const WindowsExclusiveFileLockHandle&) = delete;
            WindowsExclusiveFileLockHandle(WindowsExclusiveFileLockHandle&&) = delete;
            WindowsExclusiveFileLockHandle& operator=(WindowsExclusiveFileLockHandle&&) = delete;

            [[nodiscard]] bool ownsLock() const noexcept override {
                return locked_ && handle_ != INVALID_HANDLE_VALUE;
            }

            [[nodiscard]] VoidResult release() override {
                if (handle_ == INVALID_HANDLE_VALUE) {
                    locked_ = false;
                    return {};
                }

                DWORD unlockError = ERROR_SUCCESS;
                if (locked_ && UnlockFileEx(handle_, 0U, 1U, 0U, &lockRange_) == FALSE) {
                    unlockError = GetLastError();
                } else {
                    locked_ = false;
                }

                if (CloseHandle(handle_) == FALSE) {
                    const DWORD closeError = GetLastError();
                    return std::unexpected{
                        windowsExclusiveFileLockError("close", path_, closeError)};
                }
                handle_ = INVALID_HANDLE_VALUE;
                locked_ = false;

                if (unlockError != ERROR_SUCCESS) {
                    return std::unexpected{
                        windowsExclusiveFileLockError("unlock", path_, unlockError)};
                }
                return {};
            }

        private:
            HANDLE handle_{INVALID_HANDLE_VALUE};
            std::filesystem::path path_;
            OVERLAPPED lockRange_{};
            bool locked_{true};
        };

        class WindowsExclusiveFileLockBackend final : public ExclusiveFileLockBackend {
        public:
            [[nodiscard]] Result<std::unique_ptr<ExclusiveFileLockHandle>>
            tryAcquire(const std::filesystem::path& lockPath) override {
                const HANDLE rawHandle =
                    CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (rawHandle == INVALID_HANDLE_VALUE) {
                    return std::unexpected{
                        windowsExclusiveFileLockError("sentinel open", lockPath, GetLastError())};
                }
                OpenWindowsHandle openHandle{rawHandle};

                FILE_ATTRIBUTE_TAG_INFO attributes{};
                if (GetFileInformationByHandleEx(openHandle.get(), FileAttributeTagInfo,
                                                 &attributes, sizeof(attributes)) == FALSE) {
                    return std::unexpected{windowsExclusiveFileLockError("sentinel inspection",
                                                                         lockPath, GetLastError())};
                }
                if (!isWindowsRegularFileAttributes(attributes.FileAttributes)) {
                    return std::unexpected{windowsExclusiveFileLockError(
                        "sentinel validation", lockPath, ERROR_INVALID_PARAMETER)};
                }

                OVERLAPPED lockRange{};
                if (LockFileEx(openHandle.get(),
                               LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0U, 1U, 0U,
                               &lockRange) == FALSE) {
                    const DWORD lockError = GetLastError();
                    if (lockError == ERROR_LOCK_VIOLATION || lockError == ERROR_IO_PENDING) {
                        return std::unique_ptr<ExclusiveFileLockHandle>{};
                    }
                    return std::unexpected{
                        windowsExclusiveFileLockError("acquisition", lockPath, lockError)};
                }

                std::unique_ptr<ExclusiveFileLockHandle> result =
                    std::make_unique<WindowsExclusiveFileLockHandle>(openHandle.release(),
                                                                     lockPath);
                return result;
            }
        };

        class WindowsAtomicTemporaryFile final : public AtomicTemporaryFile {
        public:
            WindowsAtomicTemporaryFile(HANDLE handle, std::filesystem::path path,
                                       std::string operationName)
                : handle_(handle), path_(std::move(path)),
                  operationName_(std::move(operationName)) {}

            ~WindowsAtomicTemporaryFile() override {
                if (handle_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle_);
                }
                if (!released_) {
                    DeleteFileW(path_.c_str());
                }
            }

            WindowsAtomicTemporaryFile(const WindowsAtomicTemporaryFile&) = delete;
            WindowsAtomicTemporaryFile& operator=(const WindowsAtomicTemporaryFile&) = delete;
            WindowsAtomicTemporaryFile(WindowsAtomicTemporaryFile&&) = delete;
            WindowsAtomicTemporaryFile& operator=(WindowsAtomicTemporaryFile&&) = delete;

            [[nodiscard]] Result<std::size_t> write(std::span<const std::byte> bytes) override {
                const auto chunkSize = static_cast<DWORD>(
                    std::min<std::size_t>(bytes.size(), std::numeric_limits<DWORD>::max()));
                DWORD written = 0U;
                if (WriteFile(handle_, bytes.data(), chunkSize, &written, nullptr) == FALSE) {
                    return std::unexpected{
                        windowsFileError(operationName_ + " write", path_, GetLastError())};
                }
                if (written == 0U && !bytes.empty()) {
                    return std::unexpected{
                        windowsFileError(operationName_ + " write", path_, ERROR_WRITE_FAULT)};
                }
                return static_cast<std::size_t>(written);
            }

            [[nodiscard]] VoidResult flush() override {
                if (FlushFileBuffers(handle_) == FALSE) {
                    return std::unexpected{
                        windowsFileError(operationName_ + " flush", path_, GetLastError())};
                }
                return {};
            }

            [[nodiscard]] VoidResult close() override {
                if (CloseHandle(handle_) == FALSE) {
                    return std::unexpected{
                        windowsFileError(operationName_ + " close", path_, GetLastError())};
                }
                handle_ = INVALID_HANDLE_VALUE;
                return {};
            }

            [[nodiscard]] const std::filesystem::path& path() const noexcept override {
                return path_;
            }

            void releaseCleanupOwnership() noexcept override {
                released_ = true;
            }

        private:
            HANDLE handle_{INVALID_HANDLE_VALUE};
            std::filesystem::path path_;
            std::string operationName_;
            bool released_{};
        };

        class WindowsAtomicFileBackend final : public AtomicFileBackend {
        public:
            Result<std::unique_ptr<AtomicTemporaryFile>>
            createUniqueTemporary(const std::filesystem::path& target) override {
                constexpr std::uint32_t kMaximumCreateAttempts = 128U;
                for (std::uint32_t attempt = 0U; attempt < kMaximumCreateAttempts; ++attempt) {
                    auto temporary = temporaryPathFor(target, nextTemporaryId_.fetch_add(1U));
                    const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (handle != INVALID_HANDLE_VALUE) {
                        OpenWindowsTemporary openTemporary{handle, std::move(temporary)};
                        std::unique_ptr<AtomicTemporaryFile> result =
                            std::make_unique<WindowsAtomicTemporaryFile>(
                                openTemporary.handle(), openTemporary.path(), "temporary");
                        openTemporary.release();
                        return result;
                    }

                    const DWORD createError = GetLastError();
                    if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS) {
                        return std::unexpected{
                            windowsFileError("temporary creation", target, createError)};
                    }
                }

                return std::unexpected{
                    windowsFileError("temporary creation", target, ERROR_FILE_EXISTS)};
            }

            Result<std::unique_ptr<AtomicTemporaryFile>>
            createStaged(const std::filesystem::path& target,
                         const std::filesystem::path& staged) override {
                const DWORD targetAttributes = GetFileAttributesW(target.c_str());
                if (targetAttributes == INVALID_FILE_ATTRIBUTES) {
                    return std::unexpected{
                        windowsFileError("staged target inspection", target, GetLastError())};
                }
                if (!isWindowsRegularFileAttributes(targetAttributes)) {
                    return std::unexpected{windowsFileError("staged target validation", target,
                                                            ERROR_INVALID_PARAMETER)};
                }

                const HANDLE handle = CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr,
                                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE) {
                    return std::unexpected{
                        windowsFileError("staged creation", staged, GetLastError())};
                }

                OpenWindowsTemporary openStaged{handle, staged};
                std::unique_ptr<AtomicTemporaryFile> result =
                    std::make_unique<WindowsAtomicTemporaryFile>(openStaged.handle(),
                                                                 openStaged.path(), "staged");
                openStaged.release();
                return result;
            }

            [[nodiscard]] StagedFileArtifactState
            inspectArtifactState(const std::filesystem::path& path) noexcept override {
                const DWORD attributes = GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES) {
                    return StagedFileArtifactState::Present;
                }
                const DWORD inspectionError = GetLastError();
                if (inspectionError == ERROR_FILE_NOT_FOUND ||
                    inspectionError == ERROR_PATH_NOT_FOUND) {
                    return StagedFileArtifactState::Absent;
                }
                return StagedFileArtifactState::Indeterminate;
            }

            AtomicReplaceOutcome replace(const std::filesystem::path& temporary,
                                         const std::filesystem::path& target) override {
                const DWORD targetAttributes = GetFileAttributesW(target.c_str());
                if (targetAttributes != INVALID_FILE_ATTRIBUTES) {
                    auto backup = createUniqueBackup(temporary);
                    if (!backup) {
                        auto error = std::move(backup.error());
                        error.message += " commitPointReached=false.";
                        return {.commitState = AtomicReplaceCommitState::NotReached,
                                .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                                .error = std::move(error)};
                    }
                    return replaceExistingWindowsFileWithRecovery(target, temporary, *backup,
                                                                  replaceOperations_);
                }

                const DWORD attributeError = GetLastError();
                if (attributeError != ERROR_FILE_NOT_FOUND &&
                    attributeError != ERROR_PATH_NOT_FOUND) {
                    auto error = windowsFileError("target inspection", target, attributeError);
                    error.message += " commitPointReached=false.";
                    return {.commitState = AtomicReplaceCommitState::NotReached,
                            .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                            .error = std::move(error)};
                }

                if (MoveFileExW(temporary.c_str(), target.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
                    return {.commitState = AtomicReplaceCommitState::NotReached,
                            .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                            .error = windowsReplacementError(
                                GetLastError(), "false", "not-required", target, temporary, {})};
                }
                return {.commitState = AtomicReplaceCommitState::Committed,
                        .temporaryDisposition = AtomicTemporaryDisposition::Preserve,
                        .error = std::nullopt};
            }

            StagedFileReplacementOutcome
            replaceStaged(const std::filesystem::path& target, const std::filesystem::path& staged,
                          const std::filesystem::path& backup) override {
                const DWORD stagedAttributes = GetFileAttributesW(staged.c_str());
                if (stagedAttributes == INVALID_FILE_ATTRIBUTES) {
                    const DWORD inspectionError = GetLastError();
                    const bool stagedMissing = inspectionError == ERROR_FILE_NOT_FOUND ||
                                               inspectionError == ERROR_PATH_NOT_FOUND;
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = stagedMissing ? StagedFileArtifactState::Absent
                                                         : StagedFileArtifactState::Indeterminate,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = windowsReplacementError(inspectionError, "false",
                                                         "staged-inspection-failed", target, staged,
                                                         backup),
                    };
                }
                if (!isWindowsRegularFileAttributes(stagedAttributes)) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = windowsReplacementError(ERROR_INVALID_PARAMETER, "false",
                                                         "staged-is-not-a-regular-file", target,
                                                         staged, backup),
                    };
                }

                const DWORD backupAttributes = GetFileAttributesW(backup.c_str());
                if (backupAttributes != INVALID_FILE_ATTRIBUTES) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = observedArtifactState(replaceOperations_, staged),
                        .backupFileState = observedArtifactState(replaceOperations_, backup),
                        .error = windowsReplacementError(ERROR_ALREADY_EXISTS, "false",
                                                         "backup-already-exists", target, staged,
                                                         backup),
                    };
                }
                const DWORD backupInspectionError = GetLastError();
                if (backupInspectionError != ERROR_FILE_NOT_FOUND &&
                    backupInspectionError != ERROR_PATH_NOT_FOUND) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Indeterminate,
                        .error = windowsReplacementError(backupInspectionError, "false",
                                                         "backup-inspection-failed", target, staged,
                                                         backup),
                    };
                }

                const DWORD targetAttributes = GetFileAttributesW(target.c_str());
                if (targetAttributes != INVALID_FILE_ATTRIBUTES) {
                    if (!isWindowsRegularFileAttributes(targetAttributes)) {
                        return {
                            .commitState = StagedFileCommitState::NotCommitted,
                            .stagedFileState = StagedFileArtifactState::Present,
                            .backupFileState = StagedFileArtifactState::Absent,
                            .error = windowsReplacementError(ERROR_INVALID_PARAMETER, "false",
                                                             "target-is-not-a-regular-file", target,
                                                             staged, backup),
                        };
                    }
                    return replaceExistingWindowsStagedFileWithRecovery(target, staged, backup,
                                                                        replaceOperations_);
                }

                const DWORD targetInspectionError = GetLastError();
                if (targetInspectionError != ERROR_FILE_NOT_FOUND &&
                    targetInspectionError != ERROR_PATH_NOT_FOUND) {
                    return {
                        .commitState = StagedFileCommitState::NotCommitted,
                        .stagedFileState = StagedFileArtifactState::Present,
                        .backupFileState = StagedFileArtifactState::Absent,
                        .error = windowsReplacementError(targetInspectionError, "false",
                                                         "target-inspection-failed", target, staged,
                                                         backup),
                    };
                }

                return {
                    .commitState = StagedFileCommitState::NotCommitted,
                    .stagedFileState = observedArtifactState(replaceOperations_, staged),
                    .backupFileState = observedArtifactState(replaceOperations_, backup),
                    .error = windowsReplacementError(ERROR_FILE_NOT_FOUND, "false",
                                                     "target-missing", target, staged, backup),
                };
            }

        private:
            [[nodiscard]] Result<std::filesystem::path>
            createUniqueBackup(const std::filesystem::path& replacement) {
                constexpr std::uint32_t kMaximumCreateAttempts = 128U;
                for (std::uint32_t attempt = 0U; attempt < kMaximumCreateAttempts; ++attempt) {
                    auto backup = backupPathFor(replacement, nextTemporaryId_.fetch_add(1U));
                    const DWORD attributes = GetFileAttributesW(backup.c_str());
                    if (attributes == INVALID_FILE_ATTRIBUTES) {
                        const DWORD inspectionError = GetLastError();
                        if (inspectionError == ERROR_FILE_NOT_FOUND ||
                            inspectionError == ERROR_PATH_NOT_FOUND) {
                            return backup;
                        }
                        return std::unexpected{
                            windowsFileError("backup inspection", backup, inspectionError)};
                    }
                }
                return std::unexpected{
                    windowsFileError("backup allocation", replacement, ERROR_FILE_EXISTS)};
            }

            std::atomic<std::uint64_t> nextTemporaryId_{1U};
            SystemWindowsReplaceOperations replaceOperations_;
        };

    } // namespace

    bool isWindowsRegularFileAttributes(std::uint32_t attributes) noexcept {
        constexpr std::uint32_t kNonRegularAttributes =
            FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;
        return (attributes & kNonRegularAttributes) == 0U;
    }

    AtomicReplaceOutcome replaceExistingWindowsFileWithRecovery(
        const std::filesystem::path& target, const std::filesystem::path& replacement,
        const std::filesystem::path& backup, WindowsReplaceOperations& operations) {
        auto outcome =
            replaceExistingWindowsStagedFileWithRecovery(target, replacement, backup, operations);
        if (outcome.commitState == StagedFileCommitState::Committed) {
            const auto cleanupError = operations.deleteFile(backup);
            if (cleanupError != ERROR_SUCCESS) {
                operations.reportWarning(
                    "Core atomic replacement backup cleanup failed commitPointReached=true "
                    "target='" +
                    filePathToUtf8(target) + "' replacement='" + filePathToUtf8(replacement) +
                    "' backup='" + filePathToUtf8(backup) + "' Windows error " +
                    std::to_string(cleanupError) + ".");
            }
            return {.commitState = AtomicReplaceCommitState::Committed,
                    .temporaryDisposition = AtomicTemporaryDisposition::Preserve,
                    .error = std::nullopt};
        }

        if (outcome.commitState == StagedFileCommitState::Indeterminate) {
            return {.commitState = AtomicReplaceCommitState::Indeterminate,
                    .temporaryDisposition = AtomicTemporaryDisposition::Preserve,
                    .error = std::move(outcome.error)};
        }

        return {.commitState = AtomicReplaceCommitState::NotReached,
                .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                .error = std::move(outcome.error)};
    }

    StagedFileReplacementOutcome replaceExistingWindowsStagedFileWithRecovery(
        const std::filesystem::path& target, const std::filesystem::path& staged,
        const std::filesystem::path& backup, WindowsReplaceOperations& operations) {
        const auto replaceError = operations.replaceFile(target, staged, backup);
        if (replaceError == ERROR_SUCCESS) {
            return {
                .commitState = StagedFileCommitState::Committed,
                .stagedFileState = observedArtifactState(operations, staged),
                .backupFileState = observedArtifactState(operations, backup),
                .error = std::nullopt,
            };
        }

        if (replaceError == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            const auto recoveryError = operations.moveFile(backup, target);
            if (recoveryError == ERROR_SUCCESS) {
                return {
                    .commitState = StagedFileCommitState::NotCommitted,
                    .stagedFileState = observedArtifactState(operations, staged),
                    .backupFileState = observedArtifactState(operations, backup),
                    .error = windowsReplacementError(replaceError, "false", "restored", target,
                                                     staged, backup),
                };
            }
            return {
                .commitState = StagedFileCommitState::Indeterminate,
                .stagedFileState = observedArtifactState(operations, staged),
                .backupFileState = observedArtifactState(operations, backup),
                .error = windowsReplacementError(replaceError, "indeterminate", "failed", target,
                                                 staged, backup, recoveryError),
            };
        }

        // Microsoft documents that 1175, 1176 with a backup, and ordinary failures retain the
        // original names and do not create the requested backup.
        return {
            .commitState = StagedFileCommitState::NotCommitted,
            .stagedFileState = observedArtifactState(operations, staged),
            .backupFileState = observedArtifactState(operations, backup),
            .error = windowsReplacementError(replaceError, "false", "not-required", target, staged,
                                             backup),
        };
    }

    AtomicFileBackend& atomicFileBackend() {
        static WindowsAtomicFileBackend backend;
        return backend;
    }

    ExclusiveFileLockBackend& exclusiveFileLockBackend() {
        static WindowsExclusiveFileLockBackend backend;
        return backend;
    }

} // namespace asharia::core::detail
