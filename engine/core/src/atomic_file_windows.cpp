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

        class WindowsAtomicTemporaryFile final : public AtomicTemporaryFile {
        public:
            WindowsAtomicTemporaryFile(HANDLE handle, std::filesystem::path path)
                : handle_(handle), path_(std::move(path)) {}

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
                        windowsFileError("temporary write", path_, GetLastError())};
                }
                if (written == 0U && !bytes.empty()) {
                    return std::unexpected{
                        windowsFileError("temporary write", path_, ERROR_WRITE_FAULT)};
                }
                return static_cast<std::size_t>(written);
            }

            [[nodiscard]] VoidResult flush() override {
                if (FlushFileBuffers(handle_) == FALSE) {
                    return std::unexpected{
                        windowsFileError("temporary flush", path_, GetLastError())};
                }
                return {};
            }

            [[nodiscard]] VoidResult close() override {
                if (CloseHandle(handle_) == FALSE) {
                    return std::unexpected{
                        windowsFileError("temporary close", path_, GetLastError())};
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
                            std::make_unique<WindowsAtomicTemporaryFile>(openTemporary.handle(),
                                                                         openTemporary.path());
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

    AtomicReplaceOutcome replaceExistingWindowsFileWithRecovery(
        const std::filesystem::path& target, const std::filesystem::path& replacement,
        const std::filesystem::path& backup, WindowsReplaceOperations& operations) {
        const auto replaceError = operations.replaceFile(target, replacement, backup);
        if (replaceError == ERROR_SUCCESS) {
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

        if (replaceError == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            const auto recoveryError = operations.moveFile(backup, target);
            if (recoveryError == ERROR_SUCCESS) {
                return {.commitState = AtomicReplaceCommitState::NotReached,
                        .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                        .error = windowsReplacementError(replaceError, "false", "restored", target,
                                                         replacement, backup)};
            }
            return {.commitState = AtomicReplaceCommitState::Indeterminate,
                    .temporaryDisposition = AtomicTemporaryDisposition::Preserve,
                    .error = windowsReplacementError(replaceError, "indeterminate", "failed",
                                                     target, replacement, backup, recoveryError)};
        }

        // Microsoft documents that ordinary ReplaceFile failures retain the original names and
        // do not create the requested backup. Do not delete this path here: another process may
        // have raced our prior absence check and own a file with that name.
        return {.commitState = AtomicReplaceCommitState::NotReached,
                .temporaryDisposition = AtomicTemporaryDisposition::Cleanup,
                .error = windowsReplacementError(replaceError, "false", "not-required", target,
                                                 replacement, backup)};
    }

    AtomicFileBackend& atomicFileBackend() {
        static WindowsAtomicFileBackend backend;
        return backend;
    }

} // namespace asharia::core::detail
