#include "asharia/project/project_descriptor_io.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/archive/json_archive.hpp"

namespace asharia::project {
    namespace {

        using archive::ArchiveMember;
        using archive::ArchiveValue;
        using archive::ArchiveValueKind;
        using namespace std::string_view_literals;

        inline constexpr std::uint64_t kMaxProjectDescriptorBytes = 16ULL * 1024ULL * 1024ULL;
        inline constexpr std::string_view kForbiddenProjectNameCharacters = "<>:\"/\\|?*";

        [[nodiscard]] Error projectDescriptorIoError(std::string message) {
            return Error{ErrorDomain::Project,
                         static_cast<int>(AshariaProjectIoErrorCode::DescriptorIo),
                         std::move(message)};
        }

        [[nodiscard]] Error projectOperationError(AshariaProjectIoErrorCode code,
                                                  std::string message) {
            return Error{ErrorDomain::Project, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string utf8 = path.u8string();
            return std::string{utf8.begin(), utf8.end()};
        }

        [[nodiscard]] bool isPortableProjectDirectoryName(std::string_view name) noexcept {
            if (name.empty() || name.size() > 255U || name == "." || name == ".." ||
                name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
                return false;
            }

            return std::ranges::none_of(name, [](char character) {
                const auto value = static_cast<unsigned char>(character);
                return value < 0x20U ||
                       kForbiddenProjectNameCharacters.find(character) != std::string_view::npos;
            });
        }

        class OwnedStagingDirectory {
        public:
            explicit OwnedStagingDirectory(std::filesystem::path path) : path_(std::move(path)) {}

            OwnedStagingDirectory(const OwnedStagingDirectory&) = delete;
            OwnedStagingDirectory& operator=(const OwnedStagingDirectory&) = delete;
            OwnedStagingDirectory(OwnedStagingDirectory&&) = delete;
            OwnedStagingDirectory& operator=(OwnedStagingDirectory&&) = delete;

            ~OwnedStagingDirectory() {
                if (committed_) {
                    return;
                }
                std::error_code removeError;
                std::filesystem::remove_all(path_, removeError);
            }

            void commit() noexcept {
                committed_ = true;
            }

        private:
            std::filesystem::path path_;
            bool committed_{false};
        };

        [[nodiscard]] bool containsName(std::span<const std::string_view> names,
                                        std::string_view name) noexcept {
            return std::ranges::any_of(
                names, [name](std::string_view allowed) { return allowed == name; });
        }

        [[nodiscard]] VoidResult
        validateObjectMembers(const ArchiveValue& value, std::string_view context,
                              std::span<const std::string_view> allowedMembers) {
            if (value.kind != ArchiveValueKind::Object) {
                return std::unexpected{
                    projectDescriptorIoError(std::string{context} + " must be a JSON object.")};
            }

            for (const ArchiveMember& member : value.objectValue) {
                if (!containsName(allowedMembers, member.key)) {
                    return std::unexpected{projectDescriptorIoError(
                        std::string{context} + " contains unknown member '" + member.key + "'.")};
                }
            }

            return {};
        }

        [[nodiscard]] Result<const ArchiveValue*> requiredMember(const ArchiveValue& object,
                                                                 std::string_view memberName,
                                                                 ArchiveValueKind expectedKind,
                                                                 std::string_view context) {
            const ArchiveValue* value = object.findMemberValue(memberName);
            if (value == nullptr) {
                return std::unexpected{projectDescriptorIoError(std::string{context} +
                                                                " is missing required member '" +
                                                                std::string{memberName} + "'.")};
            }

            if (value->kind != expectedKind) {
                return std::unexpected{projectDescriptorIoError(std::string{context} + " member '" +
                                                                std::string{memberName} +
                                                                "' has an unexpected type.")};
            }

            return value;
        }

        [[nodiscard]] Result<std::string> requiredString(const ArchiveValue& object,
                                                         std::string_view memberName,
                                                         std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::String, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            return (*value)->stringValue;
        }

        [[nodiscard]] Result<std::uint32_t> requiredUint32(const ArchiveValue& object,
                                                           std::string_view memberName,
                                                           std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Integer, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }

            if ((*value)->integerValue <= 0 ||
                (*value)->integerValue >
                    static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                return std::unexpected{projectDescriptorIoError(
                    std::string{context} + " member '" + std::string{memberName} +
                    "' must be a positive uint32 value.")};
            }

            return static_cast<std::uint32_t>((*value)->integerValue);
        }

