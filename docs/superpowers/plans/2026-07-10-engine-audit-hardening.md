# Engine Audit Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Harden native-engine file I/O, asset publication and parsing, Vulkan swapchain/enumeration lifetime, and repository verification gates without changing Studio.

**Architecture:** asharia::core supplies platform-independent bounded reads and atomic single-file replacement backed by focused Windows/POSIX implementations. Asset publication and blob limits remain asset-pipeline policy, synchronous RHI recreation destroys one bounded old resource set after queue idle, and clean test-enabled presets plus Windows CI prove the native code path.

**Tech Stack:** C++23, CMake 3.28 presets, Conan 2.26.2, Ninja, MSVC 19.4, ClangCL/clang-tidy 19, Vulkan 1.4, PowerShell, GitHub Actions.

## Global Constraints

- Studio, Avalonia, managed code, and Studio-only native bridge behavior are outside scope.
- Run powershell -ExecutionPolicy Bypass -File scripts\bootstrap-conan.ps1 before CMake when Conan toolchains are absent.
- Keep build/conan/ and build/cmake/ separate.
- C/C++ files use UTF-8 with BOM; all other text files use UTF-8 without BOM.
- asharia::rhi_vulkan must not depend on RenderGraph.
- renderer_basic stays backend-neutral.
- Packages never include another package's src/.
- Do not add vkDeviceWaitIdle to resize or render paths.
- Check every VkResult; destructors that cannot return must log failure context.
- Follow repository naming and conventional-commit rules.
- Every behavior change starts with a failing focused test.
- Each task ends in one independently reviewable commit.

---

### Task 1: Add bounded Core file reads

**Files:**
- Create: engine/core/include/asharia/core/file_io.hpp
- Create: engine/core/src/file_io.cpp
- Create: engine/core/src/file_io_internal.hpp
- Create: engine/core/tests/file_io_tests.cpp
- Modify: engine/core/CMakeLists.txt
- Modify: engine/core/asharia.package.json

**Interfaces:**
- Consumes: asharia::Result, asharia::ErrorDomain::Core.
- Produces: FileReadLimits, readFileBytes(), readFileText().

- [ ] **Step 1: Add the public contract and failing tests**

Add this contract:

~~~cpp
namespace asharia::core {

    struct FileReadLimits {
        std::uint64_t maxBytes{};
    };

    [[nodiscard]] Result<std::vector<std::byte>>
    readFileBytes(const std::filesystem::path& path, FileReadLimits limits);

    [[nodiscard]] Result<std::string>
    readFileText(const std::filesystem::path& path, FileReadLimits limits);

} // namespace asharia::core
~~~

Create tests named rejectsZeroReadLimit, readsAtExactByteLimit, rejectsFileAboveByteLimit, readsEmptyFile, and rejectsGrowthAfterMeasuredSize. The growth test calls the private readBoundedStream seam with measured size 3 and a 4-byte stream and requires an error containing "grew while it was being read".

- [ ] **Step 2: Register and prove red state**

Add asharia-core-file-io-tests behind ASHARIA_BUILD_TESTS, link only asharia::core, and register it in the package manifest.

Run:

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug -DASHARIA_BUILD_TESTS=ON && cmake --build --preset msvc-debug --target asharia-core-file-io-tests"
~~~

Expected: compile/link failure because the new functions are not implemented.

- [ ] **Step 3: Implement bounded reading**

Private seam:

~~~cpp
namespace asharia::core::detail {

    [[nodiscard]] Result<std::vector<std::byte>>
    readBoundedStream(std::istream& stream, std::uint64_t measuredBytes,
                      FileReadLimits limits, const std::filesystem::path& path);

} // namespace asharia::core::detail
~~~

Required guards:

~~~cpp
if (limits.maxBytes == 0U) {
    return std::unexpected{fileIoError("read", path, "maximum byte count is zero")};
}
if (measuredBytes > limits.maxBytes || measuredBytes > SIZE_MAX ||
    measuredBytes > static_cast<std::uint64_t>(
                        std::numeric_limits<std::streamsize>::max())) {
    return std::unexpected{
        fileIoError("read", path, "file exceeds configured byte limit")};
}
~~~

