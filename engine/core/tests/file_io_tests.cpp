#include <algorithm>
#include <array>
#include <cerrno>
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
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
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

    constexpr std::string_view kUtf8FilenameMarker{"\xE6\x96\x87\xE4\xBB\xB6"}; // 文件

    [[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
        std::vector<std::byte> bytes(text.size());
        if (!text.empty()) {
            std::memcpy(bytes.data(), text.data(), text.size());
        }
        return bytes;
    }

    [[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!text.empty()) {
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        return stream.good();
    }

    [[nodiscard]] std::string readTextFile(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    struct FakeExclusiveFileLockState {
        std::filesystem::path observedPath;
        std::size_t acquireCalls{};
        std::size_t releaseCalls{};
        std::size_t destructorCalls{};
        bool ownsLock{};
    };

    class FakeExclusiveFileLockHandle final
        : public asharia::core::detail::ExclusiveFileLockHandle {
    public:
        FakeExclusiveFileLockHandle(FakeExclusiveFileLockState* state, bool failRelease) noexcept
            : state_(state), failRelease_(failRelease) {
            state_->ownsLock = true;
        }

        ~FakeExclusiveFileLockHandle() override {
            ++state_->destructorCalls;
            state_->ownsLock = false;
        }

        FakeExclusiveFileLockHandle(const FakeExclusiveFileLockHandle&) = delete;
        FakeExclusiveFileLockHandle& operator=(const FakeExclusiveFileLockHandle&) = delete;
        FakeExclusiveFileLockHandle(FakeExclusiveFileLockHandle&&) = delete;
        FakeExclusiveFileLockHandle& operator=(FakeExclusiveFileLockHandle&&) = delete;

        [[nodiscard]] bool ownsLock() const noexcept override {
            return state_->ownsLock;
        }

        [[nodiscard]] asharia::VoidResult release() override {
            ++state_->releaseCalls;
            if (failRelease_) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 91, "fake lock release failed"}};
            }
            state_->ownsLock = false;
            return {};
        }

    private:
        FakeExclusiveFileLockState* state_;
        bool failRelease_{};
    };

    class FakeExclusiveFileLockBackend final
        : public asharia::core::detail::ExclusiveFileLockBackend {
    public:
        enum class Behavior : std::uint8_t {
            Acquired,
            Contended,
            Failed,
        };

        [[nodiscard]] asharia::Result<
            std::unique_ptr<asharia::core::detail::ExclusiveFileLockHandle>>
        tryAcquire(const std::filesystem::path& lockPath) override {
            ++state.acquireCalls;
            state.observedPath = lockPath;
            if (behavior == Behavior::Failed) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 90, "fake lock acquisition failed"}};
            }
            if (behavior == Behavior::Contended) {
                return std::unique_ptr<asharia::core::detail::ExclusiveFileLockHandle>{};
            }

            std::unique_ptr<asharia::core::detail::ExclusiveFileLockHandle> result =
                std::make_unique<FakeExclusiveFileLockHandle>(&state, failRelease);
            return result;
        }

        Behavior behavior{Behavior::Acquired};
        bool failRelease{};
        FakeExclusiveFileLockState state;
    };

    [[nodiscard]] bool rejectsEmptyExclusiveFileLockPathBeforeBackendUse() {
        FakeExclusiveFileLockBackend backend;

        const auto attempt =
            asharia::core::detail::tryAcquireExclusiveFileLockWithBackend({}, backend);

        return !attempt && attempt.error().domain == asharia::ErrorDomain::Core &&
               contains(attempt.error().message, "must be non-empty") &&
               backend.state.acquireCalls == 0U;
    }

    [[nodiscard]] bool distinguishesExclusiveFileLockContentionFromFailure() {
        FakeExclusiveFileLockBackend contended;
        contended.behavior = FakeExclusiveFileLockBackend::Behavior::Contended;
        FakeExclusiveFileLockBackend failed;
        failed.behavior = FakeExclusiveFileLockBackend::Behavior::Failed;

        const auto contention = asharia::core::detail::tryAcquireExclusiveFileLockWithBackend(
            "save/project.writer.lock", contended);
        const auto failure = asharia::core::detail::tryAcquireExclusiveFileLockWithBackend(
            "save/project.writer.lock", failed);

        return contention && !contention->has_value() && !failure && failure.error().code == 90 &&
               contended.state.acquireCalls == 1U && failed.state.acquireCalls == 1U;
    }

    [[nodiscard]] bool movesAndExplicitlyReleasesExclusiveFileLockOwnership() {
        FakeExclusiveFileLockBackend backend;
        auto attempt = asharia::core::detail::tryAcquireExclusiveFileLockWithBackend(
            "save/project.writer.lock", backend);
        if (!attempt) {
            return false;
        }
        auto optionalLock = std::move(*attempt);
        if (!optionalLock) {
            return false;
        }

        asharia::core::ExclusiveFileLock original = std::move(*optionalLock);
        asharia::core::ExclusiveFileLock moved = std::move(original);
        const auto released = moved.release();

        return released && !moved.ownsLock() && backend.state.releaseCalls == 1U &&
               backend.state.destructorCalls == 1U && !backend.state.ownsLock;
    }

    [[nodiscard]] bool preservesFailedExclusiveFileLockForDestructorRelease() {
        FakeExclusiveFileLockBackend backend;
        backend.failRelease = true;
        bool explicitFailurePreservedOwnership = false;
        {
            auto attempt = asharia::core::detail::tryAcquireExclusiveFileLockWithBackend(
                "save/project.writer.lock", backend);
            if (!attempt) {
                return false;
            }
            auto optionalLock = std::move(*attempt);
            if (!optionalLock) {
                return false;
            }

            auto released = optionalLock->release();
            explicitFailurePreservedOwnership = !released && released.error().code == 91 &&
                                                optionalLock->ownsLock() &&
                                                backend.state.destructorCalls == 0U;
        }

        return explicitFailurePreservedOwnership && backend.state.releaseCalls == 1U &&
               backend.state.destructorCalls == 1U && !backend.state.ownsLock;
    }

    [[nodiscard]] bool serializesPlatformExclusiveFileLockAndKeepsSentinel() {
        const TempFile owner{"project.writer.lock"};
        const auto& lockPath = owner.path();

        auto first = asharia::core::tryAcquireExclusiveFileLock(lockPath);
        if (!first) {
            return false;
        }
        auto firstLock = std::move(*first);
        if (!firstLock || !firstLock->ownsLock() || !std::filesystem::is_regular_file(lockPath)) {
            return false;
        }

        const auto second = asharia::core::tryAcquireExclusiveFileLock(lockPath);
        if (!second || *second) {
            return false;
        }

        const auto released = firstLock->release();
        auto third = asharia::core::tryAcquireExclusiveFileLock(lockPath);
        if (!released || !third) {
            return false;
        }
        auto thirdLock = std::move(*third);
        return thirdLock && thirdLock->ownsLock() && std::filesystem::is_regular_file(lockPath);
    }

    [[nodiscard]] bool releasesPlatformExclusiveFileLockOnDestruction() {
        const TempFile owner{"project.writer.lock"};
        const auto& lockPath = owner.path();
        {
            auto first = asharia::core::tryAcquireExclusiveFileLock(lockPath);
            if (!first) {
                return false;
            }
            auto firstLock = std::move(*first);
            if (!firstLock) {
                return false;
            }
        }

        auto second = asharia::core::tryAcquireExclusiveFileLock(lockPath);
        if (!second) {
            return false;
        }
        auto secondLock = std::move(*second);
        return secondLock && secondLock->ownsLock();
    }

    [[nodiscard]] bool rejectsInvalidPlatformExclusiveFileLockTargets() {
        const TempFile owner{"lock-directory"};
        std::error_code createError;
        if (!std::filesystem::create_directory(owner.path(), createError)) {
            return false;
        }

        const auto directoryAttempt = asharia::core::tryAcquireExclusiveFileLock(owner.path());
        const auto missingParentAttempt =
            asharia::core::tryAcquireExclusiveFileLock(owner.path() / "missing" / "writer.lock");

        return !directoryAttempt && !missingParentAttempt &&
               directoryAttempt.error().domain == asharia::ErrorDomain::Core &&
               missingParentAttempt.error().domain == asharia::ErrorDomain::Core;
    }

