# Detailed Design: RenderGraph Declaration, Compilation, And Execution

## Context

The current renderer needs to declare passes, resources, and command summaries without leaking Vulkan details. A backend-specific layer then performs barrier/layout/stage mapping and command recording. `packages/rendergraph` provides this intermediate layer.

## Goals

- Let callers declare images, buffers, passes, slots, params, and command summaries.
- Validate resource access during compile; schema, params-type, slot-schema, and command-schema checks run when callers use `compile(schemaRegistry)`.
- Generate pass order, transitions, dependencies, culled passes, and diagnostics.
- Allow backend execution through callbacks or an executor registry.
- Keep the public API backend-agnostic.

## Non-Goals

- Do not create Vulkan images or buffers.
- Do not record Vulkan command buffers.
- Do not manage swapchains.
- Do not decide shader module, pipeline layout, or descriptor set lifetime.
- Do not put editor UI state in RenderGraph.

## Current Constraints

- `RenderGraphImageState` and `RenderGraphBufferState` are abstract states, not Vulkan layout/access flags.
- `RenderGraphShaderStage` currently has only `None`, `Fragment`, and `Compute`.
- Pass params must be trivially copyable. `PassBuilder::setParams()` stores them in a byte vector.
- The schema registry decides which resource slots and command kinds each pass type allows.
- A side-effect pass uses `hasSideEffects` to prevent incorrect culling.

## Overall Design

Callers first build a graph declaration. The compile stage reads declarations and the optional schema registry:

1. Validate resource descriptions.
2. Validate pass slots and commands; schema validation runs only when a registry is provided.
3. Build dependencies from resource reads and writes.
4. Cull passes that allow culling and have no reachable output or side effects.
5. Reorder transient producers before readers.
6. Calculate `transitionsBefore` and `bufferTransitionsBefore` for each compiled pass.
7. Calculate `finalTransitions` and `finalBufferTransitions` for imported images and buffers after pass compilation.
8. Let callers derive diagnostics through `diagnosticsSnapshot(compiled)` and debug tables through `formatDebugTables(compiled)`.

Execution has two paths:

- `RenderGraphPassCallback`: the pass binds a callback directly.
- `RenderGraphExecutorRegistry`: `execute(executorRegistry)` uses the pass-bound callback first, then finds an executor by pass type.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `render_graph.hpp` | Aggregate include |
| `render_graph_builder.hpp` | `RenderGraph` and `PassBuilder` public declaration API |
| `render_graph_types.hpp` | Handles, states, descs, slot schema, command data |
| `render_graph_command_list.hpp` | Command summary builder |
| `render_graph_compile.hpp` | Compile result public data |
| `render_graph_execution.hpp` | Schema registry and executor registry |
| `render_graph_diagnostics.hpp` | Diagnostics snapshot data |
| `src/render_graph_*validation*` | Resource/pass/slot/command validation |
| `src/render_graph_dependency_*` | Dependency collection, culling, sorting |
| `src/render_graph_debug_*` | Debug names and table formatting |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `RenderGraphImageDesc` | `name`, `format`, `extent`, `initialState`, `initialShaderStage`, `finalState`, `finalShaderStage`, `lifetime` | Image declaration |
| `RenderGraphBufferDesc` | `name`, `byteSize`, `initialState`, `initialShaderStage`, `finalState`, `finalShaderStage`, `lifetime` | Buffer declaration |
| `RenderGraphPassSchema` | `type`, `paramsType`, `resourceSlots`, `allowedCommands`, `allowCulling`, `hasSideEffects` | Pass contract |
| `RenderGraphCommand` | `kind`, `name`, `secondaryName`, `floatValues`, `intValue`, `uintValues` | Backend-readable command summary |
| `RenderGraphCompileResult` | passes, transitions, dependencies, culled passes, transient resources | Compiled execution plan |

## API Design

The main public API is documented in [../api/rendergraph-api.md](../api/rendergraph-api.md).

Example:

```cpp
asharia::RenderGraph graph;
auto backbuffer = graph.importImage({
    .name = "Backbuffer",
    .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
    .extent = {.width = 1280, .height = 720},
    .initialState = asharia::RenderGraphImageState::Undefined,
    .finalState = asharia::RenderGraphImageState::Present,
});

graph.addPass("Clear", asharia::kBasicTransferClearPassType)
    .writeTransfer("target", backbuffer)
    .setParams(asharia::kBasicTransferClearParamsType,
               asharia::BasicTransferClearParams{.color = {0.0F, 0.0F, 0.0F, 1.0F}})
    .recordCommands([](asharia::RenderGraphCommandList& commands) {
        commands.clearColor("target", {0.0F, 0.0F, 0.0F, 1.0F});
    });

auto schemas = asharia::basicRenderGraphSchemaRegistry();
auto compiled = graph.compile(schemas);
```

## Key Flows

### Normal Flow

1. Register schemas.
2. Import external resources and create transient resources.
3. Add pass declarations.
4. Compile graph.
5. Inspect diagnostics if needed.
6. Execute through callbacks or backend executors.

### Failure Flow

- Invalid resource desc: compile returns `Result<RenderGraphCompileResult>` error.
- Missing schema: `compile(schemaRegistry)` returns error.
- Slot access mismatch: `compile(schemaRegistry)` returns error.
- Command kind not allowed by schema: `compile(schemaRegistry)` returns error.
- Executor missing at execution time: execute returns `Result<void>` error.
- Stale compile result: `execute(compiled)` returns an error when graph declarations changed after compile.

### Boundary Flow

- A transient resource producer can be reordered before its reader.
- A side-effect pass with no output must not be culled when `hasSideEffects` is true.
- Optional slots may be absent only when the schema marks them optional.

## Lifetime

`RenderGraph` owns declarations through an internal `Impl`. It is copyable and movable. The compile result is a value returned to the caller. Execution checks declaration generation and declared pass/image/buffer counts before accepting a compile result, so callers must recompile after mutating a graph.

## Error Handling

Errors use `asharia::Error` and `Result<T>`. RenderGraph errors should use `ErrorDomain::RenderGraph` and include:

- pass name or resource name when available,
- schema type when relevant,
- command kind or slot name when validation fails.

## Test Plan

Core tests:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

Runtime smoke:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --bench-rendergraph --warmup 10 --frames 100 --output build\rendergraph-bench.json
```

## Risks

- Schema names are strings. A typo can turn into missing schema or slot mismatch. Mitigation: use constants such as `kBasicTransferClearPassType`.
- Params are raw bytes. A mismatched `paramsType` and struct layout can pass compile but fail backend expectations. Mitigation: pair constants and typed structs in the same package.
- Backend-specific behavior can leak upward if Vulkan concepts are added to `render_graph_types.hpp`. Mitigation: add mappings only under `rhi_vulkan_rendergraph`.
