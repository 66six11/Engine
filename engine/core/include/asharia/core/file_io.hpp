#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::core {

    namespace detail {
        class ExclusiveFileLockHandle;
        struct ExclusiveFileLockFactory;
    } // namespace detail

    // Move-only ownership of one cooperative, process-wide exclusive lock. Destruction releases
    // the operating-system lock on a best-effort basis; use release() when release failures must
    // be observed.
    class ExclusiveFileLock final {
    public:
        ~ExclusiveFileLock();

        ExclusiveFileLock(const ExclusiveFileLock&) = delete;
        ExclusiveFileLock& operator=(const ExclusiveFileLock&) = delete;
        ExclusiveFileLock(ExclusiveFileLock&& other) noexcept;
        ExclusiveFileLock& operator=(ExclusiveFileLock&& other) noexcept;

        [[nodiscard]] bool ownsLock() const noexcept;
        [[nodiscard]] VoidResult release();

    private:
        friend struct detail::ExclusiveFileLockFactory;

        explicit ExclusiveFileLock(
            std::unique_ptr<detail::ExclusiveFileLockHandle> handle) noexcept;

        std::unique_ptr<detail::ExclusiveFileLockHandle> handle_;
    };

    struct FileReadLimits {
        std::uint64_t maxBytes{};
    };

    struct AtomicFileWriteOptions {
        bool flushFileBuffers{true};
    };

    // Describes whether staged bytes became the target. Indeterminate requires the caller to
    // inspect its recovery evidence before retrying or deleting any path.
    enum class StagedFileCommitState : std::uint8_t {
        NotCommitted,
        Committed,
        Indeterminate,
    };

    // Observed path presence immediately before the function returns. A concurrent filesystem
    // writer may invalidate the observation after return.
    enum class StagedFileArtifactState : std::uint8_t {
        Absent,
        Present,
        Indeterminate,
    };

    // A committed outcome has no error. NotCommitted and Indeterminate outcomes carry the
    // platform failure that prevented a confirmed commit.
    struct StagedFileReplacementOutcome {
        StagedFileCommitState commitState{StagedFileCommitState::NotCommitted};
        StagedFileArtifactState stagedFileState{StagedFileArtifactState::Indeterminate};
        StagedFileArtifactState backupFileState{StagedFileArtifactState::Indeterminate};
        std::optional<Error> error;
    };

    // Success is represented by no error and a Present staged file. A failed preparation can
    // intentionally leave a partial caller-owned staged artifact for explicit recovery.
    struct StagedFilePreparationOutcome {
        StagedFileArtifactState stagedFileState{StagedFileArtifactState::Indeterminate};
        std::optional<Error> error;
    };

    [[nodiscard]] Result<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path,
                                                               FileReadLimits limits);

    [[nodiscard]] Result<std::string> readFileText(const std::filesystem::path& path,
                                                   FileReadLimits limits);

    [[nodiscard]] VoidResult writeFileBytesAtomically(const std::filesystem::path& path,
                                                      std::span<const std::byte> bytes,
                                                      AtomicFileWriteOptions options = {});

    [[nodiscard]] VoidResult writeFileTextAtomically(const std::filesystem::path& path,
                                                     std::string_view text,
                                                     AtomicFileWriteOptions options = {});

    // Tries once to acquire a cooperative cross-process writer lock anchored at lockPath.
    //
    // An engaged Result containing a lock means acquired; an engaged Result containing
    // std::nullopt means another cooperating writer owns it; an unexpected Error means the
    // attempt failed. The sentinel is created if absent and intentionally remains after release:
    // its presence is not ownership evidence and callers must never delete or replace it.
    //
    // The lock is process-local-filesystem coordination, not a hostile-writer or distributed
    // lease. All writers must use the same stable path and keep the returned object alive through
    // their final revalidation and commit.
    [[nodiscard]] Result<std::optional<ExclusiveFileLock>>
    tryAcquireExclusiveFileLock(const std::filesystem::path& lockPath);

    // Exclusively creates a caller-named staged file beside an existing regular target, writes
    // every byte, flushes the file contents, and closes it. The target is not modified.
    //
    // Target and staged must be distinct lexical paths in the same directory. The staged path
    // must not exist and is caller-owned as soon as it is created. A failure after creation
    // preserves the partial staged file and reports its observed state; callers must not pass a
    // failed preparation to replaceFileFromStaged().
    //
    // This operation does not lock other writers, flush parent-directory metadata, create a
    // journal, or make a multi-file transaction durable.
    [[nodiscard]] StagedFilePreparationOutcome
    prepareStagedFileBytes(const std::filesystem::path& target, const std::filesystem::path& staged,
                           std::span<const std::byte> bytes);

    [[nodiscard]] StagedFilePreparationOutcome
    prepareStagedFileText(const std::filesystem::path& target, const std::filesystem::path& staged,
                          std::string_view text);

    // Replaces an existing regular target while retaining its previous bytes at backup.
    //
    // The caller owns all three paths, must serialize other writers, and must provide distinct,
    // non-aliasing paths on one volume. Staged and target must exist as regular files; backup
    // must not exist. On success, staged is consumed and backup remains caller-owned. The
    // function rejects a missing target and never treats this operation as first publication.
    //
    // This operation does not write or flush staged bytes and does not flush parent-directory
    // metadata. It is a recoverable replacement primitive, not a complete crash-safe transaction.
    [[nodiscard]] StagedFileReplacementOutcome
    replaceFileFromStaged(const std::filesystem::path& target, const std::filesystem::path& staged,
                          const std::filesystem::path& backup);

} // namespace asharia::core