Allocate only after guards, read the measured bytes completely, then probe one extra byte. Reject growth and short reads. readFileText() calls readFileBytes() and converts bytes without parsing text format.

- [ ] **Step 4: Verify and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-core-file-io-tests && build\cmake\msvc-debug\engine\core\asharia-core-file-io-tests.exe"
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
git add engine/core
git commit -m "feat: add bounded core file reads"
~~~

Expected: tests and checks exit 0.

---

### Task 2: Add atomic Core file replacement

**Files:**
- Modify: engine/core/include/asharia/core/file_io.hpp
- Modify: engine/core/src/file_io.cpp
- Modify: engine/core/src/file_io_internal.hpp
- Create: engine/core/src/atomic_file_windows.cpp
- Create: engine/core/src/atomic_file_posix.cpp
- Modify: engine/core/tests/file_io_tests.cpp
- Modify: engine/core/CMakeLists.txt

**Interfaces:**
- Consumes: Task 1.
- Produces: AtomicFileWriteOptions and atomic byte/text writers.

- [ ] **Step 1: Add declarations and failing coordinator tests**

Public API:

~~~cpp
struct AtomicFileWriteOptions {
    bool flushFileBuffers{true};
};

[[nodiscard]] VoidResult
writeFileBytesAtomically(const std::filesystem::path& path,
                         std::span<const std::byte> bytes,
                         AtomicFileWriteOptions options = {});

[[nodiscard]] VoidResult
writeFileTextAtomically(const std::filesystem::path& path,
                        std::string_view text,
                        AtomicFileWriteOptions options = {});
~~~

Private seam:

~~~cpp
class AtomicFileBackend {
public:
    virtual ~AtomicFileBackend() = default;
    [[nodiscard]] virtual Result<std::filesystem::path>
    writeUniqueTemporary(const std::filesystem::path& target,
                         std::span<const std::byte> bytes,
                         AtomicFileWriteOptions options) = 0;
    [[nodiscard]] virtual VoidResult
    replace(const std::filesystem::path& temporary,
            const std::filesystem::path& target) = 0;
    virtual void removeTemporary(const std::filesystem::path& temporary) noexcept = 0;
};

[[nodiscard]] VoidResult
writeFileBytesAtomicallyWithBackend(const std::filesystem::path& target,
                                    std::span<const std::byte> bytes,
                                    AtomicFileWriteOptions options,
                                    AtomicFileBackend& backend);
~~~

Add fake-backend tests for complete replacement, temporary-write failure, replace failure, temporary cleanup, and missing parent.

- [ ] **Step 2: Prove red state**

Run the Task 1 focused target. Expected: link failure for atomic symbols.

- [ ] **Step 3: Implement coordinator and platform backends**

Coordinator:

~~~cpp
auto temporary = backend.writeUniqueTemporary(target, bytes, options);
if (!temporary) {
    return std::unexpected{std::move(temporary.error())};
}
auto replaced = backend.replace(*temporary, target);
if (!replaced) {
    backend.removeTemporary(*temporary);
    return std::unexpected{std::move(replaced.error())};
}
return {};
~~~

Windows requirements: CREATE_NEW unique same-directory temp, complete WriteFile loop, optional FlushFileBuffers, checked CloseHandle, ReplaceFileW for existing target, MoveFileExW with REPLACE_EXISTING and WRITE_THROUGH otherwise, and contextual GetLastError conversion.

POSIX requirements: open with O_CREAT|O_EXCL, complete write loop handling EINTR, optional fsync, checked close, same-directory rename, and cleanup of only the owned temp file.

CMake selects one backend:

~~~cmake
if(WIN32)
  target_sources(asharia-core PRIVATE src/atomic_file_windows.cpp)
else()
  target_sources(asharia-core PRIVATE src/atomic_file_posix.cpp)
endif()
~~~

- [ ] **Step 4: Verify and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-core-file-io-tests && build\cmake\msvc-debug\engine\core\asharia-core-file-io-tests.exe"
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
git add engine/core
git commit -m "feat: add atomic core file replacement"
~~~

Expected: all injected failures preserve old bytes and remove temps.

---

