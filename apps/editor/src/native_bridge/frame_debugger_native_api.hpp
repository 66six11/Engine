#pragma once

#include <cstdint>

#define EDITOR_NATIVE_ABI_VERSION 1U

#if defined(_WIN32)
#define EDITOR_NATIVE_CALL __cdecl
#if defined(EDITOR_NATIVE_BUILD)
#define EDITOR_NATIVE_API __declspec(dllexport)
#else
#define EDITOR_NATIVE_API __declspec(dllimport)
#endif
#else
#define EDITOR_NATIVE_CALL
#define EDITOR_NATIVE_API __attribute__((visibility("default")))
#endif

extern "C" {

enum EditorFrameDebuggerNativeStatus : std::uint32_t {
    EditorFrameDebuggerNativeStatus_Success = 0U,
    EditorFrameDebuggerNativeStatus_InvalidArgument = 1U,
    EditorFrameDebuggerNativeStatus_Unavailable = 2U,
    EditorFrameDebuggerNativeStatus_UnsupportedAbi = 3U,
    EditorFrameDebuggerNativeStatus_InternalError = 4U,
};

enum EditorFrameDebuggerNativeSnapshotFormat : std::uint32_t {
    EditorFrameDebuggerNativeSnapshotFormat_Unknown = 0U,
    EditorFrameDebuggerNativeSnapshotFormat_JsonUtf8 = 1U,
};

struct EditorFrameDebuggerNativeAbiHeader {
    std::uint32_t abiVersion;
    std::uint32_t structSize;
};

struct EditorFrameDebuggerNativeStringView {
    const char* data;
    std::uint64_t byteLength;
};

struct EditorFrameDebuggerNativeSnapshotBuffer {
    EditorFrameDebuggerNativeAbiHeader header;
    void* data;
    std::uint64_t byteLength;
    std::uint32_t format;
};

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_frame_debugger_acquire_snapshot(EditorFrameDebuggerNativeSnapshotBuffer* snapshot);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL
editor_frame_debugger_release_snapshot(EditorFrameDebuggerNativeSnapshotBuffer snapshot);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_frame_debugger_request_capture();

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_frame_debugger_request_resume();

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_frame_debugger_select_execution_event(
    EditorFrameDebuggerNativeStringView executionEventIdUtf8);

} // extern "C"
