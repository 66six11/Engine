#include "asharia/editor_content/asset_catalog_native_api.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/editor_content/asset_catalog_snapshot.hpp"

#include "asset_catalog_snapshot_json.hpp"

namespace {

    static_assert(sizeof(void*) == 8U, "Editor content native ABI v1 requires a 64-bit process.");
    static_assert(sizeof(AshariaEditorContentNativeStatus) == 4U);
    static_assert(sizeof(AshariaEditorContentNativeAbiHeader) == 8U);
    static_assert(sizeof(AshariaEditorContentNativeStringView) == 16U);
    static_assert(sizeof(AshariaEditorContentNativeLimits) == 32U);
    static_assert(sizeof(AshariaEditorContentNativeQueryRequest) == 88U);
    static_assert(offsetof(AshariaEditorContentNativeQueryRequest, limits) == 56U);
    static_assert(sizeof(AshariaEditorContentNativeTextSpan) == 16U);
    static_assert(sizeof(AshariaEditorContentNativeResult) == 56U);
    static_assert(offsetof(AshariaEditorContentNativeResult, payloadJsonUtf8) == 24U);
    static_assert(offsetof(AshariaEditorContentNativeResult, messageUtf8) == 40U);

    constexpr std::uint64_t kMaxPathBytes = 32ULL * 1024ULL;
    constexpr std::uint64_t kMaxTargetProfileBytes = 1024ULL;
    constexpr std::uint64_t kHardMaxSourceFiles = 1'000'000ULL;
    constexpr std::uint64_t kHardMaxSourceBytes = 1ULL << 40U;
    constexpr std::uint64_t kHardMaxDiagnostics = 100'000ULL;
    constexpr std::uint64_t kHardMaxResponseBytes = 64ULL * 1024ULL * 1024ULL;
    constexpr std::size_t kMaxMessageBytes = 64ULL * 1024ULL;
    constexpr std::size_t kMaxJsonStringBytes = 64ULL * 1024ULL;
    constexpr std::size_t kMaxSourceRoots = 1'024U;
    constexpr std::size_t kMaxNavigationNodes = 100'000U;
    constexpr std::size_t kMaxRows = 100'000U;
    constexpr std::size_t kMaxSubAssets = 100'000U;

    struct Operation {
        std::uint32_t status{AshariaEditorContentNativeStatus_InternalError};
        std::string payload;
        std::string message;
    };

    [[nodiscard]] constexpr bool continuation(unsigned char value) noexcept {
        return (value & 0xC0U) == 0x80U;
    }

    [[nodiscard]] constexpr std::size_t sequenceSize(unsigned char first) noexcept {
        if (first >= 0xC2U && first <= 0xDFU) {
            return 2U;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            return 3U;
        }
        if (first >= 0xF0U && first <= 0xF4U) {
            return 4U;
        }
        return 0U;
    }

    [[nodiscard]] constexpr bool validSecondByte(unsigned char first,
                                                 unsigned char second) noexcept {
        if (!continuation(second)) {
            return false;
        }
        return (first != 0xE0U || second >= 0xA0U) && (first != 0xEDU || second <= 0x9FU) &&
               (first != 0xF0U || second >= 0x90U) && (first != 0xF4U || second <= 0x8FU);
    }

    [[nodiscard]] bool validUtf8(std::string_view text) noexcept {
        std::size_t index = 0U;
        while (index < text.size()) {
            const auto first = static_cast<unsigned char>(text[index]);
            if (first <= 0x7FU) {
                ++index;
                continue;
            }
            const std::size_t size = sequenceSize(first);
            if (size == 0U || index + size > text.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[index + 1U]);
            if (!validSecondByte(first, second)) {
                return false;
            }
            for (std::size_t offset = 2U; offset < size; ++offset) {
                if (!continuation(static_cast<unsigned char>(text[index + offset]))) {
                    return false;
                }
            }
            index += size;
        }
        return true;
    }

    [[nodiscard]] std::uint32_t readText(AshariaEditorContentNativeStringView value,
                                         std::uint64_t maxBytes, bool required,
                                         std::string_view name, std::string_view& text,
                                         std::string& message) {
        if ((!required && value.byteLength == 0U && value.data == nullptr)) {
            text = {};
            return AshariaEditorContentNativeStatus_Success;
        }
        if (value.data == nullptr || (required && value.byteLength == 0U) ||
            value.byteLength > maxBytes ||
            value.byteLength > std::numeric_limits<std::size_t>::max()) {
            message = std::string{name} + " is missing or exceeds its size limit.";
            return AshariaEditorContentNativeStatus_InvalidArgument;
        }
        text = std::string_view{value.data, static_cast<std::size_t>(value.byteLength)};
        if (text.find('\0') != std::string_view::npos) {
            message = std::string{name} + " contains an embedded NUL byte.";
            return AshariaEditorContentNativeStatus_InvalidArgument;
        }
        if (!validUtf8(text)) {
            message = std::string{name} + " is not valid UTF-8.";
            return AshariaEditorContentNativeStatus_InvalidUtf8;
        }
        return AshariaEditorContentNativeStatus_Success;
    }

    [[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
        std::u8string value;
        value.reserve(text.size());
        for (const char byte : text) {
            value.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        }
        return std::filesystem::path{value};
    }

    [[nodiscard]] Operation failure(std::uint32_t status, std::string message) {
        if (!validUtf8(message)) {
            message = "Catalog query failed with an invalid native message.";
        }
        if (message.size() > kMaxMessageBytes) {
            message.resize(kMaxMessageBytes);
            while (!message.empty() && !validUtf8(message)) {
                message.pop_back();
            }
        }
        return Operation{.status = status, .payload = {}, .message = std::move(message)};
    }

    [[nodiscard]] bool validLimits(const AshariaEditorContentNativeLimits& limits) noexcept {
        return limits.maxSourceFiles > 0U && limits.maxSourceFiles <= kHardMaxSourceFiles &&
               limits.maxTotalSourceBytes > 0U &&
               limits.maxTotalSourceBytes <= kHardMaxSourceBytes && limits.maxDiagnostics > 0U &&
               limits.maxDiagnostics <= kHardMaxDiagnostics && limits.maxResponseBytes > 0U &&
               limits.maxResponseBytes <= kHardMaxResponseBytes;
    }

    [[nodiscard]] Operation query(const AshariaEditorContentNativeQueryRequest& request) {
        std::string_view project;
        std::string_view target;
        std::string_view manifest;
        std::string message;
        for (const auto [status, ignored] :
             std::array{std::pair{readText(request.projectPathUtf8, kMaxPathBytes, true,
                                           "projectPath", project, message),
                                  0},
                        std::pair{readText(request.targetProfileUtf8, kMaxTargetProfileBytes, true,
                                           "targetProfile", target, message),
                                  0},
                        std::pair{readText(request.productManifestPathUtf8, kMaxPathBytes, false,
                                           "productManifestPath", manifest, message),
                                  0}}) {
            (void)ignored;
            if (status != AshariaEditorContentNativeStatus_Success) {
                return failure(status, std::move(message));
            }
        }
        if (!validLimits(request.limits)) {
            return failure(AshariaEditorContentNativeStatus_InvalidArgument,
                           "Catalog query limits are zero or exceed hard limits.");
        }

        const asharia::editor::EditorAssetCatalogSnapshot snapshot =
            asharia::editor::loadEditorAssetCatalogSnapshot(
                asharia::editor::EditorAssetCatalogSnapshotRequest{
                    .projectFile = pathFromUtf8(project),
                    .productManifestFile = pathFromUtf8(manifest),
                    .targetProfile = std::string{target},
                    .maxSourceRoots = kMaxSourceRoots,
                    .maxSourceFiles = request.limits.maxSourceFiles,
                    .maxTotalSourceBytes = request.limits.maxTotalSourceBytes,
                    .maxDiagnostics = request.limits.maxDiagnostics,
                });
        if (!snapshot.project) {
            const std::string error = snapshot.diagnostics.empty()
                                          ? "Catalog query could not open the project descriptor."
                                          : snapshot.diagnostics.front().message;
            return failure(AshariaEditorContentNativeStatus_InvalidProject, error);
        }
        if (snapshot.diagnostics.size() > request.limits.maxDiagnostics) {
            return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                           "Catalog query exceeded the diagnostic limit.");
        }
        if (std::ranges::any_of(snapshot.diagnostics, [](const auto& diagnostic) {
                return diagnostic.code ==
                       asharia::editor::EditorAssetCatalogDiagnosticCode::LimitExceeded;
            })) {
            return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                           "Catalog query exceeded an ingestion limit.");
        }
        if (snapshot.project.assetSourceRoots.size() > kMaxSourceRoots ||
            snapshot.catalogView.entries.size() > kMaxRows) {
            return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                           "Catalog query exceeded a response count limit.");
        }
        std::uint64_t totalDiagnostics = snapshot.diagnostics.size();
        std::size_t totalSubAssets = 0U;
        for (const asharia::asset::AssetCatalogViewEntry& entry : snapshot.catalogView.entries) {
            if (entry.diagnostics.size() > request.limits.maxDiagnostics - totalDiagnostics ||
                entry.subAssets.size() > kMaxSubAssets - totalSubAssets) {
                return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                               "Catalog query exceeded a nested response count limit.");
            }
            totalDiagnostics += entry.diagnostics.size();
            totalSubAssets += entry.subAssets.size();
        }
        if (asharia::editor::makeEditorAssetCatalogNavigationNodes(snapshot).size() >
            kMaxNavigationNodes) {
            return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                           "Catalog query exceeded the navigation-node limit.");
        }
        auto payload = asharia::editor::writeEditorAssetCatalogSnapshotJson(
            snapshot, kMaxJsonStringBytes,
            static_cast<std::size_t>(request.limits.maxResponseBytes));
        if (!payload) {
            return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                           std::move(payload.error().message));
        }
        if (payload->size() > request.limits.maxResponseBytes) {
            return failure(AshariaEditorContentNativeStatus_LimitExceeded,
                           "Catalog query JSON exceeded the response byte limit.");
        }
        return Operation{.status = AshariaEditorContentNativeStatus_Success,
                         .payload = std::move(*payload),
                         .message = {}};
    }

    [[nodiscard]] AshariaEditorContentNativeResult emptyResult(std::uint32_t status) noexcept {
        return AshariaEditorContentNativeResult{
            .header = {.abiVersion = ASHARIA_EDITOR_CONTENT_NATIVE_ABI_VERSION,
                       .structSize = sizeof(AshariaEditorContentNativeResult)},
            .operationStatus = status,
            .reserved = 0U,
            .requiredByteLength = 0U,
            .payloadJsonUtf8 = {},
            .messageUtf8 = {},
        };
    }

    [[nodiscard]] std::uint32_t write(const Operation& operation, void* responseBuffer,
                                      std::uint64_t responseCapacity,
                                      AshariaEditorContentNativeResult& result) noexcept {
        result = emptyResult(operation.status);
        result.requiredByteLength = operation.payload.size() + operation.message.size();
        result.payloadJsonUtf8 = {.byteOffset = 0U, .byteLength = operation.payload.size()};
        result.messageUtf8 = {.byteOffset = operation.payload.size(),
                              .byteLength = operation.message.size()};
        if (responseCapacity > std::numeric_limits<std::size_t>::max()) {
            return AshariaEditorContentNativeStatus_InvalidArgument;
        }
        if (result.requiredByteLength > responseCapacity ||
            (result.requiredByteLength > 0U && responseBuffer == nullptr)) {
            return AshariaEditorContentNativeStatus_BufferTooSmall;
        }
        std::span bytes{static_cast<char*>(responseBuffer),
                        static_cast<std::size_t>(responseCapacity)};
        if (!operation.payload.empty()) {
            std::ranges::copy(operation.payload, bytes.begin());
        }
        if (!operation.message.empty()) {
            std::ranges::copy(operation.message, bytes.begin() + static_cast<std::ptrdiff_t>(
                                                                     operation.payload.size()));
        }
        return operation.status;
    }

    [[nodiscard]] std::uint32_t
    writeLiteralFailure(std::uint32_t status, std::string_view message, void* responseBuffer,
                        std::uint64_t responseCapacity,
                        AshariaEditorContentNativeResult& result) noexcept {
        result = emptyResult(status);
        result.requiredByteLength = message.size();
        result.payloadJsonUtf8 = {};
        result.messageUtf8 = {.byteOffset = 0U, .byteLength = message.size()};
        if (responseCapacity > std::numeric_limits<std::size_t>::max()) {
            return AshariaEditorContentNativeStatus_InvalidArgument;
        }
        if (result.requiredByteLength > responseCapacity ||
            (result.requiredByteLength > 0U && responseBuffer == nullptr)) {
            return AshariaEditorContentNativeStatus_BufferTooSmall;
        }
        if (!message.empty()) {
            std::memcpy(responseBuffer, message.data(), message.size());
        }
        return status;
    }

} // namespace