### Task 3: Migrate archive, serialization, schema, and domain JSON

**Files:**
- Modify: packages/archive/include/asharia/archive/json_archive.hpp
- Modify: packages/archive/src/json_archive.cpp
- Modify: packages/archive/tests/archive_smoke_tests.cpp
- Modify: packages/serialization/include/asharia/serialization/text_archive.hpp
- Modify: packages/serialization/src/text_archive.cpp
- Modify: packages/serialization/tests/serialization_smoke_tests.cpp
- Modify: packages/schema/src/schema_document.cpp
- Modify: packages/schema/tests/schema_smoke_tests.cpp
- Modify: packages/project-core/src/project_descriptor_io.cpp
- Modify: packages/asset-core/src/asset_metadata_io.cpp
- Modify: packages/asset-pipeline/src/asset_product_manifest_io.cpp
- Modify: packages/material-instance/src/amat_io.cpp

**Interfaces:**
- Consumes: Task 2.
- Produces: atomic persistent writes and explicit bounded domain reads.

- [ ] **Step 1: Add failing option/overwrite tests**

Archive option:

~~~cpp
struct JsonArchiveFileOptions {
    std::uint64_t maxBytes{64ULL * 1024ULL * 1024ULL};
};

[[nodiscard]] Result<ArchiveValue>
readJsonArchiveFile(const std::filesystem::path& path,
                    JsonArchiveFileOptions options = {});
~~~

Test exact limit, one byte above an injected limit, overwrite of an existing archive, and absence of .asharia-tmp files. Add equivalent small-limit tests to serialization and schema.

- [ ] **Step 2: Prove red state**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-archive-smoke-tests asharia-serialization-smoke-tests asharia-schema-smoke-tests"
~~~

Expected: new option or behavior tests fail.

- [ ] **Step 3: Migrate implementations**

Use named limits:

~~~cpp
inline constexpr std::uint64_t kMaxJsonArchiveBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxProjectDescriptorBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxAssetMetadataBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxAssetProductManifestBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxMaterialInstanceBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaxSchemaDocumentBytes = 64ULL * 1024ULL * 1024ULL;
~~~

Archive/serialization writers call writeFileTextAtomically(). Readers call readFileText() and reuse existing text parsers. Domain wrappers pass their own limit.

- [ ] **Step 4: Verify and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-archive-smoke-tests asharia-serialization-smoke-tests asharia-schema-smoke-tests asharia-project-core-smoke-tests asharia-asset-core-smoke-tests asharia-material-instance-smoke-tests asharia-asset-pipeline-smoke-tests && ctest --test-dir build\cmake\msvc-debug --output-on-failure -R \"asharia-(archive|serialization|schema|project-core|asset-core|material-instance|asset-pipeline)\""
git add packages/archive packages/serialization packages/schema packages/project-core packages/asset-core packages/asset-pipeline/src/asset_product_manifest_io.cpp packages/material-instance
git commit -m "refactor: centralize persistent file io"
~~~

Expected: selected tests pass and direct production truncation is gone from those files.

---

### Task 4: Migrate native Editor and CLI outputs

**Files:**
- Modify: apps/editor/src/editor_asset_import_settings_command.cpp
- Modify: apps/editor/src/editor_settings.cpp
- Modify: apps/editor/src/editor_command_smoke.cpp
- Modify: packages/shader-slang/tools/slang_reflect.cpp
- Modify: packages/shader-slang/CMakeLists.txt
- Modify: tools/asset-processor/src/asset_processor_execute.cpp
- Modify: tools/asset-processor/src/asset_processor_smoke.cpp

**Interfaces:**
- Consumes: Tasks 2-3.
- Produces: native commands/tools that cannot truncate old visible output.

- [ ] **Step 1: Add failing regressions**

Add tests named editImportSettingsReplacesMetadataAtomically, undoImportSettingsRestoresExactOriginalBytes, failedMetadataCommitAddsNoReimportRequest, and rejectsOversizedImportMetadataWithoutMutation. The failure case uses a missing parent, requires failure and an empty reimport log. The oversized case creates valid metadata followed by whitespace so the file is exactly `16ULL * 1024ULL * 1024ULL + 1ULL` bytes, requires a limit error, and proves the file and reimport log are unchanged. Tool smoke pre-creates output, replaces it, parses it, and verifies no temp remains.

