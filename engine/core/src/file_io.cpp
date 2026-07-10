#include "asharia/core/file_io.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fstream>
#include <limits>
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

} // namespace asharia::core
