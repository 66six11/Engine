# ADR-001: RenderGraph Header API/Implementation Split

Date: 2026-05-25
Status: Accepted

## Context

At the start of this ADR, `packages/rendergraph/include/asharia/rendergraph/render_graph.hpp`
was ~4000 lines and `asharia-rendergraph` was an INTERFACE target. The header carried:

- 7 enum types (format, state, shader stage, lifetime, slot access, command kind)
- 12 POD structs (handles, descriptors, transitions, slots, schema, commands)
- `RenderGraphCommandList` (command accumulator + interpreter)
- `RenderGraph::PassBuilder` (fluent builder)
- `RenderGraph` class (import, create, addPass, compile, execute, diagnostics)
- Internal Pass data structures and compiler logic
- Debug table formatting (Markdown)

All current consumers include `render_graph.hpp` and transitively compile everything.
Adding cache, alias, multi-queue, or unsafe/native pass features before splitting will
make the public API unstable across all includers.

Industry references:
- Unreal RDG: `RenderGraph.h` (public) + separate internal headers for builder, resources, utils
- Unity RenderGraph: `RenderGraph.cs` + `RenderGraphBuilder.cs` + `RenderGraphPass.cs`
- Filament FrameGraph: `FrameGraph.h` + `FrameGraphHandle.h` + `FrameGraphPassResources.h`

## Decision

Split `render_graph.hpp` into multiple headers with stable public/implementation boundaries,
while keeping a single aggregate include for existing consumers (zero migration cost).

### Target Structure

```
include/asharia/rendergraph/
  render_graph_types.hpp        # handles, enums, descs, transitions, slots, schema, commands
  render_graph_command_list.hpp # RenderGraphCommandList (separated when it grows)
  render_graph_builder.hpp      # RenderGraph + PassBuilder declarations
  render_graph_compile.hpp      # RenderGraphCompileResult, compile declaration
  render_graph_diagnostics.hpp  # RenderGraphDiagnosticsSnapshot factory
  render_graph.hpp              # aggregate header: includes all of the above
```

### Phase 1 (this ADR): types extraction

Move lines 20-199 of `render_graph.hpp` into `render_graph_types.hpp`:

| What moves | Lines |
|------------|-------|
| `RenderGraphImageHandle`, `RenderGraphBufferHandle`, `RenderGraphExtent2D` | 20-37 |
| `RenderGraphImageFormat` enum | 39-43 |
| `RenderGraphImageState` enum | 45-55 |
| `RenderGraphBufferState` enum | 57-64 |
| `RenderGraphShaderStage` enum | 66-70 |
| `RenderGraphImageLifetime` enum | 72-75 |
| `RenderGraphBufferLifetime` enum | 77-80 |
| `RenderGraphImageAccess`, `RenderGraphBufferAccess` | 82-96 |
| `RenderGraphImageDesc` | 98-107 |
| `RenderGraphBufferDesc` | 109-117 |
| `RenderGraphImageTransition`, `RenderGraphBufferTransition` | 119-135 |
| `RenderGraphImageSlot`, `RenderGraphBufferSlot` | 137-147 |
| `RenderGraphSlotAccess` enum | 149-161 |
| `RenderGraphResourceSlotSchema` | 163-168 |
| `RenderGraphCommandKind` enum | 170-180 |
| `RenderGraphPassSchema` | 182-189 |
| `RenderGraphCommand` | 191-198 |

### What stays in `render_graph.hpp`

- `RenderGraphCommandList` class (200+)
- `RenderGraph` class + `PassBuilder`
- `RenderGraphSchemaRegistry`
- `RenderGraphPassContext`, `RenderGraphExecutorRegistry`
- `RenderGraphCompileResult` and `RenderGraphDiagnosticsSnapshot` before Phase 3
- All compile/execute/diagnostics/formatting logic

### Phase 2+ status

- `RenderGraphCommandList` moves to own header when its declaration grows beyond the current small surface
- Compile result types moved to `render_graph_compile.hpp`
- Diagnostics snapshot to `render_graph_diagnostics.hpp`
- Implementation is moving from header to `src/`; Phase 4-A made the target STATIC and moved
  `RenderGraphCommandList` method bodies into `src/render_graph.cpp`
- Phase 4-B moved registry methods, non-template `PassBuilder` methods, resource/pass facade
  methods, and public compile/execute overloads into `src/render_graph.cpp`

## Consequences

### Positive
- Includers that only need types (e.g., `rhi_vulkan_rendergraph`, tests, package consumers)
  can include `render_graph_types.hpp` instead of the full 4000-line header
- Reduces transitive compile impact when RenderGraph internals change
- Names `render_graph_types.hpp` directly convey purpose ("just the data contracts")
- Zero API breakage for existing consumers (aggregate header includes types)

### Negative
- One more header file to maintain
- Phase 2+ will require careful include dependency management

### Mitigation
- Aggregate headers include self-contained type/compile contracts before exposing builder APIs
- Package test validates that types header is self-contained (compiles standalone)
- CMake target stayed INTERFACE throughout Phase 1; Phase 4-A changed it to STATIC once the
  first implementation translation unit existed

## Commit plan

1. Create `render_graph_types.hpp` with extracted type content
2. Update `render_graph.hpp`: remove duplicated content, add `#include "render_graph_types.hpp"`
3. Build MSVC + ClangCL, run `--smoke-rendergraph` + all renderer smoke
4. (Future) Package-local test for standalone types header compilation