#if defined(_WIN32)
    class FakeWindowsReplaceOperations final
        : public asharia::core::detail::WindowsReplaceOperations {
    public:
        enum class ReplaceBehavior : std::uint8_t {
            Success,
            UnableToRemove1175,
            UnableToMove1176,
            Partial1177,
            CrossVolumeFailure,
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
            if (replaceBehavior == ReplaceBehavior::UnableToRemove1175) {
                return ERROR_UNABLE_TO_REMOVE_REPLACED;
            }
            if (replaceBehavior == ReplaceBehavior::UnableToMove1176) {
                return ERROR_UNABLE_TO_MOVE_REPLACEMENT;
            }
            if (replaceBehavior == ReplaceBehavior::CrossVolumeFailure) {
                return ERROR_NOT_SAME_DEVICE;
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

        [[nodiscard]] std::optional<bool>
        fileExists(const std::filesystem::path& path) noexcept override {
            return files.contains(path);
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

    [[nodiscard]] bool stagedWindowsCommitPreservesCallerOwnedBackup() {
        FakeWindowsReplaceOperations operations;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"save/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {staged, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingWindowsStagedFileWithRecovery(
            target, staged, backup, operations);

        return outcome.commitState == asharia::core::StagedFileCommitState::Committed &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Absent &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               !outcome.error.has_value() && operations.files.at(target) == "new" &&
               !operations.files.contains(staged) && operations.files.at(backup) == "old" &&
               operations.deleteCalls == 0U;
    }

    [[nodiscard]] bool stagedWindowsRegularFileValidationRejectsReparsePoints() {
        return asharia::core::detail::isWindowsRegularFileAttributes(FILE_ATTRIBUTE_NORMAL) &&
               asharia::core::detail::isWindowsRegularFileAttributes(FILE_ATTRIBUTE_ARCHIVE) &&
               !asharia::core::detail::isWindowsRegularFileAttributes(FILE_ATTRIBUTE_DIRECTORY) &&
               !asharia::core::detail::isWindowsRegularFileAttributes(
                   FILE_ATTRIBUTE_REPARSE_POINT) &&
               !asharia::core::detail::isWindowsRegularFileAttributes(FILE_ATTRIBUTE_ARCHIVE |
                                                                      FILE_ATTRIBUTE_REPARSE_POINT);
    }

    [[nodiscard]] bool stagedWindowsPreCommitFailuresPreserveNames() {
        constexpr std::array failures{
            FakeWindowsReplaceOperations::ReplaceBehavior::UnableToRemove1175,
            FakeWindowsReplaceOperations::ReplaceBehavior::UnableToMove1176,
            FakeWindowsReplaceOperations::ReplaceBehavior::CrossVolumeFailure,
        };
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"save/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};

        for (const auto failure : failures) {
            FakeWindowsReplaceOperations operations;
            operations.replaceBehavior = failure;
            operations.files = {{target, "old"}, {staged, "new"}};

            const auto outcome =
                asharia::core::detail::replaceExistingWindowsStagedFileWithRecovery(
                    target, staged, backup, operations);
            if (outcome.commitState != asharia::core::StagedFileCommitState::NotCommitted ||
                outcome.stagedFileState != asharia::core::StagedFileArtifactState::Present ||
                outcome.backupFileState != asharia::core::StagedFileArtifactState::Absent ||
                !outcome.error.has_value() || operations.files.at(target) != "old" ||
                operations.files.at(staged) != "new" || operations.files.contains(backup)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool stagedWindows1177ReportsRestoredArtifacts() {
        FakeWindowsReplaceOperations operations;
        operations.replaceBehavior = FakeWindowsReplaceOperations::ReplaceBehavior::Partial1177;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"save/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {staged, "new"}};

        const auto restored = asharia::core::detail::replaceExistingWindowsStagedFileWithRecovery(
            target, staged, backup, operations);
        if (restored.commitState != asharia::core::StagedFileCommitState::NotCommitted ||
            restored.stagedFileState != asharia::core::StagedFileArtifactState::Present ||
            restored.backupFileState != asharia::core::StagedFileArtifactState::Absent ||
            !restored.error.has_value() || operations.files.at(target) != "old" ||
            operations.files.at(staged) != "new" || operations.files.contains(backup)) {
            return false;
        }

        FakeWindowsReplaceOperations failedRestore;
        failedRestore.replaceBehavior = FakeWindowsReplaceOperations::ReplaceBehavior::Partial1177;
        failedRestore.moveError = ERROR_ACCESS_DENIED;
        failedRestore.files = {{target, "old"}, {staged, "new"}};
        const auto indeterminate =
            asharia::core::detail::replaceExistingWindowsStagedFileWithRecovery(
                target, staged, backup, failedRestore);
        return indeterminate.commitState == asharia::core::StagedFileCommitState::Indeterminate &&
               indeterminate.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               indeterminate.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               indeterminate.error.has_value() && !failedRestore.files.contains(target) &&
               failedRestore.files.at(staged) == "new" && failedRestore.files.at(backup) == "old";
    }
#else
    class FakePosixReplaceOperations final : public asharia::core::detail::PosixReplaceOperations {
    public:
        [[nodiscard]] int createLink(const std::filesystem::path& existing,
                                     const std::filesystem::path& linkPath) override {
            if (linkError != 0) {
                return linkError;
            }
            if (files.contains(linkPath)) {
                return EEXIST;
            }
            if (!files.contains(existing)) {
                return ENOENT;
            }
            files[linkPath] = files.at(existing);
            return 0;
        }

        [[nodiscard]] int renameFile(const std::filesystem::path& source,
                                     const std::filesystem::path& target) override {
            if (renameError != 0) {
                return renameError;
            }
            if (!files.contains(source)) {
                return ENOENT;
            }
            files[target] = files.at(source);
            files.erase(source);
            return 0;
        }

        [[nodiscard]] std::optional<bool>
        fileExists(const std::filesystem::path& path) noexcept override {
            return files.contains(path);
        }

        int linkError{};
        int renameError{};
        std::map<std::filesystem::path, std::string> files;
    };

    [[nodiscard]] bool stagedPosixCommitPreservesCallerOwnedBackup() {
        FakePosixReplaceOperations operations;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"save/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {staged, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingPosixStagedFile(
            target, staged, backup, operations);

        return outcome.commitState == asharia::core::StagedFileCommitState::Committed &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Absent &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               !outcome.error.has_value() && operations.files.at(target) == "new" &&
               !operations.files.contains(staged) && operations.files.at(backup) == "old";
    }

    [[nodiscard]] bool stagedPosixBackupLinkFailurePreservesNames() {
        FakePosixReplaceOperations operations;
        operations.linkError = EACCES;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"save/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {staged, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingPosixStagedFile(
            target, staged, backup, operations);

        return outcome.commitState == asharia::core::StagedFileCommitState::NotCommitted &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Absent &&
               outcome.error.has_value() && outcome.error->code == EACCES &&
               operations.files.at(target) == "old" && operations.files.at(staged) == "new" &&
               !operations.files.contains(backup);
    }

    [[nodiscard]] bool stagedPosixCrossVolumeRenamePreservesRecoveryArtifacts() {
        FakePosixReplaceOperations operations;
        operations.renameError = EXDEV;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"other-volume/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {staged, "new"}};

        const auto outcome = asharia::core::detail::replaceExistingPosixStagedFile(
            target, staged, backup, operations);

        return outcome.commitState == asharia::core::StagedFileCommitState::NotCommitted &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.error.has_value() && outcome.error->code == EXDEV &&
               operations.files.at(target) == "old" && operations.files.at(staged) == "new" &&
               operations.files.at(backup) == "old";
    }

    [[nodiscard]] bool stagedPosixBackupRaceIsReobserved() {
        FakePosixReplaceOperations operations;
        const std::filesystem::path target{"save/data.bin"};
        const std::filesystem::path staged{"save/data.bin.staged"};
        const std::filesystem::path backup{"save/data.bin.backup"};
        operations.files = {{target, "old"}, {staged, "new"}, {backup, "external"}};

        const auto outcome = asharia::core::detail::replaceExistingPosixStagedFile(
            target, staged, backup, operations);

        return outcome.commitState == asharia::core::StagedFileCommitState::NotCommitted &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.error.has_value() && outcome.error->code == EEXIST &&
               operations.files.at(target) == "old" && operations.files.at(staged) == "new" &&
               operations.files.at(backup) == "external";
    }
#endif

    struct FakeAtomicFileState {
        std::vector<std::byte> targetBytes{bytesOf("old")};
        std::vector<std::byte> temporaryBytes;
        std::filesystem::path targetPath;
        std::filesystem::path temporaryPath;
        std::size_t stagedCreateCalls{};
        std::size_t artifactInspectionCalls{};
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

        [[nodiscard]] asharia::Result<std::unique_ptr<asharia::core::detail::AtomicTemporaryFile>>
        createStaged(const std::filesystem::path& target,
                     const std::filesystem::path& staged) override {
            ++state.stagedCreateCalls;
            state.targetPath = target;
            state.temporaryPath = staged;
            if (stagedAlreadyExists) {
                state.temporaryExists = true;
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 15, "staged file exists"}};
            }
            if (failStagedCreate) {
                return std::unexpected{
                    asharia::Error{asharia::ErrorDomain::Core, 16, "staged create failed"}};
            }

            state.temporaryExists = true;
            return std::make_unique<FakeAtomicTemporaryFile>(
                &state, FakeAtomicTemporaryConfig{.maximumWriteBytes = maximumWriteBytes,
                                                  .failWriteCall = failWriteCall,
                                                  .overReportWrite = overReportWrite,
                                                  .failFlush = failFlush,
                                                  .failClose = failClose});
        }

        [[nodiscard]] asharia::core::StagedFileArtifactState
        inspectArtifactState(const std::filesystem::path& path) noexcept override {
            ++state.artifactInspectionCalls;
            if (indeterminateArtifactInspection) {
                return asharia::core::StagedFileArtifactState::Indeterminate;
            }
            return path == state.temporaryPath && state.temporaryExists
                       ? asharia::core::StagedFileArtifactState::Present
                       : asharia::core::StagedFileArtifactState::Absent;
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

        // This override must preserve the public target/staged/backup path order.
        // NOLINTBEGIN(bugprone-easily-swappable-parameters)
        [[nodiscard]] asharia::core::StagedFileReplacementOutcome
        replaceStaged(const std::filesystem::path& target, const std::filesystem::path& staged,
                      const std::filesystem::path& backup) override {
            static_cast<void>(target);
            static_cast<void>(staged);
            static_cast<void>(backup);
            ++stagedReplaceCalls;
            return stagedReplaceOutcome;
        }
        // NOLINTEND(bugprone-easily-swappable-parameters)

        bool failCreate{};
        bool failStagedCreate{};
        bool stagedAlreadyExists{};
        bool indeterminateArtifactInspection{};
        std::size_t maximumWriteBytes{std::numeric_limits<std::size_t>::max()};
        std::size_t failWriteCall{std::numeric_limits<std::size_t>::max()};
        bool overReportWrite{};
        bool failFlush{};
        bool failClose{};
        bool failReplace{};
        bool indeterminateReplaceFailure{};
        std::size_t stagedReplaceCalls{};
        asharia::core::StagedFileReplacementOutcome stagedReplaceOutcome{
            .commitState = asharia::core::StagedFileCommitState::Committed,
            .stagedFileState = asharia::core::StagedFileArtifactState::Absent,
            .backupFileState = asharia::core::StagedFileArtifactState::Present,
            .error = std::nullopt,
        };
        FakeAtomicFileState state;
    };

    [[nodiscard]] bool rejectsInvalidStagedPreparationPathsBeforeBackendUse() {
        FakeAtomicFileBackend backend;
        const auto aliased = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "save/./data.bin", bytesOf("new"), backend);
        const auto differentDirectory = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "other/data.bin.staged", bytesOf("new"), backend);
        const auto empty = asharia::core::detail::prepareStagedFileBytesWithBackend(
            {}, "save/data.bin.staged", bytesOf("new"), backend);

        return aliased.error.has_value() && contains(aliased.error->message, "distinct files") &&
               differentDirectory.error.has_value() &&
               contains(differentDirectory.error->message, "share one directory") &&
               empty.error.has_value() && contains(empty.error->message, "non-empty") &&
               backend.state.stagedCreateCalls == 0U;
    }

    [[nodiscard]] bool stagedPreparationWritesFlushesAndPreservesArtifact() {
        FakeAtomicFileBackend backend;
        backend.maximumWriteBytes = 2U;
        const auto bytes = bytesOf("new-data");

        const auto outcome = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "save/data.bin.staged", bytes, backend);

        return !outcome.error.has_value() &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               backend.state.stagedCreateCalls == 1U && backend.state.writeCalls == 4U &&
               backend.state.flushCalls == 1U && backend.state.closeCalls == 1U &&
               backend.state.replaceCalls == 0U && backend.state.released &&
               backend.state.temporaryExists && backend.state.temporaryBytes == bytes &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool stagedPreparationFailurePreservesPartialArtifact() {
        FakeAtomicFileBackend backend;
        backend.maximumWriteBytes = 2U;
        backend.failWriteCall = 2U;

        const auto outcome = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "save/data.bin.staged", bytesOf("new-data"), backend);

        return outcome.error.has_value() && outcome.error->code == 12 &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               backend.state.writeCalls == 2U && backend.state.flushCalls == 0U &&
               backend.state.closeCalls == 0U && backend.state.released &&
               backend.state.temporaryExists && backend.state.temporaryBytes == bytesOf("ne") &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool stagedPreparationFlushFailurePreservesArtifact() {
        FakeAtomicFileBackend backend;
        backend.failFlush = true;

        const auto outcome = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "save/data.bin.staged", bytesOf("new"), backend);

        return outcome.error.has_value() && outcome.error->code == 13 &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               backend.state.writeCalls == 1U && backend.state.flushCalls == 1U &&
               backend.state.closeCalls == 0U && backend.state.released &&
               backend.state.temporaryExists && backend.state.temporaryBytes == bytesOf("new") &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool stagedPreparationCloseFailurePreservesArtifact() {
        FakeAtomicFileBackend backend;
        backend.failClose = true;

        const auto outcome = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "save/data.bin.staged", bytesOf("new"), backend);

        return outcome.error.has_value() && outcome.error->code == 14 &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               backend.state.writeCalls == 1U && backend.state.flushCalls == 1U &&
               backend.state.closeCalls == 1U && backend.state.released &&
               backend.state.temporaryExists && backend.state.temporaryBytes == bytesOf("new") &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool stagedPreparationRejectsExistingArtifactWithoutOverwrite() {
        FakeAtomicFileBackend backend;
        backend.stagedAlreadyExists = true;
        backend.state.temporaryBytes = bytesOf("external");

        const auto outcome = asharia::core::detail::prepareStagedFileBytesWithBackend(
            "save/data.bin", "save/data.bin.staged", bytesOf("new"), backend);

        return outcome.error.has_value() && outcome.error->code == 15 &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               backend.state.writeCalls == 0U && backend.state.flushCalls == 0U &&
               backend.state.closeCalls == 0U &&
               backend.state.temporaryBytes == bytesOf("external") &&
               backend.state.targetBytes == bytesOf("old");
    }

    [[nodiscard]] bool rejectsAliasedStagedReplacementPathsBeforeBackendUse() {
        FakeAtomicFileBackend backend;
        const auto outcome = asharia::core::detail::replaceFileFromStagedWithBackend(
            "save/data.bin", "save/./data.bin", "save/data.bin.backup", backend);

        return outcome.commitState == asharia::core::StagedFileCommitState::NotCommitted &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Indeterminate &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Indeterminate &&
               outcome.error.has_value() && contains(outcome.error->message, "distinct files") &&
               backend.stagedReplaceCalls == 0U;
    }

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

    [[nodiscard]] bool stagedPreparationHandsOffToReplacement() {
        const TempFile owner{"target.bin"};
        const auto& target = owner.path();
        const auto staged = target.parent_path() / "target.bin.prepared";
        const auto backup = target.parent_path() / "target.bin.previous";
        if (!owner.write("old")) {
            return false;
        }

        const auto preparation = asharia::core::prepareStagedFileText(target, staged, "new");
        const bool prepared =
            !preparation.error.has_value() &&
            preparation.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
            readTextFile(target) == "old" && readTextFile(staged) == "new";

        const auto replacement = prepared
                                     ? asharia::core::replaceFileFromStaged(target, staged, backup)
                                     : asharia::core::StagedFileReplacementOutcome{};
        const bool replaced =
            replacement.commitState == asharia::core::StagedFileCommitState::Committed &&
            replacement.stagedFileState == asharia::core::StagedFileArtifactState::Absent &&
            replacement.backupFileState == asharia::core::StagedFileArtifactState::Present &&
            !replacement.error.has_value() && readTextFile(target) == "new" &&
            readTextFile(backup) == "old";

        std::error_code cleanupError;
        std::filesystem::remove(staged, cleanupError);
        cleanupError.clear();
        std::filesystem::remove(backup, cleanupError);
        return prepared && replaced;
    }

    [[nodiscard]] bool stagedPreparationRejectsMissingTarget() {
        const TempFile owner{"unused.bin"};
        const auto& target = owner.path();
        const auto staged = target.parent_path() / "missing-target.bin.prepared";

        const auto outcome = asharia::core::prepareStagedFileText(target, staged, "new");

        std::error_code cleanupError;
        std::filesystem::remove(staged, cleanupError);
        return outcome.error.has_value() &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Absent &&
               !std::filesystem::exists(target);
    }

    [[nodiscard]] bool stagedPreparationDoesNotOverwriteExistingArtifact() {
        const TempFile owner{"target.bin"};
        const auto& target = owner.path();
        const auto staged = target.parent_path() / "target.bin.prepared";
        if (!owner.write("old") || !writeTextFile(staged, "external")) {
            return false;
        }

        const auto outcome = asharia::core::prepareStagedFileText(target, staged, "new");
        const bool preserved =
            outcome.error.has_value() &&
            outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
            readTextFile(target) == "old" && readTextFile(staged) == "external";

        std::error_code cleanupError;
        std::filesystem::remove(staged, cleanupError);
        return preserved;
    }

    [[nodiscard]] bool stagedReplacementPreservesCallerOwnedBackup() {
        const TempFile owner{"target.bin"};
        const auto& target = owner.path();
        const auto staged = target.parent_path() / "target.bin.staged";
        const auto backup = target.parent_path() / "target.bin.backup";
        if (!owner.write("old") || !writeTextFile(staged, "new")) {
            return false;
        }

        const auto outcome = asharia::core::replaceFileFromStaged(target, staged, backup);

        return outcome.commitState == asharia::core::StagedFileCommitState::Committed &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Absent &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               !outcome.error.has_value() && readTextFile(target) == "new" &&
               !std::filesystem::exists(staged) && readTextFile(backup) == "old";
    }

    [[nodiscard]] bool stagedReplacementRejectsMissingTarget() {
        const TempFile owner{"unused.bin"};
        const auto& target = owner.path();
        const auto staged = target.parent_path() / "target.bin.staged";
        const auto backup = target.parent_path() / "target.bin.backup";
        if (!writeTextFile(staged, "new")) {
            return false;
        }

        const auto outcome = asharia::core::replaceFileFromStaged(target, staged, backup);

        return outcome.commitState == asharia::core::StagedFileCommitState::NotCommitted &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Absent &&
               outcome.error.has_value() && !std::filesystem::exists(target) &&
               readTextFile(staged) == "new" && !std::filesystem::exists(backup);
    }

    [[nodiscard]] bool stagedReplacementRejectsExistingBackup() {
        const TempFile owner{"target.bin"};
        const auto& target = owner.path();
        const auto staged = target.parent_path() / "target.bin.staged";
        const auto backup = target.parent_path() / "target.bin.backup";
        if (!owner.write("old") || !writeTextFile(staged, "new") ||
            !writeTextFile(backup, "external")) {
            return false;
        }

        const auto outcome = asharia::core::replaceFileFromStaged(target, staged, backup);

        return outcome.commitState == asharia::core::StagedFileCommitState::NotCommitted &&
               outcome.stagedFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.backupFileState == asharia::core::StagedFileArtifactState::Present &&
               outcome.error.has_value() && readTextFile(target) == "old" &&
               readTextFile(staged) == "new" && readTextFile(backup) == "external";
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

    [[nodiscard]] bool failureDiagnosticsPreserveUtf8Paths() {
        const TempFile owner{"owner.bin"};
        const std::filesystem::path utf8Filename{u8"文件.bin"};
        const std::filesystem::path missingRead = owner.path().parent_path() / utf8Filename;
        const std::filesystem::path missingWrite =
            owner.path().parent_path() / "missing-parent" / utf8Filename;

        const auto read = asharia::core::readFileBytes(missingRead, {.maxBytes = 16U});
        const auto write = asharia::core::writeFileTextAtomically(missingWrite, "data");

        return !read && !write && contains(read.error().message, kUtf8FilenameMarker) &&
               contains(write.error().message, kUtf8FilenameMarker);
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
            std::pair<std::string_view, Test>{"rejectsEmptyExclusiveFileLockPathBeforeBackendUse",
                                              rejectsEmptyExclusiveFileLockPathBeforeBackendUse},
            std::pair<std::string_view, Test>{"distinguishesExclusiveFileLockContentionFromFailure",
                                              distinguishesExclusiveFileLockContentionFromFailure},
            std::pair<std::string_view, Test>{
                "movesAndExplicitlyReleasesExclusiveFileLockOwnership",
                movesAndExplicitlyReleasesExclusiveFileLockOwnership},
            std::pair<std::string_view, Test>{
                "preservesFailedExclusiveFileLockForDestructorRelease",
                preservesFailedExclusiveFileLockForDestructorRelease},
            std::pair<std::string_view, Test>{"serializesPlatformExclusiveFileLockAndKeepsSentinel",
                                              serializesPlatformExclusiveFileLockAndKeepsSentinel},
            std::pair<std::string_view, Test>{"releasesPlatformExclusiveFileLockOnDestruction",
                                              releasesPlatformExclusiveFileLockOnDestruction},
            std::pair<std::string_view, Test>{"rejectsInvalidPlatformExclusiveFileLockTargets",
                                              rejectsInvalidPlatformExclusiveFileLockTargets},
            std::pair<std::string_view, Test>{"rejectsZeroReadLimit", rejectsZeroReadLimit},
            std::pair<std::string_view, Test>{"failureDiagnosticsPreserveUtf8Paths",
                                              failureDiagnosticsPreserveUtf8Paths},
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
            std::pair<std::string_view, Test>{"stagedWindowsCommitPreservesCallerOwnedBackup",
                                              stagedWindowsCommitPreservesCallerOwnedBackup},
            std::pair<std::string_view, Test>{
                "stagedWindowsRegularFileValidationRejectsReparsePoints",
                stagedWindowsRegularFileValidationRejectsReparsePoints},
            std::pair<std::string_view, Test>{"stagedWindowsPreCommitFailuresPreserveNames",
                                              stagedWindowsPreCommitFailuresPreserveNames},
            std::pair<std::string_view, Test>{"stagedWindows1177ReportsRestoredArtifacts",
                                              stagedWindows1177ReportsRestoredArtifacts},
#else
            std::pair<std::string_view, Test>{"stagedPosixCommitPreservesCallerOwnedBackup",
                                              stagedPosixCommitPreservesCallerOwnedBackup},
            std::pair<std::string_view, Test>{"stagedPosixBackupLinkFailurePreservesNames",
                                              stagedPosixBackupLinkFailurePreservesNames},
            std::pair<std::string_view, Test>{
                "stagedPosixCrossVolumeRenamePreservesRecoveryArtifacts",
                stagedPosixCrossVolumeRenamePreservesRecoveryArtifacts},
            std::pair<std::string_view, Test>{"stagedPosixBackupRaceIsReobserved",
                                              stagedPosixBackupRaceIsReobserved},
#endif
            std::pair<std::string_view, Test>{
                "rejectsAliasedStagedReplacementPathsBeforeBackendUse",
                rejectsAliasedStagedReplacementPathsBeforeBackendUse},
            std::pair<std::string_view, Test>{
                "rejectsInvalidStagedPreparationPathsBeforeBackendUse",
                rejectsInvalidStagedPreparationPathsBeforeBackendUse},
            std::pair<std::string_view, Test>{"stagedPreparationWritesFlushesAndPreservesArtifact",
                                              stagedPreparationWritesFlushesAndPreservesArtifact},
            std::pair<std::string_view, Test>{"stagedPreparationFailurePreservesPartialArtifact",
                                              stagedPreparationFailurePreservesPartialArtifact},
            std::pair<std::string_view, Test>{"stagedPreparationFlushFailurePreservesArtifact",
                                              stagedPreparationFlushFailurePreservesArtifact},
            std::pair<std::string_view, Test>{"stagedPreparationCloseFailurePreservesArtifact",
                                              stagedPreparationCloseFailurePreservesArtifact},
            std::pair<std::string_view, Test>{
                "stagedPreparationRejectsExistingArtifactWithoutOverwrite",
                stagedPreparationRejectsExistingArtifactWithoutOverwrite},
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
            std::pair<std::string_view, Test>{"stagedPreparationHandsOffToReplacement",
                                              stagedPreparationHandsOffToReplacement},
            std::pair<std::string_view, Test>{"stagedPreparationRejectsMissingTarget",
                                              stagedPreparationRejectsMissingTarget},
            std::pair<std::string_view, Test>{"stagedPreparationDoesNotOverwriteExistingArtifact",
                                              stagedPreparationDoesNotOverwriteExistingArtifact},
            std::pair<std::string_view, Test>{"stagedReplacementPreservesCallerOwnedBackup",
                                              stagedReplacementPreservesCallerOwnedBackup},
            std::pair<std::string_view, Test>{"stagedReplacementRejectsMissingTarget",
                                              stagedReplacementRejectsMissingTarget},
            std::pair<std::string_view, Test>{"stagedReplacementRejectsExistingBackup",
                                              stagedReplacementRejectsExistingBackup},
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