        [[nodiscard]] ArchiveValue
        ignoredDirectoriesArchiveValue(std::span<const std::string> ignoredDirectoryNames) {
            std::vector<ArchiveValue> values;
            values.reserve(ignoredDirectoryNames.size());
            for (const std::string& name : ignoredDirectoryNames) {
                values.push_back(ArchiveValue::string(name));
            }
            return ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] ArchiveValue
        assetSourceRootsArchiveValue(std::span<const AssetSourceRootDesc> roots) {
            std::vector<ArchiveValue> values;
            values.reserve(roots.size());
            for (const AssetSourceRootDesc& root : roots) {
                values.push_back(ArchiveValue::object({
                    ArchiveMember{
                        .key = "rootName",
                        .value = ArchiveValue::string(root.rootName),
                    },
                    ArchiveMember{
                        .key = "directory",
                        .value = ArchiveValue::string(root.directory),
                    },
                    ArchiveMember{
                        .key = "sourcePathPrefix",
                        .value = ArchiveValue::string(root.sourcePathPrefix),
                    },
                }));
            }
            return ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] ArchiveValue
        descriptorArchiveValue(const AshariaProjectDescriptor& descriptor) {
            return ArchiveValue::object({
                ArchiveMember{
                    .key = "schema",
                    .value = ArchiveValue::string(std::string{kAshariaProjectSchema}),
                },
                ArchiveMember{
                    .key = "schemaVersion",
                    .value = ArchiveValue::integer(kAshariaProjectSchemaVersion),
                },
                ArchiveMember{
                    .key = "projectName",
                    .value = ArchiveValue::string(descriptor.projectName),
                },
                ArchiveMember{
                    .key = "projectId",
                    .value = ArchiveValue::string(formatProjectId(descriptor.projectId)),
                },
                ArchiveMember{
                    .key = "assetSourceRoots",
                    .value = assetSourceRootsArchiveValue(descriptor.assetSourceRoots),
                },
                ArchiveMember{
                    .key = "assetCacheRoot",
                    .value = ArchiveValue::string(descriptor.assetCacheRoot),
                },
                ArchiveMember{
                    .key = "assetDiscovery",
                    .value = ArchiveValue::object({
                        ArchiveMember{
                            .key = "ignoredDirectories",
                            .value = ignoredDirectoriesArchiveValue(
                                descriptor.assetDiscovery.ignoredDirectoryNames),
                        },
                    }),
                },
            });
        }

        [[nodiscard]] Result<std::vector<std::string>>
        readIgnoredDirectoryNames(const ArchiveValue& assetDiscovery) {
            auto ignoredValue =
                requiredMember(assetDiscovery, "ignoredDirectories", ArchiveValueKind::Array,
                               "Asharia project assetDiscovery");
            if (!ignoredValue) {
                return std::unexpected{std::move(ignoredValue.error())};
            }
            if ((*ignoredValue)->arrayValue.size() > kMaxProjectIgnoredDirectories) {
                return std::unexpected{projectDescriptorIoError(
                    "Asharia project assetDiscovery.ignoredDirectories exceeds the maximum "
                    "supported count of " +
                    std::to_string(kMaxProjectIgnoredDirectories) + ".")};
            }

            std::vector<std::string> names;
            names.reserve((*ignoredValue)->arrayValue.size());
            for (std::size_t index = 0; index < (*ignoredValue)->arrayValue.size(); ++index) {
                const ArchiveValue& value = (*ignoredValue)->arrayValue[index];
                if (value.kind != ArchiveValueKind::String) {
                    return std::unexpected{projectDescriptorIoError(
                        "Asharia project assetDiscovery.ignoredDirectories[" +
                        std::to_string(index) + "] must be a string.")};
                }
                names.push_back(value.stringValue);
            }

            return names;
        }

