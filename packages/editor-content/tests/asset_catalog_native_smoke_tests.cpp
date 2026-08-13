#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asharia/editor_content/asset_catalog_native_api.h"
#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace {

    struct Workspace {
        std::filesystem::path root;
        Workspace() = default;
        explicit Workspace(std::filesystem::path value) : root(std::move(value)) {}
        Workspace(const Workspace&) = delete;
        Workspace& operator=(const Workspace&) = delete;
        Workspace(Workspace&&) = delete;
        Workspace& operator=(Workspace&&) = delete;
        ~Workspace() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    [[nodiscard]] std::optional<Workspace> makeWorkspace() {
        for (std::uint64_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto path =
                std::filesystem::temp_directory_path() /
                ("asharia-editor-content-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path, error)) {
                return std::optional<Workspace>{std::in_place, path};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
        const std::u8string value = path.u8string();
        return std::string{value.begin(), value.end()};
    }

    [[nodiscard]] AshariaEditorContentNativeStringView view(std::string_view text) {
        return {.data = text.data(), .byteLength = text.size()};
    }

    [[nodiscard]] std::uint32_t query(AshariaEditorContentNativeQueryRequest& request,
                                      std::string& response,
                                      AshariaEditorContentNativeResult& result) {
        return asharia_editor_content_query(&request, response.data(), response.size(), &result,
                                            sizeof(result));
    }

    [[nodiscard]] bool
    writesDescriptor(const std::filesystem::path& descriptorFile,
                     asharia::project::ProjectId projectId,
                     std::vector<asharia::project::AssetSourceRootDesc> assetSourceRoots) {
        return asharia::project::writeAshariaProjectDescriptorFile(
                   descriptorFile,
                   asharia::project::AshariaProjectDescriptor{
                       .projectName = "CatalogNativeSmoke",
                       .projectId = projectId,
                       .assetSourceRoots = std::move(assetSourceRoots),
                       .assetCacheRoot = ".asharia/cache/assets",
                   })
            .has_value();
    }

    [[nodiscard]] bool smokeResponseLimit(AshariaEditorContentNativeQueryRequest& request,
                                          std::string& response,
                                          AshariaEditorContentNativeResult& result) {
        request.limits.maxResponseBytes = 1U;
        const auto status = query(request, response, result);
        request.limits.maxResponseBytes = 1024ULL * 1024ULL;
        return status == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.operationStatus == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.payloadJsonUtf8.byteLength == 0U && result.messageUtf8.byteLength != 0U;
    }

    [[nodiscard]] bool smokeLongStringLimit(const std::filesystem::path& descriptorFile,
                                            asharia::project::ProjectId projectId,
                                            AshariaEditorContentNativeQueryRequest& request,
                                            std::string& response,
                                            AshariaEditorContentNativeResult& result) {
        if (!writesDescriptor(descriptorFile, projectId,
                              {{.rootName = std::string(65'537U, 'x'),
                                .directory = "Assets",
                                .sourcePathPrefix = "Assets"}})) {
            return false;
        }
        const auto status = query(request, response, result);
        return status == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.operationStatus == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.payloadJsonUtf8.byteLength == 0U;
    }

    [[nodiscard]] bool smokeAggregateLimits(const Workspace& workspace,
                                            const std::filesystem::path& descriptorFile,
                                            asharia::project::ProjectId projectId,
                                            AshariaEditorContentNativeQueryRequest& request,
                                            std::string& response,
                                            AshariaEditorContentNativeResult& result) {
        std::filesystem::create_directories(workspace.root / "Second");
        std::ofstream{workspace.root / "Assets" / "first.txt"} << "first";
        std::ofstream{workspace.root / "Second" / "second.txt"} << "second";
        if (!writesDescriptor(
                descriptorFile, projectId,
                {{.rootName = "Assets", .directory = "Assets", .sourcePathPrefix = "Assets"},
                 {.rootName = "Second", .directory = "Second", .sourcePathPrefix = "Second"}})) {
            return false;
        }

        request.limits.maxSourceFiles = 1U;
        const auto aggregateFiles = query(request, response, result);
        if (aggregateFiles != AshariaEditorContentNativeStatus_LimitExceeded ||
            result.operationStatus != AshariaEditorContentNativeStatus_LimitExceeded ||
            result.payloadJsonUtf8.byteLength != 0U) {
            return false;
        }
        std::filesystem::remove(workspace.root / "Second" / "second.txt");
        const auto exactFiles = query(request, response, result);
        if ((exactFiles != AshariaEditorContentNativeStatus_Success &&
             exactFiles != AshariaEditorContentNativeStatus_BufferTooSmall) ||
            result.operationStatus != AshariaEditorContentNativeStatus_Success) {
            return false;
        }
        std::ofstream{workspace.root / "Second" / "second.txt"} << "second";
        request.limits.maxSourceFiles = 100U;

        std::ofstream{workspace.root / "Assets" / "first.txt.ameta"} << "{}";
        std::ofstream{workspace.root / "Second" / "second.txt.ameta"} << "{}";
        request.limits.maxTotalSourceBytes = 11U;
        const auto exactBytes = query(request, response, result);
        if ((exactBytes != AshariaEditorContentNativeStatus_Success &&
             exactBytes != AshariaEditorContentNativeStatus_BufferTooSmall) ||
            result.operationStatus != AshariaEditorContentNativeStatus_Success) {
            return false;
        }
        request.limits.maxTotalSourceBytes = 10U;
        const auto aggregateBytes = query(request, response, result);
        request.limits.maxTotalSourceBytes = 1024ULL * 1024ULL;
        return aggregateBytes == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.operationStatus == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.payloadJsonUtf8.byteLength == 0U;
    }

    [[nodiscard]] bool smokeDiagnosticLimit(AshariaEditorContentNativeQueryRequest& request,
                                            std::string& response,
                                            AshariaEditorContentNativeResult& result) {
        request.limits.maxDiagnostics = 1U;
        const auto status = query(request, response, result);
        request.limits.maxDiagnostics = 100U;
        return status == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.operationStatus == AshariaEditorContentNativeStatus_LimitExceeded &&
               result.payloadJsonUtf8.byteLength == 0U;
    }

    [[nodiscard]] bool smoke() {
        static_assert(sizeof(AshariaEditorContentNativeQueryRequest) == 88U);
        static_assert(sizeof(AshariaEditorContentNativeResult) == 56U);
        auto workspace = makeWorkspace();
        if (!workspace) {
            return false;
        }
        std::filesystem::create_directories(workspace->root / "Assets");
        auto projectId = asharia::project::parseProjectId("bf876e51-4d27-4a34-9c87-aee6dc51b194");
        if (!projectId) {
            return false;
        }
        const auto descriptorFile = workspace->root / "asharia.project.json";
        if (!writesDescriptor(
                descriptorFile, *projectId,
                {{.rootName = "Assets", .directory = "Assets", .sourcePathPrefix = "Assets"}})) {
            return false;
        }

        const std::string project = pathText(descriptorFile);
        const std::string target = "editor-preview";
        AshariaEditorContentNativeQueryRequest request{
            .header = {.abiVersion = ASHARIA_EDITOR_CONTENT_NATIVE_ABI_VERSION,
                       .structSize = sizeof(request)},
            .projectPathUtf8 = view(project),
            .targetProfileUtf8 = view(target),
            .productManifestPathUtf8 = {},
            .limits = {.maxSourceFiles = 100U,
                       .maxTotalSourceBytes = 1024ULL * 1024ULL,
                       .maxDiagnostics = 100U,
                       .maxResponseBytes = 1024ULL * 1024ULL},
        };
        std::array<char, 1> canary{'x'};
        AshariaEditorContentNativeResult result{};
        const auto small =
            asharia_editor_content_query(&request, canary.data(), 0U, &result, sizeof(result));
        if (small != AshariaEditorContentNativeStatus_BufferTooSmall ||
            result.operationStatus != AshariaEditorContentNativeStatus_Success ||
            result.requiredByteLength == 0U || canary[0] != 'x') {
            return false;
        }
        std::string response(result.requiredByteLength, '\0');
        const auto success = asharia_editor_content_query(&request, response.data(),
                                                          response.size(), &result, sizeof(result));
        if (success != AshariaEditorContentNativeStatus_Success ||
            response.find("com.asharia.editor.assetCatalogSnapshot") == std::string::npos ||
            response.find("\"state\"") == std::string::npos) {
            return false;
        }

        if (!smokeResponseLimit(request, response, result)) {
            return false;
        }
        if (!smokeLongStringLimit(descriptorFile, *projectId, request, response, result) ||
            !smokeAggregateLimits(*workspace, descriptorFile, *projectId, request, response,
                                  result) ||
            !smokeDiagnosticLimit(request, response, result)) {
            return false;
        }

        const std::array invalidUtf8{static_cast<char>(0xC0), static_cast<char>(0x80)};
        request.targetProfileUtf8 = {.data = invalidUtf8.data(), .byteLength = invalidUtf8.size()};
        if (asharia_editor_content_query(&request, response.data(), response.size(), &result,
                                         sizeof(result)) !=
                AshariaEditorContentNativeStatus_InvalidUtf8 ||
            result.operationStatus != AshariaEditorContentNativeStatus_InvalidUtf8 ||
            result.payloadJsonUtf8.byteLength != 0U) {
            return false;
        }
        request.targetProfileUtf8 = view(target);

        if (asharia_editor_content_query(&request, response.data(), response.size(), &result,
                                         sizeof(result) - 1U) !=
            AshariaEditorContentNativeStatus_InvalidArgument) {
            return false;
        }

        request.header.abiVersion++;
        return asharia_editor_content_query(&request, response.data(), response.size(), &result,
                                            sizeof(result)) ==
                   AshariaEditorContentNativeStatus_UnsupportedAbi &&
               result.operationStatus == AshariaEditorContentNativeStatus_UnsupportedAbi;
    }

} // namespace

int main() noexcept {
    try {
        return smoke() ? 0 : 1;
    } catch (...) {
        return 1;
    }
}
