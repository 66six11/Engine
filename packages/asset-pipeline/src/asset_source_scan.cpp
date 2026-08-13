#include "asharia/asset_pipeline/asset_source_scan.hpp"

#include <algorithm>
#include <functional>
#include <set>
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
            const std::u8string text = path.generic_u8string();
            return std::string{text.begin(), text.end()};
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

        using PathTextSet = std::set<std::string, std::less<>>;

        [[nodiscard]] bool isValidSinglePathSegment(std::string_view text) {
            return !text.empty() && text != "." && text != ".." &&
                   text.find('/') == std::string_view::npos &&
                   text.find('\\') == std::string_view::npos;
        }

        [[nodiscard]] bool isRedirectingLink(const std::filesystem::file_status& status) noexcept {
            if (std::filesystem::is_symlink(status)) {
                return true;
            }
#if defined(_WIN32)
            return status.type() == std::filesystem::file_type::junction;
#else
            return false;
#endif
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

        [[nodiscard]] bool validateLimits(AssetSourceScanResult& result,
                                          const AssetSourceScanRequest& request) {
            if (request.maxDiscoveredFiles > 0U) {
                return true;
            }
            addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRequest, {}, {}, {},
                          "Asset source scan max discovered files must be greater than zero.");
            return false;
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
            const std::filesystem::file_status linkStatus =
                std::filesystem::symlink_status(sourceRoot, rootError);
            if (rootError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              sourceRoot, {},
                              "Asset source scan could not inspect root link status for \"" +
                                  pathText(sourceRoot) + "\": " + rootError.message() + ".");
                return false;
            }
            if (isRedirectingLink(linkStatus)) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::InvalidRoot, {}, sourceRoot,
                              {},
                              "Asset source scan rejected symbolic-link root \"" +
                                  pathText(sourceRoot) + "\".");
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
            const std::string directoryName = pathText(path.filename());
            return std::ranges::any_of(
                ignoredDirectoryNames,
                [&directoryName](const std::string& ignored) { return ignored == directoryName; });
        }

        [[nodiscard]] bool isMetadataSidecarPath(const std::filesystem::path& path,
                                                 std::string_view metadataSuffix) {
            return pathText(path.filename()).ends_with(metadataSuffix);
        }

        [[nodiscard]] std::filesystem::path
        makeExpectedMetadataPath(const std::filesystem::path& sourceFilePath,
                                 std::string_view metadataSuffix) {
            std::filesystem::path metadataPath = sourceFilePath;
            metadataPath += metadataSuffix;
            return metadataPath;
        }

        using RecursiveDirectoryIterator = std::filesystem::recursive_directory_iterator;

        enum class EntryCollectionResult : std::uint8_t {
            Continue,
            LimitExceeded,
        };

        void skipRejectedEntry(RecursiveDirectoryIterator& iterator,
                               std::error_code& iteratorError) {
            iterator.disable_recursion_pending();
            iterator.increment(iteratorError);
            iteratorError.clear();
        }

        [[nodiscard]] bool rejectRedirectingEntry(AssetSourceScanResult& result,
                                                  RecursiveDirectoryIterator& iterator,
                                                  const std::filesystem::path& currentPath,
                                                  std::error_code& iteratorError) {
            std::error_code entryError;
            const std::filesystem::file_status linkStatus = iterator->symlink_status(entryError);
            if (entryError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              currentPath, {},
                              "Asset source scan could not inspect link status for \"" +
                                  pathText(currentPath) + "\": " + entryError.message() + ".");
                skipRejectedEntry(iterator, iteratorError);
                return true;
            }
            if (!isRedirectingLink(linkStatus)) {
                return false;
            }

            addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {}, currentPath,
                          {},
                          "Asset source scan rejected symbolic-link entry \"" +
                              pathText(currentPath) + "\".");
            skipRejectedEntry(iterator, iteratorError);
            return true;
        }

        [[nodiscard]] EntryCollectionResult
        collectEntry(AssetSourceScanResult& result, const AssetSourceScanRequest& request,
                     RecursiveDirectoryIterator& iterator, const std::filesystem::path& currentPath,
                     std::vector<std::filesystem::path>& sourceFiles,
                     std::vector<std::filesystem::path>& metadataFiles) {
            std::error_code entryError;
            const bool directory = iterator->is_directory(entryError);
            if (entryError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              currentPath, {},
                              "Asset source scan could not inspect \"" + pathText(currentPath) +
                                  "\": " + entryError.message() + ".");
                return EntryCollectionResult::Continue;
            }
            if (directory) {
                if (shouldIgnoreDirectory(currentPath, request.ignoredDirectoryNames)) {
                    iterator.disable_recursion_pending();
                }
                return EntryCollectionResult::Continue;
            }

            const bool regularFile = iterator->is_regular_file(entryError);
            if (entryError) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {},
                              currentPath, {},
                              "Asset source scan could not inspect file \"" +
                                  pathText(currentPath) + "\": " + entryError.message() + ".");
                return EntryCollectionResult::Continue;
            }
            if (!regularFile) {
                return EntryCollectionResult::Continue;
            }
            if (result.discoveredFileCount >= request.maxDiscoveredFiles) {
                addDiagnostic(result, AssetSourceScanDiagnosticCode::LimitExceeded, {}, {}, {},
                              "Asset source scan exceeded max discovered files limit=" +
                                  std::to_string(request.maxDiscoveredFiles) + ".");
                return EntryCollectionResult::LimitExceeded;
            }

            ++result.discoveredFileCount;
            if (isMetadataSidecarPath(currentPath, request.metadataSuffix)) {
                metadataFiles.push_back(currentPath);
            } else {
                sourceFiles.push_back(currentPath);
            }
            return EntryCollectionResult::Continue;
        }

        void advanceIterator(AssetSourceScanResult& result, RecursiveDirectoryIterator& iterator,
                             const std::filesystem::path& currentPath,
                             std::error_code& iteratorError) {
            iterator.increment(iteratorError);
            if (!iteratorError) {
                return;
            }

            addDiagnostic(result, AssetSourceScanDiagnosticCode::FilesystemError, {}, currentPath,
                          {},
                          "Asset source scan could not advance past \"" + pathText(currentPath) +
                              "\": " + iteratorError.message() + ".");
            iteratorError.clear();
        }

        void collectRegularFiles(AssetSourceScanResult& result,
                                 const AssetSourceScanRequest& request,
                                 std::vector<std::filesystem::path>& sourceFiles,
                                 std::vector<std::filesystem::path>& metadataFiles) {
            std::error_code iteratorError;
            RecursiveDirectoryIterator iterator{
                request.sourceRoot, std::filesystem::directory_options::none, iteratorError};
            const RecursiveDirectoryIterator end;
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
                if (rejectRedirectingEntry(result, iterator, currentPath, iteratorError)) {
                    continue;
                }
                if (collectEntry(result, request, iterator, currentPath, sourceFiles,
                                 metadataFiles) == EntryCollectionResult::LimitExceeded) {
                    return;
                }
                advanceIterator(result, iterator, currentPath, iteratorError);
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

            const std::string relativeText = pathText(relativePath);
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
                                  const AssetSourceScanRequest& request,
                                  std::span<const std::filesystem::path> sourceFiles,
                                  const PathTextSet& metadataFileTexts,
                                  PathTextSet& matchedMetadataFileTexts) {
            std::set<std::string, std::less<>> sourcePaths;
            for (const std::filesystem::path& sourceFilePath : sourceFiles) {
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

                if (sourcePaths.contains(sourcePath)) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::DuplicateSourcePath,
                                  sourcePath, sourceFilePath, {},
                                  "Asset source scan duplicate source path source=\"" + sourcePath +
                                      "\" file=\"" + pathText(sourceFilePath) + "\".");
                    continue;
                }

                const std::filesystem::path metadataPath =
                    makeExpectedMetadataPath(sourceFilePath, request.metadataSuffix);
                const std::string metadataText = pathText(metadataPath);
                if (!metadataFileTexts.contains(metadataText)) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::MissingMetadata,
                                  sourcePath, sourceFilePath, metadataPath,
                                  "Asset source scan missing metadata for source=\"" + sourcePath +
                                      "\" file=\"" + pathText(sourceFilePath) +
                                      "\" expectedMetadata=\"" + pathText(metadataPath) + "\".");
                    continue;
                }

                if (matchedMetadataFileTexts.contains(metadataText)) {
                    addDiagnostic(result, AssetSourceScanDiagnosticCode::DuplicateMetadataPath,
                                  sourcePath, sourceFilePath, metadataPath,
                                  "Asset source scan metadata path collision source=\"" +
                                      sourcePath + "\" metadata=\"" + pathText(metadataPath) +
                                      "\".");
                    continue;
                }

                sourcePaths.emplace(sourcePath);
                matchedMetadataFileTexts.emplace(metadataText);
                result.entries.push_back(AssetSourceScanEntry{
                    .sourcePath = std::move(sourcePath),
                    .sourceFilePath = sourceFilePath,
                    .metadataPath = metadataPath,
                });
            }
        }

        void appendOrphanMetadataDiagnostics(AssetSourceScanResult& result,
                                             const AssetSourceScanRequest& request,
                                             std::span<const std::filesystem::path> metadataFiles,
                                             const PathTextSet& matchedMetadataFileTexts) {
            for (const std::filesystem::path& metadataPath : metadataFiles) {
                const std::string metadataText = pathText(metadataPath);
                if (matchedMetadataFileTexts.contains(metadataText)) {
                    continue;
                }

                std::filesystem::path sourceFilePath = metadataPath;
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
            validateLimits(result, request) &&
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
        PathTextSet metadataFileTexts;
        for (const std::filesystem::path& metadataFile : metadataFiles) {
            metadataFileTexts.emplace(pathText(metadataFile));
        }

        result.entries.reserve(sourceFiles.size());
        result.diagnostics.reserve(result.diagnostics.size() + sourceFiles.size() +
                                   metadataFiles.size());

        PathTextSet matchedMetadataFileTexts;
        appendScannedSources(result, request, sourceFiles, metadataFileTexts,
                             matchedMetadataFileTexts);
        appendOrphanMetadataDiagnostics(result, request, metadataFiles, matchedMetadataFileTexts);

        return result;
    }

} // namespace asharia::asset
