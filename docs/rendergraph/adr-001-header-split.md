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
  render_graph_command_list.hpp # RenderGraphCommandList command accumulator
  render_graph_pass_context.hpp # pass execution context and callback
  render_graph_execution.hpp    # schema/executor registries
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

### What stayed in `render_graph.hpp` after Phase 1

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
- Phase 4-C moved diagnostics snapshot and Markdown debug table formatting into
  `src/render_graph.cpp`
- Phase 4-D moved private `compile(schemaRegistry*)` and
  `execute(compiled, executorRegistry*)` implementations into `src/render_graph.cpp`
- Phase 4-E moved dependency ordering, producer inference, and pass culling helpers into
  `src/render_graph_dependencies.cpp`
- Phase 4-F moved handle and slot validation helpers into
  `src/render_graph_validation.cpp`
- Phase 4-G moved schema and access validation helpers into
  `src/render_graph_validation.cpp`
- Phase 4-H moved transient lifetime and resource transition helpers into
  `src/render_graph_lifetime.cpp`
- Phase 4-I moved debug label and table formatting helpers into
  `src/render_graph_debug.cpp`
- Phase 5-A/B moved `RenderGraphCommandList` into `render_graph_command_list.hpp`,
  moved pass execution context plus schema/executor registries into
  `render_graph_execution.hpp`, and kept `render_graph.hpp` as the aggregate include
- Phase 5-D moved `RenderGraph` and `RenderGraph::PassBuilder` declarations into
  `render_graph_builder.hpp`, leaving `render_graph.hpp` as a pure aggregate include
- Later Phase 5 work split dependency producer inference, topo sort, and culling into
  internal `src/render_graph_dependency_builder.hpp/.cpp`,
  `src/render_graph_dependency_sort.hpp/.cpp`, and
  `src/render_graph_dependency_culling.hpp/.cpp`, then removed the old
  `src/render_graph_dependencies.cpp` compile unit from the target.
- Later Phase 5 work moved pass declaration state from the private nested
  `RenderGraph::Impl::Pass` type into internal `src/render_graph_pass.hpp`.
  Pass validation now enters through `rendergraph_internal::validatePass()`
  with image/buffer spans.
- Later Phase 5 work split public pass execution context/callback declarations
  into `render_graph_pass_context.hpp`, leaving `render_graph_execution.hpp` to
  own schema/executor registries.
- Later Phase 5 work also narrowed internal validation declarations so
  `src/render_graph_validation.hpp` forward declares the schema registry instead
  of depending on the full execution-registry header.
  A later narrowing moved handle and image/buffer graph validation helper
  implementations into `src/render_graph_validation.cpp`, leaving
  `src/render_graph_validation.hpp` as declarations over render graph types plus
  internal pass/schema forward declarations.
  A later narrowing moved schema/command/required-slot validation helpers into
  `src/render_graph_schema_validation.hpp/.cpp`; `render_graph_validation.cpp`
  now owns handle/image/buffer and pass slot/access validation and delegates the
  schema gate via `validatePassSchema()`.
  A later narrowing split the resource slot/required-slot schema gate and the
  command whitelist schema gate into
  `src/render_graph_resource_schema_validation.hpp/.cpp` and
  `src/render_graph_command_schema_validation.hpp/.cpp`, leaving
  `render_graph_schema_validation.cpp` to own schema lookup, params contract
  validation, and gate orchestration.
  A later narrowing moved pass slot/access/stage validation helpers into
  `src/render_graph_slot_validation.hpp/.cpp`; `render_graph_validation.cpp`
  now owns image/buffer handle/final-state validation and composes
  `validatePassSlots()` with `validatePassSchema()`.
  A later narrowing split image-slot and buffer-slot validation into
  `src/render_graph_image_slot_validation.hpp/.cpp` and
  `src/render_graph_buffer_slot_validation.hpp/.cpp`, leaving
  `render_graph_slot_validation.cpp` as the duplicate-slot-name and pass-slot
  orchestration layer.
  A later narrowing split image/buffer same-resource access conflict validation
  into `src/render_graph_image_access_validation.hpp/.cpp` and
  `src/render_graph_buffer_access_validation.hpp/.cpp`, leaving the image/buffer
  slot validation files focused on slot name, handle, and shader-stage checks.
