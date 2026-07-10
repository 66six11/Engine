#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "asharia/core/file_io.hpp"

#include "file_io_internal.hpp"

namespace {

    class TempFile final {
    public:
        explicit TempFile(std::string_view name)
            : path_(std::filesystem::temp_directory_path() / name) {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        ~TempFile() {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;
        TempFile(TempFile&&) = delete;
        TempFile& operator=(TempFile&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return path_;
        }

        [[nodiscard]] bool write(std::string_view text) const {
            std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
            if (!text.empty()) {
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            }
            return stream.good();
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] bool contains(std::string_view text, std::string_view token) {
        return text.find(token) != std::string_view::npos;
    }

    [[nodiscard]] bool rejectsZeroReadLimit() {
        const TempFile file{"asharia-core-file-io-zero-limit.bin"};
        if (!file.write("x")) {
            std::cerr << "rejectsZeroReadLimit could not create its input file.\n";
            return false;
        }

        auto result = asharia::core::readFileBytes(file.path(), {.maxBytes = 0U});
        return !result && result.error().domain == asharia::ErrorDomain::Core &&
               contains(result.error().message, "maximum byte count is zero");
    }

    [[nodiscard]] bool readsAtExactByteLimit() {
        const TempFile file{"asharia-core-file-io-exact-limit.txt"};
        if (!file.write("abc")) {
            std::cerr << "readsAtExactByteLimit could not create its input file.\n";
            return false;
        }

        auto result = asharia::core::readFileText(file.path(), {.maxBytes = 3U});
        return result && *result == "abc";
    }

    [[nodiscard]] bool rejectsFileAboveByteLimit() {
        const TempFile file{"asharia-core-file-io-above-limit.bin"};
        if (!file.write("abcd")) {
            std::cerr << "rejectsFileAboveByteLimit could not create its input file.\n";
            return false;
        }

        auto result = asharia::core::readFileBytes(file.path(), {.maxBytes = 3U});
        return !result && result.error().domain == asharia::ErrorDomain::Core &&
               contains(result.error().message, "file exceeds configured byte limit");
    }

    [[nodiscard]] bool readsEmptyFile() {
        const TempFile file{"asharia-core-file-io-empty.bin"};
        if (!file.write({})) {
            std::cerr << "readsEmptyFile could not create its input file.\n";
            return false;
        }

        auto result = asharia::core::readFileBytes(file.path(), {.maxBytes = 1U});
        return result && result->empty();
    }

    [[nodiscard]] bool rejectsGrowthAfterMeasuredSize() {
        std::istringstream stream{"abcd", std::ios::binary};
        auto result =
            asharia::core::detail::readBoundedStream(stream, 3U, {.maxBytes = 4U}, "growing.bin");
        return !result && result.error().domain == asharia::ErrorDomain::Core &&
               contains(result.error().message, "grew while it was being read");
    }

} // namespace

// Unexpected test exceptions are caught and converted into a failing process result below.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        using Test = bool (*)();
        const std::array tests{
            std::pair<std::string_view, Test>{"rejectsZeroReadLimit", rejectsZeroReadLimit},
            std::pair<std::string_view, Test>{"readsAtExactByteLimit", readsAtExactByteLimit},
            std::pair<std::string_view, Test>{"rejectsFileAboveByteLimit",
                                              rejectsFileAboveByteLimit},
            std::pair<std::string_view, Test>{"readsEmptyFile", readsEmptyFile},
            std::pair<std::string_view, Test>{"rejectsGrowthAfterMeasuredSize",
                                              rejectsGrowthAfterMeasuredSize},
        };

        for (const auto& [name, test] : tests) {
            if (!test()) {
                std::cerr << name << " failed.\n";
                return EXIT_FAILURE;
            }
        }

        std::cout << tests.size() << " core file I/O tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Core file I/O tests threw an exception: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Core file I/O tests threw an unknown exception.\n";
        return EXIT_FAILURE;
    }
}
