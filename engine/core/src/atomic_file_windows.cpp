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
                             path.string() + "' (Windows error " + std::to_string(errorCode) +
                             ")."};
        }

        [[nodiscard]] std::filesystem::path temporaryPathFor(const std::filesystem::path& target,
                                                             std::uint64_t uniqueValue) {
            const std::wstring temporaryName = target.filename().wstring() + L".tmp." +
                                               std::to_wstring(GetCurrentProcessId()) + L"." +
                                               std::to_wstring(uniqueValue);
            return target.parent_path() / temporaryName;
        }

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

            void releaseAfterReplace() noexcept override {
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

            VoidResult replace(const std::filesystem::path& temporary,
                               const std::filesystem::path& target) override {
                const DWORD targetAttributes = GetFileAttributesW(target.c_str());
                if (targetAttributes != INVALID_FILE_ATTRIBUTES) {
                    if (ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, 0, nullptr,
                                     nullptr) == FALSE) {
                        return std::unexpected{
                            windowsFileError("replacement", target, GetLastError())};
                    }
                    return {};
                }

                const DWORD attributeError = GetLastError();
                if (attributeError != ERROR_FILE_NOT_FOUND &&
                    attributeError != ERROR_PATH_NOT_FOUND) {
                    return std::unexpected{
                        windowsFileError("target inspection", target, attributeError)};
                }

                if (MoveFileExW(temporary.c_str(), target.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
                    return std::unexpected{windowsFileError("replacement", target, GetLastError())};
                }
                return {};
            }

        private:
            std::atomic<std::uint64_t> nextTemporaryId_{1U};
        };

    } // namespace

    AtomicFileBackend& atomicFileBackend() {
        static WindowsAtomicFileBackend backend;
        return backend;
    }

} // namespace asharia::core::detail