- Later Phase 5 work narrowed `render_graph_builder.hpp` so it depends on
  `render_graph_pass_context.hpp` for callbacks and only forward declares the
  schema/executor registries. Internal helper headers for dependency
  builder/culling/sort, lifetime, diagnostics snapshot, and debug tables no
  longer include `src/render_graph_internal.hpp`; only implementation files that
  own or adapt the PImpl declaration storage include it directly.
  A later narrowing moved Markdown debug table helper implementations into
  `src/render_graph_debug_tables.cpp`, leaving
  `src/render_graph_debug_tables.hpp` as the `formatDebugTables()` declaration
  boundary.
  A later narrowing split slot, command, transition, and transient detail table
  formatting into `src/render_graph_debug_detail_tables.hpp/.cpp`, leaving
  `render_graph_debug_tables.cpp` to assemble summary/detail table sections.
  A later narrowing moved resource/pass/dependency/culled pass summary table
  formatting into `src/render_graph_debug_summary_tables.hpp/.cpp`, leaving
  `render_graph_debug_tables.cpp` as the Markdown table assembly entry point.
  A later narrowing moved diagnostics snapshot helper implementations into
  `src/render_graph_diagnostics_snapshot.cpp`, leaving
  `src/render_graph_diagnostics_snapshot.hpp` as the `makeDiagnosticsSnapshot()`
  declaration boundary.
  A later narrowing split diagnostics pass/command/access/before-transition
  node construction into `src/render_graph_diagnostics_pass_snapshot.hpp/.cpp`,
  leaving `render_graph_diagnostics_snapshot.cpp` to assemble resource,
  dependency, final-transition, and top-level snapshot state.
- Later Phase 5 work moved schema registry lookup behind
  `src/render_graph_schema_queries.hpp/.cpp`, so `src/render_graph_pass_queries.hpp`
  and its dependency/culling/lifetime includers no longer need to transitively
  include `render_graph_execution.hpp`.
  A later narrowing moved pass/resource query and transition helper
  implementations into `src/render_graph_pass_queries.cpp`, leaving
  `src/render_graph_pass_queries.hpp` as declarations over render graph compile
  types plus internal pass/schema forward declarations.
- Later Phase 5 work narrowed dependency sorting to consume `std::span<const Pass>`
  plus dependency data instead of a template `Graph` parameter, so topo sort and
  cycle diagnostics cannot reach the full `RenderGraph::Impl` state. A later
  narrowing moved the topo sort and cycle reporting implementation into
  `src/render_graph_dependency_sort.cpp`, leaving
  `src/render_graph_dependency_sort.hpp` as the `sortPassesByDependencies()`
  declaration boundary.
- Later Phase 5 work narrowed compile-local culled-pass materialization to
  consume `std::span<const Pass>` instead of a template `Graph` parameter.
  A later narrowing moved compiled pass materialization and per-pass transition
  append into `src/render_graph_compiled_pass.hpp/.cpp`, leaving
  `render_graph_compile.cpp` to orchestrate validation, dependency building,
  culling, sorting, and final/transient resource summary.
- Later Phase 5 work narrowed active-pass culling and imported-resource write
  checks to consume pass/image/buffer spans plus dependency data instead of a
  template `Graph` parameter. A later narrowing moved the culling implementation
  into `src/render_graph_dependency_culling.cpp`, leaving
  `src/render_graph_dependency_culling.hpp` as the `findActivePasses()`
  declaration boundary.
- Later Phase 5 work narrowed dependency producer inference and add/read/build
  substeps to consume `DependencyBuildInputs` spans instead of a template
  `Graph` parameter. A later narrowing moved those substeps into
  `src/render_graph_dependency_builder.cpp`, leaving
  `src/render_graph_dependency_builder.hpp` as the `DependencyBuildInputs` and
  `buildDependencies()` declaration boundary.
  A later narrowing split image dependency producer inference and buffer
  dependency producer inference into
  `src/render_graph_image_dependency_builder.hpp/.cpp` and
  `src/render_graph_buffer_dependency_builder.hpp/.cpp`, leaving
  `render_graph_dependency_builder.cpp` as the dependency build orchestration
  entrypoint.
- Later Phase 5 work narrowed validation handle/image/buffer graph checks and
  lifetime transient allocation/declared-use/transition helpers to consume
  image/buffer/pass/compiled pass spans instead of a template `Graph` parameter.
  A later narrowing moved the lifetime implementation into
  `src/render_graph_lifetime.cpp`, leaving `src/render_graph_lifetime.hpp` as
  the declaration boundary for the compile-local lifetime helpers.
- Later Phase 5 work narrowed diagnostics/debug label, snapshot, and Markdown
  table helpers to consume a `RenderGraphDeclarationView` over image/buffer/pass
  declaration spans instead of a template `Graph` parameter.
  A later narrowing first folded handle/pass/dependency label helpers into
  `src/render_graph_debug_tables.cpp` because they are only used by debug table
  formatting, and removed the old `src/render_graph_labels.hpp` helper header.
  Phase 5-BH then moved those summary label/table helpers with the
  resource/pass/dependency/culled pass tables into
  `src/render_graph_debug_summary_tables.cpp`.
