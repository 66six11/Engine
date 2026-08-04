#include "asharia/project/project_native_api.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace {

    constexpr std::uint64_t kMaxProjectPathBytes = 32ULL * 1024ULL;
    constexpr std::uint64_t kMaxProjectNameBytes = 255ULL;
    constexpr std::uint64_t kMaxProjectIdBytes = 36ULL;
    constexpr std::size_t kMaxMessageBytes = 64ULL * 1024ULL;

    struct ProjectOperationResult {
        std::uint32_t status{AshariaProjectNativeStatus_InternalError};
        std::optional<asharia::project::OpenedAshariaProject> project;
        std::string message;
    };

    struct CreateProjectInput {
        std::string_view parentDirectory;
        std::string_view projectName;
        std::string_view projectIdText;
    };

    [[nodiscard]] ProjectOperationResult failure(std::uint32_t status, std::string message) {
        if (message.size() > kMaxMessageBytes) {
            message.resize(kMaxMessageBytes);
        }
        return ProjectOperationResult{
            .status = status,
            .project = std::nullopt,
            .message = std::move(message),
        };
    }

    [[nodiscard]] ProjectOperationResult
    success(asharia::project::OpenedAshariaProject project) {
        return ProjectOperationResult{
            .status = AshariaProjectNativeStatus_Success,
            .project = std::move(project),
            .message = {},
        };
    }

    [[nodiscard]] constexpr bool isContinuationByte(unsigned char value) noexcept {
        return (value & 0xC0U) == 0x80U;
    }

    [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept {
        std::size_t index = 0U;
        while (index < text.size()) {
            const auto first = static_cast<unsigned char>(text[index]);
            if (first <= 0x7FU) {
                ++index;
                continue;
            }

            std::size_t sequenceSize = 0U;
            if (first >= 0xC2U && first <= 0xDFU) {
                sequenceSize = 2U;
            } else if (first >= 0xE0U && first <= 0xEFU) {
                sequenceSize = 3U;
            } else if (first >= 0xF0U && first <= 0xF4U) {
                sequenceSize = 4U;
            } else {
                return false;
            }
            if (index + sequenceSize > text.size()) {
                return false;
            }

            const auto second = static_cast<unsigned char>(text[index + 1U]);
            if (!isContinuationByte(second) ||
                (first == 0xE0U && second < 0xA0U) ||
                (first == 0xEDU && second > 0x9FU) ||
                (first == 0xF0U && second < 0x90U) ||
                (first == 0xF4U && second > 0x8FU)) {
                return false;
            }
            for (std::size_t continuation = 2U; continuation < sequenceSize; ++continuation) {
                if (!isContinuationByte(
                        static_cast<unsigned char>(text[index + continuation]))) {
                    return false;
                }
            }
            index += sequenceSize;
        }
        return true;
    }

    [[nodiscard]] std::uint32_t readRequiredUtf8(AshariaProjectNativeStringView value,
                                                  std::uint64_t maxByteLength,
                                                  std::string_view fieldName,
                                                  std::string_view& text,
                                                  std::string& message) {
        if (value.data == nullptr || value.byteLength == 0U ||
            value.byteLength > maxByteLength ||
            value.byteLength > std::numeric_limits<std::size_t>::max()) {
            message = std::string{fieldName} + " is missing or exceeds its size limit.";
            return AshariaProjectNativeStatus_InvalidArgument;
        }

        text = std::string_view{value.data, static_cast<std::size_t>(value.byteLength)};
        if (text.find('\0') != std::string_view::npos) {
            message = std::string{fieldName} + " contains an embedded NUL byte.";
            return AshariaProjectNativeStatus_InvalidArgument;
        }
        if (!isValidUtf8(text)) {
            message = std::string{fieldName} + " is not valid UTF-8.";
            return AshariaProjectNativeStatus_InvalidUtf8;
        }
        return AshariaProjectNativeStatus_Success;
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

    [[nodiscard]] std::uint32_t mapProjectError(const asharia::Error& error) noexcept {
        using asharia::project::AshariaProjectIoErrorCode;
        switch (static_cast<AshariaProjectIoErrorCode>(error.code)) {
        case AshariaProjectIoErrorCode::AlreadyExists:
            return AshariaProjectNativeStatus_AlreadyExists;
        case AshariaProjectIoErrorCode::Busy:
            return AshariaProjectNativeStatus_Busy;
        case AshariaProjectIoErrorCode::IoFailure:
            return AshariaProjectNativeStatus_IoFailure;
        case AshariaProjectIoErrorCode::InvalidProject:
        case AshariaProjectIoErrorCode::DescriptorIo:
            return AshariaProjectNativeStatus_InvalidProject;
        default:
            return AshariaProjectNativeStatus_InternalError;
        }
    }

    [[nodiscard]] ProjectOperationResult openProject(std::string_view projectPath) {
        auto opened = asharia::project::openAshariaProject(pathFromUtf8(projectPath));
        if (!opened) {
            return failure(mapProjectError(opened.error()), std::move(opened.error().message));
        }
        return success(std::move(*opened));
    }

    [[nodiscard]] ProjectOperationResult createProject(const CreateProjectInput& input) {
        auto projectId = asharia::project::parseProjectId(input.projectIdText);
        if (!projectId) {
            return failure(AshariaProjectNativeStatus_InvalidProject,
                           std::move(projectId.error().message));
        }

        auto created = asharia::project::createMinimalAshariaProject(
            asharia::project::MinimalAshariaProjectCreate{
                .parentDirectory = pathFromUtf8(input.parentDirectory),
                .projectName = std::string{input.projectName},
                .projectId = *projectId,
            });
        if (!created) {
            return failure(mapProjectError(created.error()),
                           std::move(created.error().message));
        }
        return success(std::move(*created));
    }

    [[nodiscard]] AshariaProjectNativeResult emptyResult(std::uint32_t status) noexcept {
        return AshariaProjectNativeResult{
            .header =
                AshariaProjectNativeAbiHeader{
                    .abiVersion = ASHARIA_PROJECT_NATIVE_ABI_VERSION,
                    .structSize = static_cast<std::uint32_t>(sizeof(AshariaProjectNativeResult)),
                },
            .status = status,
            .reserved = 0U,
            .requiredByteLength = 0U,
            .projectRootUtf8 = {},
            .projectNameUtf8 = {},
            .projectIdUtf8 = {},
            .messageUtf8 = {},
        };
    }

    [[nodiscard]] bool hasSupportedHeader(const AshariaProjectNativeAbiHeader& header,
                                          std::size_t requiredSize) noexcept {
        return header.abiVersion == ASHARIA_PROJECT_NATIVE_ABI_VERSION &&
               header.structSize >= requiredSize;
    }

    [[nodiscard]] bool appendText(char* responseUtf8, std::uint64_t responseCapacity,
                                  std::uint64_t& cursor, std::string_view text,
                                  AshariaProjectNativeTextSpan& span) noexcept {
        span = AshariaProjectNativeTextSpan{
            .byteOffset = cursor,
            .byteLength = text.size(),
        };
        if (text.empty()) {
            return true;
        }
        if (responseUtf8 == nullptr || cursor > responseCapacity ||
            responseCapacity > std::numeric_limits<std::size_t>::max() ||
            text.size() > responseCapacity - cursor) {
            return false;
        }
        const std::span response{responseUtf8, static_cast<std::size_t>(responseCapacity)};
        const auto destination =
            response.subspan(static_cast<std::size_t>(cursor), text.size());
        std::ranges::copy(text, destination.begin());
        cursor += text.size();
        return true;
    }

    [[nodiscard]] std::uint32_t writeResult(const ProjectOperationResult& operation,
                                            char* responseUtf8,
                                            std::uint64_t responseCapacity,
                                            AshariaProjectNativeResult& result) {
        const std::string root =
            operation.project ? pathToUtf8(operation.project->root) : std::string{};
        const std::string_view name =
            operation.project ? operation.project->descriptor.projectName : std::string_view{};
        const std::string projectId = operation.project
            ? asharia::project::formatProjectId(operation.project->descriptor.projectId)
            : std::string{};

        std::uint64_t required = 0U;
        for (const std::string_view text :
             {std::string_view{root}, name, std::string_view{projectId},
              std::string_view{operation.message}}) {
            if (text.size() > std::numeric_limits<std::uint64_t>::max() - required) {
                result = emptyResult(AshariaProjectNativeStatus_InternalError);
                return result.status;
            }
            required += text.size();
        }

        result = emptyResult(operation.status);
        result.requiredByteLength = required;
        if (required > responseCapacity || (required != 0U && responseUtf8 == nullptr)) {
            result.status = AshariaProjectNativeStatus_BufferTooSmall;
            return result.status;
        }

        std::uint64_t cursor = 0U;
        const bool written =
            appendText(responseUtf8, responseCapacity, cursor, root, result.projectRootUtf8) &&
            appendText(responseUtf8, responseCapacity, cursor, name, result.projectNameUtf8) &&
            appendText(responseUtf8, responseCapacity, cursor, projectId,
                       result.projectIdUtf8) &&
            appendText(responseUtf8, responseCapacity, cursor, operation.message,
                       result.messageUtf8);
        if (!written || cursor != required) {
            result = emptyResult(AshariaProjectNativeStatus_InternalError);
            return result.status;
        }
        return result.status;
    }

    [[nodiscard]] std::uint32_t invalidResultCapacity(AshariaProjectNativeResult* result,
                                                      std::uint64_t resultCapacity) noexcept {
        if (result != nullptr && resultCapacity >= sizeof(AshariaProjectNativeResult)) {
            *result = emptyResult(AshariaProjectNativeStatus_BufferTooSmall);
        }
        return AshariaProjectNativeStatus_BufferTooSmall;
    }

} // namespace

extern "C" {

std::uint32_t ASHARIA_PROJECT_NATIVE_CALL asharia_project_open(
    const AshariaProjectNativeOpenRequest* request, char* responseUtf8,
    std::uint64_t responseCapacity, AshariaProjectNativeResult* result,
    std::uint64_t resultCapacity) {
    if (result == nullptr || resultCapacity < sizeof(AshariaProjectNativeResult)) {
        return invalidResultCapacity(result, resultCapacity);
    }
    *result = emptyResult(AshariaProjectNativeStatus_InvalidArgument);
    if (request == nullptr) {
        return result->status;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaProjectNativeOpenRequest))) {
        result->status = AshariaProjectNativeStatus_UnsupportedAbi;
        return result->status;
    }

    try {
        std::string_view projectPath;
        std::string message;
        const std::uint32_t inputStatus = readRequiredUtf8(
            request->projectPathUtf8, kMaxProjectPathBytes, "Project path", projectPath, message);
        if (inputStatus != AshariaProjectNativeStatus_Success) {
            return writeResult(failure(inputStatus, std::move(message)), responseUtf8,
                               responseCapacity, *result);
        }
        return writeResult(openProject(projectPath), responseUtf8, responseCapacity, *result);
    } catch (...) {
        *result = emptyResult(AshariaProjectNativeStatus_InternalError);
        return result->status;
    }
}