        [[nodiscard]] Result<AssetDiscoveryDesc> readAssetDiscoveryDesc(const ArchiveValue& root) {
            auto assetDiscovery = requiredMember(root, "assetDiscovery", ArchiveValueKind::Object,
                                                 "Asharia project root");
            if (!assetDiscovery) {
                return std::unexpected{std::move(assetDiscovery.error())};
            }

            constexpr std::array assetDiscoveryMembers{"ignoredDirectories"sv};
            if (auto validMembers = validateObjectMembers(
                    **assetDiscovery, "Asharia project assetDiscovery", assetDiscoveryMembers);
                !validMembers) {
                return std::unexpected{std::move(validMembers.error())};
            }

            auto names = readIgnoredDirectoryNames(**assetDiscovery);
            if (!names) {
                return std::unexpected{std::move(names.error())};
            }

            return AssetDiscoveryDesc{
                .ignoredDirectoryNames = std::move(*names),
            };
        }

        [[nodiscard]] Result<AssetSourceRootDesc> readAssetSourceRootDesc(const ArchiveValue& value,
                                                                          std::size_t index) {
            const std::string context =
                "Asharia project assetSourceRoots[" + std::to_string(index) + "]";
            constexpr std::array rootMembers{"rootName"sv, "directory"sv, "sourcePathPrefix"sv};
            if (auto validMembers = validateObjectMembers(value, context, rootMembers);
                !validMembers) {
                return std::unexpected{std::move(validMembers.error())};
            }

            auto rootName = requiredString(value, "rootName", context);
            if (!rootName) {
                return std::unexpected{std::move(rootName.error())};
            }
            auto directory = requiredString(value, "directory", context);
            if (!directory) {
                return std::unexpected{std::move(directory.error())};
            }
            auto sourcePathPrefix = requiredString(value, "sourcePathPrefix", context);
            if (!sourcePathPrefix) {
                return std::unexpected{std::move(sourcePathPrefix.error())};
            }

            return AssetSourceRootDesc{
                .rootName = std::move(*rootName),
                .directory = std::move(*directory),
                .sourcePathPrefix = std::move(*sourcePathPrefix),
            };
        }

        [[nodiscard]] Result<std::vector<AssetSourceRootDesc>>
        readAssetSourceRoots(const ArchiveValue& root) {
            auto rootsValue = requiredMember(root, "assetSourceRoots", ArchiveValueKind::Array,
                                             "Asharia project root");
            if (!rootsValue) {
                return std::unexpected{std::move(rootsValue.error())};
            }
            if ((*rootsValue)->arrayValue.size() > kMaxProjectAssetSourceRoots) {
                return std::unexpected{projectDescriptorIoError(
                    "Asharia project assetSourceRoots exceeds the maximum supported count of " +
                    std::to_string(kMaxProjectAssetSourceRoots) + ".")};
            }

            std::vector<AssetSourceRootDesc> roots;
            roots.reserve((*rootsValue)->arrayValue.size());
            for (std::size_t index = 0; index < (*rootsValue)->arrayValue.size(); ++index) {
                auto rootDesc = readAssetSourceRootDesc((*rootsValue)->arrayValue[index], index);
                if (!rootDesc) {
                    return std::unexpected{std::move(rootDesc.error())};
                }
                roots.push_back(std::move(*rootDesc));
            }

            return roots;
        }

