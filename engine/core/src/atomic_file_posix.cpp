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
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include "file_io_internal.hpp"

namespace asharia::core::detail {
    namespace {

        [[nodiscard]] Error posixFileError(std::string_view action,
                                           const std::filesystem::path& path, int errorCode) {
            return Error{ErrorDomain::Core, errorCode,
                         "Core atomic file " + std::string{action} + " failed for '" +
                             path.string() + "' (POSIX error " + std::to_string(errorCode) + ")."};
        }

        [[nodiscard]] std::filesystem::path temporaryPathFor(const std::filesystem::path& target,
                                                             std::uint64_t uniqueValue) {
            const std::string temporaryName = target.filename().string() + ".tmp." +
                                              std::to_string(getpid()) + "." +
                                              std::to_string(uniqueValue);
            return target.parent_path() / temporaryName;
        }

        class PosixAtomicFileBackend final : public AtomicFileBackend {
        public:
            Result<std::filesystem::path>
            writeUniqueTemporary(const std::filesystem::path& target,
                                 std::span<const std::byte> bytes,
                                 AtomicFileWriteOptions options) override {
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

                if (copyTargetMode && fchmod(descriptor, temporaryMode) < 0) {
                    const int modeError = errno;
                    const int closeResult = close(descriptor);
                    const int closeError = closeResult < 0 ? errno : 0;
                    unlink(temporary.c_str());
                    return std::unexpected{
                        posixFileError(closeResult < 0 ? "temporary close" : "permission copy",
                                       temporary, closeResult < 0 ? closeError : modeError)};
                }

                std::size_t offset = 0U;
                while (offset < bytes.size()) {
                    const auto remaining = bytes.size() - offset;
                    const auto chunkSize = std::min<std::size_t>(
                        remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
                    const ssize_t written = write(descriptor, bytes.data() + offset, chunkSize);
                    if (written < 0 && errno == EINTR) {
                        continue;
                    }
                    if (written <= 0) {
                        const int writeError = written == 0 ? EIO : errno;
                        const int closeResult = close(descriptor);
                        const int closeError = closeResult < 0 ? errno : 0;
                        unlink(temporary.c_str());
                        return std::unexpected{
                            posixFileError(closeResult < 0 ? "temporary close" : "temporary write",
                                           temporary, closeResult < 0 ? closeError : writeError)};
                    }
                    offset += static_cast<std::size_t>(written);
                }

                if (options.flushFileBuffers && fsync(descriptor) < 0) {
                    const int flushError = errno;
                    const int closeResult = close(descriptor);
                    const int closeError = closeResult < 0 ? errno : 0;
                    unlink(temporary.c_str());
                    return std::unexpected{
                        posixFileError(closeResult < 0 ? "temporary close" : "temporary flush",
                                       temporary, closeResult < 0 ? closeError : flushError)};
                }

                if (close(descriptor) < 0) {
                    const int closeError = errno;
                    unlink(temporary.c_str());
                    return std::unexpected{
                        posixFileError("temporary close", temporary, closeError)};
                }

                return temporary;
            }

            VoidResult replace(const std::filesystem::path& temporary,
                               const std::filesystem::path& target) override {
                if (rename(temporary.c_str(), target.c_str()) < 0) {
                    return std::unexpected{posixFileError("replacement", target, errno)};
                }
                return {};
            }

            void removeTemporary(const std::filesystem::path& temporary) noexcept override {
                unlink(temporary.c_str());
            }

        private:
            std::atomic<std::uint64_t> nextTemporaryId_{1U};
        };

    } // namespace

    AtomicFileBackend& atomicFileBackend() {
        static PosixAtomicFileBackend backend;
        return backend;
    }

} // namespace asharia::core::detail
