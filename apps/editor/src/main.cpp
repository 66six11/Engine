#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/core/version.hpp"

#include "editor_app.hpp"
#include "editor_asset_catalog.hpp"

namespace {

    [[nodiscard]] bool argEquals(const char* arg, std::string_view expected) {
        return arg != nullptr && std::string_view{arg} == expected;
    }

    [[nodiscard]] bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return argEquals(arg, expected); });
    }

    struct OptionValueResult {
        bool succeeded{false};
        std::string value;
        std::string error;
    };

    [[nodiscard]] OptionValueResult readOptionValue(std::span<char*> args, std::size_t& index,
                                                    std::string_view option) {
        const std::size_t valueIndex = index + 1U;
        if (valueIndex >= args.size() || args[valueIndex] == nullptr ||
            std::string_view{args[valueIndex]}.starts_with("--")) {
            return OptionValueResult{.succeeded = false,
                                     .value = {},
                                     .error = "Missing value for " + std::string{option} + "."};
        }
        index = valueIndex;
        return OptionValueResult{
            .succeeded = true,
            .value = args[valueIndex],
            .error = {},
        };
    }

    struct ParsedInteractiveArgs {
        asharia::editor::EditorAssetCatalogRunConfig assetCatalog;
        bool checkProject{false};
        std::string error;
    };

    [[nodiscard]] ParsedInteractiveArgs parseInteractiveArgs(std::span<char*> args) {
        ParsedInteractiveArgs parsed{};
        for (std::size_t index = 1U; index < args.size(); ++index) {
            if (args[index] == nullptr) {
                continue;
            }

            const std::string_view option{args[index]};
            if (option == "--project") {
                OptionValueResult value = readOptionValue(args, index, option);
                if (!value.succeeded) {
                    parsed.error = std::move(value.error);
                    return parsed;
                }
                parsed.assetCatalog.projectFile = std::filesystem::path{value.value};
            } else if (option == "--check-project") {
                OptionValueResult value = readOptionValue(args, index, option);
                if (!value.succeeded) {
                    parsed.error = std::move(value.error);
                    return parsed;
                }
                parsed.checkProject = true;
                parsed.assetCatalog.projectFile = std::filesystem::path{value.value};
            } else if (option == "--product-manifest") {
                OptionValueResult value = readOptionValue(args, index, option);
                if (!value.succeeded) {
                    parsed.error = std::move(value.error);
                    return parsed;
                }
                parsed.assetCatalog.productManifestFile = std::filesystem::path{value.value};
            } else if (option == "--asset-target-profile") {
                OptionValueResult value = readOptionValue(args, index, option);
                if (!value.succeeded) {
                    parsed.error = std::move(value.error);
                    return parsed;
                }
                parsed.assetCatalog.targetProfile = std::move(value.value);
            } else {
                parsed.error = "Unknown editor option " + std::string{option} + ".";
                return parsed;
            }
        }
        if (parsed.assetCatalog.projectFile.empty() &&
            (!parsed.assetCatalog.productManifestFile.empty() ||
             !parsed.assetCatalog.targetProfile.empty())) {
            parsed.error = "--product-manifest and --asset-target-profile require --project.";
        }
        return parsed;
    }

    void printUsage();

    [[nodiscard]] bool validateSingleSmokeArg(std::span<char*> args, std::string_view option) {
        if (args.size() == 2U) {
            return true;
        }
        asharia::logError("Editor smoke option " + std::string{option} +
                          " cannot be combined with project-loading options.");
        printUsage();
        return false;
    }

    [[nodiscard]] asharia::editor::EditorAssetCatalogSnapshotRequest
    snapshotRequestFor(const asharia::editor::EditorAssetCatalogRunConfig& config) {
        std::string targetProfile = config.targetProfile;
        if (targetProfile.empty()) {
            targetProfile = "editor-preview";
        }
        return asharia::editor::EditorAssetCatalogSnapshotRequest{
            .projectFile = config.projectFile,
            .productManifestFile = config.productManifestFile,
            .targetProfile = std::move(targetProfile),
        };
    }

    [[nodiscard]] int checkProjectAssetCatalog(
        const asharia::editor::EditorAssetCatalogRunConfig& config) {
        const asharia::editor::EditorAssetCatalogSnapshotRequest request =
            snapshotRequestFor(config);
        const asharia::editor::EditorAssetCatalogSnapshot snapshot =
            asharia::editor::loadEditorAssetCatalogSnapshot(request);

        std::cout << "project=" << request.projectFile.string() << '\n'
                  << "targetProfile=" << request.targetProfile << '\n'
                  << "rows=" << snapshot.catalogView.entries.size() << '\n'
                  << "diagnostics=" << snapshot.diagnostics.size() << '\n';
        for (const asharia::asset::AssetCatalogViewEntry& entry :
             snapshot.catalogView.entries) {
            std::cout << "row"
                      << " sourcePath=" << std::quoted(entry.sourcePath)
                      << " displayName=" << std::quoted(entry.displayName)
                      << " type=" << std::quoted(entry.assetTypeName)
                      << " importer=" << std::quoted(entry.importerName)
                      << " profile=" << std::quoted(entry.importProfileName)
                      << " role=" << std::quoted(entry.assetRoleName)
                      << " productState="
                      << asharia::asset::assetCatalogProductStateName(entry.productState)
                      << " currentProducts=" << entry.currentProductCount
                      << " staleProducts=" << entry.staleProductCount
                      << " subAssets=" << entry.subAssets.size() << '\n';
            for (const asharia::asset::AssetCatalogSubAssetViewEntry& subAsset :
                 entry.subAssets) {
                std::cout << "sub-asset"
                          << " sourcePath=" << std::quoted(entry.sourcePath)
                          << " stableId=" << std::quoted(subAsset.stableId)
                          << " displayName=" << std::quoted(subAsset.displayName)
                          << " role=" << std::quoted(subAsset.assetRoleName) << '\n';
            }
        }
        for (const asharia::editor::EditorAssetCatalogDiagnostic& diagnostic :
             snapshot.diagnostics) {
            std::cout
                << asharia::editor::editorAssetCatalogDiagnosticSeverityName(diagnostic.severity)
                << ' '
                << asharia::editor::editorAssetCatalogDiagnosticCodeName(diagnostic.code);
            if (!diagnostic.sourcePath.empty()) {
                std::cout << " sourcePath=" << diagnostic.sourcePath;
            }
            if (!diagnostic.path.empty()) {
                std::cout << " path=" << diagnostic.path.string();
            }
            std::cout << " message=" << std::quoted(diagnostic.message) << '\n';
        }
        return snapshot.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    void printVersion() {
        std::cout << asharia::kEngineName << " editor " << asharia::kEngineVersion.major << '.'
                  << asharia::kEngineVersion.minor << '.' << asharia::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout << "Usage: asharia-editor [--help] [--version] [--smoke-editor-shell] "
                     "[--smoke-editor-asset-browser] [--smoke-editor-viewport] "
                     "[--smoke-editor-viewport-resize] [--smoke-editor-frame-debugger]\n"
                     "       asharia-editor [--project <asharia.project.json>] "
                     "[--product-manifest <products.aproducts.json>] "
                     "[--asset-target-profile <profile>]\n"
                     "       asharia-editor --check-project <asharia.project.json> "
                     "[--product-manifest <products.aproducts.json>] "
                     "[--asset-target-profile <profile>]\n";
    }

} // namespace

