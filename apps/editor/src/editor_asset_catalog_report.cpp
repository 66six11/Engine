#include "editor_asset_catalog_report.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/asset_core/asset_catalog_view.hpp"

#include "editor_asset_icon.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] asharia::archive::ArchiveMember member(std::string key,
                                                             asharia::archive::ArchiveValue value) {
            return asharia::archive::ArchiveMember{.key = std::move(key),
                                                   .value = std::move(value)};
        }

        [[nodiscard]] asharia::archive::ArchiveValue stringValue(std::string value) {
            return asharia::archive::ArchiveValue::string(std::move(value));
        }

        [[nodiscard]] asharia::archive::ArchiveValue stringValue(std::string_view value) {
            return asharia::archive::ArchiveValue::string(std::string{value});
        }

        [[nodiscard]] asharia::archive::ArchiveValue stringValue(const char* value) {
            return asharia::archive::ArchiveValue::string(value == nullptr ? std::string{}
                                                                           : std::string{value});
        }

        [[nodiscard]] std::string pathUtf8String(const std::filesystem::path& path) {
            const std::u8string value = path.generic_u8string();
            return std::string{value.begin(), value.end()};
        }

        [[nodiscard]] asharia::archive::ArchiveValue pathValue(const std::filesystem::path& path) {
            return stringValue(pathUtf8String(path));
        }

        [[nodiscard]] std::string_view assetDiagnosticSeverityName(
            asharia::asset::AssetCatalogDiagnosticSeverity severity) noexcept {
            switch (severity) {
            case asharia::asset::AssetCatalogDiagnosticSeverity::Info:
                return "info";
            case asharia::asset::AssetCatalogDiagnosticSeverity::Warning:
                return "warning";
            case asharia::asset::AssetCatalogDiagnosticSeverity::Error:
                return "error";
            }
            return "info";
        }

        [[nodiscard]] asharia::archive::ArchiveValue countValue(std::size_t value) {
            return asharia::archive::ArchiveValue::integer(static_cast<std::int64_t>(value));
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        diagnosticValue(const EditorAssetCatalogDiagnostic& diagnostic) {
            return asharia::archive::ArchiveValue::object({
                member("severity",
                       stringValue(editorAssetCatalogDiagnosticSeverityName(diagnostic.severity))),
                member("code", stringValue(editorAssetCatalogDiagnosticCodeName(diagnostic.code))),
                member("sourcePath", stringValue(diagnostic.sourcePath)),
                member("path", pathValue(diagnostic.path)),
                member("message", stringValue(diagnostic.message)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        rowDiagnosticValue(const asharia::asset::AssetCatalogDiagnostic& diagnostic) {
            return asharia::archive::ArchiveValue::object({
                member("severity", stringValue(assetDiagnosticSeverityName(diagnostic.severity))),
                member("code", stringValue(asharia::asset::assetCatalogDiagnosticCodeName(
                                   diagnostic.code))),
                member("sourcePath", stringValue(diagnostic.sourcePath)),
                member("message", stringValue(diagnostic.message)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        subAssetValue(const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset) {
            return asharia::archive::ArchiveValue::object({
                member("stableId", stringValue(subAsset.stableId)),
                member("displayName", stringValue(subAsset.displayName)),
                member("assetRole", stringValue(subAsset.assetRoleName)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue subAssetsValue(
            const std::vector<asharia::asset::AssetCatalogSubAssetViewEntry>& subAssets) {
            std::vector<asharia::archive::ArchiveValue> values;
            values.reserve(subAssets.size());
            for (const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset : subAssets) {
                values.push_back(subAssetValue(subAsset));
            }
            return asharia::archive::ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] asharia::archive::ArchiveValue rowDiagnosticsValue(
            const std::vector<asharia::asset::AssetCatalogDiagnostic>& diagnostics) {
            std::vector<asharia::archive::ArchiveValue> values;
            values.reserve(diagnostics.size());
            for (const asharia::asset::AssetCatalogDiagnostic& diagnostic : diagnostics) {
                values.push_back(rowDiagnosticValue(diagnostic));
            }
            return asharia::archive::ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] asharia::archive::ArchiveValue iconTintValue(const EditorIconTint& tint) {
            return asharia::archive::ArchiveValue::object({
                member("red", asharia::archive::ArchiveValue::floating(tint.red)),
                member("green", asharia::archive::ArchiveValue::floating(tint.green)),
                member("blue", asharia::archive::ArchiveValue::floating(tint.blue)),
                member("alpha", asharia::archive::ArchiveValue::floating(tint.alpha)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        iconDescriptorValue(const EditorIconDescriptor& descriptor) {
            return asharia::archive::ArchiveValue::object({
                member("id", stringValue(descriptor.id.value)),
                member("tooltipKey", stringValue(descriptor.tooltipKey)),
                member("tooltipFallback", stringValue(descriptor.tooltipFallback)),
                member("tint", iconTintValue(descriptor.tint)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        sourceRootValue(const EditorAssetCatalogResolvedSourceRoot& root) {
            return asharia::archive::ArchiveValue::object({
                member("name", stringValue(root.rootName)),
                member("sourcePathPrefix", stringValue(root.sourcePathPrefix)),
                member("directory", pathValue(root.directory)),
                member("resolvedDirectory", pathValue(root.resolvedDirectory)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        sourceRootsValue(const EditorAssetCatalogSnapshot& snapshot) {
            const std::vector<EditorAssetCatalogResolvedSourceRoot> roots =
                resolveEditorAssetCatalogSourceRoots(snapshot);
            std::vector<asharia::archive::ArchiveValue> values;
            values.reserve(roots.size());
            for (const EditorAssetCatalogResolvedSourceRoot& root : roots) {
                values.push_back(sourceRootValue(root));
            }
            return asharia::archive::ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] EditorAssetIconQuery
        navigationNodeIconQuery(const EditorAssetCatalogNavigationNode& node) {
            const bool folderNode = node.kind == EditorAssetCatalogNavigationNodeKind::SourceRoot ||
                                    node.kind == EditorAssetCatalogNavigationNodeKind::Folder;
            return EditorAssetIconQuery{
                .folder = folderNode,
                .assetType = node.assetTypeName,
                .importerId = node.importerName,
                .extension = node.extension,
                .diagnostic = editorAssetIconDiagnosticStateForProductState(node.productState),
                .sourcePath = node.sourcePath.empty() ? node.scopePath : node.sourcePath,
                .displayName = node.displayName,
                .guidText = node.guidText,
                .importProfile = node.importProfileName,
                .assetRole = node.assetRoleName,
                .subAssetCount = node.subAssetCount,
            };
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        navigationNodeValue(const EditorAssetCatalogNavigationNode& node,
                            const EditorAssetIconRegistry& iconRegistry) {
            const EditorIconDescriptor resolvedIcon =
                iconRegistry.resolveAssetIcon(navigationNodeIconQuery(node));
            return asharia::archive::ArchiveValue::object({
                member("kind", stringValue(editorAssetCatalogNavigationNodeKindName(node.kind))),
                member("key", stringValue(node.key)),
                member("parentKey", stringValue(node.parentKey)),
                member("displayName", stringValue(node.displayName)),
                member("scopePath", stringValue(node.scopePath)),
                member("sourcePath", stringValue(node.sourcePath)),
                member("sourceRootName", stringValue(node.sourceRootName)),
                member("sourceRootPrefix", stringValue(node.sourceRootPrefix)),
                member("sourceRootDirectory", pathValue(node.sourceRootDirectory)),
                member("guid", stringValue(node.guidText)),
                member("stableId", stringValue(node.stableId)),
                member("assetType", stringValue(node.assetTypeName)),
                member("importer", stringValue(node.importerName)),
                member("extension", stringValue(node.extension)),
                member("importProfile", stringValue(node.importProfileName)),
                member("assetRole", stringValue(node.assetRoleName)),
                member("subAssetCount", countValue(node.subAssetCount)),
                member("productState", stringValue(asharia::asset::assetCatalogProductStateName(
                                           node.productState))),
                member("resolvedIcon", iconDescriptorValue(resolvedIcon)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        navigationNodesValue(const EditorAssetCatalogSnapshot& snapshot,
                             const EditorAssetIconRegistry& iconRegistry) {
            const std::vector<EditorAssetCatalogNavigationNode> nodes =
                makeEditorAssetCatalogNavigationNodes(snapshot);
            std::vector<asharia::archive::ArchiveValue> values;
            values.reserve(nodes.size());
            for (const EditorAssetCatalogNavigationNode& node : nodes) {
                values.push_back(navigationNodeValue(node, iconRegistry));
            }
            return asharia::archive::ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        rowValue(const EditorAssetCatalogSnapshot& snapshot,
                 const asharia::asset::AssetCatalogViewEntry& entry,
                 const EditorAssetIconRegistry& iconRegistry) {
            const EditorIconDescriptor resolvedIcon =
                iconRegistry.resolveAssetIcon(makeEditorAssetIconQuery(entry));
            const EditorAssetCatalogResolvedSourceRoot sourceRoot =
                resolveEditorAssetCatalogSourceRootForSourcePath(snapshot, entry.sourcePath);
            const std::filesystem::path sourceFilePath =
                resolveEditorAssetCatalogSourceFilePath(snapshot, entry.sourcePath);
            const std::filesystem::path metadataFilePath =
                resolveEditorAssetCatalogMetadataFilePath(snapshot, entry.sourcePath);
            return asharia::archive::ArchiveValue::object({
                member("guid", stringValue(entry.guidText)),
                member("sourcePath", stringValue(entry.sourcePath)),
                member("sourceRootName", stringValue(sourceRoot.rootName)),
                member("sourceRootPrefix", stringValue(sourceRoot.sourcePathPrefix)),
                member("sourceRootDirectory", pathValue(sourceRoot.resolvedDirectory)),
                member("sourceFilePath", pathValue(sourceFilePath)),
                member("metadataFilePath", pathValue(metadataFilePath)),
                member("displayName", stringValue(entry.displayName)),
                member("extension", stringValue(entry.extension)),
                member("assetType", stringValue(entry.assetTypeName)),
                member("importer", stringValue(entry.importerName)),
                member("importerVersion",
                       asharia::archive::ArchiveValue::integer(entry.importerVersion.value)),
                member("importProfile", stringValue(entry.importProfileName)),
                member("assetRole", stringValue(entry.assetRoleName)),
                member("resolvedIcon", iconDescriptorValue(resolvedIcon)),
                member("productState", stringValue(asharia::asset::assetCatalogProductStateName(
                                           entry.productState))),
                member("currentProductCount", countValue(entry.currentProductCount)),
                member("staleProductCount", countValue(entry.staleProductCount)),
                member("subAssets", subAssetsValue(entry.subAssets)),
                member("diagnostics", rowDiagnosticsValue(entry.diagnostics)),
            });
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        rowsValue(const EditorAssetCatalogSnapshot& snapshot,
                  const EditorAssetIconRegistry& iconRegistry) {
            std::vector<asharia::archive::ArchiveValue> values;
            values.reserve(snapshot.catalogView.entries.size());
            for (const asharia::asset::AssetCatalogViewEntry& entry :
                 snapshot.catalogView.entries) {
                values.push_back(rowValue(snapshot, entry, iconRegistry));
            }
            return asharia::archive::ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] asharia::archive::ArchiveValue
        diagnosticsValue(const std::vector<EditorAssetCatalogDiagnostic>& diagnostics) {
            std::vector<asharia::archive::ArchiveValue> values;
            values.reserve(diagnostics.size());
            for (const EditorAssetCatalogDiagnostic& diagnostic : diagnostics) {
                values.push_back(diagnosticValue(diagnostic));
            }
            return asharia::archive::ArchiveValue::array(std::move(values));
        }

    } // namespace

    std::string
    writeEditorAssetCatalogSnapshotTextReport(const EditorAssetCatalogSnapshotRequest& request,
                                              const EditorAssetCatalogSnapshot& snapshot) {
        std::ostringstream output;
        const std::string_view targetProfile = snapshot.targetProfile.empty()
                                                   ? std::string_view{request.targetProfile}
                                                   : std::string_view{snapshot.targetProfile};
        output << "project=" << pathUtf8String(snapshot.projectFile) << '\n'
               << "productManifest=" << pathUtf8String(snapshot.productManifestFile) << '\n'
               << "targetProfile=" << targetProfile << '\n'
               << "sourceRoots=" << snapshot.project.assetSourceRoots.size() << '\n'
               << "navigationNodes=" << makeEditorAssetCatalogNavigationNodes(snapshot).size()
               << '\n'
               << "rows=" << snapshot.catalogView.entries.size() << '\n'
               << "diagnostics=" << snapshot.diagnostics.size() << '\n';
        for (const EditorAssetCatalogResolvedSourceRoot& root :
             resolveEditorAssetCatalogSourceRoots(snapshot)) {
            output << "source-root"
                   << " name=" << std::quoted(root.rootName)
                   << " prefix=" << std::quoted(root.sourcePathPrefix)
                   << " directory=" << std::quoted(pathUtf8String(root.directory))
                   << " resolvedDirectory=" << std::quoted(pathUtf8String(root.resolvedDirectory))
                   << '\n';
        }
        for (const EditorAssetCatalogNavigationNode& node :
             makeEditorAssetCatalogNavigationNodes(snapshot)) {
            output << "navigation-node"
                   << " kind=" << std::quoted(editorAssetCatalogNavigationNodeKindName(node.kind))
                   << " key=" << std::quoted(node.key) << " parent=" << std::quoted(node.parentKey)
                   << " displayName=" << std::quoted(node.displayName)
                   << " scopePath=" << std::quoted(node.scopePath)
                   << " sourcePath=" << std::quoted(node.sourcePath)
                   << " stableId=" << std::quoted(node.stableId) << '\n';
        }
        for (const asharia::asset::AssetCatalogViewEntry& entry : snapshot.catalogView.entries) {
            const EditorAssetCatalogResolvedSourceRoot sourceRoot =
                resolveEditorAssetCatalogSourceRootForSourcePath(snapshot, entry.sourcePath);
            output << "row"
                   << " sourcePath=" << std::quoted(entry.sourcePath)
                   << " sourceRoot=" << std::quoted(sourceRoot.rootName)
                   << " sourceRootPrefix=" << std::quoted(sourceRoot.sourcePathPrefix)
                   << " sourceFile="
                   << std::quoted(pathUtf8String(
                          resolveEditorAssetCatalogSourceFilePath(snapshot, entry.sourcePath)))
                   << " metadataFile="
                   << std::quoted(pathUtf8String(
                          resolveEditorAssetCatalogMetadataFilePath(snapshot, entry.sourcePath)))
                   << " displayName=" << std::quoted(entry.displayName)
                   << " type=" << std::quoted(entry.assetTypeName)
                   << " importer=" << std::quoted(entry.importerName)
                   << " profile=" << std::quoted(entry.importProfileName)
                   << " role=" << std::quoted(entry.assetRoleName) << " productState="
                   << asharia::asset::assetCatalogProductStateName(entry.productState)
                   << " currentProducts=" << entry.currentProductCount
                   << " staleProducts=" << entry.staleProductCount
                   << " subAssets=" << entry.subAssets.size() << '\n';
            for (const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset : entry.subAssets) {
                output << "sub-asset"
                       << " sourcePath=" << std::quoted(entry.sourcePath)
                       << " stableId=" << std::quoted(subAsset.stableId)
                       << " displayName=" << std::quoted(subAsset.displayName)
                       << " role=" << std::quoted(subAsset.assetRoleName) << '\n';
            }
        }
        for (const EditorAssetCatalogDiagnostic& diagnostic : snapshot.diagnostics) {
            output << editorAssetCatalogDiagnosticSeverityName(diagnostic.severity) << ' '
                   << editorAssetCatalogDiagnosticCodeName(diagnostic.code);
            if (!diagnostic.sourcePath.empty()) {
                output << " sourcePath=" << diagnostic.sourcePath;
            }
            if (!diagnostic.path.empty()) {
                output << " path=" << pathUtf8String(diagnostic.path);
            }
            output << " message=" << std::quoted(diagnostic.message) << '\n';
        }
        return output.str();
    }

    Result<std::string>
    writeEditorAssetCatalogSnapshotJsonReport(const EditorAssetCatalogSnapshotRequest& request,
                                              const EditorAssetCatalogSnapshot& snapshot) {
        const EditorAssetIconRegistry iconRegistry;
        return writeEditorAssetCatalogSnapshotJsonReport(request, snapshot, iconRegistry);
    }

    Result<std::string>
    writeEditorAssetCatalogSnapshotJsonReport(const EditorAssetCatalogSnapshotRequest& request,
                                              const EditorAssetCatalogSnapshot& snapshot,
                                              const EditorAssetIconRegistry& iconRegistry) {
        const std::string_view targetProfile = snapshot.targetProfile.empty()
                                                   ? std::string_view{request.targetProfile}
                                                   : std::string_view{snapshot.targetProfile};
        const asharia::archive::ArchiveValue report = asharia::archive::ArchiveValue::object({
            member("schema", stringValue("com.asharia.editor.assetCatalogCheck")),
            member("schemaVersion", asharia::archive::ArchiveValue::integer(1)),
            member("projectFile", pathValue(snapshot.projectFile)),
            member("productManifestFile", pathValue(snapshot.productManifestFile)),
            member("targetProfile", stringValue(targetProfile)),
            member("succeeded", asharia::archive::ArchiveValue::boolean(snapshot.succeeded())),
            member("sourceRoots", sourceRootsValue(snapshot)),
            member("navigationNodes", navigationNodesValue(snapshot, iconRegistry)),
            member("rowCount", countValue(snapshot.catalogView.entries.size())),
            member("diagnosticCount", countValue(snapshot.diagnostics.size())),
            member("rows", rowsValue(snapshot, iconRegistry)),
            member("diagnostics", diagnosticsValue(snapshot.diagnostics)),
        });
        return asharia::archive::writeJsonArchive(report);
    }

} // namespace asharia::editor