- [ ] **Step 2: Prove red state**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-editor asharia-slang-reflect asharia-asset-processor"
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
~~~

Expected: the oversized-metadata regression fails while the local unbounded reader remains.

- [ ] **Step 3: Migrate production paths**

Use `readFileText(path, {.maxBytes = 16ULL * 1024ULL * 1024ULL})` and `writeFileTextAtomically()` in native Editor. Record reimport work only after commit. Use atomic output for Slang/asset-processor user outputs and bounded reads for tool diagnostics.

- [ ] **Step 4: Verify and commit**

~~~powershell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-asset-browser
ctest --test-dir build\cmake\msvc-debug --output-on-failure -R "asharia-(shader-slang|asset-processor)"
rg -n "std::ofstream.*trunc|std::ofstream out" apps/editor/src packages/shader-slang/tools tools/asset-processor/src
git add apps/editor/src/editor_asset_import_settings_command.cpp apps/editor/src/editor_settings.cpp apps/editor/src/editor_command_smoke.cpp packages/shader-slang tools/asset-processor
git commit -m "refactor: use atomic io in native tools"
~~~

Expected: smokes/tests pass and search finds no production truncating writer.

---

### Task 5: Bound product blob allocations

**Files:**
- Modify: packages/asset-pipeline/include/asharia/asset_pipeline/asset_product_blob.hpp
- Create: packages/asset-pipeline/src/asset_product_blob_limits.hpp
- Create: packages/asset-pipeline/src/asset_product_blob_limits.cpp
- Modify: packages/asset-pipeline/src/asset_product_blob.cpp
- Modify: packages/asset-pipeline/tests/asset_pipeline_smoke_tests.cpp
- Modify: packages/asset-pipeline/CMakeLists.txt

**Interfaces:**
- Consumes: Task 1.
- Produces: AssetProductBlobReadLimits and limit-aware readers.

- [ ] **Step 1: Add limits and malicious-input tests**

~~~cpp
struct AssetProductBlobReadLimits {
    std::uint64_t maxProductBytes{512ULL * 1024ULL * 1024ULL};
    std::uint32_t maxTextureMipRecords{32};
    std::uint32_t maxShaderProperties{4096};
    std::uint32_t maxShaderPasses{1024};
    std::uint32_t maxShaderBindings{16384};
    std::uint32_t maxShaderEntries{4096};
};
~~~

Pass limits by value with default {} in every request/span overload. Tests cover UINT64_MAX count in tiny files, every exact limit, limit+1, impossible 1x1 mip count, oversized payload sizes, and exception-free Result errors.

- [ ] **Step 2: Prove red state**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-asset-pipeline-smoke-tests && build\cmake\msvc-debug\packages\asset-pipeline\asharia-asset-pipeline-smoke-tests.exe"
~~~

Expected: compile failure or maximal-count crash/failure.

- [ ] **Step 3: Implement one validator**

~~~cpp
struct AssetProductRecordLimitRequest {
    std::uint64_t count{};
    std::uint64_t hardLimit{};
    std::size_t headerLineCount{};
    std::size_t minimumLinesPerRecord{};
    std::string_view recordName;
    std::string_view relativeProductPath;
};

[[nodiscard]] Result<std::size_t>
validateAssetProductRecordCount(const AssetProductRecordLimitRequest& request);
~~~

Reject invalid minimum field count, size_t overflow, hard-limit excess, and count above headerLineCount/minimumLinesPerRecord before reserve. Check mip count against 1+floor(log2(max(width,height))). Keep subtraction-based byte ranges.

- [ ] **Step 4: Verify both compilers and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-asset-pipeline-smoke-tests && build\cmake\msvc-debug\packages\asset-pipeline\asharia-asset-pipeline-smoke-tests.exe"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug -DASHARIA_BUILD_TESTS=ON && cmake --build --preset clangcl-debug --target asharia-asset-pipeline-smoke-tests && build\cmake\clangcl-debug\packages\asset-pipeline\asharia-asset-pipeline-smoke-tests.exe"
git add packages/asset-pipeline
git commit -m "fix: bound asset product blob parsing"
~~~

