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

        class PosixAtomicTemporaryFile final : public AtomicTemporaryFile {
        public:
            PosixAtomicTemporaryFile(int descriptor, std::filesystem::path path)
                : descriptor_(descriptor), path_(std::move(path)) {}

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
                    return std::unexpected{posixFileError("temporary write", path_, errno)};
                }
                if (written == 0 && !bytes.empty()) {
                    return std::unexpected{posixFileError("temporary write", path_, EIO)};
                }
                return static_cast<std::size_t>(written);
            }

            [[nodiscard]] VoidResult flush() override {
                int flushResult = -1;
                do {
                    flushResult = fsync(descriptor_);
                } while (flushResult < 0 && errno == EINTR);
                if (flushResult < 0) {
                    return std::unexpected{posixFileError("temporary flush", path_, errno)};
                }
                return {};
            }

            [[nodiscard]] VoidResult close() override {
                const int descriptor = std::exchange(descriptor_, -1);
                if (::close(descriptor) < 0) {
                    return std::unexpected{posixFileError("temporary close", path_, errno)};
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
            bool released_{};
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
                                                               openTemporary.path());
                openTemporary.release();
                return result;
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

        private:
            std::atomic<std::uint64_t> nextTemporaryId_{1U};
        };

    } // namespace

    AtomicFileBackend& atomicFileBackend() {
        static PosixAtomicFileBackend backend;
        return backend;
    }

} // namespace asharia::core::detail
