#include "asharia/asset_pipeline/asset_source_scan.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_metadata.hpp"

namespace asharia::asset {
    namespace {

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            return path.generic_string();
        }

        void addDiagnostic(AssetSourceScanResult& result, AssetSourceScanDiagnosticCode code,
                           std::string sourcePath, std::filesystem::path sourceFilePath,
                           std::filesystem::path metadataPath, std::string message) {
            result.diagnostics.push_back(AssetSourceScanDiagnostic{
                .code = code,
                .sourcePath = std::move(sourcePath),
                .sourceFilePath = std::move(sourceFilePath),
                .metadataPath = std::move(metadataPath),
                .message = std::move(message),
            });
        }

        struct SourceScanFileView {
            std::span<const std::filesystem::path> sourceFiles;
            std::span<const std::filesystem::path> metadataFiles;
        };

        struct MetadataMatchView {
            std::span<const std::filesystem::path> metadataFiles;
            std::span<const std::filesystem::path> matchedMetadataFiles;
        };

        [[nodiscard]] bool containsPathText(std::span<const std::filesystem::path> paths,
                                            const std::filesystem::path& candidate) {
            const std::string candidateText = pathText(candidate);
            return std::ranges::any_of(paths, [&candidateText](const std::filesystem::path& path) {
                return pathText(path) == candidateText;
            });
        }

        [[nodiscard]] bool containsSourcePath(std::span<const AssetSourceScanEntry> entries,
                                              std::string_view sourcePath) {
            return std::ranges::any_of(entries, [sourcePath](const AssetSourceScanEntry& entry) {
                return entry.sourcePath == sourcePath;
            });
        }

        [[nodiscard]] bool isValidSinglePathSegment(std::string_view text) {
            return !text.empty() && text != "." && text != ".." &&
                   text.find('/') == std::string_view::npos &&
                   text.find('\\') == std::string_view::npos;
        }