Expected: both binaries exit 0 without new diagnostics.

---

### Task 6: Stage product publication and commit manifest last

**Files:**
- Create: packages/asset-pipeline/src/asset_product_publication.hpp
- Create: packages/asset-pipeline/src/asset_product_publication.cpp
- Modify: packages/asset-pipeline/src/asset_product_execution.cpp
- Modify: packages/asset-pipeline/tests/asset_pipeline_smoke_tests.cpp
- Modify: packages/asset-pipeline/CMakeLists.txt

**Interfaces:**
- Consumes: Tasks 2, 3, and 5.
- Produces: private publishAssetProducts() with injectable package-local operations.

- [ ] **Step 1: Add model and failing consistency tests**

~~~cpp
struct AssetProductPublicationItem {
    SourceAssetRecord source;
    AssetProductRecord product;
    std::filesystem::path finalPath;
    std::vector<std::uint8_t> bytes;
};

struct AssetProductPublicationRequest {
    std::filesystem::path outputRoot;
    std::filesystem::path manifestPath;
    AssetProductManifestDocument manifest;
    std::span<const AssetProductPublicationItem> products;
};

struct AssetProductPublicationResult {
    std::vector<AssetProductWrite> writes;
    bool manifestWritten{};
};
~~~

Private operations support unique staging directory, atomic writes, bounded reads, final publication, and staging removal. Fake tests prove manifest ordering, old-manifest preservation on product/manifest failure, staged-hash rejection, and cleanup.

- [ ] **Step 2: Prove red state**

Run Task 5 MSVC test. Expected: tests fail because final files are written directly.

- [ ] **Step 3: Implement staging**

Create .asharia-product-staging/<process-counter>. Write products beneath products/, re-read with expected size and hash, write/re-read/validate manifest, publish products atomically, publish manifest last, clean staging. Convert failures to existing ProductWriteFailed or ManifestWriteFailed with phase and paths. Replace the direct write loop in executeAssetProducts().

- [ ] **Step 4: Verify and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-asset-pipeline-smoke-tests asharia-asset-processor && ctest --test-dir build\cmake\msvc-debug --output-on-failure -R \"asharia-(asset-pipeline|asset-processor)\""
powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
git add packages/asset-pipeline tools/asset-processor
git commit -m "feat: stage asset product publication"
~~~

Expected: all tests and boundary gate pass.

---

### Task 7: Stabilize tool fingerprints

**Files:**
- Create: packages/asset-pipeline/include/asharia/asset_pipeline/asset_tool_fingerprint.hpp
- Create: packages/asset-pipeline/src/asset_tool_fingerprint.cpp
- Modify: packages/asset-pipeline/src/asset_import_planning.cpp
- Modify: packages/asset-pipeline/include/asharia/asset_pipeline/asset_import_planning.hpp
- Modify: packages/asset-pipeline/tests/asset_pipeline_smoke_tests.cpp
- Modify: packages/asset-pipeline/CMakeLists.txt

**Interfaces:**
- Consumes: Core Result/error conventions and the current asset FNV-1a convention.
- Produces: AssetToolFingerprint, fingerprintAssetTool(), ToolFingerprintFailed.

- [ ] **Step 1: Add deterministic tests**

~~~cpp
struct AssetToolFingerprint {
    std::uint64_t fileSize{};
    std::uint64_t contentHash{};
    std::uint64_t versionHash{};

    [[nodiscard]] friend bool operator==(const AssetToolFingerprint&,
                                         const AssetToolFingerprint&) = default;
};

[[nodiscard]] Result<AssetToolFingerprint>
fingerprintAssetTool(const std::filesystem::path& executable,
                     std::string_view logicalToolName);
~~~

Same filename/bytes in different paths/timestamps must match; one changed byte must differ; missing/unreadable files return Asset error. Add ToolFingerprintFailed planning diagnostic and resolver-injection test.

- [ ] **Step 2: Prove red state**

Run asset-pipeline tests. Expected: path/timestamp identity test fails.

