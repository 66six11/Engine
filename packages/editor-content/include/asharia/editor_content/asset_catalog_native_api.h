#pragma once

#include <stdint.h>

#if defined(_WIN32)
#if defined(ASHARIA_EDITOR_CONTENT_NATIVE_BUILD)
#define ASHARIA_EDITOR_CONTENT_NATIVE_API __declspec(dllexport)
#else
#define ASHARIA_EDITOR_CONTENT_NATIVE_API __declspec(dllimport)
#endif
#define ASHARIA_EDITOR_CONTENT_NATIVE_CALL __cdecl
#else
#define ASHARIA_EDITOR_CONTENT_NATIVE_API __attribute__((visibility("default")))
#define ASHARIA_EDITOR_CONTENT_NATIVE_CALL
#endif

#if defined(__cplusplus)
#define ASHARIA_EDITOR_CONTENT_NATIVE_NOEXCEPT noexcept
#else
#define ASHARIA_EDITOR_CONTENT_NATIVE_NOEXCEPT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ASHARIA_EDITOR_CONTENT_NATIVE_ABI_VERSION 1U

typedef enum AshariaEditorContentNativeStatus {
    AshariaEditorContentNativeStatus_Success = 0U,
    AshariaEditorContentNativeStatus_InvalidArgument = 1U,
    AshariaEditorContentNativeStatus_UnsupportedAbi = 2U,
    AshariaEditorContentNativeStatus_InvalidUtf8 = 3U,
    AshariaEditorContentNativeStatus_InvalidProject = 4U,
    AshariaEditorContentNativeStatus_IoFailure = 5U,
    AshariaEditorContentNativeStatus_LimitExceeded = 6U,
    AshariaEditorContentNativeStatus_Cancelled = 7U,
    AshariaEditorContentNativeStatus_BufferTooSmall = 8U,
    AshariaEditorContentNativeStatus_InternalError = 9U,
} AshariaEditorContentNativeStatus;

typedef struct AshariaEditorContentNativeAbiHeader {
    uint32_t abiVersion;
    uint32_t structSize;
} AshariaEditorContentNativeAbiHeader;

typedef struct AshariaEditorContentNativeStringView {
    const char* data;
    uint64_t byteLength;
} AshariaEditorContentNativeStringView;

typedef struct AshariaEditorContentNativeLimits {
    uint64_t maxSourceFiles;
    uint64_t maxTotalSourceBytes;
    uint64_t maxDiagnostics;
    uint64_t maxResponseBytes;
} AshariaEditorContentNativeLimits;

typedef struct AshariaEditorContentNativeQueryRequest {
    AshariaEditorContentNativeAbiHeader header;
    AshariaEditorContentNativeStringView projectPathUtf8;
    AshariaEditorContentNativeStringView targetProfileUtf8;
    AshariaEditorContentNativeStringView productManifestPathUtf8;
    AshariaEditorContentNativeLimits limits;
} AshariaEditorContentNativeQueryRequest;

typedef struct AshariaEditorContentNativeTextSpan {
    uint64_t byteOffset;
    uint64_t byteLength;
} AshariaEditorContentNativeTextSpan;

typedef struct AshariaEditorContentNativeResult {
    AshariaEditorContentNativeAbiHeader header;
    uint32_t operationStatus;
    uint32_t reserved;
    uint64_t requiredByteLength;
    AshariaEditorContentNativeTextSpan payloadJsonUtf8;
    AshariaEditorContentNativeTextSpan messageUtf8;
} AshariaEditorContentNativeResult;

ASHARIA_EDITOR_CONTENT_NATIVE_API uint32_t ASHARIA_EDITOR_CONTENT_NATIVE_CALL
asharia_editor_content_query(const AshariaEditorContentNativeQueryRequest* request,
                             void* responseBuffer, uint64_t responseCapacity,
                             AshariaEditorContentNativeResult* result,
                             uint64_t resultCapacity) ASHARIA_EDITOR_CONTENT_NATIVE_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif
