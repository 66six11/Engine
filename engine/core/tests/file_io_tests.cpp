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
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

    struct FakeAtomicFileState {
        std::vector<std::byte> targetBytes{bytesOf("old")};
        std::vector<std::byte> temporaryBytes;
        std::filesystem::path targetPath;
        std::filesystem::path temporaryPath;
        std::size_t writeCalls{};
        std::size_t flushCalls{};
        std::size_t closeCalls{};
        std::size_t replaceCalls{};
        bool temporaryExists{};
        bool released{};
    };

    struct FakeAtomicTemporaryConfig {
        std::size_t maximumWriteBytes;
        std::size_t failWriteCall;
        bool failFlush;
        bool failClose;
    };

    class FakeAtomicTemporaryFile final : public asharia::core::detail::AtomicTemporaryFile {
    public:
        FakeAtomicTemporaryFile(FakeAtomicFileState* state, FakeAtomicTemporaryConfig config)
            : state_(state), config_(config) {}

        ~FakeAtomicTemporaryFile() override {
            if (!state_->released) {
                state_->temporaryExists = false;
            }
        }

        FakeAtomicTemporaryFile(const FakeAtomicTemporaryFile&) = delete;
        FakeAtomicTemporaryFile& operator=(const FakeAtomicTemporaryFile&) = delete;
        FakeAtomicTemporaryFile(FakeAtomicTemporaryFile&&) = delete;
        FakeAtomicTemporaryFile& operator=(FakeAtomicTemporaryFile&&) = delete;

        [[nodiscard]] asharia::Result<std::size_t>
        write(std::span<const std::byte> bytes) override {
            ++state_->writeCalls;
            if (state_->writeCalls == config_.failWriteCall) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 12, "temporary write failed"}};
            }

            const auto written = std::min(bytes.size(), config_.maximumWriteBytes);
            state_->temporaryBytes.insert(state_->temporaryBytes.end(), bytes.begin(),
                                          bytes.begin() + static_cast<std::ptrdiff_t>(written));
            return written;
        }

        [[nodiscard]] asharia::VoidResult flush() override {
            ++state_->flushCalls;
            if (config_.failFlush) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 13, "temporary flush failed"}};
            }
            return {};
        }

        [[nodiscard]] asharia::VoidResult close() override {
            ++state_->closeCalls;
            if (config_.failClose) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 14, "temporary close failed"}};
            }
            return {};
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept override {
            return state_->temporaryPath;
        }

        void releaseAfterReplace() noexcept override {
            state_->released = true;
            state_->temporaryExists = false;
        }

    private:
        FakeAtomicFileState* state_;
        FakeAtomicTemporaryConfig config_;
    };

    class FakeAtomicFileBackend final : public asharia::core::detail::AtomicFileBackend {
    public:
        [[nodiscard]] asharia::Result<std::unique_ptr<asharia::core::detail::AtomicTemporaryFile>>
        createUniqueTemporary(const std::filesystem::path& target) override {
            state.targetPath = target;
            if (failCreate) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 11, "temporary create failed"}};
            }

            state.temporaryPath = target.parent_path() / (target.filename().string() + ".tmp.fake");
            state.temporaryExists = true;
            return std::make_unique<FakeAtomicTemporaryFile>(
                &state, FakeAtomicTemporaryConfig{.maximumWriteBytes = maximumWriteBytes,
                                                  .failWriteCall = failWriteCall,
                                                  .failFlush = failFlush,
                                                  .failClose = failClose});
        }

        [[nodiscard]] asharia::VoidResult replace(const std::filesystem::path& temporary,
                                                  const std::filesystem::path& target) override {
            ++state.replaceCalls;
            if (failReplace) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 22, "replace failed"}};
            }

            if (temporary != state.temporaryPath || target != state.targetPath) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 23, "replace paths mismatched"}};
            }
            state.targetBytes = state.temporaryBytes;
            return {};
        }

        bool failCreate{};
        std::size_t maximumWriteBytes{std::numeric_limits<std::size_t>::max()};
        std::size_t failWriteCall{std::numeric_limits<std::size_t>::max()};
        bool failFlush{};
        bool failClose{};
        bool failReplace{};
        FakeAtomicFileState state;
    };

    [[nodiscard]] bool createFailurePreservesOriginal() {
        FakeAtomicFileBackend backend;
        backend.failCreate = true;
        const auto replacement = bytesOf("new");

        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", replacement, {}, backend);

        return !result && result.error().code == 11 && backend.state.writeCalls == 0U &&
               backend.state.replaceCalls == 0U && !backend.state.temporaryExists &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool partialWriteFailurePreservesOriginalAndCleansTemporary() {
        FakeAtomicFileBackend backend;
        backend.maximumWriteBytes = 2U;
        backend.failWriteCall = 2U;
        const auto replacement = bytesOf("new-data");

        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", replacement, {}, backend);

        return !result && result.error().code == 12 && backend.state.writeCalls == 2U &&
               backend.state.replaceCalls == 0U && !backend.state.temporaryExists &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool flushFailurePreservesOriginalAndCleansTemporary() {
        FakeAtomicFileBackend backend;
        backend.failFlush = true;
        const auto replacement = bytesOf("new");

        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", replacement, {}, backend);

        return !result && result.error().code == 13 && backend.state.flushCalls == 1U &&
               backend.state.closeCalls == 0U && backend.state.replaceCalls == 0U &&
               !backend.state.temporaryExists && backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool closeFailurePreservesOriginalAndCleansTemporary() {
        FakeAtomicFileBackend backend;
        backend.failClose = true;
        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", bytesOf("new"), {}, backend);

        return !result && result.error().code == 14 && backend.state.closeCalls == 1U &&
               backend.state.replaceCalls == 0U && !backend.state.temporaryExists &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool replaceFailurePreservesOriginalAndCleansTemporary() {
        FakeAtomicFileBackend backend;
        backend.failReplace = true;
        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", bytesOf("new"), {}, backend);

        return !result && result.error().code == 22 && backend.state.replaceCalls == 1U &&
               !backend.state.released && !backend.state.temporaryExists &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool partialWritesCompleteReplacementAndReleaseTemporary() {
        FakeAtomicFileBackend backend;
        backend.maximumWriteBytes = 2U;
        const auto replacement = bytesOf("new-data");
        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", replacement, {}, backend);

        return result && backend.state.writeCalls == 4U && backend.state.flushCalls == 1U &&
               backend.state.closeCalls == 1U && backend.state.replaceCalls == 1U &&
               backend.state.released && !backend.state.temporaryExists &&
               backend.state.targetBytes == replacement;
    }

    [[nodiscard]] bool disabledFlushSkipsFlushStage() {
        FakeAtomicFileBackend backend;
        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", bytesOf("new"), {.flushFileBuffers = false}, backend);

        return result && backend.state.flushCalls == 0U && backend.state.closeCalls == 1U &&
               backend.state.released;
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

    [[nodiscard]] bool createsPermanentFileUsingWindowsPlatformBackend() {
#if defined(_WIN32)
        const TempFile file{"asharia-core-file-io-atomic-attributes.txt"};
        auto written = asharia::core::writeFileTextAtomically(file.path(), "persistent");
        if (!written) {
            std::cerr << written.error().message << '\n';
            return false;
        }

        const DWORD attributes = GetFileAttributesW(file.path().c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_TEMPORARY) == 0U;
#else
        return true;
#endif
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
            std::pair<std::string_view, Test>{"createFailurePreservesOriginal",
                                              createFailurePreservesOriginal},
            std::pair<std::string_view, Test>{
                "partialWriteFailurePreservesOriginalAndCleansTemporary",
                partialWriteFailurePreservesOriginalAndCleansTemporary},
            std::pair<std::string_view, Test>{"flushFailurePreservesOriginalAndCleansTemporary",
                                              flushFailurePreservesOriginalAndCleansTemporary},
            std::pair<std::string_view, Test>{"closeFailurePreservesOriginalAndCleansTemporary",
                                              closeFailurePreservesOriginalAndCleansTemporary},
            std::pair<std::string_view, Test>{"replaceFailurePreservesOriginalAndCleansTemporary",
                                              replaceFailurePreservesOriginalAndCleansTemporary},
            std::pair<std::string_view, Test>{"partialWritesCompleteReplacementAndReleaseTemporary",
                                              partialWritesCompleteReplacementAndReleaseTemporary},
            std::pair<std::string_view, Test>{"disabledFlushSkipsFlushStage",
                                              disabledFlushSkipsFlushStage},
            std::pair<std::string_view, Test>{"rejectsMissingAtomicWriteParent",
                                              rejectsMissingAtomicWriteParent},
            std::pair<std::string_view, Test>{"writesAndReplacesUsingPlatformBackend",
                                              writesAndReplacesUsingPlatformBackend},
            std::pair<std::string_view, Test>{"createsPermanentFileUsingWindowsPlatformBackend",
                                              createsPermanentFileUsingWindowsPlatformBackend},
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