- [ ] **Step 3: Implement streaming identity**

Normalize logical name and filename to lowercase ASCII. Reject executables larger than `2ULL * 1024ULL * 1024ULL * 1024ULL`, then stream through one fixed 1 MiB buffer while enforcing the same cumulative limit in case the file grows. Hash logical name, normalized filename, file size, and content hash. Remove canonical path and timestamp. Default dependency construction returns Result and emits Error-severity ToolFingerprintFailed instead of a synthetic missing hash.

- [ ] **Step 4: Verify and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-asset-pipeline-smoke-tests && build\cmake\msvc-debug\packages\asset-pipeline\asharia-asset-pipeline-smoke-tests.exe"
git add packages/asset-pipeline
git commit -m "fix: stabilize asset tool fingerprints"
~~~

Expected: deterministic identity and existing planning tests pass.

---

### Task 8: Bound swapchain retirement

**Files:**
- Modify: packages/rhi-vulkan/include/asharia/rhi_vulkan/vulkan_frame_loop.hpp
- Modify: packages/rhi-vulkan/src/vulkan_frame_loop.cpp
- Modify: apps/sample-viewer/src/main.cpp
- Modify: apps/editor/src/editor_viewport_smoke.cpp
- Modify: docs/architecture/flow.md
- Modify: docs/architecture/frame-loop-threading.md

**Interfaces:**
- Consumes: synchronous fence + vkQueueWaitIdle recreation.
- Produces: VulkanSwapchainRetirementStats and zero-pending invariant.

- [ ] **Step 1: Add stats and failing resize assertions**

~~~cpp
struct VulkanSwapchainRetirementStats {
    std::uint64_t retired{};
    std::uint64_t destroyed{};
    std::uint64_t pending{};
};
~~~

Expose swapchainRetirementStats(). Extend sample resize to eight nonzero extents and both resize smokes to require pending==0 and retired==destroyed after each completed recreate.

- [ ] **Step 2: Prove red state**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug"
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
~~~

Expected: missing accessor or pending retained resources.

- [ ] **Step 3: Implement bounded local retirement**

Remove retiredSwapchainResources_. After checked fence and queue waits, move current swapchain/views/semaphores to one local owner, call vkCreateSwapchainKHR with its handle, clean every partial new set, install only a complete new set, then destroy the local old set before return. On create failure, destroy the retired old set and leave empty retryable member state. Log destructor queue-idle failure.

- [ ] **Step 4: Verify both compilers, document, and commit**

~~~powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
build\cmake\clangcl-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-resize
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
git add packages/rhi-vulkan apps/sample-viewer/src/main.cpp apps/editor/src/editor_viewport_smoke.cpp docs/architecture/flow.md docs/architecture/frame-loop-threading.md
git commit -m "fix: bound swapchain retirement"
~~~

Expected: four smokes exit 0; pending remains zero.

---

### Task 9: Retry incomplete Vulkan enumerations

**Files:**
- Create: packages/rhi-vulkan/src/vulkan_enumeration.hpp
- Modify: packages/rhi-vulkan/src/vulkan_context.cpp
- Modify: packages/rhi-vulkan/src/vulkan_frame_loop.cpp
- Create: packages/rhi-vulkan/tests/vulkan_enumeration_tests.cpp
- Modify: packages/rhi-vulkan/CMakeLists.txt
- Modify: packages/rhi-vulkan/asharia.package.json

**Interfaces:**
- Consumes: Vulkan error helpers.
- Produces: private enumerateVulkanVector<T>().

- [ ] **Step 1: Add CPU retry tests**

~~~cpp
template <typename ValueT, typename QueryT>
[[nodiscard]] Result<std::vector<ValueT>>
enumerateVulkanVector(QueryT&& query, std::string_view failureContext);
~~~

Query signature: VkResult(uint32_t*, ValueT*). Test zero, immediate success, one incomplete then larger success, count shrink, and non-incomplete failure context.

- [ ] **Step 2: Register and prove red state**

Add asharia-rhi-vulkan-enumeration-tests under ASHARIA_BUILD_TESTS and package manifest.

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-rhi-vulkan-enumeration-tests"
~~~