int main(int argc, char** argv) {
    try {
        std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (hasArg(args, "--help")) {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--smoke-editor-shell")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-shell")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeShell);
        }

        if (hasArg(args, "--smoke-editor-asset-browser")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-asset-browser")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(
                asharia::editor::EditorRunMode::SmokeAssetBrowser);
        }

        if (hasArg(args, "--smoke-editor-viewport")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-viewport")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeViewport);
        }

        if (hasArg(args, "--smoke-editor-viewport-resize")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-viewport-resize")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeViewportResize);
        }

        if (hasArg(args, "--smoke-editor-frame-debugger")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-frame-debugger")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeFrameDebugger);
        }

        if (args.size() == 1) {
            return asharia::editor::runEditor(
                asharia::editor::EditorRunConfig{.mode = asharia::editor::EditorRunMode::Interactive,
                                                 .assetCatalog = {}});
        }

        const ParsedInteractiveArgs parsed = parseInteractiveArgs(args);
        if (!parsed.error.empty()) {
            asharia::logError(parsed.error);
            printUsage();
            return EXIT_FAILURE;
        }
        if (parsed.checkProject) {
            return checkProjectAssetCatalog(parsed.assetCatalog);
        }

        return asharia::editor::runEditor(
            asharia::editor::EditorRunConfig{.mode = asharia::editor::EditorRunMode::Interactive,
                                             .assetCatalog = parsed.assetCatalog});
    } catch (const std::exception& exception) {
        asharia::logError(exception.what());
    } catch (...) {
        asharia::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
