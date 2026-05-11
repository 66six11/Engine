# AGENTS.md

## Build system

- **Conan must run before CMake.** Run `.\scripts\bootstrap-conan.ps1` first — it generates toolchain files under `build/conan/` that CMake presets depend on.
- **`build/conan/` and `build/cmake/` stay separate.** VS may delete `build/cmake/` on cache clean; if you delete `build/conan/`, toolchain files are lost and you must re-bootstrap.
- **`conan.lock` is committed.** `bootstrap-conan.ps1` automatically passes it to `conan install` to pin dependencies (glfw, glm, vulkan-headers, vulkan-memory-allocator).
- **PowerShell builds need the Conan environment bat.** Use:
  ```
  cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
  ```
  The `conanbuild.bat` sets VS compiler env vars required by Ninja+MSVC. From "Developer PowerShell for VS 2022" you can skip it.
- **Two-compiler workflow:** `msvc-*` presets for daily dev; `clangcl-*` presets for pre-commit with `clang-tidy` enabled (`ASHARIA_ENABLE_CLANG_TIDY=ON`). Always run at least `clangcl-debug` before committing.

## Encoding — critical

- **C/C++ sources (`.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`, `.ipp`, `.inl`, `.ps1`): MUST use UTF-8 with BOM.**
- **Everything else (`.json`, `.cmake`, `.py`, `.md`, `.slang`, `.ps1` [contradiction — check `.editorconfig`], `.clang-format`, `.clang-tidy`, etc.): MUST use UTF-8 without BOM.**
- Verify with: `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- Fix violations with: `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1 -Fix`
- Conan-generated `CMakeUserPresets.json` with BOM triggers Python JSON parse errors — this is a known gotcha.

## Architecture constraints

- **`asharia::rhi_vulkan` must NOT depend on RenderGraph.** It is the base Vulkan backend target.
- **`asharia::rhi_vulkan_rendergraph`** is the separate target that translates abstract render graph state into Vulkan barrier/layout/stage types.
- **`asharia::renderer_basic` must be backend-agnostic.** Vulkan command recording goes in `asharia::renderer_basic_vulkan`, never in `renderer_basic`.
- **Packages must not include another package's `src/`.** Only `include/` is public API. CMake targets use `asharia::<name>` aliases.
- Include order: `<vulkan/...>` → `<...>` → `"asharia/..."` → other project headers (enforced by `.clang-format`).
- No `vkDeviceWaitIdle` in render loops — only in shutdown, early MVP simplify paths, or debug probes (must be commented).

## Naming & style

- Types: `PascalCase`; functions/variables: `camelBack`; constants: `kPascalCase`; private members: `trailing_`.
- Constructors stay lightweight. Vulkan/OS/IO-failable work goes into explicit `create()` functions or factory methods.
- No bare owning pointers, no bare `new`/`delete`. All GPU allocations go through VMA.
- `VkResult` must never be ignored. Convert to project error types with context preserved.

## Testing

- **There are no standalone test executables.** All smoke tests live as `--smoke-*` flags in `asharia-sample-viewer.exe`.
- Run smoke tests: `build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-window`, `--smoke-vulkan`, `--smoke-frame`, `--smoke-rendergraph`, `--smoke-dynamic-rendering`, `--smoke-resize`, `--smoke-triangle`.
- `ASHARIA_BUILD_TESTS` option exists but is OFF and not yet populated.

## Pre-commit gate (from docs/review-workflow.md)

```
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

For frame-loop/swapchain/render-graph changes, also run all smoke commands on both MSVC and ClangCL builds.

## Commit style

- Conventional commits: `docs:`, `feat:`, `build:`, `test:`, etc. Lowercase, imperative, concise.
- Generated files (`build/`, `ConanPresets.json`, `CMakeUserPresets.json`, `*.spv`, `/generated/`) are gitignored.

## Key docs

- `docs/architecture.md` — module boundaries, ownership, lifetime ordering
- `docs/flow-architecture.md` — real dependency graph and frame loop data flow (must be updated when these change)
- `docs/package-architecture.md` — package-first rules and CMake conventions
- `docs/coding-standard.md` — C++23, Vulkan lifecycle, sync rules
- `docs/review-workflow.md` — full pre-commit checklist
- `docs/encoding-policy.md` — BOM rules (with known toolchain quirks)

## Shader pipeline

- Slang → SPIR-V via `asharia_add_slang_shader()` CMake helper (from `packages/shader-slang`).
- Output SPIR-V is validated with `spirv-val`.
- Shader metadata/reflection is via Slang JSON (under active development).
