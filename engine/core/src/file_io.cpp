#include "asharia/core/file_io.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fstream>
#include <limits>
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
                         "Core file " + std::string{action} + " failed for '" + path.string() +
                             "': " + std::string{reason} + "."};
        }

    } // namespace

    namespace detail {

        Result<std::vector<std::byte>> readBoundedStream(std::istream& stream,
                                                         std::uint64_t measuredBytes,
                                                         FileReadLimits limits,
                                                         const std::filesystem::path& path) {
            if (limits.maxBytes == 0U) {
                return std::unexpected{fileIoError("read", path, "maximum byte count is zero")};
            }
            if (measuredBytes > limits.maxBytes ||
                measuredBytes > static_cast<std::uint64_t>(SIZE_MAX) ||
                measuredBytes >
                    static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
                return std::unexpected{
                    fileIoError("read", path, "file exceeds configured byte limit")};
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
                    fileIoError("read", path, "file grew while it was being read")};
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
                auto written = (*temporary)->write(bytes.subspan(offset));
                if (!written) {
                    return std::unexpected{std::move(written.error())};
                }
                if (*written == 0U) {
                    return std::unexpected{
                        fileIoError("write", target, "temporary write made no progress")};
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

            auto replaced = backend.replace((*temporary)->path(), target);
            if (!replaced) {
                return std::unexpected{std::move(replaced.error())};
            }

            (*temporary)->releaseAfterReplace();
            return {};
        }

    } // namespace detail

    Result<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path,
                                                 FileReadLimits limits) {
        if (limits.maxBytes == 0U) {
            return std::unexpected{fileIoError("read", path, "maximum byte count is zero")};
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

} // namespace asharia::core