Expected: compile failure for missing helper.

- [ ] **Step 3: Implement and migrate**

Loop count/fill, resize on success, retry only VK_INCOMPLETE, contextual error otherwise. Migrate instance layers/extensions, device extensions, physical devices, surface formats, present modes, and swapchain images.

- [ ] **Step 4: Verify and commit**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --build --preset msvc-debug --target asharia-rhi-vulkan-enumeration-tests asharia-sample-viewer && build\cmake\msvc-debug\packages\rhi-vulkan\asharia-rhi-vulkan-enumeration-tests.exe"
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -RunCheapGates
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-frame
git add packages/rhi-vulkan
git commit -m "fix: retry incomplete Vulkan enumerations"
~~~

Expected: tests, gates, and smokes pass.

---

### Task 10: Add test presets, warning enforcement, and CI

**Files:**
- Modify: CMakePresets.json
- Modify: .clang-tidy
- Create: .github/workflows/native-code-quality.yml
- Modify: test sources reported by ClangCL
- Modify: docs/workflow/build.md
- Modify: docs/workflow/review.md
- Modify: docs/developer-documentation-system/en/workflow/build.md
- Modify: docs/developer-documentation-system/zh/workflow/build.md
- Modify: docs/developer-documentation-system/en/workflow/review.md
- Modify: docs/developer-documentation-system/zh/workflow/review.md

**Interfaces:**
- Consumes: Tasks 1-9 test targets.
- Produces: msvc-debug-tests, clangcl-debug-tests, native CI.

- [ ] **Step 1: Add test-enabled presets**

~~~json
{
  "name": "msvc-debug-tests",
  "displayName": "MSVC Debug Tests",
  "inherits": "msvc-debug",
  "binaryDir": "${sourceDir}/build/cmake/msvc-debug-tests",
  "cacheVariables": {"ASHARIA_BUILD_TESTS": "ON"}
},
{
  "name": "clangcl-debug-tests",
  "displayName": "ClangCL Debug Tests",
  "inherits": "clangcl-debug",
  "binaryDir": "${sourceDir}/build/cmake/clangcl-debug-tests",
  "cacheVariables": {"ASHARIA_BUILD_TESTS": "ON"}
}
~~~

Add matching build/test presets. cmake --list-presets=all must resolve them.

- [ ] **Step 2: Build clean test trees**

~~~powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
~~~

Expected before cleanup: tests pass but known ClangCL diagnostics print.

- [ ] **Step 3: Fix diagnostics and enforce them**

Fix every reported production/test diagnostic without blanket suppression. Then set:

~~~yaml
WarningsAsErrors: '*'
~~~

Re-run ClangCL test build. Expected: no promoted warning.

- [ ] **Step 4: Add Windows workflow**

Use checkout@v4, setup-python@v5 with Python 3.12, Conan 2.26.2, Vulkan SDK 1.4.313.0 official installer, bootstrap-conan, encoding/diff/asset boundary gates, then both test preset commands. Trigger PR, main push, manual dispatch. Do not run GPU/window smokes on hosted CI.

Core job commands:

~~~yaml
- name: Install Conan
  shell: pwsh
  run: python -m pip install "conan==2.26.2"
- name: Install Vulkan SDK
  shell: pwsh
  run: |
    $installer = Join-Path $env:RUNNER_TEMP 'VulkanSDK.exe'
    Invoke-WebRequest 'https://sdk.lunarg.com/sdk/download/1.4.313.0/windows/VulkanSDK-1.4.313.0-Installer.exe' -OutFile $installer
    Start-Process -FilePath $installer -Wait -ArgumentList '--accept-licenses','--default-answer','--confirm-command','install'
    $sdkRoot = 'C:\VulkanSDK\1.4.313.0'
    "VULKAN_SDK=$sdkRoot" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    (Join-Path $sdkRoot 'Bin') | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
- name: Bootstrap Conan
  shell: pwsh
  run: powershell -ExecutionPolicy Bypass -File scripts\bootstrap-conan.ps1
- name: MSVC tests
  shell: cmd
  run: build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure
- name: ClangCL tests
  shell: cmd
  run: build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure
~~~