        [[nodiscard]] Result<AshariaProjectDescriptor>
        readAshariaProjectDescriptorArchive(const ArchiveValue& archive) {
            constexpr std::array rootMembers{
                "schema"sv,           "schemaVersion"sv,  "projectName"sv,   "projectId"sv,
                "assetSourceRoots"sv, "assetCacheRoot"sv, "assetDiscovery"sv};
            if (auto validRoot =
                    validateObjectMembers(archive, "Asharia project root", rootMembers);
                !validRoot) {
                return std::unexpected{std::move(validRoot.error())};
            }

            auto schema = requiredString(archive, "schema", "Asharia project root");
            if (!schema) {
                return std::unexpected{std::move(schema.error())};
            }
            if (*schema != kAshariaProjectSchema) {
                return std::unexpected{projectDescriptorIoError(
                    "Asharia project root has unsupported schema '" + *schema + "'.")};
            }

            auto schemaVersion = requiredUint32(archive, "schemaVersion", "Asharia project root");
            if (!schemaVersion) {
                return std::unexpected{std::move(schemaVersion.error())};
            }
            if (*schemaVersion != kAshariaProjectSchemaVersion) {
                return std::unexpected{projectDescriptorIoError(
                    "Asharia project root has unsupported schemaVersion '" +
                    std::to_string(*schemaVersion) + "'.")};
            }

            auto projectName = requiredString(archive, "projectName", "Asharia project root");
            if (!projectName) {
                return std::unexpected{std::move(projectName.error())};
            }
            auto projectIdText = requiredString(archive, "projectId", "Asharia project root");
            if (!projectIdText) {
                return std::unexpected{std::move(projectIdText.error())};
            }
            auto projectId = parseProjectId(*projectIdText);
            if (!projectId) {
                return std::unexpected{std::move(projectId.error())};
            }
            auto roots = readAssetSourceRoots(archive);
            if (!roots) {
                return std::unexpected{std::move(roots.error())};
            }
            auto assetCacheRoot = requiredString(archive, "assetCacheRoot", "Asharia project root");
            if (!assetCacheRoot) {
                return std::unexpected{std::move(assetCacheRoot.error())};
            }
            auto assetDiscovery = readAssetDiscoveryDesc(archive);
            if (!assetDiscovery) {
                return std::unexpected{std::move(assetDiscovery.error())};
            }

            AshariaProjectDescriptor descriptor{
                .projectName = std::move(*projectName),
                .projectId = *projectId,
                .assetSourceRoots = std::move(*roots),
                .assetCacheRoot = std::move(*assetCacheRoot),
                .assetDiscovery = std::move(*assetDiscovery),
            };
            if (auto validDescriptor = validateAshariaProjectDescriptor(descriptor);
                !validDescriptor) {
                return std::unexpected{std::move(validDescriptor.error())};
            }

            return descriptor;
        }

    } // namespace

    Result<std::string>
    writeAshariaProjectDescriptorText(const AshariaProjectDescriptor& descriptor) {
        auto validDescriptor = validateAshariaProjectDescriptor(descriptor);
        if (!validDescriptor) {
            return std::unexpected{std::move(validDescriptor.error())};
        }

        auto text = archive::writeJsonArchive(descriptorArchiveValue(descriptor));
        if (!text) {
            return std::unexpected{
                projectDescriptorIoError("Failed to write Asharia project descriptor project=\"" +
                                         descriptor.projectName + "\": " + text.error().message)};
        }

        return *text;
    }

    VoidResult writeAshariaProjectDescriptorFile(const std::filesystem::path& path,
                                                 const AshariaProjectDescriptor& descriptor) {
        auto validDescriptor = validateAshariaProjectDescriptor(descriptor);
        if (!validDescriptor) {
            return std::unexpected{std::move(validDescriptor.error())};
        }

        auto written = archive::writeJsonArchiveFile(path, descriptorArchiveValue(descriptor));
        if (!written) {
            return std::unexpected{
                projectDescriptorIoError("Failed to write Asharia project descriptor file '" +
                                         path.string() + "': " + written.error().message)};
        }

        return {};
    }

    Result<AshariaProjectDescriptor> readAshariaProjectDescriptorText(std::string_view text) {
        auto parsedArchive = archive::readJsonArchive(text);
        if (!parsedArchive) {
            return std::unexpected{projectDescriptorIoError(
                "Failed to read Asharia project descriptor: " + parsedArchive.error().message)};
        }

        return readAshariaProjectDescriptorArchive(*parsedArchive);
    }

    Result<AshariaProjectDescriptor>
    readAshariaProjectDescriptorFile(const std::filesystem::path& path) {
        auto archive = archive::readJsonArchiveFile(path, {.maxBytes = kMaxProjectDescriptorBytes});
        if (!archive) {
            return std::unexpected{
                projectDescriptorIoError("Failed to parse Asharia project descriptor file '" +
                                         path.string() + "': " + archive.error().message)};
        }

        auto descriptor = readAshariaProjectDescriptorArchive(*archive);
        if (!descriptor) {
            return std::unexpected{
                projectDescriptorIoError("Failed to validate Asharia project descriptor file '" +
                                         path.string() + "': " + descriptor.error().message)};
        }

        return descriptor;
    }

