#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace asharia::asset {

    enum class AssetSourceSnapshotDiagnosticCode {
        InvalidEntry,
        MissingSourceFile,
        SourceFileNotRegular,
        SourceFileReadFailed,
        DuplicateSourcePath,
        ByteLimitExceeded,
    };

    struct AssetSourceSnapshotEntry {
        std::string sourcePath;
        std::filesystem::path sourceFilePath;

        [[nodiscard]] friend bool operator==(const AssetSourceSnapshotEntry&,
                                             const AssetSourceSnapshotEntry&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !sourcePath.empty() && !sourceFilePath.empty();
        }
    };

    struct AssetSourceSnapshot {
        std::string sourcePath;
        std::filesystem::path sourceFilePath;
        std::uint64_t sourceHash{};

        [[nodiscard]] friend bool operator==(const AssetSourceSnapshot&,
                                             const AssetSourceSnapshot&) = default;
    };

    struct AssetSourceSnapshotDiagnostic {
        AssetSourceSnapshotDiagnosticCode code{AssetSourceSnapshotDiagnosticCode::InvalidEntry};
        std::string sourcePath;
        std::filesystem::path sourceFilePath;
        std::string message;

        [[nodiscard]] friend bool operator==(const AssetSourceSnapshotDiagnostic&,
                                             const AssetSourceSnapshotDiagnostic&) = default;
    };

    struct AssetSourceSnapshotResult {
        std::vector<AssetSourceSnapshot> snapshots;
        std::vector<AssetSourceSnapshotDiagnostic> diagnostics;
        std::uint64_t bytesHashed{};

        [[nodiscard]] bool succeeded() const noexcept {
            return diagnostics.empty();
        }
    };

    [[nodiscard]] AssetSourceSnapshotResult
    snapshotAssetSourceFiles(std::span<const AssetSourceSnapshotEntry> entries,
                             std::uint64_t maxBytes = (std::numeric_limits<std::uint64_t>::max)());

} // namespace asharia::asset