std::uint32_t ASHARIA_PROJECT_NATIVE_CALL asharia_project_create_minimal(
    const AshariaProjectNativeCreateRequest* request, char* responseUtf8,
    std::uint64_t responseCapacity, AshariaProjectNativeResult* result,
    std::uint64_t resultCapacity) {
    if (result == nullptr || resultCapacity < sizeof(AshariaProjectNativeResult)) {
        return invalidResultCapacity(result, resultCapacity);
    }
    *result = emptyResult(AshariaProjectNativeStatus_InvalidArgument);
    if (request == nullptr) {
        return result->status;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaProjectNativeCreateRequest))) {
        result->status = AshariaProjectNativeStatus_UnsupportedAbi;
        return result->status;
    }

    try {
        std::string_view parentDirectory;
        std::string_view projectName;
        std::string_view projectId;
        std::string message;
        std::uint32_t inputStatus = readRequiredUtf8(
            request->parentDirectoryUtf8, kMaxProjectPathBytes, "Project parent directory",
            parentDirectory, message);
        if (inputStatus == AshariaProjectNativeStatus_Success) {
            inputStatus = readRequiredUtf8(request->projectNameUtf8, kMaxProjectNameBytes,
                                           "Project name", projectName, message);
        }
        if (inputStatus == AshariaProjectNativeStatus_Success) {
            inputStatus = readRequiredUtf8(request->projectIdUtf8, kMaxProjectIdBytes,
                                           "Project id", projectId, message);
        }
        if (inputStatus != AshariaProjectNativeStatus_Success) {
            return writeResult(failure(inputStatus, std::move(message)), responseUtf8,
                               responseCapacity, *result);
        }
        return writeResult(
            createProject(CreateProjectInput{
                .parentDirectory = parentDirectory,
                .projectName = projectName,
                .projectIdText = projectId,
            }),
            responseUtf8, responseCapacity, *result);
    } catch (...) {
        *result = emptyResult(AshariaProjectNativeStatus_InternalError);
        return result->status;
    }
}

} // extern "C"

static_assert(sizeof(AshariaProjectNativeAbiHeader) == 8U);
static_assert(sizeof(AshariaProjectNativeStringView) == 16U);
static_assert(sizeof(AshariaProjectNativeOpenRequest) == 24U);
static_assert(sizeof(AshariaProjectNativeCreateRequest) == 56U);
static_assert(sizeof(AshariaProjectNativeTextSpan) == 16U);
static_assert(sizeof(AshariaProjectNativeResult) == 88U);
static_assert(offsetof(AshariaProjectNativeResult, requiredByteLength) == 16U);
static_assert(offsetof(AshariaProjectNativeResult, projectRootUtf8) == 24U);