    Result<std::filesystem::path>
    resolveContainedProjectPath(const std::filesystem::path& projectRoot,
                                const std::filesystem::path& projectRelativePath,
                                std::string_view context) {
        if (projectRoot.empty() || projectRelativePath.empty() ||
            projectRelativePath.is_absolute()) {
            return std::unexpected{projectOperationError(
                AshariaProjectIoErrorCode::InvalidProject,
                std::string{context} + " must be a non-empty project-relative path.")};
        }

        std::error_code projectError;
        const std::filesystem::path canonicalProject =
            std::filesystem::weakly_canonical(projectRoot, projectError);
        if (projectError) {
            return std::unexpected{projectOperationError(AshariaProjectIoErrorCode::IoFailure,
                                                         "Failed to resolve project root for " +
                                                             std::string{context} + ": " +
                                                             projectError.message() + ".")};
        }

        std::error_code candidateError;
        const std::filesystem::path candidate = projectRoot / projectRelativePath;
        std::filesystem::path canonicalCandidate =
            std::filesystem::weakly_canonical(candidate, candidateError);
        if (candidateError) {
            return std::unexpected{projectOperationError(
                AshariaProjectIoErrorCode::IoFailure, "Failed to resolve " + std::string{context} +
                                                          " path '" + pathText(candidate) +
                                                          "': " + candidateError.message() + ".")};
        }

        const std::filesystem::path relative =
            canonicalCandidate.lexically_relative(canonicalProject);
        const auto firstComponent = relative.begin();
        const bool escapesProject =
            firstComponent != relative.end() && *firstComponent == std::filesystem::path{".."};
        const bool contained = !relative.empty() && !relative.is_absolute() && !escapesProject;
        if (!contained) {
            return std::unexpected{projectOperationError(
                AshariaProjectIoErrorCode::InvalidProject,
                std::string{context} + " resolves outside the project root.")};
        }

        return canonicalCandidate;
    }

    Result<OpenedAshariaProject> openAshariaProject(const std::filesystem::path& projectPath) {
        std::error_code statusError;
        const std::filesystem::file_status status =
            std::filesystem::status(projectPath, statusError);
        if (statusError) {
            return std::unexpected{
                projectOperationError(AshariaProjectIoErrorCode::InvalidProject,
                                      "Could not inspect Asharia project path '" +
                                          pathText(projectPath) + "': " + statusError.message())};
        }

        std::filesystem::path root;
        if (std::filesystem::is_directory(status)) {
            root = projectPath;
        } else if (std::filesystem::is_regular_file(status) &&
                   projectPath.filename() ==
                       std::filesystem::path{std::string{kDefaultAshariaProjectFileName}}) {
            root = projectPath.parent_path();
        } else {
            return std::unexpected{
                projectOperationError(AshariaProjectIoErrorCode::InvalidProject,
                                      "Asharia project path must be a project directory or '" +
                                          std::string{kDefaultAshariaProjectFileName} + "'.")};
        }

        std::error_code canonicalError;
        root = std::filesystem::canonical(root, canonicalError);
        if (canonicalError || root.empty()) {
            return std::unexpected{
                projectOperationError(AshariaProjectIoErrorCode::InvalidProject,
                                      "Could not resolve Asharia project root '" + pathText(root) +
                                          "': " + canonicalError.message())};
        }

        const std::filesystem::path descriptorPath =
            root / std::string{kDefaultAshariaProjectFileName};
        auto descriptor = readAshariaProjectDescriptorFile(descriptorPath);
        if (!descriptor) {
            return std::unexpected{std::move(descriptor.error())};
        }

        return OpenedAshariaProject{
            .root = std::move(root),
            .descriptor = std::move(*descriptor),
        };
    }

