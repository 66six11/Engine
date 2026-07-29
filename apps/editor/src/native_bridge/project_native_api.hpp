#pragma once

#include <cstdint>

#include "native_bridge/frame_debugger_native_api.hpp"

extern "C" {

enum EditorProjectNativeStatus : std::uint32_t {
    EditorProjectNativeStatus_Success = 0U,
    EditorProjectNativeStatus_InvalidArgument = 1U,
    EditorProjectNativeStatus_UnsupportedAbi = 2U,
    EditorProjectNativeStatus_InvalidUtf8 = 3U,
    EditorProjectNativeStatus_AlreadyExists = 4U,
    EditorProjectNativeStatus_Busy = 5U,
    EditorProjectNativeStatus_InvalidProject = 6U,
    EditorProjectNativeStatus_IoFailure = 7U,
    EditorProjectNativeStatus_InternalError = 8U,
};

struct EditorProjectNativeAbiHeader {
    std::uint32_t abiVersion;
    std::uint32_t structSize;
};

struct EditorProjectNativeStringView {
    const char* data;
    std::uint64_t byteLength;
};

struct EditorProjectNativeOpenRequest {
    EditorProjectNativeAbiHeader header;
    EditorProjectNativeStringView projectRootUtf8;
};

struct EditorProjectNativeCreateRequest {
    EditorProjectNativeAbiHeader header;
    EditorProjectNativeStringView projectRootUtf8;
    EditorProjectNativeStringView projectNameUtf8;
    EditorProjectNativeStringView projectIdUtf8;
};

struct EditorProjectNativeResult {
    EditorProjectNativeAbiHeader header;
    std::uint32_t status;
    void* projectRootUtf8;
    std::uint64_t projectRootByteLength;
    void* projectNameUtf8;
    std::uint64_t projectNameByteLength;
    void* projectIdUtf8;
    std::uint64_t projectIdByteLength;
    void* messageUtf8;
    std::uint64_t messageByteLength;
};

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_project_open(
    const EditorProjectNativeOpenRequest* request, EditorProjectNativeResult* result);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_project_create_minimal(
    const EditorProjectNativeCreateRequest* request, EditorProjectNativeResult* result);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL
editor_project_release_result(EditorProjectNativeResult result);

} // extern "C"
