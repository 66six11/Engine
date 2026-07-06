#include "native_bridge/frame_debugger_native_api.hpp"

#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/renderer_basic_vulkan/render_view.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_frame_debugger_snapshot_projector.hpp"

namespace {

    [[nodiscard]] asharia::editor::EditorFrameDebugger& frameDebugger() {
        static asharia::editor::EditorFrameDebugger debugger;
        return debugger;
    }

    [[nodiscard]] std::mutex& frameDebuggerMutex() {
        static std::mutex mutex;
        return mutex;
    }

    [[nodiscard]] std::uint32_t unavailableStatus() {
        return EditorFrameDebuggerNativeStatus_Unavailable;
    }

    [[nodiscard]] std::uint32_t invalidArgumentStatus() {
        return EditorFrameDebuggerNativeStatus_InvalidArgument;
    }

    [[nodiscard]] std::uint32_t unsupportedAbiStatus() {
        return EditorFrameDebuggerNativeStatus_UnsupportedAbi;
    }

    [[nodiscard]] std::uint32_t internalErrorStatus() {
        return EditorFrameDebuggerNativeStatus_InternalError;
    }

    [[nodiscard]] std::uint32_t successStatus() {
        return EditorFrameDebuggerNativeStatus_Success;
    }

    [[nodiscard]] std::uint32_t snapshotBufferStructSize() {
        return static_cast<std::uint32_t>(sizeof(EditorFrameDebuggerNativeSnapshotBuffer));
    }

    [[nodiscard]] bool hasSupportedHeader(const EditorFrameDebuggerNativeSnapshotBuffer& snapshot) {
        return snapshot.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               snapshot.header.structSize >= snapshotBufferStructSize();
    }

    [[nodiscard]] EditorFrameDebuggerNativeSnapshotBuffer
    clearedSnapshotBuffer(EditorFrameDebuggerNativeAbiHeader header) {
        return EditorFrameDebuggerNativeSnapshotBuffer{
            .header = header,
            .data = nullptr,
            .byteLength = 0U,
            .format = EditorFrameDebuggerNativeSnapshotFormat_Unknown,
        };
    }

    [[nodiscard]] bool tryMakeStringView(EditorFrameDebuggerNativeStringView value,
                                         std::string_view& result) {
        if (value.data == nullptr || value.byteLength == 0U ||
            value.byteLength > std::numeric_limits<std::size_t>::max()) {
            return false;
        }

        result = std::string_view{
            value.data,
            static_cast<std::size_t>(value.byteLength),
        };
        return result.find('\0') == std::string_view::npos;
    }

    [[nodiscard]] std::optional<asharia::BasicRenderViewExecutionEventId>
    parseExecutionEventId(std::string_view value) {
        constexpr std::string_view kPrefix{"event:"};
        if (!value.starts_with(kPrefix)) {
            return std::nullopt;
        }

        const std::string_view digits = value.substr(kPrefix.size());
        if (digits.empty()) {
            return std::nullopt;
        }

        std::size_t consumed{};
        std::uint64_t parsed{};
        try {
            parsed = std::stoull(std::string{digits}, &consumed, 10);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (consumed != digits.size()) {
            return std::nullopt;
        }

        return asharia::BasicRenderViewExecutionEventId{.value = parsed};
    }

} // namespace

extern "C" {

std::uint32_t EDITOR_NATIVE_CALL
editor_frame_debugger_acquire_snapshot(EditorFrameDebuggerNativeSnapshotBuffer* snapshot) {
    if (snapshot == nullptr) {
        return invalidArgumentStatus();
    }

    const EditorFrameDebuggerNativeAbiHeader header = snapshot->header;
    if (!hasSupportedHeader(*snapshot)) {
        return unsupportedAbiStatus();
    }

    *snapshot = clearedSnapshotBuffer(header);

    std::lock_guard lock{frameDebuggerMutex()};
    auto json = asharia::editor::writeFrameDebuggerSnapshotJson(frameDebugger());
    if (!json || json->empty()) {
        return internalErrorStatus();
    }

    // The C ABI returns a native-owned byte buffer; callers transfer it back
    // through editor_frame_debugger_release_snapshot.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
    std::unique_ptr<char[]> buffer;
    try {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
        buffer = std::make_unique_for_overwrite<char[]>(json->size() + 1U);
    } catch (const std::bad_alloc&) {
        return internalErrorStatus();
    }

    std::memcpy(buffer.get(), json->data(), json->size());
    buffer[json->size()] = '\0';
    *snapshot = EditorFrameDebuggerNativeSnapshotBuffer{
        .header = header,
        .data = buffer.release(),
        .byteLength = static_cast<std::uint64_t>(json->size()),
        .format = EditorFrameDebuggerNativeSnapshotFormat_JsonUtf8,
    };
    return successStatus();
}

void EDITOR_NATIVE_CALL
editor_frame_debugger_release_snapshot(EditorFrameDebuggerNativeSnapshotBuffer snapshot) {
    const std::unique_ptr<char[]> data{static_cast<char*>(snapshot.data)};
}

std::uint32_t EDITOR_NATIVE_CALL editor_frame_debugger_request_capture() {
    std::lock_guard lock{frameDebuggerMutex()};
    return frameDebugger().requestCapture() ? successStatus() : unavailableStatus();
}

std::uint32_t EDITOR_NATIVE_CALL editor_frame_debugger_request_resume() {
    std::lock_guard lock{frameDebuggerMutex()};
    return frameDebugger().requestResume() ? successStatus() : unavailableStatus();
}

std::uint32_t EDITOR_NATIVE_CALL editor_frame_debugger_select_execution_event(
    EditorFrameDebuggerNativeStringView executionEventIdUtf8) {
    std::string_view executionEventId;
    if (!tryMakeStringView(executionEventIdUtf8, executionEventId)) {
        return invalidArgumentStatus();
    }

    const std::optional<asharia::BasicRenderViewExecutionEventId> eventId =
        parseExecutionEventId(executionEventId);
    if (!eventId) {
        return invalidArgumentStatus();
    }

    std::lock_guard lock{frameDebuggerMutex()};
    return frameDebugger().selectReplayEvent(*eventId) ? successStatus() : unavailableStatus();
}

} // extern "C"
