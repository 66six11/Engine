#include "native_bridge/frame_debugger_native_smoke.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/core/log.hpp"

#include "native_bridge/frame_debugger_native_api.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool requiredIntegerMemberEquals(const asharia::archive::ArchiveValue& value,
                                                       std::string_view key,
                                                       std::int64_t expected) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr &&
                   member->kind == asharia::archive::ArchiveValueKind::Integer &&
                   member->integerValue == expected;
        }

        [[nodiscard]] bool requiredStringMemberEquals(const asharia::archive::ArchiveValue& value,
                                                      std::string_view key, const char* expected) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr &&
                   member->kind == asharia::archive::ArchiveValueKind::String &&
                   member->stringValue == expected;
        }

        [[nodiscard]] bool
        snapshotHasSchemaState(const EditorFrameDebuggerNativeSnapshotBuffer& snapshot,
                               const char* expectedState) {
            if (snapshot.header.abiVersion != EDITOR_NATIVE_ABI_VERSION ||
                snapshot.header.structSize < sizeof(EditorFrameDebuggerNativeSnapshotBuffer) ||
                snapshot.data == nullptr || snapshot.byteLength == 0U ||
                snapshot.byteLength > std::numeric_limits<std::size_t>::max() ||
                snapshot.format != EditorFrameDebuggerNativeSnapshotFormat_JsonUtf8) {
                asharia::logError(
                    "Editor native bridge smoke acquired an invalid snapshot buffer.");
                return false;
            }

            auto parsed = asharia::archive::readJsonArchive(std::string{
                static_cast<const char*>(snapshot.data),
                static_cast<std::size_t>(snapshot.byteLength),
            });
            if (!parsed) {
                asharia::logError("Editor native bridge smoke acquired invalid JSON.");
                return false;
            }

            if (!requiredIntegerMemberEquals(*parsed, "schemaVersion", 1) ||
                !requiredStringMemberEquals(*parsed, "state", expectedState)) {
                asharia::logError(
                    "Editor native bridge smoke acquired an unexpected snapshot schema/state.");
                return false;
            }
            return true;
        }

        [[nodiscard]] EditorFrameDebuggerNativeSnapshotBuffer makeSnapshotBufferForCall() {
            return EditorFrameDebuggerNativeSnapshotBuffer{
                .header =
                    EditorFrameDebuggerNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorFrameDebuggerNativeSnapshotBuffer)),
                    },
                .data = nullptr,
                .byteLength = 0U,
                .format = EditorFrameDebuggerNativeSnapshotFormat_Unknown,
            };
        }

        [[nodiscard]] EditorFrameDebuggerNativeStringView stringView(std::string_view value) {
            return EditorFrameDebuggerNativeStringView{
                .data = value.data(),
                .byteLength = static_cast<std::uint64_t>(value.size()),
            };
        }

        [[nodiscard]] bool acquireAndValidate(const char* expectedState) {
            EditorFrameDebuggerNativeSnapshotBuffer snapshot = makeSnapshotBufferForCall();
            const std::uint32_t status = editor_frame_debugger_acquire_snapshot(&snapshot);
            if (status != EditorFrameDebuggerNativeStatus_Success) {
                asharia::logError("Editor native bridge smoke could not acquire a snapshot.");
                return false;
            }

            const bool valid = snapshotHasSchemaState(snapshot, expectedState);
            editor_frame_debugger_release_snapshot(snapshot);
            return valid;
        }

    } // namespace

    bool runFrameDebuggerNativeBridgeSmoke() {
        if (!acquireAndValidate("Running")) {
            return false;
        }

        if (editor_frame_debugger_request_capture() != EditorFrameDebuggerNativeStatus_Success) {
            asharia::logError("Editor native bridge smoke could not request capture.");
            return false;
        }

        if (!acquireAndValidate("CaptureRequested")) {
            return false;
        }

        if (editor_frame_debugger_request_resume() != EditorFrameDebuggerNativeStatus_Unavailable) {
            asharia::logError(
                "Editor native bridge smoke allowed resume without a paused capture.");
            return false;
        }

        if (editor_frame_debugger_select_execution_event(stringView("event:7")) !=
            EditorFrameDebuggerNativeStatus_Unavailable) {
            asharia::logError(
                "Editor native bridge smoke selected an event without a paused capture.");
            return false;
        }

        return true;
    }
} // namespace asharia::editor