- Later Phase 5 work centralized diagnostics facade view construction in
  `makeRenderGraphDeclarationView()`, so helper reuse does not need to name the
  private nested `RenderGraph::Impl` type.
  A later narrowing moved that factory implementation into
  `src/render_graph_declaration_view.cpp`, leaving
  `src/render_graph_declaration_view.hpp` as a self-contained view/factory
  declaration boundary over render graph types and the internal `Pass`
  declaration state.
- Later Phase 5 work narrowed pass/resource query helpers and validation-only
  slot/schema/access helpers to consume the concrete internal `Pass` declaration
  type instead of keeping a generic `template <typename Pass>` entry point.
- Later Phase 5 work moved the public diagnostics facade implementations into
  `render_graph_diagnostics.cpp`, so `RenderGraph::Impl` no longer declares
  diagnostics snapshot or Markdown debug table member entry points.
- Later Phase 5 work moved compile/execute behavior entry points behind the
  internal `render_graph_operations.hpp` interface. `render_graph_compile.cpp`
  and `render_graph_execution.cpp` now consume `RenderGraphDeclarationView`
  instead of defining `RenderGraph::Impl` member functions, leaving `Impl` as
  the image/buffer/pass declaration container.
- Later Phase 5 work moved the public compile facade into
  `render_graph_compile.cpp` and the public execute facade into
  `render_graph_execution.cpp`, so `render_graph.cpp` only owns `RenderGraph`
  lifetime, copy, and move operations.
- Phase 5-E moved stateless compile/dependency/schema helpers that do not access private graph
  state into `.cpp` file-local functions, reducing implementation-only declarations in
  `render_graph_builder.hpp`
- Phase 5-F split implementation responsibilities under `src/`: command list methods moved to
  `render_graph_command_list.cpp`, schema/executor registry methods moved to
  `render_graph_registry.cpp`, and non-template builder/resource declaration methods moved to
  `render_graph_builder.cpp`. Later Phase 5 work further narrowed
  `render_graph.cpp` to the `RenderGraph` lifetime and copy/move facade.
  A later narrowing split graph image/buffer resource declaration methods and
  pass declaration/default state construction into
  `src/render_graph_resource_declarations.cpp` and
  `src/render_graph_pass_declarations.cpp`, leaving
  `render_graph_builder.cpp` to own only `PassBuilder` fluent mutation methods.
- Diagnostics snapshot types now live in `render_graph_diagnostics.hpp`; consumers that only
  inspect diagnostics should include that narrow header instead of the aggregate header
- Consumers that only record command summaries should include `render_graph_command_list.hpp`;
  consumers that only register schemas or executors should include `render_graph_execution.hpp`
- Consumers that build graphs directly can include `render_graph_builder.hpp`

## Consequences

### Positive
- Includers that only need types (e.g., `rhi_vulkan_rendergraph`, tests, package consumers)
  can include `render_graph_types.hpp` instead of the full 4000-line header
- Includers that only need command recording helpers can include `render_graph_command_list.hpp`
- Includers that only need pass callback/context can include `render_graph_pass_context.hpp`
- Includers that only need schema registry or executor registry can include
  `render_graph_execution.hpp`
- Includers that build graph declarations can include `render_graph_builder.hpp`;
  callers that instantiate registries include `render_graph_execution.hpp`
- Includers that only need diagnostics can include `render_graph_diagnostics.hpp`
- Reduces transitive compile impact when RenderGraph internals change
- Names `render_graph_types.hpp` directly convey purpose ("just the data contracts")
- Zero API breakage for existing consumers (aggregate header includes types)

### Negative
- One more header file to maintain
- Phase 2+ will require careful include dependency management

### Mitigation
- Aggregate headers include self-contained type/compile contracts before exposing builder APIs
- Package tests validate that the types, compile, diagnostics, and aggregate headers are
  self-contained (compile standalone)
- CMake target stayed INTERFACE throughout Phase 1; Phase 4-A changed it to STATIC once the
  first implementation translation unit existed

## Commit plan

1. Create `render_graph_types.hpp` with extracted type content
2. Update `render_graph.hpp`: remove duplicated content, add `#include "render_graph_types.hpp"`
3. Build MSVC + ClangCL, run `--smoke-rendergraph` + all renderer smoke
4. (Future) Package-local test for standalone types header compilation
