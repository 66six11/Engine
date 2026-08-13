#include "asset_catalog_snapshot_json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>

#include "asharia/asset_core/asset_catalog_view.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] Error jsonError(std::string message) {
            return Error{ErrorDomain::Asset, 0, std::move(message)};
        }

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string value = path.generic_u8string();
            return std::string{value.begin(), value.end()};
        }

        struct BoundedJsonWriterLimits {
            std::size_t maxStringBytes{};
            std::size_t maxResponseBytes{};
        };

        struct JsonMemberName {
            std::string_view value;
        };

        class BoundedJsonWriter {
        public:
            explicit BoundedJsonWriter(BoundedJsonWriterLimits limits)
                : maxStringBytes_(limits.maxStringBytes),
                  maxResponseBytes_(limits.maxResponseBytes) {
                constexpr std::size_t kInitialCapacity = std::size_t{64U} * std::size_t{1'024U};
                output_.reserve((std::min)(maxResponseBytes_, kInitialCapacity));
            }

            void beginObject() {
                append('{');
            }

            void endObject() {
                append('}');
            }

            void beginArray() {
                append('[');
            }

            void endArray() {
                append(']');
            }

            void member(std::string_view name, bool& first) {
                separator(first);
                string(name);
                append(':');
            }

            void element(bool& first) {
                separator(first);
            }

            void string(std::string_view value) {
                if (value.size() > maxStringBytes_) {
                    stringExceeded_ = true;
                    return;
                }
                append('"');
                constexpr std::string_view kHex = "0123456789abcdef";
                for (const unsigned char byte : value) {
                    switch (byte) {
                    case '"':
                        append("\\\"");
                        break;
                    case '\\':
                        append("\\\\");
                        break;
                    case '\b':
                        append("\\b");
                        break;
                    case '\f':
                        append("\\f");
                        break;
                    case '\n':
                        append("\\n");
                        break;
                    case '\r':
                        append("\\r");
                        break;
                    case '\t':
                        append("\\t");
                        break;
                    default:
                        if (byte < 0x20U) {
                            append("\\u00");
                            append(kHex[(byte >> 4U) & 0xFU]);
                            append(kHex[byte & 0xFU]);
                        } else {
                            append(static_cast<char>(byte));
                        }
                        break;
                    }
                }
                append('"');
            }

            void path(const std::filesystem::path& value) {
                string(pathText(value));
            }

            void integer(std::size_t value) {
                std::array<char, 32U> buffer{};
                char* const end =
                    std::next(buffer.data(), static_cast<std::ptrdiff_t>(buffer.size()));
                const auto converted = std::to_chars(buffer.data(), end, value);
                if (converted.ec != std::errc{}) {
                    responseExceeded_ = true;
                    return;
                }
                append(std::string_view{buffer.data(), static_cast<std::size_t>(std::distance(
                                                           buffer.data(), converted.ptr))});
            }

            [[nodiscard]] Result<std::string> finish() {
                if (stringExceeded_) {
                    return std::unexpected{
                        jsonError("Editor asset catalog JSON contains a string exceeding its "
                                  "UTF-8 byte limit.")};
                }
                if (responseExceeded_) {
                    return std::unexpected{
                        jsonError("Editor asset catalog JSON exceeds the response byte limit.")};
                }
                if (output_.size() >= maxResponseBytes_) {
                    return std::unexpected{
                        jsonError("Editor asset catalog JSON exceeds the response byte limit.")};
                }
                output_.push_back('\n');
                return std::move(output_);
            }

        private:
            void separator(bool& first) {
                if (first) {
                    first = false;
                } else {
                    append(',');
                }
            }

            void append(char value) {
                if (responseExceeded_) {
                    return;
                }
                if (output_.size() >= maxResponseBytes_) {
                    responseExceeded_ = true;
                    return;
                }
                output_.push_back(value);
            }

            void append(std::string_view value) {
                if (responseExceeded_) {
                    return;
                }
                if (value.size() > maxResponseBytes_ - output_.size()) {
                    responseExceeded_ = true;
                    return;
                }
                output_.append(value);
            }

            std::size_t maxStringBytes_{};
            std::size_t maxResponseBytes_{};
            std::string output_;
            bool stringExceeded_{false};
            bool responseExceeded_{false};
        };

        [[nodiscard]] std::string_view
        severityName(EditorAssetCatalogDiagnosticSeverity severity) noexcept {
            switch (severity) {
            case EditorAssetCatalogDiagnosticSeverity::Info:
                return "info";
            case EditorAssetCatalogDiagnosticSeverity::Warning:
                return "warning";
            case EditorAssetCatalogDiagnosticSeverity::Error:
                return "error";
            }
            return "error";
        }

        [[nodiscard]] std::string_view
        rowSeverityName(asharia::asset::AssetCatalogDiagnosticSeverity severity) noexcept {
            switch (severity) {
            case asharia::asset::AssetCatalogDiagnosticSeverity::Info:
                return "info";
            case asharia::asset::AssetCatalogDiagnosticSeverity::Warning:
                return "warning";
            case asharia::asset::AssetCatalogDiagnosticSeverity::Error:
                return "error";
            }
            return "error";
        }

        [[nodiscard]] std::string_view
        productStateName(asharia::asset::AssetCatalogProductState state) noexcept {
            switch (state) {
            case asharia::asset::AssetCatalogProductState::NotTracked:
                return "not-tracked";
            case asharia::asset::AssetCatalogProductState::Ready:
                return "ready";
            case asharia::asset::AssetCatalogProductState::MissingProduct:
                return "missing-product";
            case asharia::asset::AssetCatalogProductState::StaleProduct:
                return "stale-product";
            case asharia::asset::AssetCatalogProductState::InvalidProduct:
                return "invalid-product";
            }
            return "invalid-product";
        }

        [[nodiscard]] std::string_view
        rowDiagnosticCodeName(asharia::asset::AssetCatalogDiagnosticCode code) noexcept {
            switch (code) {
            case asharia::asset::AssetCatalogDiagnosticCode::MissingProduct:
                return "missing-product";
            case asharia::asset::AssetCatalogDiagnosticCode::StaleProduct:
                return "stale-product";
            case asharia::asset::AssetCatalogDiagnosticCode::InvalidProductRecord:
                return "invalid-product-record";
            case asharia::asset::AssetCatalogDiagnosticCode::SourceMetadata:
                return "source-metadata";
            }
            return "source-metadata";
        }

        [[nodiscard]] std::string_view
        snapshotState(const EditorAssetCatalogSnapshot& snapshot) noexcept {
            bool warning = false;
            for (const EditorAssetCatalogDiagnostic& value : snapshot.diagnostics) {
                if (value.severity == EditorAssetCatalogDiagnosticSeverity::Error) {
                    return "failed";
                }
                warning =
                    warning || value.severity == EditorAssetCatalogDiagnosticSeverity::Warning;
            }
            return warning ? "degraded" : "ready";
        }

        void stringMember(BoundedJsonWriter& writer, bool& first, JsonMemberName name,
                          std::string_view value) {
            writer.member(name.value, first);
            writer.string(value);
        }

        void pathMember(BoundedJsonWriter& writer, bool& first, std::string_view name,
                        const std::filesystem::path& value) {
            writer.member(name, first);
            writer.path(value);
        }

        void countMember(BoundedJsonWriter& writer, bool& first, std::string_view name,
                         std::size_t value) {
            writer.member(name, first);
            writer.integer(value);
        }

        void writeSourceRoot(BoundedJsonWriter& writer,
                             const EditorAssetCatalogResolvedSourceRoot& value) {
            writer.beginObject();
            bool first = true;
            stringMember(writer, first, {"name"}, value.rootName);
            stringMember(writer, first, {"sourcePathPrefix"}, value.sourcePathPrefix);
            pathMember(writer, first, "directory", value.directory);
            pathMember(writer, first, "resolvedDirectory", value.resolvedDirectory);
            writer.endObject();
        }

        void writeNavigationNode(BoundedJsonWriter& writer,
                                 const EditorAssetCatalogNavigationNode& value) {
            writer.beginObject();
            bool first = true;
            stringMember(writer, first, {"kind"},
                         editorAssetCatalogNavigationNodeKindName(value.kind));
            stringMember(writer, first, {"key"}, value.key);
            stringMember(writer, first, {"parentKey"}, value.parentKey);
            stringMember(writer, first, {"displayName"}, value.displayName);
            stringMember(writer, first, {"scopePath"}, value.scopePath);
            stringMember(writer, first, {"sourcePath"}, value.sourcePath);
            stringMember(writer, first, {"sourceRootName"}, value.sourceRootName);
            stringMember(writer, first, {"sourceRootPrefix"}, value.sourceRootPrefix);
            pathMember(writer, first, "sourceRootDirectory", value.sourceRootDirectory);
            stringMember(writer, first, {"guid"}, value.guidText);
            stringMember(writer, first, {"stableId"}, value.stableId);
            stringMember(writer, first, {"assetType"}, value.assetTypeName);
            stringMember(writer, first, {"importer"}, value.importerName);
            stringMember(writer, first, {"extension"}, value.extension);
            stringMember(writer, first, {"importProfile"}, value.importProfileName);
            stringMember(writer, first, {"assetRole"}, value.assetRoleName);
            countMember(writer, first, "subAssetCount", value.subAssetCount);
            stringMember(writer, first, {"productState"}, productStateName(value.productState));
            writer.endObject();
        }

        void writeSubAsset(BoundedJsonWriter& writer,
                           const asharia::asset::AssetCatalogSubAssetViewEntry& value) {
            writer.beginObject();
            bool first = true;
            stringMember(writer, first, {"stableId"}, value.stableId);
            stringMember(writer, first, {"displayName"}, value.displayName);
            stringMember(writer, first, {"assetRole"}, value.assetRoleName);
            writer.endObject();
        }

        void writeRowDiagnostic(BoundedJsonWriter& writer,
                                const asharia::asset::AssetCatalogDiagnostic& value) {
            writer.beginObject();
            bool first = true;
            stringMember(writer, first, {"severity"}, rowSeverityName(value.severity));
            stringMember(writer, first, {"code"}, rowDiagnosticCodeName(value.code));
            stringMember(writer, first, {"sourcePath"}, value.sourcePath);
            stringMember(writer, first, {"message"}, value.message);
            writer.endObject();
        }

        void writeRow(BoundedJsonWriter& writer, const EditorAssetCatalogSnapshot& snapshot,
                      const asharia::asset::AssetCatalogViewEntry& value) {
            const EditorAssetCatalogResolvedSourceRoot root =
                resolveEditorAssetCatalogSourceRootForSourcePath(snapshot, value.sourcePath);
            writer.beginObject();
            bool first = true;
            stringMember(writer, first, {"guid"}, value.guidText);
            stringMember(writer, first, {"sourcePath"}, value.sourcePath);
            stringMember(writer, first, {"sourceRootName"}, root.rootName);
            stringMember(writer, first, {"sourceRootPrefix"}, root.sourcePathPrefix);
            pathMember(writer, first, "sourceRootDirectory", root.resolvedDirectory);
            pathMember(writer, first, "sourceFilePath",
                       resolveEditorAssetCatalogSourceFilePath(snapshot, value.sourcePath));
            pathMember(writer, first, "metadataFilePath",
                       resolveEditorAssetCatalogMetadataFilePath(snapshot, value.sourcePath));
            stringMember(writer, first, {"displayName"}, value.displayName);
            stringMember(writer, first, {"extension"}, value.extension);
            stringMember(writer, first, {"assetType"}, value.assetTypeName);
            stringMember(writer, first, {"importer"}, value.importerName);
            countMember(writer, first, "importerVersion", value.importerVersion.value);
            stringMember(writer, first, {"importProfile"}, value.importProfileName);
            stringMember(writer, first, {"assetRole"}, value.assetRoleName);
            stringMember(writer, first, {"productState"}, productStateName(value.productState));
            countMember(writer, first, "currentProductCount", value.currentProductCount);
            countMember(writer, first, "staleProductCount", value.staleProductCount);
            writer.member("subAssets", first);
            writer.beginArray();
            bool firstSubAsset = true;
            for (const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset : value.subAssets) {
                writer.element(firstSubAsset);
                writeSubAsset(writer, subAsset);
            }
            writer.endArray();
            writer.member("diagnostics", first);
            writer.beginArray();
            bool firstDiagnostic = true;
            for (const asharia::asset::AssetCatalogDiagnostic& diagnostic : value.diagnostics) {
                writer.element(firstDiagnostic);
                writeRowDiagnostic(writer, diagnostic);
            }
            writer.endArray();
            writer.endObject();
        }

        void writeDiagnostic(BoundedJsonWriter& writer, const EditorAssetCatalogDiagnostic& value) {
            writer.beginObject();
            bool first = true;
            stringMember(writer, first, {"severity"}, severityName(value.severity));
            stringMember(writer, first, {"code"}, editorAssetCatalogDiagnosticCodeName(value.code));
            stringMember(writer, first, {"sourcePath"}, value.sourcePath);
            pathMember(writer, first, "path", value.path);
            stringMember(writer, first, {"message"}, value.message);
            writer.endObject();
        }

    } // namespace

    Result<std::string>
    writeEditorAssetCatalogSnapshotJson(const EditorAssetCatalogSnapshot& snapshot,
                                        std::size_t maxUtf8Bytes, std::size_t maxResponseBytes) {
        BoundedJsonWriter writer{BoundedJsonWriterLimits{
            .maxStringBytes = maxUtf8Bytes,
            .maxResponseBytes = maxResponseBytes,
        }};
        writer.beginObject();
        bool first = true;
        stringMember(writer, first, {"schema"}, "com.asharia.editor.assetCatalogSnapshot");
        countMember(writer, first, "schemaVersion", 1U);
        stringMember(writer, first, {"state"}, snapshotState(snapshot));
        stringMember(writer, first, {"projectId"},
                     asharia::project::formatProjectId(snapshot.project.projectId));
        pathMember(writer, first, "projectFile", snapshot.projectFile);
        pathMember(writer, first, "productManifestFile", snapshot.productManifestFile);
        stringMember(writer, first, {"targetProfile"}, snapshot.targetProfile);

        const std::vector<EditorAssetCatalogResolvedSourceRoot> roots =
            resolveEditorAssetCatalogSourceRoots(snapshot);
        writer.member("sourceRoots", first);
        writer.beginArray();
        bool firstRoot = true;
        for (const EditorAssetCatalogResolvedSourceRoot& root : roots) {
            writer.element(firstRoot);
            writeSourceRoot(writer, root);
        }
        writer.endArray();

        const std::vector<EditorAssetCatalogNavigationNode> nodes =
            makeEditorAssetCatalogNavigationNodes(snapshot);
        writer.member("navigationNodes", first);
        writer.beginArray();
        bool firstNode = true;
        for (const EditorAssetCatalogNavigationNode& node : nodes) {
            writer.element(firstNode);
            writeNavigationNode(writer, node);
        }
        writer.endArray();

        writer.member("rows", first);
        writer.beginArray();
        bool firstRow = true;
        for (const asharia::asset::AssetCatalogViewEntry& row : snapshot.catalogView.entries) {
            writer.element(firstRow);
            writeRow(writer, snapshot, row);
        }
        writer.endArray();

        writer.member("diagnostics", first);
        writer.beginArray();
        bool firstDiagnostic = true;
        for (const EditorAssetCatalogDiagnostic& diagnostic : snapshot.diagnostics) {
            writer.element(firstDiagnostic);
            writeDiagnostic(writer, diagnostic);
        }
        writer.endArray();
        writer.endObject();
        return writer.finish();
    }

} // namespace asharia::editor