    Result<OpenedAshariaProject>
    createMinimalAshariaProject(const MinimalAshariaProjectCreate& request) {
        if (!isPortableProjectDirectoryName(request.projectName)) {
            return std::unexpected{
                projectOperationError(AshariaProjectIoErrorCode::InvalidProject,
                                      "Asharia project name must be a portable directory name of "
                                      "at most 255 UTF-8 bytes.")};
        }
        if (!request.projectId) {
            return std::unexpected{projectOperationError(AshariaProjectIoErrorCode::InvalidProject,
                                                         "Asharia project id is invalid.")};
        }

        std::error_code canonicalError;
        std::filesystem::path parent =
            std::filesystem::canonical(request.parentDirectory, canonicalError);
        if (canonicalError || parent.empty()) {
            return std::unexpected{projectOperationError(
                AshariaProjectIoErrorCode::IoFailure,
                "Could not resolve Asharia project parent directory '" +
                    pathText(request.parentDirectory) + "': " + canonicalError.message())};
        }

        std::error_code directoryError;
        if (!std::filesystem::is_directory(parent, directoryError) || directoryError) {
            return std::unexpected{projectOperationError(
                AshariaProjectIoErrorCode::IoFailure,
                "Asharia project parent is not a readable directory: '" + pathText(parent) + "'.")};
        }

        const std::filesystem::path projectRoot = parent / request.projectName;
        std::error_code existsError;
        if (std::filesystem::exists(projectRoot, existsError)) {
            return std::unexpected{projectOperationError(AshariaProjectIoErrorCode::AlreadyExists,
                                                         "Asharia project path already exists: '" +
                                                             pathText(projectRoot) + "'.")};
        }
        if (existsError) {
            return std::unexpected{
                projectOperationError(AshariaProjectIoErrorCode::IoFailure,
                                      "Could not inspect Asharia project path '" +
                                          pathText(projectRoot) + "': " + existsError.message())};
        }

        const std::filesystem::path stagingRoot =
            parent / (".asharia-project-create-" + formatProjectId(request.projectId));
        std::error_code createError;
        if (!std::filesystem::create_directory(stagingRoot, createError)) {
            return std::unexpected{projectOperationError(
                createError ? AshariaProjectIoErrorCode::IoFailure
                            : AshariaProjectIoErrorCode::Busy,
                "Could not acquire Asharia project staging directory '" + pathText(stagingRoot) +
                    "': " + (createError ? createError.message() : "already exists"))};
        }
        OwnedStagingDirectory ownedStaging{stagingRoot};

        const AshariaProjectDescriptor descriptor{
            .projectName = request.projectName,
            .projectId = request.projectId,
            .assetSourceRoots =
                {
                    AssetSourceRootDesc{
                        .rootName = "project-assets",
                        .directory = "Assets",
                        .sourcePathPrefix = "Assets",
                    },
                },
            .assetCacheRoot = ".asharia/cache/assets",
            .assetDiscovery =
                AssetDiscoveryDesc{
                    .ignoredDirectoryNames = {".git", ".asharia"},
                },
        };
        if (auto validDescriptor = validateAshariaProjectDescriptor(descriptor); !validDescriptor) {
            return std::unexpected{std::move(validDescriptor.error())};
        }

        std::filesystem::create_directories(stagingRoot / "Assets", createError);
        if (!createError) {
            std::filesystem::create_directories(stagingRoot / ".asharia" / "cache" / "assets",
                                                createError);
        }
        if (createError) {
            return std::unexpected{
                projectOperationError(AshariaProjectIoErrorCode::IoFailure,
                                      "Could not create Asharia project layout in '" +
                                          pathText(stagingRoot) + "': " + createError.message())};
        }

        const std::filesystem::path stagedDescriptor =
            stagingRoot / std::string{kDefaultAshariaProjectFileName};
        if (auto written = writeAshariaProjectDescriptorFile(stagedDescriptor, descriptor);
            !written) {
            return std::unexpected{projectOperationError(AshariaProjectIoErrorCode::IoFailure,
                                                         std::move(written.error().message))};
        }
        if (auto verified = readAshariaProjectDescriptorFile(stagedDescriptor); !verified) {
            return std::unexpected{projectOperationError(AshariaProjectIoErrorCode::InvalidProject,
                                                         std::move(verified.error().message))};
        }

        std::filesystem::rename(stagingRoot, projectRoot, createError);
        if (createError) {
            std::error_code publishedExistsError;
            const bool publishedExists = std::filesystem::exists(projectRoot, publishedExistsError);
            const auto code = publishedExists && !publishedExistsError
                                  ? AshariaProjectIoErrorCode::AlreadyExists
                                  : AshariaProjectIoErrorCode::IoFailure;
            return std::unexpected{projectOperationError(
                code, "Could not publish Asharia project '" + pathText(projectRoot) +
                          "': " + createError.message())};
        }
        ownedStaging.commit();
        return openAshariaProject(projectRoot);
    }

} // namespace asharia::project
