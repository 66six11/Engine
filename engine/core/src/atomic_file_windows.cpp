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

        struct OpenTemporary {
            HANDLE handle{INVALID_HANDLE_VALUE};
            std::filesystem::path path;
        };

        [[nodiscard]] Result<OpenTemporary>
        createUniqueTemporary(const std::filesystem::path& target,
                              std::atomic<std::uint64_t>& nextTemporaryId) {
            constexpr std::uint32_t kMaximumCreateAttempts = 128U;
            for (std::uint32_t attempt = 0U; attempt < kMaximumCreateAttempts; ++attempt) {
                auto temporary = temporaryPathFor(target, nextTemporaryId.fetch_add(1U));
                const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                                  CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
                if (handle != INVALID_HANDLE_VALUE) {
                    return OpenTemporary{.handle = handle, .path = std::move(temporary)};
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

        [[nodiscard]] Error closeAndRemoveFailedTemporary(HANDLE handle,
                                                          const std::filesystem::path& temporary,
                                                          std::string_view action,
                                                          DWORD operationError) {
            if (CloseHandle(handle) == FALSE) {
                const DWORD closeError = GetLastError();
                DeleteFileW(temporary.c_str());
                return windowsFileError("temporary close", temporary, closeError);
            }

            DeleteFileW(temporary.c_str());
            return windowsFileError(action, temporary, operationError);
        }

        [[nodiscard]] VoidResult writeAllTemporary(HANDLE handle,
                                                   const std::filesystem::path& temporary,
                                                   std::span<const std::byte> bytes) {
            std::size_t offset = 0U;
            while (offset < bytes.size()) {
                const auto remaining = bytes.size() - offset;
                const auto chunkSize = static_cast<DWORD>(
                    std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
                DWORD written = 0U;
                const BOOL writeSucceeded =
                    WriteFile(handle, bytes.data() + offset, chunkSize, &written, nullptr);
                if (writeSucceeded == FALSE || written == 0U) {
                    const DWORD writeError =
                        writeSucceeded == FALSE ? GetLastError() : ERROR_WRITE_FAULT;
                    return std::unexpected{closeAndRemoveFailedTemporary(
                        handle, temporary, "temporary write", writeError)};
                }
                offset += written;
            }

            return {};
        }

        class WindowsAtomicFileBackend final : public AtomicFileBackend {
        public:
            Result<std::filesystem::path>
            writeUniqueTemporary(const std::filesystem::path& target,
                                 std::span<const std::byte> bytes,
                                 AtomicFileWriteOptions options) override {
                auto temporary = createUniqueTemporary(target, nextTemporaryId_);
                if (!temporary) {
                    return std::unexpected{std::move(temporary.error())};
                }

                auto written = writeAllTemporary(temporary->handle, temporary->path, bytes);
                if (!written) {
                    return std::unexpected{std::move(written.error())};
                }

                if (options.flushFileBuffers && FlushFileBuffers(temporary->handle) == FALSE) {
                    const DWORD flushError = GetLastError();
                    return std::unexpected{closeAndRemoveFailedTemporary(
                        temporary->handle, temporary->path, "temporary flush", flushError)};
                }

                if (CloseHandle(temporary->handle) == FALSE) {
                    const DWORD closeError = GetLastError();
                    DeleteFileW(temporary->path.c_str());
                    return std::unexpected{
                        windowsFileError("temporary close", temporary->path, closeError)};
                }

                return std::move(temporary->path);
            }

            VoidResult replace(const std::filesystem::path& temporary,
                               const std::filesystem::path& target) override {
                const DWORD targetAttributes = GetFileAttributesW(target.c_str());
                if (targetAttributes != INVALID_FILE_ATTRIBUTES) {
                    if (ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
                                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) == FALSE) {
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

            void removeTemporary(const std::filesystem::path& temporary) noexcept override {
                DeleteFileW(temporary.c_str());
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