- [ ] **Step 5: Update docs, verify, and commit**

~~~powershell
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
git add CMakePresets.json .clang-tidy .github/workflows/native-code-quality.yml docs engine packages apps tools
git commit -m "ci: enforce native build and test gates"
~~~

Expected: docs/encoding/whitespace pass.

---

### Task 11: Completion audit and full native verification

**Files:**
- Modify only files required to correct a verification failure attributable to Tasks 1-10.

**Interfaces:**
- Consumes: approved design and Tasks 1-10.
- Produces: requirement-by-requirement completion evidence.

- [ ] **Step 1: Prove migration coverage**

~~~powershell
rg -n "std::ofstream.*trunc|std::ofstream out" engine packages apps/editor/src tools --glob '!apps/studio/**' -g '*.cpp' -g '*.hpp'
rg -n "last_write_time|weakly_canonical" packages/asset-pipeline/src/asset_import_planning.cpp
rg -n "retiredSwapchainResources_" packages/rhi-vulkan
rg -n "reserve\(static_cast<std::size_t>\(.*Count" packages/asset-pipeline/src/asset_product_blob.cpp
powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
~~~

Expected: no production unsafe pattern remains; test fixtures may directly create new temporary files.

- [ ] **Step 2: Run all registered tests**

Run both Task 10 test preset commands. Expected: every registered test passes under both compilers.

- [ ] **Step 3: Run standard builds**

~~~powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
~~~

Expected: both test-off builds pass.

- [ ] **Step 4: Run 70 non-Studio native smokes**

Run this once with msvc-debug and once with clangcl-debug:

~~~powershell
$preset = 'msvc-debug'
$viewer = "build\cmake\$preset\apps\sample-viewer\asharia-sample-viewer.exe"
$editor = "build\cmake\$preset\apps\editor\asharia-editor.exe"
$viewerFlags = @(
  '--smoke-window','--smoke-vulkan','--smoke-frame','--smoke-rendergraph',
  '--smoke-transient','--smoke-dynamic-rendering','--smoke-resize','--smoke-triangle',
  '--smoke-depth-triangle','--smoke-mesh','--smoke-mesh-3d','--smoke-draw-list',
  '--smoke-mrt','--smoke-descriptor-layout','--smoke-material-binding',
  '--smoke-fullscreen-texture','--smoke-scene-draw-packet',
  '--smoke-render-view-grid-readback','--smoke-offscreen-viewport',
  '--smoke-compute-dispatch','--smoke-buffer-upload','--smoke-texture-upload',
  '--smoke-renderer-format-contract','--smoke-deferred-deletion',
  '--smoke-reflection-registry','--smoke-reflection-transform',
  '--smoke-reflection-attributes','--smoke-serialization-roundtrip',
  '--smoke-serialization-json-archive','--smoke-serialization-migration'
)
$editorFlags = @(
  '--smoke-editor-shell','--smoke-editor-asset-browser','--smoke-editor-viewport',
  '--smoke-editor-viewport-resize','--smoke-editor-frame-debugger'
)
foreach ($flag in $viewerFlags) {
  & $viewer $flag
  if ($LASTEXITCODE -ne 0) { throw "$viewer $flag failed with $LASTEXITCODE" }
}
foreach ($flag in $editorFlags) {
  & $editor $flag
  if ($LASTEXITCODE -ne 0) { throw "$editor $flag failed with $LASTEXITCODE" }
}
~~~

Change preset to clangcl-debug for the second run. Expected: 35 per compiler, 70 total, all pass. Exclude native-bridge/shared-viewport Studio flags.

- [ ] **Step 5: Run final repository gates**

~~~powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
git diff --check
git status --short
~~~

Expected: all scripts pass and only intentional work is present.

- [ ] **Step 6: Audit design requirements**

Read docs/superpowers/specs/2026-07-10-engine-audit-hardening-design.md section by section. Map every Goal and Verification Matrix row to fresh test/command output. Add a missing test instead of inferring completion. If verification exposes a defect, return to the task that owns that behavior, add the failing regression there, implement the correction, rerun that task's complete gate, and use that task's explicit file list and commit message. Do not create an empty verification commit.
