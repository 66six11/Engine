#pragma once

#include <stdint.h>

#if defined(_WIN32)
#if defined(ASHARIA_PROJECT_NATIVE_BUILD)
#define ASHARIA_PROJECT_NATIVE_API __declspec(dllexport)
#else
#define ASHARIA_PROJECT_NATIVE_API __declspec(dllimport)
#endif
#define ASHARIA_PROJECT_NATIVE_CALL __cdecl
#else
#define ASHARIA_PROJECT_NATIVE_API __attribute__((visibility("default")))
#define ASHARIA_PROJECT_NATIVE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ASHARIA_PROJECT_NATIVE_ABI_VERSION 1U

typedef enum AshariaProjectNativeStatus {
    AshariaProjectNativeStatus_Success = 0U,
    AshariaProjectNativeStatus_InvalidArgument = 1U,
    AshariaProjectNativeStatus_UnsupportedAbi = 2U,
    AshariaProjectNativeStatus_InvalidUtf8 = 3U,
    AshariaProjectNativeStatus_AlreadyExists = 4U,
    AshariaProjectNativeStatus_Busy = 5U,
    AshariaProjectNativeStatus_InvalidProject = 6U,
    AshariaProjectNativeStatus_IoFailure = 7U,
    AshariaProjectNativeStatus_BufferTooSmall = 8U,
    AshariaProjectNativeStatus_InternalError = 9U,
} AshariaProjectNativeStatus;

typedef struct AshariaProjectNativeAbiHeader {
    uint32_t abiVersion;
    uint32_t structSize;
} AshariaProjectNativeAbiHeader;

typedef struct AshariaProjectNativeStringView {
    const char* data;
    uint64_t byteLength;
} AshariaProjectNativeStringView;

typedef struct AshariaProjectNativeOpenRequest {
    AshariaProjectNativeAbiHeader header;
    AshariaProjectNativeStringView projectPathUtf8;
} AshariaProjectNativeOpenRequest;

typedef struct AshariaProjectNativeCreateRequest {
    AshariaProjectNativeAbiHeader header;
    AshariaProjectNativeStringView parentDirectoryUtf8;
    AshariaProjectNativeStringView projectNameUtf8;
    AshariaProjectNativeStringView projectIdUtf8;
} AshariaProjectNativeCreateRequest;

typedef struct AshariaProjectNativeTextSpan {
    uint64_t byteOffset;
    uint64_t byteLength;
} AshariaProjectNativeTextSpan;

typedef struct AshariaProjectNativeResult {
    AshariaProjectNativeAbiHeader header;
    uint32_t status;
    uint32_t reserved;
    uint64_t requiredByteLength;
    AshariaProjectNativeTextSpan projectRootUtf8;
    AshariaProjectNativeTextSpan projectNameUtf8;
    AshariaProjectNativeTextSpan projectIdUtf8;
    AshariaProjectNativeTextSpan messageUtf8;
} AshariaProjectNativeResult;

ASHARIA_PROJECT_NATIVE_API uint32_t ASHARIA_PROJECT_NATIVE_CALL asharia_project_open(
    const AshariaProjectNativeOpenRequest* request,
    char* responseUtf8,
    uint64_t responseCapacity,
    AshariaProjectNativeResult* result,
    uint64_t resultCapacity);

ASHARIA_PROJECT_NATIVE_API uint32_t ASHARIA_PROJECT_NATIVE_CALL asharia_project_create_minimal(
    const AshariaProjectNativeCreateRequest* request,
    char* responseUtf8,
    uint64_t responseCapacity,
    AshariaProjectNativeResult* result,
    uint64_t resultCapacity);

#ifdef __cplusplus
} // extern "C"
#endif
