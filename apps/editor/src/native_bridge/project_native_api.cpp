#include "native_bridge/project_native_api.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/core/file_io.hpp"
#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace {

    constexpr std::uint64_t kMaxProjectRootUtf8Bytes = 32ULL * 1024ULL;
    constexpr std::uint64_t kMaxProjectNameUtf8Bytes = 1024U;
    constexpr std::uint64_t kMaxProjectIdUtf8Bytes = 64U;

    // The C ABI transfers native-owned byte arrays back through an explicit release function.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
    using OwnedText = std::unique_ptr<char[]>;

    struct ProjectSnapshot {
        std::filesystem::path root;
        asharia::project::AshariaProjectDescriptor descriptor;
    };

    struct MinimalProjectInput {
        std::string_view root;
        std::string_view name;
        std::string_view projectId;
    };

    struct ProjectOperationResult {
        std::uint32_t status{EditorProjectNativeStatus_InternalError};
        std::optional<ProjectSnapshot> snapshot;
        std::string message;
    };

    [[nodiscard]] ProjectOperationResult failure(std::uint32_t status, std::string message) {
        return ProjectOperationResult{
            .status = status,
            .snapshot = std::nullopt,
            .message = std::move(message),
        };
    }

    [[nodiscard]] ProjectOperationResult success(ProjectSnapshot snapshot) {
        return ProjectOperationResult{
            .status = EditorProjectNativeStatus_Success,
            .snapshot = std::move(snapshot),
            .message = {},
        };
    }

    [[nodiscard]] constexpr bool isContinuationByte(unsigned char value) noexcept {
        return (value & 0xC0U) == 0x80U;
    }

    [[nodiscard]] bool isValidTwoByteSequence(std::string_view text, std::size_t index) noexcept {
        const auto first = static_cast<unsigned char>(text[index]);
        return first >= 0xC2U && first <= 0xDFU && index + 1U < text.size() &&
               isContinuationByte(static_cast<unsigned char>(text[index + 1U]));
    }

    [[nodiscard]] bool isValidThreeByteSequence(std::string_view text, std::size_t index) noexcept {
        if (index + 2U >= text.size()) {
            return false;
        }

        const auto first = static_cast<unsigned char>(text[index]);
        const auto second = static_cast<unsigned char>(text[index + 1U]);
        const auto third = static_cast<unsigned char>(text[index + 2U]);
        const bool validSecond =
            (first == 0xE0U && second >= 0xA0U && second <= 0xBFU) ||
            (first == 0xEDU && second >= 0x80U && second <= 0x9FU) ||
            (((first >= 0xE1U && first <= 0xECU) || (first >= 0xEEU && first <= 0xEFU)) &&
             isContinuationByte(second));
        return validSecond && isContinuationByte(third);
    }

    [[nodiscard]] bool isValidFourByteSequence(std::string_view text, std::size_t index) noexcept {
        if (index + 3U >= text.size()) {
            return false;
        }

        const auto first = static_cast<unsigned char>(text[index]);
        const auto second = static_cast<unsigned char>(text[index + 1U]);
        const auto third = static_cast<unsigned char>(text[index + 2U]);
        const auto fourth = static_cast<unsigned char>(text[index + 3U]);
        const bool validSecond = (first == 0xF0U && second >= 0x90U && second <= 0xBFU) ||
                                 (first == 0xF4U && second >= 0x80U && second <= 0x8FU) ||
                                 (first >= 0xF1U && first <= 0xF3U && isContinuationByte(second));
        return validSecond && isContinuationByte(third) && isContinuationByte(fourth);
    }

    [[nodiscard]] std::size_t validUtf8SequenceSize(std::string_view text,
                                                    std::size_t index) noexcept {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7FU) {
            return 1U;
        }
        if (isValidTwoByteSequence(text, index)) {
            return 2U;
        }
        if (first >= 0xE0U && first <= 0xEFU && isValidThreeByteSequence(text, index)) {
            return 3U;
        }
        if (first >= 0xF0U && first <= 0xF4U && isValidFourByteSequence(text, index)) {
            return 4U;
        }
        return 0U;
    }

    [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept {
        std::size_t index = 0U;
        while (index < text.size()) {
            const std::size_t sequenceSize = validUtf8SequenceSize(text, index);
            if (sequenceSize == 0U) {
                return false;
            }
            index += sequenceSize;
        }
        return true;
    }

    [[nodiscard]] std::uint32_t readRequiredUtf8(EditorProjectNativeStringView value,
                                                 std::uint64_t maxByteLength,
                                                 std::string_view fieldName, std::string_view& text,
                                                 std::string& message) {
        if (value.data == nullptr || value.byteLength == 0U || value.byteLength > maxByteLength ||
            value.byteLength > std::numeric_limits<std::size_t>::max()) {
            message = std::string{fieldName} + " is missing or exceeds its size limit.";
            return EditorProjectNativeStatus_InvalidArgument;
        }

        text = std::string_view{value.data, static_cast<std::size_t>(value.byteLength)};
        if (text.find('\0') != std::string_view::npos) {
            message = std::string{fieldName} + " contains an embedded NUL byte.";
            return EditorProjectNativeStatus_InvalidArgument;
        }
        if (!isValidUtf8(text)) {
            message = std::string{fieldName} + " is not valid UTF-8.";
            return EditorProjectNativeStatus_InvalidUtf8;
        }
        return EditorProjectNativeStatus_Success;
    }

    [[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
        std::u8string utf8;
        utf8.reserve(text.size());
        for (const char value : text) {
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
        }
        return std::filesystem::path{utf8};
    }

    [[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
        const std::u8string utf8 = path.u8string();
        return std::string{utf8.begin(), utf8.end()};
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    absoluteProjectRoot(std::string_view rootText, std::string& message) {
        std::error_code error;
        std::filesystem::path root = std::filesystem::absolute(pathFromUtf8(rootText), error);
        if (error || root.empty()) {
            message = "Could not resolve the project root: " + error.message();
            return std::nullopt;
        }
        return root.lexically_normal();
    }

    [[nodiscard]] bool ensureDirectory(const std::filesystem::path& path, std::string& message) {
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(path, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            message = "Could not inspect directory '" + pathToUtf8(path) + "': " + error.message();
            return false;
        }
        if (!error && std::filesystem::exists(status)) {
            if (!std::filesystem::is_directory(status)) {
                message = "Expected a directory at '" + pathToUtf8(path) + "'.";
                return false;
            }
            return true;
        }

        error.clear();
        std::filesystem::create_directories(path, error);
        if (error) {
            message = "Could not create directory '" + pathToUtf8(path) + "': " + error.message();
            return false;
        }
        return true;
    }

    [[nodiscard]] ProjectOperationResult openProjectRoot(std::string_view rootText) {
        std::string message;
        auto root = absoluteProjectRoot(rootText, message);
        if (!root) {
            return failure(EditorProjectNativeStatus_InvalidArgument, std::move(message));
        }

        std::error_code error;
        const bool isDirectory = std::filesystem::is_directory(*root, error);
        if (error || !isDirectory) {
            message = error ? error.message() : "the selected path is not a directory";
            return failure(EditorProjectNativeStatus_InvalidProject,
                           "Could not open project root '" + pathToUtf8(*root) + "': " + message +
                               ".");
        }

        const std::filesystem::path descriptorPath =
            *root / std::string{asharia::project::kDefaultAshariaProjectFileName};
        auto descriptor = asharia::project::readAshariaProjectDescriptorFile(descriptorPath);
        if (!descriptor) {
            return failure(EditorProjectNativeStatus_InvalidProject,
                           std::move(descriptor.error().message));
        }

        return success(ProjectSnapshot{
            .root = std::move(*root),
            .descriptor = std::move(*descriptor),
        });
    }

    [[nodiscard]] asharia::project::AshariaProjectDescriptor
    makeMinimalDescriptor(std::string_view projectName, asharia::project::ProjectId projectId) {
        return asharia::project::AshariaProjectDescriptor{
            .projectName = std::string{projectName},
            .projectId = projectId,
            .assetSourceRoots =
                {
                    asharia::project::AssetSourceRootDesc{
                        .rootName = "project-assets",
                        .directory = "Assets",
                        .sourcePathPrefix = "Assets",
                    },
                },
            .assetCacheRoot = ".asharia/cache/assets",
            .assetDiscovery =
                asharia::project::AssetDiscoveryDesc{
                    .ignoredDirectoryNames = {".git", ".asharia"},
                },
        };
    }

    [[nodiscard]] ProjectOperationResult createMinimalProject(MinimalProjectInput input) {
        auto projectId = asharia::project::parseProjectId(input.projectId);
        if (!projectId) {
            return failure(EditorProjectNativeStatus_InvalidProject,
                           std::move(projectId.error().message));
        }

        asharia::project::AshariaProjectDescriptor descriptor =
            makeMinimalDescriptor(input.name, *projectId);
        auto validDescriptor = asharia::project::validateAshariaProjectDescriptor(descriptor);
        if (!validDescriptor) {
            return failure(EditorProjectNativeStatus_InvalidProject,
                           std::move(validDescriptor.error().message));
        }

        std::string message;
        auto root = absoluteProjectRoot(input.root, message);
        if (!root) {
            return failure(EditorProjectNativeStatus_InvalidArgument, std::move(message));
        }
        if (!ensureDirectory(*root, message)) {
            return failure(EditorProjectNativeStatus_IoFailure, std::move(message));
        }

        const std::filesystem::path privateDirectory = *root / ".asharia";
        if (!ensureDirectory(privateDirectory, message)) {
            return failure(EditorProjectNativeStatus_IoFailure, std::move(message));
        }

        auto lockAttempt =
            asharia::core::tryAcquireExclusiveFileLock(privateDirectory / "project-create.lock");
        if (!lockAttempt) {
            return failure(EditorProjectNativeStatus_IoFailure,
                           std::move(lockAttempt.error().message));
        }
        auto projectCreateLock = std::move(*lockAttempt);
        if (!projectCreateLock) {
            return failure(EditorProjectNativeStatus_Busy,
                           "Another process is creating this project.");
        }

        const std::filesystem::path descriptorPath =
            *root / std::string{asharia::project::kDefaultAshariaProjectFileName};
        std::error_code existsError;
        const bool descriptorExists = std::filesystem::exists(descriptorPath, existsError);
        if (existsError) {
            return failure(EditorProjectNativeStatus_IoFailure,
                           "Could not inspect project descriptor '" + pathToUtf8(descriptorPath) +
                               "': " + existsError.message());
        }
        if (descriptorExists) {
            return failure(EditorProjectNativeStatus_AlreadyExists,
                           "A project descriptor already exists at '" + pathToUtf8(descriptorPath) +
                               "'.");
        }

        if (!ensureDirectory(*root / "Assets", message) ||
            !ensureDirectory(*root / ".asharia" / "cache" / "assets", message)) {
            return failure(EditorProjectNativeStatus_IoFailure, std::move(message));
        }

        auto written =
            asharia::project::writeAshariaProjectDescriptorFile(descriptorPath, descriptor);
        if (!written) {
            return failure(EditorProjectNativeStatus_IoFailure, std::move(written.error().message));
        }

        auto persisted = asharia::project::readAshariaProjectDescriptorFile(descriptorPath);
        if (!persisted) {
            return failure(EditorProjectNativeStatus_InvalidProject,
                           std::move(persisted.error().message));
        }

        return success(ProjectSnapshot{
            .root = std::move(*root),
            .descriptor = std::move(*persisted),
        });
    }

    [[nodiscard]] bool hasSupportedHeader(const EditorProjectNativeAbiHeader& header,
                                          std::size_t requiredSize) noexcept {
        return header.abiVersion == EDITOR_NATIVE_ABI_VERSION && header.structSize >= requiredSize;
    }

    [[nodiscard]] EditorProjectNativeResult emptyResult(std::uint32_t status) noexcept {
        return EditorProjectNativeResult{
            .header =
                EditorProjectNativeAbiHeader{
                    .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                    .structSize = static_cast<std::uint32_t>(sizeof(EditorProjectNativeResult)),
                },
            .status = status,
            .projectRootUtf8 = nullptr,
            .projectRootByteLength = 0U,
            .projectNameUtf8 = nullptr,
            .projectNameByteLength = 0U,
            .projectIdUtf8 = nullptr,
            .projectIdByteLength = 0U,
            .messageUtf8 = nullptr,
            .messageByteLength = 0U,
        };
    }

    [[nodiscard]] OwnedText copyText(std::string_view text) {
        if (text.empty()) {
            return {};
        }
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
        auto copy = std::make_unique_for_overwrite<char[]>(text.size() + 1U);
        std::ranges::copy(text, copy.get());
        copy[text.size()] = '\0';
        return copy;
    }

    void assignResult(EditorProjectNativeResult& result, const ProjectOperationResult& operation) {
        const std::string rootText =
            operation.snapshot ? pathToUtf8(operation.snapshot->root) : std::string{};
        const std::string_view name =
            operation.snapshot ? operation.snapshot->descriptor.projectName : std::string_view{};
        const std::string projectId =
            operation.snapshot
                ? asharia::project::formatProjectId(operation.snapshot->descriptor.projectId)
                : std::string{};

        OwnedText root = copyText(rootText);
        OwnedText projectName = copyText(name);
        OwnedText projectIdCopy = copyText(projectId);
        OwnedText message = copyText(operation.message);

        result = emptyResult(operation.status);
        result.projectRootUtf8 = root.release();
        result.projectRootByteLength = rootText.size();
        result.projectNameUtf8 = projectName.release();
        result.projectNameByteLength = name.size();
        result.projectIdUtf8 = projectIdCopy.release();
        result.projectIdByteLength = projectId.size();
        result.messageUtf8 = message.release();
        result.messageByteLength = operation.message.size();
    }

    [[nodiscard]] std::uint32_t finish(EditorProjectNativeResult& result,
                                       const ProjectOperationResult& operation) {
        assignResult(result, operation);
        return operation.status;
    }

    [[nodiscard]] std::uint32_t finishInternalError(EditorProjectNativeResult& result) noexcept {
        result = emptyResult(EditorProjectNativeStatus_InternalError);
        return EditorProjectNativeStatus_InternalError;
    }

} // namespace

extern "C" {

std::uint32_t EDITOR_NATIVE_CALL editor_project_open(const EditorProjectNativeOpenRequest* request,
                                                     EditorProjectNativeResult* result) {
    if (result == nullptr) {
        return EditorProjectNativeStatus_InvalidArgument;
    }
    *result = emptyResult(EditorProjectNativeStatus_InvalidArgument);
    if (request == nullptr) {
        return EditorProjectNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(EditorProjectNativeOpenRequest))) {
        result->status = EditorProjectNativeStatus_UnsupportedAbi;
        return EditorProjectNativeStatus_UnsupportedAbi;
    }

    try {
        std::string_view rootText;
        std::string message;
        const std::uint32_t rootStatus = readRequiredUtf8(
            request->projectRootUtf8, kMaxProjectRootUtf8Bytes, "Project root", rootText, message);
        if (rootStatus != EditorProjectNativeStatus_Success) {
            return finish(*result, failure(rootStatus, std::move(message)));
        }
        return finish(*result, openProjectRoot(rootText));
    } catch (...) {
        return finishInternalError(*result);
    }
}

std::uint32_t EDITOR_NATIVE_CALL editor_project_create_minimal(
    const EditorProjectNativeCreateRequest* request, EditorProjectNativeResult* result) {
    if (result == nullptr) {
        return EditorProjectNativeStatus_InvalidArgument;
    }
    *result = emptyResult(EditorProjectNativeStatus_InvalidArgument);
    if (request == nullptr) {
        return EditorProjectNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(EditorProjectNativeCreateRequest))) {
        result->status = EditorProjectNativeStatus_UnsupportedAbi;
        return EditorProjectNativeStatus_UnsupportedAbi;
    }

    try {
        std::string_view rootText;
        std::string_view projectName;
        std::string_view projectId;
        std::string message;
        std::uint32_t inputStatus = readRequiredUtf8(
            request->projectRootUtf8, kMaxProjectRootUtf8Bytes, "Project root", rootText, message);
        if (inputStatus == EditorProjectNativeStatus_Success) {
            inputStatus = readRequiredUtf8(request->projectNameUtf8, kMaxProjectNameUtf8Bytes,
                                           "Project name", projectName, message);
        }
        if (inputStatus == EditorProjectNativeStatus_Success) {
            inputStatus = readRequiredUtf8(request->projectIdUtf8, kMaxProjectIdUtf8Bytes,
                                           "Project id", projectId, message);
        }
        if (inputStatus != EditorProjectNativeStatus_Success) {
            return finish(*result, failure(inputStatus, std::move(message)));
        }
        return finish(*result, createMinimalProject(MinimalProjectInput{
                                   .root = rootText,
                                   .name = projectName,
                                   .projectId = projectId,
                               }));
    } catch (...) {
        return finishInternalError(*result);
    }
}

void EDITOR_NATIVE_CALL editor_project_release_result(EditorProjectNativeResult result) {
    const OwnedText root{static_cast<char*>(result.projectRootUtf8)};
    const OwnedText projectName{static_cast<char*>(result.projectNameUtf8)};
    const OwnedText projectId{static_cast<char*>(result.projectIdUtf8)};
    const OwnedText message{static_cast<char*>(result.messageUtf8)};
}

} // extern "C"