        [[nodiscard]] bool validateMetadataSuffix(AssetSourceScanResult& result,
                                                  std::string_view suffix) {
            if (suffix.empty()) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRequest, {}, {}, {},
                              "Asset source scan metadata suffix is missing.");
                return false;
            }

            if (suffix.find('/') != std::string_view::npos ||
                suffix.find('\\') != std::string_view::npos) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRequest, {}, {}, {},
                              "Asset source scan metadata suffix must be a filename suffix.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool
        validateIgnoredDirectoryNames(AssetSourceScanResult& result,
                                      std::span<const std::string> ignoredDirectoryNames) {
            bool valid = true;
            for (std::size_t index = 0; index < ignoredDirectoryNames.size(); ++index) {
                const std::string& name = ignoredDirectoryNames[index];
                if (isValidSinglePathSegment(name)) {
                    continue;
                }

                addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRequest, {}, {}, {},
                              "Asset source scan ignored directory name[" + std::to_string(index) +
                                  "] must be a single non-empty path segment.");
                valid = false;
            }

            return valid;
        }

        [[nodiscard]] bool validateSourcePathPrefix(AssetSourceScanResult& result,
                                                    std::string_view sourcePathPrefix) {
            if (sourcePathPrefix.empty()) {
                return true;
            }

            if (auto validPrefix = validateAssetSourcePath(sourcePathPrefix); !validPrefix) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRequest,
                              std::string{sourcePathPrefix}, {}, {},
                              "Asset source scan source path prefix is invalid: " +
                                  validPrefix.error().message);
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validateRoot(AssetSourceScanResult& result,
                                        const std::filesystem::path& sourceRoot) {
            if (sourceRoot.empty()) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRoot, {}, sourceRoot,
                              {}, "Asset source scan root is missing.");
                return false;
            }

            std::error_code rootError;
            const bool exists = std::filesystem::exists(sourceRoot, rootError);
            if (rootError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              sourceRoot, {},
                              "Asset source scan could not query root \"" + pathText(sourceRoot) +
                                  "\": " + rootError.message() + ".");
                return false;
            }

            if (!exists) {
                addDiagnostic(
                    result, AssetSourceScanDiagnosticCode::InvalidRoot, {}, sourceRoot, {},
                    "Asset source scan root \"" + pathText(sourceRoot) + "\" does not exist.");
                return false;
            }

            rootError.clear();
            const bool directory = std::filesystem::is_directory(sourceRoot, rootError);
            if (rootError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              sourceRoot, {},
                              "Asset source scan could not inspect root \"" + pathText(sourceRoot) +
                                  "\": " + rootError.message() + ".");
                return false;
            }

            if (!directory) {
                addDiagnostic(
                    result, AssetSourceScanDiagnosticCode::InvalidRoot, {}, sourceRoot, {},
                    "Asset source scan root \"" + pathText(sourceRoot) + "\" is not a directory.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool
        shouldIgnoreDirectory(const std::filesystem::path& path,
                              std::span<const std::string> ignoredDirectoryNames) {
            const std::string directoryName = path.filename().generic_string();
            return std::ranges::any_of(
                ignoredDirectoryNames,
                [&directoryName](const std::string& ignored) { return ignored == directoryName; });
        }

        [[nodiscard]] bool isMetadataSidecarPath(const std::filesystem::path& path,
                                                 std::string_view metadataSuffix) {
            return path.filename().generic_string().ends_with(metadataSuffix);
        }

        [[nodiscard]] std::filesystem::path
        makeExpectedMetadataPath(const std::filesystem::path& sourceFilePath,
                                 std::string_view metadataSuffix) {
            std::filesystem::path metadataPath = sourceFilePath;
            metadataPath += metadataSuffix;
            return metadataPath;
        }

        void collectRegularFiles(AssetSourceScanResult& result,
                                 const AssetSourceScanRequest& request,
                                 std::vector<std::filesystem::path>& sourceFiles,
                                 std::vector<std::filesystem::path>& metadataFiles) {
            std::error_code iteratorError;
            std::filesystem::recursive_directory_iterator iterator{
                request.sourceRoot, std::filesystem::directory_options::none, iteratorError};
            const std::filesystem::recursive_directory_iterator end;
            if (iteratorError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              request.sourceRoot, {},
                              "Asset source scan could not open root \"" +
                                  pathText(request.sourceRoot) + "\": " + iteratorError.message() +
                                  ".");
                return;
            }

            while (iterator != end) {
                const std::filesystem::path currentPath = iterator->path();

                std::error_code entryError;
                const bool directory = iterator->is_directory(entryError);
                if (entryError) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                                  currentPath, {},
                                  "Asset source scan could not inspect \"" + pathText(currentPath) +
                                      "\": " + entryError.message() + ".");
                } else if (directory) {
                    if (shouldIgnoreDirectory(currentPath, request.ignoredDirectoryNames)) {
                        iterator.disable_recursion_pending();
                    }
                } else {
                    entryError.clear();
                    const bool regularFile = iterator->is_regular_file(entryError);
                    if (entryError) {
                        addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                                      currentPath, {},
                                      "Asset source scan could not inspect file \"" +
                                          pathText(currentPath) + "\": " + entryError.message() +
                                          ".");
                    } else if (regularFile) {
                        if (isMetadataSidecarPath(currentPath, request.metadataSuffix)) {
                            metadataFiles.push_back(currentPath);
                        } else {
                            sourceFiles.push_back(currentPath);
                        }
                    }
                }

                iterator.increment(iteratorError);
                if (iteratorError) {
                    addDiagnostic(
                        result, AssetSourceScanDiagnosticCode::FilesystemError, {}, currentPath, {},
                        "Asset source scan could not advance past \"" + pathText(currentPath) +
                            "\": " + iteratorError.message() + ".");
                    iteratorError.clear();
                }
            }
        }

        void sortPathList(std::vector<std::filesystem::path>& paths) {
            std::ranges::sort(
                paths, [](const std::filesystem::path& left, const std::filesystem::path& right) {
                    return pathText(left) < pathText(right);
                });
        }

        [[nodiscard]] std::string makeSourcePath(const AssetSourceScanRequest& request,
                                                 const std::filesystem::path& sourceFilePath,
                                                 bool& valid, std::string& errorMessage) {
            std::error_code relativeError;
            const std::filesystem::path relativePath =
                std::filesystem::relative(sourceFilePath, request.sourceRoot, relativeError);
            if (relativeError) {
                valid = false;
                errorMessage = "could not make relative path: " + relativeError.message();
                return {};
            }

            const std::string relativeText = relativePath.generic_string();
            std::string sourcePath = request.sourcePathPrefix.empty()
                                         ? relativeText
                                         : request.sourcePathPrefix + "/" + relativeText;
            if (auto validSourcePath = validateAssetSourcePath(sourcePath); !validSourcePath) {
                valid = false;
                errorMessage = validSourcePath.error().message;
                return sourcePath;
            }

            valid = true;
            return sourcePath;
        }

        void appendScannedSources(AssetSourceScanResult& result,
                                  const AssetSourceScanRequest& request, SourceScanFileView files,
                                  std::vector<std::filesystem::path>& matchedMetadataFiles) {
            for (const std::filesystem::path& sourceFilePath : files.sourceFiles) {
                bool validSourcePath = false;
                std::string sourcePathError;
                std::string sourcePath =
                    makeSourcePath(request, sourceFilePath, validSourcePath, sourcePathError);
                if (!validSourcePath) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidSourcePath,
                                  std::move(sourcePath), sourceFilePath, {},
                                  "Asset source scan rejected \"" + pathText(sourceFilePath) +
                                      "\": " + sourcePathError);
                    continue;
                }

                if (containsSourcePath(result.entries, sourcePath)) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::DuplicateSourcePath,
                                  sourcePath, sourceFilePath, {},
                                  "Asset source scan duplicate source path source=\"" + sourcePath +
                                      "\" file=\"" + pathText(sourceFilePath) + "\".");
                    continue;
                }

                const std::filesystem::path metadataPath =
                    makeExpectedMetadataPath(sourceFilePath, request.metadataSuffix);
                if (!containsPathText(files.metadataFiles, metadataPath)) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::MissingMetadata,
                                  sourcePath, sourceFilePath, metadataPath,
                                  "Asset source scan missing metadata for source=\"" + sourcePath +
                                      "\" file=\"" + pathText(sourceFilePath) +
                                      "\" expectedMetadata=\"" + pathText(metadataPath) + "\".");
                    continue;
                }

                if (containsPathText(matchedMetadataFiles, metadataPath)) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::DuplicateMetadataPath,
                                  sourcePath, sourceFilePath, metadataPath,
                                  "Asset source scan metadata path collision source=\"" +
                                      sourcePath + "\" metadata=\"" + pathText(metadataPath) +
                                      "\".");
                    continue;
                }

                matchedMetadataFiles.push_back(metadataPath);
                result.entries.push_back(AssetSourceScanEntry{
                    .sourcePath = std::move(sourcePath),
                    .sourceFilePath = sourceFilePath,
                    .metadataPath = metadataPath,
                });
            }
        }

        void appendOrphanMetadataDiagnostics(AssetSourceScanResult& result,
                                             const AssetSourceScanRequest& request,
                                             MetadataMatchView files) {
            for (const std::filesystem::path& metadataPath : files.metadataFiles) {
                if (containsPathText(files.matchedMetadataFiles, metadataPath)) {
                    continue;
                }

                std::filesystem::path sourceFilePath = metadataPath;
                const std::string metadataText = pathText(metadataPath);
                std::string sourcePath;
                if (metadataText.ends_with(request.metadataSuffix)) {
                    sourceFilePath = std::filesystem::path{metadataText.substr(
                        0, metadataText.size() - request.metadataSuffix.size())};
                    bool validSourcePath = false;
                    std::string sourcePathError;
                    sourcePath =
                        makeSourcePath(request, sourceFilePath, validSourcePath, sourcePathError);
                    if (!validSourcePath) {
                        sourcePath.clear();
                    }
                }

                addDiagnostic(result, AssetSourceScanDiagnosticCode::OrphanMetadata, sourcePath,
                              sourceFilePath, metadataPath,
                              "Asset source scan found orphan metadata metadata=\"" +
                                  pathText(metadataPath) + "\".");
            }
        }

    } // namespace

    AssetSourceScanResult scanAssetSourceTree(const AssetSourceScanRequest& request) {
        AssetSourceScanResult result;

        const bool requestValid =
            validateMetadataSuffix(result, request.metadataSuffix) &&
            validateIgnoredDirectoryNames(result, request.ignoredDirectoryNames) &&
            validateSourcePathPrefix(result, request.sourcePathPrefix) &&
            validateRoot(result, request.sourceRoot);
        if (!requestValid) {
            return result;
        }

        std::vector<std::filesystem::path> sourceFiles;
        std::vector<std::filesystem::path> metadataFiles;
        collectRegularFiles(result, request, sourceFiles, metadataFiles);
        sortPathList(sourceFiles);
        sortPathList(metadataFiles);

        result.entries.reserve(sourceFiles.size());
        result.diagnostics.reserve(result.diagnostics.size() + sourceFiles.size() +
                                   metadataFiles.size());

        std::vector<std::filesystem::path> matchedMetadataFiles;
        matchedMetadataFiles.reserve(sourceFiles.size());
        appendScannedSources(result, request,
                             SourceScanFileView{
                                 .sourceFiles = sourceFiles,
                                 .metadataFiles = metadataFiles,
                             },
                             matchedMetadataFiles);
        appendOrphanMetadataDiagnostics(result, request,
                                        MetadataMatchView{
                                            .metadataFiles = metadataFiles,
                                            .matchedMetadataFiles = matchedMetadataFiles,
                                        });

        return result;
    }

} // namespace asharia::asset
