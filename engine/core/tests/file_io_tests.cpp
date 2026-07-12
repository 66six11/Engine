#include <algorithm>
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
#include <map>
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

#if defined(_WIN32)
    class FakeWindowsReplaceOperations final
        : public asharia::core::detail::WindowsReplaceOperations {
    public:
        enum class ReplaceBehavior : std::uint8_t {
            Success,
            Partial1177,
            OrdinaryFailure,
        };

        // This override must preserve the target/replacement/backup order of the Win32 seam.
        // NOLINTBEGIN(bugprone-easily-swappable-parameters)
        [[nodiscard]] std::uint32_t replaceFile(const std::filesystem::path& target,
                                                const std::filesystem::path& replacement,
                                                const std::filesystem::path& backup) override {
            if (replaceBehavior == ReplaceBehavior::OrdinaryFailure) {
                return 5U;
            }

            files[backup] = files.at(target);
            files.erase(target);
            if (replaceBehavior == ReplaceBehavior::Partial1177) {
                if (createConcurrentTargetOnPartial) {
                    files[target] = "concurrent";
                }
                return ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
            }

            files[target] = files.at(replacement);
            files.erase(replacement);
            return ERROR_SUCCESS;
        }
        // NOLINTEND(bugprone-easily-swappable-parameters)

        [[nodiscard]] std::uint32_t moveFile(const std::filesystem::path& source,
                                             const std::filesystem::path& target) override {
            if (moveError != ERROR_SUCCESS) {
                return moveError;
            }
            if (files.contains(target)) {
                return ERROR_ALREADY_EXISTS;
            }
            files[target] = files.at(source);
            files.erase(source);
            return ERROR_SUCCESS;
        }

        [[nodiscard]] std::uint32_t deleteFile(const std::filesystem::path& path) override {
            ++deleteCalls;
            if (deleteError != ERROR_SUCCESS) {
                return deleteError;
            }
            files.erase(path);
            return ERROR_SUCCESS;
        }

        void reportWarning(std::string_view warning) noexcept override {
            observedWarningSize = std::min(warning.size(), observedWarning.size());
            std::memcpy(observedWarning.data(), warning.data(), observedWarningSize);
        }

        [[nodiscard]] std::string_view warningText() const noexcept {
            return {observedWarning.data(), observedWarningSize};
        }

        ReplaceBehavior replaceBehavior{ReplaceBehavior::Success};
        std::uint32_t moveError{ERROR_SUCCESS};
        std::uint32_t deleteError{ERROR_SUCCESS};
        bool createConcurrentTargetOnPartial{};
        std::size_t deleteCalls{};
        std::map<std::filesystem::path, std::string> files;
        std::array<char, 1024> observedWarning{};
        std::size_t observedWarningSize{};
    };

    [[nodiscard]] bool restoresOldTargetAfterPartialWindowsReplacement() {
        FakeWindowsReplaceOperations operations;
        operations.replaceBehavior = FakeWindowsReplaceOperations::ReplaceBehavior::Partial1177;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path replacement{"save/data.bin.tmp"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {replacement, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingWindowsFileWithRecovery(
            target, replacement, backup, operations);

        return outcome.commitState == asharia::core::detail::AtomicReplaceCommitState::NotReached &&
               outcome.temporaryDisposition ==
                   asharia::core::detail::AtomicTemporaryDisposition::Cleanup &&
               outcome.error.has_value() &&
               contains(outcome.error->message, "commitPointReached=false") &&
               contains(outcome.error->message, "recovery=restored") &&
               contains(outcome.error->message, target.string()) &&
               contains(outcome.error->message, replacement.string()) &&
               contains(outcome.error->message, backup.string()) &&
               operations.files.at(target) == "old" && operations.files.at(replacement) == "new" &&
               !operations.files.contains(backup);
    }

    [[nodiscard]] bool preservesRecoveryArtifactsWhenWindowsRestoreFails() {
        FakeWindowsReplaceOperations operations;
        operations.replaceBehavior = FakeWindowsReplaceOperations::ReplaceBehavior::Partial1177;
        operations.moveError = ERROR_ACCESS_DENIED;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path replacement{"save/data.bin.tmp"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {replacement, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingWindowsFileWithRecovery(
            target, replacement, backup, operations);

        return outcome.commitState ==
                   asharia::core::detail::AtomicReplaceCommitState::Indeterminate &&
               outcome.temporaryDisposition ==
                   asharia::core::detail::AtomicTemporaryDisposition::Preserve &&
               outcome.error.has_value() &&
               contains(outcome.error->message, "commitPointReached=indeterminate") &&
               contains(outcome.error->message, "recoveryError=5") &&
               contains(outcome.error->message, target.string()) &&
               contains(outcome.error->message, replacement.string()) &&
               contains(outcome.error->message, backup.string()) &&
               !operations.files.contains(target) && operations.files.at(replacement) == "new" &&
               operations.files.at(backup) == "old";
    }

    [[nodiscard]] bool preservesConcurrentTargetAfterPartialWindowsReplacement() {
        FakeWindowsReplaceOperations operations;
        operations.replaceBehavior = FakeWindowsReplaceOperations::ReplaceBehavior::Partial1177;
        operations.createConcurrentTargetOnPartial = true;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path replacement{"save/data.bin.tmp"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {replacement, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingWindowsFileWithRecovery(
            target, replacement, backup, operations);

        return outcome.commitState ==
                   asharia::core::detail::AtomicReplaceCommitState::Indeterminate &&
               outcome.temporaryDisposition ==
                   asharia::core::detail::AtomicTemporaryDisposition::Preserve &&
               outcome.error.has_value() &&
               contains(outcome.error->message, "commitPointReached=indeterminate") &&
               contains(outcome.error->message,
                        "recoveryError=" + std::to_string(ERROR_ALREADY_EXISTS)) &&
               operations.files.at(target) == "concurrent" &&
               operations.files.at(replacement) == "new" && operations.files.at(backup) == "old";
    }

    [[nodiscard]] bool commitsWindowsReplacementDespiteBackupCleanupFailure() {
        FakeWindowsReplaceOperations operations;
        operations.deleteError = ERROR_ACCESS_DENIED;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path replacement{"save/data.bin.tmp"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {replacement, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingWindowsFileWithRecovery(
            target, replacement, backup, operations);

        return outcome.commitState == asharia::core::detail::AtomicReplaceCommitState::Committed &&
               outcome.temporaryDisposition ==
                   asharia::core::detail::AtomicTemporaryDisposition::Preserve &&
               !outcome.error.has_value() && operations.files.at(target) == "new" &&
               !operations.files.contains(replacement) && operations.files.at(backup) == "old" &&
               contains(operations.warningText(), "backup cleanup") &&
               contains(operations.warningText(), backup.string());
    }

    [[nodiscard]] bool ordinaryWindowsReplaceFailurePreservesOriginalNames() {
        FakeWindowsReplaceOperations operations;
        operations.replaceBehavior = FakeWindowsReplaceOperations::ReplaceBehavior::OrdinaryFailure;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path replacement{"save/data.bin.tmp"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {replacement, "new"}, {backup, "external"}};

        const auto outcome = asharia::core::detail::replaceExistingWindowsFileWithRecovery(
            target, replacement, backup, operations);

        return outcome.commitState == asharia::core::detail::AtomicReplaceCommitState::NotReached &&
               outcome.temporaryDisposition ==
                   asharia::core::detail::AtomicTemporaryDisposition::Cleanup &&
               outcome.error.has_value() &&
               contains(outcome.error->message, "commitPointReached=false") &&
               operations.files.at(target) == "old" && operations.files.at(replacement) == "new" &&
               operations.files.at(backup) == "external" && operations.deleteCalls == 0U;
    }
#endif

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
        bool overReportWrite;
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

            if (config_.overReportWrite) {
                return bytes.size() + 1U;
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

        void releaseCleanupOwnership() noexcept override {
            state_->released = true;
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
                                                  .overReportWrite = overReportWrite,
                                                  .failFlush = failFlush,
                                                  .failClose = failClose});
        }

        [[nodiscard]] asharia::core::detail::AtomicReplaceOutcome
        replace(const std::filesystem::path& temporary,
                const std::filesystem::path& target) override {
            ++state.replaceCalls;
            if (failReplace) {
                return {.commitState = asharia::core::detail::AtomicReplaceCommitState::NotReached,
                        .temporaryDisposition =
                            asharia::core::detail::AtomicTemporaryDisposition::Cleanup,
                        .error = asharia::Error{asharia::ErrorDomain::Core, 22,
                                                "replace failed commitPointReached=false"}};
            }

            if (indeterminateReplaceFailure) {
                return {.commitState =
                            asharia::core::detail::AtomicReplaceCommitState::Indeterminate,
                        .temporaryDisposition =
                            asharia::core::detail::AtomicTemporaryDisposition::Preserve,
                        .error = asharia::Error{asharia::ErrorDomain::Core, 24,
                                                "replace failed commitPointReached=indeterminate"}};
            }

            if (temporary != state.temporaryPath || target != state.targetPath) {
                return {.commitState = asharia::core::detail::AtomicReplaceCommitState::NotReached,
                        .temporaryDisposition =
                            asharia::core::detail::AtomicTemporaryDisposition::Cleanup,
                        .error = asharia::Error{asharia::ErrorDomain::Core, 23,
                                                "replace paths mismatched"}};
            }
            state.targetBytes = state.temporaryBytes;
            state.temporaryExists = false;
            return {.commitState = asharia::core::detail::AtomicReplaceCommitState::Committed,
                    .temporaryDisposition =
                        asharia::core::detail::AtomicTemporaryDisposition::Preserve,
                    .error = std::nullopt};
        }

        bool failCreate{};
        std::size_t maximumWriteBytes{std::numeric_limits<std::size_t>::max()};
        std::size_t failWriteCall{std::numeric_limits<std::size_t>::max()};
        bool overReportWrite{};
        bool failFlush{};
        bool failClose{};
        bool failReplace{};
        bool indeterminateReplaceFailure{};
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

    [[nodiscard]] bool indeterminateReplaceFailurePreservesTemporary() {
        FakeAtomicFileBackend backend;
        backend.indeterminateReplaceFailure = true;
        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            "save/data.bin", bytesOf("new"), {}, backend);

        return !result && result.error().code == 24 && backend.state.replaceCalls == 1U &&
               backend.state.released && backend.state.temporaryExists &&
               backend.state.temporaryBytes == bytesOf("new") &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool overReportedWritePreservesOriginalAndCleansTemporary() {
        FakeAtomicFileBackend backend;
        backend.overReportWrite = true;
        const std::filesystem::path target{"save/over-report.bin"};
        const auto result = asharia::core::detail::writeFileBytesAtomicallyWithBackend(
            target, bytesOf("new"), {}, backend);

        return !result && result.error().domain == asharia::ErrorDomain::Core &&
               contains(result.error().message, target.string()) &&
               contains(result.error().message, "reportedBytes=4") &&
               contains(result.error().message, "remainingBytes=3") &&
               backend.state.replaceCalls == 0U && !backend.state.temporaryExists &&
               backend.state.targetBytes == bytesOf("old");
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
               contains(result.error().message, "maxBytes=0") &&
               contains(result.error().message, "must be greater than zero");
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
               contains(result.error().message, "observedBytes=4") &&
               contains(result.error().message, "maxBytes=3");
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
               contains(result.error().message, "measuredBytes=3") &&
               contains(result.error().message, "observedBytesAtLeast=4") &&
               contains(result.error().message, "maxBytes=4");
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
#if defined(_WIN32)
            std::pair<std::string_view, Test>{"restoresOldTargetAfterPartialWindowsReplacement",
                                              restoresOldTargetAfterPartialWindowsReplacement},
            std::pair<std::string_view, Test>{"preservesRecoveryArtifactsWhenWindowsRestoreFails",
                                              preservesRecoveryArtifactsWhenWindowsRestoreFails},
            std::pair<std::string_view, Test>{
                "preservesConcurrentTargetAfterPartialWindowsReplacement",
                preservesConcurrentTargetAfterPartialWindowsReplacement},
            std::pair<std::string_view, Test>{
                "commitsWindowsReplacementDespiteBackupCleanupFailure",
                commitsWindowsReplacementDespiteBackupCleanupFailure},
            std::pair<std::string_view, Test>{"ordinaryWindowsReplaceFailurePreservesOriginalNames",
                                              ordinaryWindowsReplaceFailurePreservesOriginalNames},
#endif
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
            std::pair<std::string_view, Test>{"indeterminateReplaceFailurePreservesTemporary",
                                              indeterminateReplaceFailurePreservesTemporary},
            std::pair<std::string_view, Test>{
                "overReportedWritePreservesOriginalAndCleansTemporary",
                overReportedWritePreservesOriginalAndCleansTemporary},
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