extern "C" uint32_t ASHARIA_EDITOR_CONTENT_NATIVE_CALL asharia_editor_content_query(
    const AshariaEditorContentNativeQueryRequest* request, void* responseBuffer,
    uint64_t responseCapacity, AshariaEditorContentNativeResult* result,
    uint64_t resultCapacity) noexcept {
    if (result == nullptr || resultCapacity < sizeof(AshariaEditorContentNativeResult)) {
        return AshariaEditorContentNativeStatus_InvalidArgument;
    }
    *result = emptyResult(AshariaEditorContentNativeStatus_InvalidArgument);
    if (request == nullptr) {
        return result->operationStatus;
    }
    if (request->header.abiVersion != ASHARIA_EDITOR_CONTENT_NATIVE_ABI_VERSION ||
        request->header.structSize < sizeof(AshariaEditorContentNativeQueryRequest)) {
        result->operationStatus = AshariaEditorContentNativeStatus_UnsupportedAbi;
        return result->operationStatus;
    }
    try {
        return write(query(*request), responseBuffer, responseCapacity, *result);
    } catch (const std::bad_alloc&) {
        return writeLiteralFailure(AshariaEditorContentNativeStatus_InternalError,
                                   "Catalog query allocation failed.", responseBuffer,
                                   responseCapacity, *result);
    } catch (const std::exception&) {
        return writeLiteralFailure(AshariaEditorContentNativeStatus_InternalError,
                                   "Catalog query failed with a native error.", responseBuffer,
                                   responseCapacity, *result);
    } catch (...) {
        return writeLiteralFailure(AshariaEditorContentNativeStatus_InternalError,
                                   "Catalog query failed with an unknown native error.",
                                   responseBuffer, responseCapacity, *result);
    }
}
