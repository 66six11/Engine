#include "asharia/asset_pipeline/asset_import_planning.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "asharia/asset_core/asset_guid.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::string_view kShaderCompileReflectionImporterName =
            "com.asharia.importer.shader-compile-reflection";

        using ToolFingerprintCacheKey = std::pair<std::uint64_t, std::string>;
        using ToolFingerprintCache =
            std::map<ToolFingerprintCacheKey, Result<AssetToolFingerprint>>;
        using GuidBytes = std::array<std::uint8_t, 16>;

        struct AssetProductKeyLess {
            [[nodiscard]] bool operator()(const AssetProductKey& lhs,
                                          const AssetProductKey& rhs) const noexcept {
                return std::tie(lhs.guid.bytes, lhs.assetType.value, lhs.importerId.value,
                                lhs.importerVersion.value, lhs.sourceHash, lhs.settingsHash,
                                lhs.dependencyHash, lhs.targetProfileHash) <
                       std::tie(rhs.guid.bytes, rhs.assetType.value, rhs.importerId.value,
                                rhs.importerVersion.value, rhs.sourceHash, rhs.settingsHash,
                                rhs.dependencyHash, rhs.targetProfileHash);
            }
        };

        using SnapshotsBySourcePath = std::map<std::string, std::size_t, std::less<>>;
        using ProductsByKey = std::map<AssetProductKey, std::size_t, AssetProductKeyLess>;
        using ProductsByGuid = std::map<GuidBytes, std::vector<const AssetProductRecord*>>;

        [[nodiscard]] std::string formatHash64(std::uint64_t value) {
            constexpr std::string_view kHexDigits = "0123456789abcdef";
            std::string text(16, '0');
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto shift = static_cast<std::uint32_t>((text.size() - index - 1) * 4);
                text[index] = kHexDigits[(value >> shift) & 0xFU];
            }
            return text;
        }

        [[nodiscard]] bool isAsciiAlpha(char character) noexcept {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        [[nodiscard]] bool isValidTargetProfile(std::string_view targetProfile,
                                                std::string& reason) {
            if (targetProfile.empty()) {
                reason = "target profile is missing";
                return false;
            }

            if (targetProfile.find('\\') != std::string_view::npos ||
                targetProfile.find('/') != std::string_view::npos) {
                reason = "target profile must be a single path segment";
                return false;
            }

            if (targetProfile == "." || targetProfile == "..") {
                reason = "target profile must not be '.' or '..'";
                return false;
            }

            if (targetProfile.size() >= 2 && isAsciiAlpha(targetProfile[0]) &&
                targetProfile[1] == ':') {
                reason = "target profile must not use a drive prefix";
                return false;
            }

            return true;
        }

        void addDiagnostic(
            AssetImportPlanResult& result, AssetImportPlanDiagnosticCode code,
            std::string sourcePath, std::string message,
            AssetImportPlanDiagnosticSeverity severity = AssetImportPlanDiagnosticSeverity::Error) {
            result.diagnostics.push_back(AssetImportPlanDiagnostic{
                .code = code,
                .severity = severity,
                .sourcePath = std::move(sourcePath),
                .message = std::move(message),
            });
        }

        [[nodiscard]] std::string sourceLabel(const SourceAssetRecord& source) {
            return "guid=\"" + formatAssetGuid(source.guid) + "\" source=\"" + source.sourcePath +
                   "\"";
        }

        [[nodiscard]] bool sourceIndexOrdersBefore(std::span<const DiscoveredSourceAsset> sources,
                                                   std::size_t leftIndex, std::size_t rightIndex) {
            const DiscoveredSourceAsset& left = sources[leftIndex];
            const DiscoveredSourceAsset& right = sources[rightIndex];
            if (left.source.sourcePath != right.source.sourcePath) {
                return left.source.sourcePath < right.source.sourcePath;
            }
            return left.source.guid.bytes < right.source.guid.bytes;
        }

        [[nodiscard]] std::string toolExecutableName(std::string_view toolName) {
            std::string executable{toolName};
#if defined(_WIN32)
            executable += ".exe";
#endif
            return executable;
        }

        [[nodiscard]] bool isRegularFile(const std::filesystem::path& path) {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error);
        }

        [[nodiscard]] std::optional<std::string> environmentVariable(std::string_view name) {
            const std::string variableName{name};
#if defined(_WIN32)
            const DWORD requiredSize = GetEnvironmentVariableA(variableName.c_str(), nullptr, 0);
            if (requiredSize == 0) {
                return std::nullopt;
            }
            std::string text(requiredSize, '\0');
            const DWORD writtenSize =
                GetEnvironmentVariableA(variableName.c_str(), text.data(), requiredSize);
            if (writtenSize == 0 || writtenSize >= requiredSize) {
                return std::nullopt;
            }
            text.resize(writtenSize);
            return text;
#else
            if (const char* value = std::getenv(variableName.c_str()); value != nullptr) {
                return std::string{value};
            }
            return std::nullopt;
#endif
        }

        [[nodiscard]] std::optional<std::filesystem::path>
        findToolExecutable(std::string_view toolName) {
            const std::string executable = toolExecutableName(toolName);
            const std::optional<std::string> pathValue = environmentVariable("PATH");
            if (pathValue) {
#if defined(_WIN32)
                constexpr char kSeparator = ';';
#else
                constexpr char kSeparator = ':';
#endif
                const std::string_view pathText{*pathValue};
                std::size_t segmentBegin = 0;
                while (segmentBegin <= pathText.size()) {
                    std::size_t segmentEnd = pathText.find(kSeparator, segmentBegin);
                    if (segmentEnd == std::string_view::npos) {
                        segmentEnd = pathText.size();
                    }
                    if (segmentEnd > segmentBegin) {
                        std::filesystem::path candidate =
                            std::filesystem::path{std::string{
                                pathText.substr(segmentBegin, segmentEnd - segmentBegin)}} /
                            executable;
                        if (isRegularFile(candidate)) {
                            return candidate;
                        }
                    }
                    if (segmentEnd == pathText.size()) {
                        break;
                    }
                    segmentBegin = segmentEnd + 1U;
                }
            }

            const std::optional<std::string> vulkanSdkValue = environmentVariable("VULKAN_SDK");
            if (vulkanSdkValue) {
                std::filesystem::path candidate =
                    std::filesystem::path{*vulkanSdkValue} / "Bin" / executable;
                if (isRegularFile(candidate)) {
                    return candidate;
                }
            }

            return std::nullopt;
        }

        [[nodiscard]] Result<AssetToolFingerprint>
        resolveDefaultToolFingerprint(std::string_view toolName) {
            const std::optional<std::filesystem::path> executable = findToolExecutable(toolName);
            if (!executable) {
                return std::unexpected{Error{ErrorDomain::Asset, 0,
                                             "Could not locate required asset tool executable '" +
                                                 std::string{toolName} +
                                                 "' in PATH or VULKAN_SDK."}};
            }
            return fingerprintAssetTool(*executable, toolName);
        }

        [[nodiscard]] bool
        hasToolVersionFor(std::span<const AssetImportToolVersionDependency> toolVersions,
                          ImporterId importerId, std::string_view toolName) {
            return std::ranges::any_of(
                toolVersions,
                [importerId, toolName](const AssetImportToolVersionDependency& toolVersion) {
                    return toolVersion.importerId == importerId && toolVersion.toolName == toolName;
                });
        }

        [[nodiscard]] bool isShaderCompileReflectionSource(const SourceAssetRecord& source) {
            return source.importerName == kShaderCompileReflectionImporterName &&
                   source.importerId == makeImporterId(kShaderCompileReflectionImporterName);
        }

        [[nodiscard]] std::vector<std::string> unresolvedImplicitToolDependencies(
            const SourceAssetRecord& source,
            std::span<const AssetImportToolVersionDependency> toolVersions,
            AssetImportToolDependencyPolicy toolDependencyPolicy) {
            if (toolDependencyPolicy != AssetImportToolDependencyPolicy::DeclaredOnly ||
                !isShaderCompileReflectionSource(source)) {
                return {};
            }

            std::vector<std::string> unresolved;
            for (const std::string_view toolName :
                 {std::string_view{"slangc"}, std::string_view{"spirv-val"}}) {
                if (!hasToolVersionFor(toolVersions, source.importerId, toolName)) {
                    unresolved.emplace_back(toolName);
                }
            }
            return unresolved;
        }

        [[nodiscard]] std::string unresolvedToolList(std::span<const std::string> toolNames) {
            std::string list;
            for (const std::string& toolName : toolNames) {
                if (!list.empty()) {
                    list += ", ";
                }
                list += "'" + toolName + "'";
            }
            return list;
        }

        [[nodiscard]] VoidResult appendDefaultShaderToolVersionDependency(
            std::vector<AssetImportToolVersionDependency>& toolVersions, ImporterId importerId,
            std::string_view toolName, AssetToolFingerprintResolver resolver,
            ToolFingerprintCache& fingerprintCache) {
            if (hasToolVersionFor(toolVersions, importerId, toolName)) {
                return {};
            }

            ToolFingerprintCacheKey cacheKey{importerId.value, std::string{toolName}};
            auto cached = fingerprintCache.find(cacheKey);
            if (cached == fingerprintCache.end()) {
                Result<AssetToolFingerprint> resolved =
                    resolver != nullptr ? resolver(toolName)
                                        : resolveDefaultToolFingerprint(toolName);
                cached = fingerprintCache.emplace(std::move(cacheKey), std::move(resolved)).first;
            }
            const Result<AssetToolFingerprint>& fingerprint = cached->second;
            if (!fingerprint) {
                return std::unexpected{fingerprint.error()};
            }
            toolVersions.push_back(AssetImportToolVersionDependency{
                .importerId = importerId,
                .toolName = std::string{toolName},
                .versionHash = fingerprint->versionHash,
            });
            return {};
        }

        [[nodiscard]] AssetImportRequestReason
        classifyMissReason(std::span<const AssetProductRecord* const> products,
                           const AssetProductKey& productKey) {
            bool sawGuid = false;
            bool sawTargetProfile = false;
            for (const AssetProductRecord* productPointer : products) {
                const AssetProductRecord& product = *productPointer;
                sawGuid = true;
                if (product.key.targetProfileHash != productKey.targetProfileHash) {
                    continue;
                }

                sawTargetProfile = true;
                if (product.key.sourceHash != productKey.sourceHash) {
                    return AssetImportRequestReason::SourceChanged;
                }
                if (product.key.settingsHash != productKey.settingsHash) {
                    return AssetImportRequestReason::SettingsChanged;
                }
                if (product.key.importerId != productKey.importerId ||
                    product.key.importerVersion != productKey.importerVersion) {
                    return AssetImportRequestReason::ImporterChanged;
                }
                if (product.key.assetType != productKey.assetType) {
                    return AssetImportRequestReason::AssetTypeChanged;
                }
                if (product.key.dependencyHash != productKey.dependencyHash) {
                    return AssetImportRequestReason::DependencyChanged;
                }
            }

            return sawGuid && !sawTargetProfile ? AssetImportRequestReason::TargetProfileChanged
                                                : AssetImportRequestReason::MissingProduct;
        }

        [[nodiscard]] Result<std::vector<AssetDependency>>
        makeImportDependencies(const SourceAssetRecord& source,
                               std::span<const AssetImportToolVersionDependency> toolVersions,
                               AssetToolFingerprintResolver resolver,
                               AssetImportToolDependencyPolicy toolDependencyPolicy,
                               ToolFingerprintCache& fingerprintCache) {
            std::vector<AssetDependency> dependencies{
                AssetDependency{
                    .owner = source.guid,
                    .kind = AssetDependencyKind::SourceFile,
                    .path = source.sourcePath,
                    .hash = source.sourceHash,
                },
                AssetDependency{
                    .owner = source.guid,
                    .kind = AssetDependencyKind::ImportSettings,
                    .path = {},
                    .hash = source.settingsHash,
                },
            };

            std::vector<AssetImportToolVersionDependency> matchingToolVersions;
            for (const AssetImportToolVersionDependency& toolVersion : toolVersions) {
                if (toolVersion.importerId == source.importerId) {
                    matchingToolVersions.push_back(toolVersion);
                }
            }
            if (toolDependencyPolicy == AssetImportToolDependencyPolicy::ResolveImplicit &&
                isShaderCompileReflectionSource(source)) {
                if (auto appended = appendDefaultShaderToolVersionDependency(
                        matchingToolVersions, source.importerId, "slangc", resolver,
                        fingerprintCache);
                    !appended) {
                    return std::unexpected{appended.error()};
                }
                if (auto appended = appendDefaultShaderToolVersionDependency(
                        matchingToolVersions, source.importerId, "spirv-val", resolver,
                        fingerprintCache);
                    !appended) {
                    return std::unexpected{appended.error()};
                }
            }
            std::ranges::sort(matchingToolVersions,
                              [](const AssetImportToolVersionDependency& left,
                                 const AssetImportToolVersionDependency& right) {
                                  if (left.toolName != right.toolName) {
                                      return left.toolName < right.toolName;
                                  }
                                  return left.versionHash < right.versionHash;
                              });

            dependencies.reserve(dependencies.size() + matchingToolVersions.size());
            for (const AssetImportToolVersionDependency& toolVersion : matchingToolVersions) {
                dependencies.push_back(AssetDependency{
                    .owner = source.guid,
                    .kind = AssetDependencyKind::ToolVersion,
                    .path = toolVersion.toolName,
                    .hash = toolVersion.versionHash,
                });
            }

            return dependencies;
        }

        template <typename Key, typename Compare>
        [[nodiscard]] std::span<const std::size_t>
        laterDuplicateIndices(const std::map<Key, std::vector<std::size_t>, Compare>& groups,
                              const Key& key, std::size_t index) {
            const auto group = groups.find(key);
            if (group == groups.end()) {
                return {};
            }
            const auto firstLater = std::ranges::upper_bound(group->second, index);
            return std::span<const std::size_t>{firstLater, group->second.end()};
        }

        [[nodiscard]] bool validatePlanSources(AssetImportPlanResult& result,
                                               std::span<const DiscoveredSourceAsset> sources) {
            std::map<GuidBytes, std::vector<std::size_t>> indicesByGuid;
            std::map<std::string, std::vector<std::size_t>, std::less<>> indicesBySourcePath;
            for (std::size_t index = 0; index < sources.size(); ++index) {
                indicesByGuid[sources[index].source.guid.bytes].push_back(index);
                indicesBySourcePath[sources[index].source.sourcePath].push_back(index);
            }

            bool valid = true;
            for (std::size_t index = 0; index < sources.size(); ++index) {
                const DiscoveredSourceAsset& source = sources[index];
                if (auto sourceValid = validateSourceAssetRecord(source.source); !sourceValid) {
                    addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidSource,
                                  source.source.sourcePath,
                                  "Asset import planning rejected source[" + std::to_string(index) +
                                      "] " + sourceLabel(source.source) + ": " +
                                      sourceValid.error().message);
                    valid = false;
                    continue;
                }

                const std::uint64_t computedSettingsHash = hashAssetImportSettings(source.settings);
                if (computedSettingsHash != source.source.settingsHash) {
                    addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidSource,
                                  source.source.sourcePath,
                                  "Asset import planning source[" + std::to_string(index) + "] " +
                                      sourceLabel(source.source) +
                                      " has a settings hash mismatch.");
                    valid = false;
                }

                const std::span<const std::size_t> sameGuid =
                    laterDuplicateIndices(indicesByGuid, source.source.guid.bytes, index);
                const std::span<const std::size_t> samePath =
                    laterDuplicateIndices(indicesBySourcePath, source.source.sourcePath, index);
                std::size_t guidOffset = 0U;
                std::size_t pathOffset = 0U;
                while (guidOffset < sameGuid.size() || pathOffset < samePath.size()) {
                    const std::size_t guidIndex =
                        guidOffset < sameGuid.size() ? sameGuid[guidOffset] : sources.size();
                    const std::size_t pathIndex =
                        pathOffset < samePath.size() ? samePath[pathOffset] : sources.size();
                    const std::size_t otherIndex = (std::min)(guidIndex, pathIndex);
                    const DiscoveredSourceAsset& other = sources[otherIndex];
                    if (guidIndex == otherIndex) {
                        addDiagnostic(result, AssetImportPlanDiagnosticCode::DuplicateSource,
                                      source.source.sourcePath,
                                      "Asset import planning source[" + std::to_string(index) +
                                          "] " + sourceLabel(source.source) +
                                          " duplicates source[" + std::to_string(otherIndex) +
                                          "] guid=\"" + formatAssetGuid(other.source.guid) + "\".");
                        valid = false;
                        ++guidOffset;
                    }

                    if (pathIndex == otherIndex) {
                        addDiagnostic(result, AssetImportPlanDiagnosticCode::DuplicateSource,
                                      source.source.sourcePath,
                                      "Asset import planning source[" + std::to_string(index) +
                                          "] " + sourceLabel(source.source) +
                                          " duplicates source path with source[" +
                                          std::to_string(otherIndex) + "].");
                        valid = false;
                        ++pathOffset;
                    }
                }
            }

            return valid;
        }

        [[nodiscard]] bool validatePlanSnapshots(AssetImportPlanResult& result,
                                                 std::span<const AssetSourceSnapshot> snapshots) {
            std::map<std::string, std::vector<std::size_t>, std::less<>> indicesBySourcePath;
            for (std::size_t index = 0; index < snapshots.size(); ++index) {
                indicesBySourcePath[snapshots[index].sourcePath].push_back(index);
            }

            bool valid = true;
            for (std::size_t index = 0; index < snapshots.size(); ++index) {
                const AssetSourceSnapshot& snapshot = snapshots[index];
                if (auto sourcePathValid = validateAssetSourcePath(snapshot.sourcePath);
                    !sourcePathValid) {
                    addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidSourceSnapshot,
                                  snapshot.sourcePath,
                                  "Asset import planning rejected snapshot[" +
                                      std::to_string(index) +
                                      "]: " + sourcePathValid.error().message);
                    valid = false;
                }

                if (snapshot.sourceHash == 0) {
                    addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidSourceSnapshot,
                                  snapshot.sourcePath,
                                  "Asset import planning snapshot[" + std::to_string(index) +
                                      "] is missing a source hash.");
                    valid = false;
                }

                const std::span<const std::size_t> duplicates =
                    laterDuplicateIndices(indicesBySourcePath, snapshot.sourcePath, index);
                for (const std::size_t otherIndex : duplicates) {
                    addDiagnostic(result, AssetImportPlanDiagnosticCode::DuplicateSourceSnapshot,
                                  snapshot.sourcePath,
                                  "Asset import planning snapshot[" + std::to_string(index) +
                                      "] duplicates source path with snapshot[" +
                                      std::to_string(otherIndex) + "].");
                    valid = false;
                }
            }

            return valid;
        }

        [[nodiscard]] bool
        validateProductDependencies(AssetImportPlanResult& result,
                                    std::span<const AssetDependency> declarations) {
            if (declarations.size() > 4096U) {
                addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidProductDependency, {},
                              "Declared product dependencies exceed the limit of 4096.");
                return false;
            }
            std::set<std::pair<std::array<std::uint8_t, 16>, std::string>> dependencyIds;
            for (const auto& dependency : declarations) {
                if (!dependency.owner || dependency.kind != AssetDependencyKind::AssetReference ||
                    dependency.hash == 0U || !validateAssetProductPath(dependency.path) ||
                    !dependencyIds.emplace(dependency.owner.bytes, dependency.path).second) {
                    addDiagnostic(
                        result, AssetImportPlanDiagnosticCode::InvalidProductDependency,
                        dependency.path,
                        "Product dependencies require a nonzero owner/hash, a valid product "
                        "path, AssetReference kind and unique owner/path pairs.");
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool appendProductDependencies(AssetImportPlanResult& result,
                                                     const DiscoveredSourceAsset& discovered,
                                                     std::span<const AssetDependency> declarations,
                                                     std::vector<AssetDependency>& dependencies) {
            for (const auto& dependency : declarations) {
                if (dependency.owner == discovered.source.guid) {
                    dependencies.push_back(dependency);
                }
            }
            if (isShaderCompileReflectionSource(discovered.source)) {
                const auto setting = std::ranges::find(
                    discovered.settings, "shader.authoringProductPath", &AssetImportSetting::key);
                if (setting != discovered.settings.end() &&
                    !std::ranges::any_of(dependencies, [&](const auto& dependency) {
                        return dependency.kind == AssetDependencyKind::AssetReference &&
                               dependency.path == setting->value;
                    })) {
                    addDiagnostic(
                        result, AssetImportPlanDiagnosticCode::UnresolvedProductDependency,
                        discovered.source.sourcePath,
                        "Shader authoring product dependency was not declared: " + setting->value,
                        AssetImportPlanDiagnosticSeverity::Warning);
                    return false;
                }
            }
            return true;
        }

        void populatePlanIndex(std::span<const AssetSourceSnapshot> snapshots,
                               std::span<const AssetProductRecord> products,
                               SnapshotsBySourcePath& snapshotsBySourcePath,
                               ProductsByKey& productsByKey, ProductsByGuid& productsByGuid) {
            for (std::size_t snapshotIndex = 0U; snapshotIndex < snapshots.size();
                 ++snapshotIndex) {
                snapshotsBySourcePath.emplace(snapshots[snapshotIndex].sourcePath, snapshotIndex);
            }
            for (std::size_t productIndex = 0U; productIndex < products.size(); ++productIndex) {
                const AssetProductRecord& product = products[productIndex];
                productsByKey.emplace(product.key, productIndex);
                productsByGuid[product.key.guid.bytes].push_back(&product);
            }
        }

    } // namespace

    std::string makeAssetImportProductPath(const AssetProductKey& productKey,
                                           std::string_view targetProfile) {
        return std::string{targetProfile} + "/products/" + formatAssetGuid(productKey.guid) + "/" +
               formatHash64(hashAssetProductKey(productKey)) + ".aproduct";
    }

    AssetImportPlanResult planAssetImports(std::span<const DiscoveredSourceAsset> sources,
                                           std::span<const AssetSourceSnapshot> snapshots,
                                           const AssetProductManifestDocument& productManifest,
                                           std::string_view targetProfile,
                                           const AssetImportPlanOptions& options) {
        AssetImportPlanResult result{
            .targetProfile = std::string{targetProfile},
            .targetProfileHash = makeAssetTargetProfileHash(targetProfile),
            .requests = {},
            .cacheHits = {},
            .diagnostics = {},
        };

        std::string invalidTargetReason;
        if (!isValidTargetProfile(targetProfile, invalidTargetReason)) {
            addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidTargetProfile, {},
                          "Asset import planning rejected target profile '" +
                              std::string{targetProfile} + "': " + invalidTargetReason + ".");
            return result;
        }

        if (auto validManifest = validateAssetProductManifestDocument(productManifest);
            !validManifest) {
            addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidProductManifest, {},
                          "Asset import planning rejected product manifest: " +
                              validManifest.error().message);
            return result;
        }

        const bool validSources = validatePlanSources(result, sources);
        const bool validSnapshots = validatePlanSnapshots(result, snapshots);
        if (!validSources || !validSnapshots) {
            return result;
        }
        if (!validateProductDependencies(result, options.productDependencies)) {
            return result;
        }
        SnapshotsBySourcePath snapshotsBySourcePath;
        ProductsByKey productsByKey;
        ProductsByGuid productsByGuid;
        populatePlanIndex(snapshots, productManifest.products, snapshotsBySourcePath, productsByKey,
                          productsByGuid);

        std::vector<std::size_t> orderedSourceIndices;
        orderedSourceIndices.reserve(sources.size());
        for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
            orderedSourceIndices.push_back(sourceIndex);
        }
        std::ranges::sort(orderedSourceIndices,
                          [sources](std::size_t leftIndex, std::size_t rightIndex) {
                              return sourceIndexOrdersBefore(sources, leftIndex, rightIndex);
                          });

        ToolFingerprintCache fingerprintCache;

        for (std::size_t sourceIndex : orderedSourceIndices) {
            const DiscoveredSourceAsset& discovered = sources[sourceIndex];
            const auto snapshotIndex = snapshotsBySourcePath.find(discovered.source.sourcePath);
            if (snapshotIndex == snapshotsBySourcePath.end()) {
                addDiagnostic(result, AssetImportPlanDiagnosticCode::MissingSourceSnapshot,
                              discovered.source.sourcePath,
                              "Asset import planning source " + sourceLabel(discovered.source) +
                                  " is missing a source snapshot.");
                continue;
            }

            SourceAssetRecord plannedSource = discovered.source;
            plannedSource.sourceHash = snapshots[snapshotIndex->second].sourceHash;
            plannedSource.settingsHash = hashAssetImportSettings(discovered.settings);
            if (discovered.source.sourceHash != plannedSource.sourceHash) {
                addDiagnostic(result, AssetImportPlanDiagnosticCode::MetadataSourceHashDrift,
                              plannedSource.sourcePath,
                              "Asset import planning source " + sourceLabel(plannedSource) +
                                  " has metadata sourceHash=\"" +
                                  formatHash64(discovered.source.sourceHash) +
                                  "\" but current snapshot sourceHash=\"" +
                                  formatHash64(plannedSource.sourceHash) +
                                  "\". Planning will use the current snapshot hash for product key "
                                  "freshness.",
                              AssetImportPlanDiagnosticSeverity::Warning);
            }

            const std::vector<std::string> unresolvedTools = unresolvedImplicitToolDependencies(
                plannedSource, options.toolVersions, options.toolDependencyPolicy);
            if (!unresolvedTools.empty()) {
                addDiagnostic(result, AssetImportPlanDiagnosticCode::UnresolvedToolDependency,
                              plannedSource.sourcePath,
                              "Asset import planning cannot prove a product key for " +
                                  sourceLabel(plannedSource) +
                                  " because declared-only tool dependencies " +
                                  unresolvedToolList(unresolvedTools) +
                                  " were not provided. No environment tools were resolved.",
                              AssetImportPlanDiagnosticSeverity::Warning);
                continue;
            }

            Result<std::vector<AssetDependency>> dependencyResult = makeImportDependencies(
                plannedSource, options.toolVersions, options.toolFingerprintResolver,
                options.toolDependencyPolicy, fingerprintCache);
            if (!dependencyResult) {
                addDiagnostic(result, AssetImportPlanDiagnosticCode::ToolFingerprintFailed,
                              plannedSource.sourcePath,
                              "Asset import planning could not fingerprint a required tool for " +
                                  sourceLabel(plannedSource) + ": " +
                                  dependencyResult.error().message);
                continue;
            }
            std::vector<AssetDependency> dependencies = std::move(*dependencyResult);
            if (!appendProductDependencies(result, discovered, options.productDependencies,
                                           dependencies)) {
                continue;
            }
            const std::uint64_t dependencyHash = hashAssetDependencies(dependencies);
            AssetProductKey productKey =
                makeAssetProductKey(plannedSource, dependencyHash, result.targetProfileHash);
            const std::string productPath = makeAssetImportProductPath(productKey, targetProfile);
            if (auto validPath = validateAssetProductPath(productPath); !validPath) {
                addDiagnostic(result, AssetImportPlanDiagnosticCode::InvalidProductManifest,
                              plannedSource.sourcePath,
                              "Asset import planning generated invalid product path for " +
                                  sourceLabel(plannedSource) + ": " + validPath.error().message);
                continue;
            }

            const auto productIndex = productsByKey.find(productKey);
            if (productIndex != productsByKey.end()) {
                result.cacheHits.push_back(AssetImportCacheHit{
                    .source = std::move(plannedSource),
                    .dependencies = std::move(dependencies),
                    .product = productManifest.products[productIndex->second],
                });
                continue;
            }

            result.requests.push_back(AssetImportRequest{
                .source = std::move(plannedSource),
                .settings = discovered.settings,
                .dependencies = std::move(dependencies),
                .productKey = productKey,
                .relativeProductPath = productPath,
                .reason =
                    [&productsByGuid, &productKey]() {
                        const auto products = productsByGuid.find(productKey.guid.bytes);
                        const std::span<const AssetProductRecord* const> matching =
                            products == productsByGuid.end()
                                ? std::span<const AssetProductRecord* const>{}
                                : std::span<const AssetProductRecord* const>{products->second};
                        return classifyMissReason(matching, productKey);
                    }(),
            });
        }

        return result;
    }

} // namespace asharia::asset
