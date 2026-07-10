#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/file_io.hpp"

#include "file_io_internal.hpp"

namespace {

    [[nodiscard]] std::filesystem::path createUniqueTestDirectory() {
        constexpr std::uint32_t kMaximumCreateAttempts = 128U;
        const auto temporaryRoot = std::filesystem::temp_directory_path();

        for (std::uint32_t attempt = 0U; attempt < kMaximumCreateAttempts; ++attempt) {
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate =
                temporaryRoot / ("asharia-core-file-io-tests-" + std::to_string(timestamp) + "." +
                                 std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                return candidate;
            }
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error{"Could not create test directory",
                                                        candidate, error};
            }
        }

        throw std::runtime_error{"Could not allocate a unique Core file I/O test directory."};
    }

    class TempFile final {
    public:
        explicit TempFile(std::string_view name)
            : root_(createUniqueTestDirectory()), path_(root_ / name) {}

        ~TempFile() {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
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
        std::filesystem::path root_;
        std::filesystem::path path_;
    };

    [[nodiscard]] bool contains(std::string_view text, std::string_view token) {
        return text.find(token) != std::string_view::npos;
    }

    [[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
        std::vector<std::byte> bytes(text.size());
        if (!text.empty()) {
            std::memcpy(bytes.data(), text.data(), text.size());
        }
        return bytes;
    }

    class FakeAtomicFileBackend final : public asharia::core::detail::AtomicFileBackend {
    public:
        [[nodiscard]] asharia::Result<std::filesystem::path>
        writeUniqueTemporary(const std::filesystem::path& target, std::span<const std::byte> bytes,
                             asharia::core::AtomicFileWriteOptions options) override {
            writeCalled = true;
            writeTarget = target;
            observedOptions = options;
            if (failWrite) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 11, "temporary write failed"}};
            }

            temporaryPath = target.parent_path() / (target.filename().string() + ".tmp.fake");
            temporaryBytes.assign(bytes.begin(), bytes.end());
            temporaryExists = true;
            return temporaryPath;
        }

        [[nodiscard]] asharia::VoidResult replace(const std::filesystem::path& temporary,
                                                  const std::filesystem::path& target) override {
            replaceCalled = true;
            replaceTemporary = temporary;
            replaceTarget = target;
            if (failReplace) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 22, "replace failed"}};
            }

            targetBytes = temporaryBytes;
            temporaryExists = false;
            return {};
        }

        void removeTemporary(const std::filesystem::path& temporary) noexcept override {
            cleanupCalled = true;
            cleanupMatchedTemporary = temporary == temporaryPath;
            temporaryExists = false;
        }

        bool failWrite{};
        bool failReplace{};
        bool writeCalled{};
        bool replaceCalled{};
        bool cleanupCalled{};
        bool cleanupMatchedTemporary{};
        bool temporaryExists{};
        asharia::core::AtomicFileWriteOptions observedOptions{};
        std::filesystem::path writeTarget;
        std::filesystem::path temporaryPath;
        std::filesystem::path replaceTemporary;
        std::filesystem::path replaceTarget;
        std::vector<std::byte> targetBytes{bytesOf("old")};
        std::vector<std::byte> temporaryBytes;
    };

    [[nodiscard]] bool completesAtomicReplacement() {
        FakeAtomicFileBackend backend;
        const auto replacement = bytesOf("new");
        const std::filesystem::path target{"save/data.bin"};

        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            target, replacement, {.flushFileBuffers = false}, backend);

        return result && backend.writeCalled && backend.replaceCalled && !backend.cleanupCalled &&
               !backend.temporaryExists && backend.writeTarget == target &&
               backend.replaceTarget == target &&
               backend.replaceTemporary == backend.temporaryPath &&
               backend.targetBytes == replacement && !backend.observedOptions.flushFileBuffers;
    }

    [[nodiscard]] bool propagatesTemporaryWriteFailure() {
        FakeAtomicFileBackend backend;
        backend.failWrite = true;
        const auto replacement = bytesOf("new");

        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", replacement, {}, backend);

        return !result && result.error().code == 11 && backend.writeCalled &&
               !backend.replaceCalled && !backend.cleanupCalled &&
               backend.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool replaceFailurePreservesOriginalAndCleansTemporary() {
        FakeAtomicFileBackend backend;
        backend.failReplace = true;
        const auto replacement = bytesOf("new");

        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", replacement, {}, backend);

        return !result && result.error().code == 22 && backend.replaceCalled &&
               backend.cleanupCalled && backend.cleanupMatchedTemporary &&
               !backend.temporaryExists && backend.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool rejectsMissingAtomicWriteParent() {
        const auto root = createUniqueTestDirectory();
        const auto target = root / "missing" / "data.txt";

        const auto result = asharia::core::writeFileTextAtomically(target, "new");

        std::error_code cleanupError;
        std::filesystem::remove_all(root, cleanupError);
        return !result && result.error().domain == asharia::ErrorDomain::Core &&
               contains(result.error().message, target.string()) &&
               !std::filesystem::exists(target);
    }

    [[nodiscard]] bool writesAndReplacesUsingPlatformBackend() {
        const TempFile file{"asharia-core-file-io-atomic-replacement.txt"};
        const auto initial = bytesOf("old");
        auto written = asharia::core::writeFileBytesAtomically(file.path(), initial,
                                                               {.flushFileBuffers = false});
        if (!written) {
            std::cerr << written.error().message << '\n';
            return false;
        }

        auto initialRead = asharia::core::readFileText(file.path(), {.maxBytes = 3U});
        if (!initialRead || *initialRead != "old") {
            return false;
        }

        auto replaced = asharia::core::writeFileTextAtomically(file.path(), "new");
        if (!replaced) {
            std::cerr << replaced.error().message << '\n';
            return false;
        }

        auto replacementRead = asharia::core::readFileText(file.path(), {.maxBytes = 3U});
        return replacementRead && *replacementRead == "new";
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
            std::pair<std::string_view, Test>{"completesAtomicReplacement",
                                              completesAtomicReplacement},
            std::pair<std::string_view, Test>{"propagatesTemporaryWriteFailure",
                                              propagatesTemporaryWriteFailure},
            std::pair<std::string_view, Test>{"replaceFailurePreservesOriginalAndCleansTemporary",
                                              replaceFailurePreservesOriginalAndCleansTemporary},
            std::pair<std::string_view, Test>{"rejectsMissingAtomicWriteParent",
                                              rejectsMissingAtomicWriteParent},
            std::pair<std::string_view, Test>{"writesAndReplacesUsingPlatformBackend",
                                              writesAndReplacesUsingPlatformBackend},
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
