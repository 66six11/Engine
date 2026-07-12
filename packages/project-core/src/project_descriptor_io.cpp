#include "asharia/project/project_descriptor_io.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
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

        [[nodiscard]] Error projectDescriptorIoError(std::string message) {
            return Error{ErrorDomain::Project, 2, std::move(message)};
        }

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

} // namespace asharia::project
