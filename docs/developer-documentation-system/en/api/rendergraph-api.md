# API Reference: RenderGraph

## `RenderGraph`

Owns a graph declaration and compiles it into an execution plan.

### `importImage(RenderGraphImageDesc desc)`

Registers an externally owned image.

Parameters:

| Parameter | Type | Required | Description |
|---|---|---|---|
| `desc` | `RenderGraphImageDesc` | yes | name, format, extent, initial/final state, lifetime |

Return values:

| Type | Description |
|---|---|
| `RenderGraphImageHandle` | Image handle used by pass slots |

Errors:

| Error | Trigger |
|---|---|
| No direct returned error | Resource descriptions are validated during `compile()` |

### `createTransientImage(RenderGraphImageDesc desc)`

Registers a graph-owned transient image declaration.

The returned handle is valid for declarations in the same graph. Backend code owns the actual allocation.

### `importBuffer(RenderGraphBufferDesc desc)`

Registers an externally owned buffer.

### `createTransientBuffer(RenderGraphBufferDesc desc)`

Registers a graph-owned transient buffer declaration.

### `addPass(std::string name)`

Creates a pass builder without a type. A type should be set through the overload when schema validation is required.

### `addPass(std::string name, std::string type)`

Creates a pass builder with a schema type.

Return values:

| Type | Description |
|---|---|
| `RenderGraph::PassBuilder` | Fluent builder for slots, params, commands, callbacks |

### `compile()`

Compiles without an explicit schema registry. Resource and dependency validation still applies, but missing-schema, params-type, schema slot, and schema command checks only apply to `compile(schemaRegistry)`.

### `compile(const RenderGraphSchemaRegistry& schemaRegistry)`

Validates and compiles against registered schemas.

Return values:

| Type | Description |
|---|---|
| `Result<RenderGraphCompileResult>` | Success contains ordered passes, transitions, dependencies, culled passes, transient resources |

Errors:

| Error | Trigger |
|---|---|
| `ErrorDomain::RenderGraph` | invalid resource, dependency/cycle failure, or schema failures when using `compile(schemaRegistry)` |

### `execute()`

Compiles the graph without an explicit schema registry and executes callbacks attached to passes.

### `execute(const RenderGraphExecutorRegistry& executorRegistry)`

Compiles the graph without an explicit schema registry and executes using pass-bound callbacks first, then falls back to registered executors by pass type.

### `execute(const RenderGraphCompileResult& compiled)`

Executes a previously compiled plan using pass-bound callbacks.

### `execute(const RenderGraphCompileResult& compiled, const RenderGraphExecutorRegistry& executorRegistry)`

Executes a previously compiled plan using pass-bound callbacks first, then executor registry entries.

Execution rejects stale compile results when the graph declaration generation, pass count, image count, or buffer count no longer matches the current graph.

### `diagnosticsSnapshot(const RenderGraphCompileResult& compiled)`

Builds a diagnostics snapshot for UI or debugging.

### `formatDebugTables(const RenderGraphCompileResult& compiled)`

Formats readable debug tables for smoke output.

## `RenderGraph::PassBuilder`

### Resource Slots

| Function | Access |
|---|---|
| `writeColor(image)` | color attachment write |
| `readTexture(slotName, image, shaderStage)` | shader read |
| `readDepth(slotName, image)` | depth read |
| `writeDepth(slotName, image)` | depth write |
| `readDepthTexture(slotName, image, shaderStage)` | depth sampled read |
| `readTransfer(slotName, image)` | transfer read |
| `writeTransfer(slotName, image)` | transfer write |
| `readBuffer(slotName, buffer, shaderStage)` | buffer shader read |
| `readTransferBuffer(slotName, buffer)` | buffer transfer read |
| `writeBuffer(slotName, buffer)` | buffer transfer write |
| `readWriteStorageBuffer(slotName, buffer, shaderStage)` | storage read/write |

### Params And Commands

| Function | Description |
|---|---|
| `setParamsType(std::string)` | Sets params type id |
| `setParamsData(std::vector<std::byte>)` | Sets raw params bytes |
| `setParams<Params>(paramsType, params)` | Copies trivially copyable params into bytes |
| `execute(RenderGraphPassCallback)` | Attaches callback |
| `setCommands(RenderGraphCommandList)` | Attaches command summary list |
| `recordCommands(recorder)` | Builds command list by lambda |

## `RenderGraphCommandList`

Command summary builder:

| Function | Description |
|---|---|
| `setShader(shaderAsset, shaderPass)` | Select shader identity |
| `setTexture(bindingName, slotName)` | Bind texture slot by name |
| `setFloat(bindingName, value)` | Set float |
| `setInt(bindingName, value)` | Set int |
| `setVec4(bindingName, value)` | Set vec4 |
| `drawFullscreenTriangle()` | Request fullscreen triangle draw |
| `clearColor(slotName, color)` | Clear image slot |
| `fillBuffer(slotName, value)` | Fill buffer |
| `copyImage(sourceSlotName, targetSlotName)` | Image copy |
| `copyBuffer(sourceSlotName, targetSlotName)` | Buffer copy |
| `copyBufferToImage(sourceSlotName, targetSlotName)` | Buffer to image copy |
| `copyImageToBuffer(sourceSlotName, targetSlotName)` | Image to buffer copy |
| `dispatch(x, y, z)` | Compute dispatch |

## `RenderGraphSchemaRegistry`

Registers pass schemas. A schema defines pass type, params type, resource slots, allowed command kinds, culling, and side effects.

## Example

```cpp
auto schemas = asharia::basicRenderGraphSchemaRegistry();

asharia::RenderGraph graph;
auto target = graph.importImage({
    .name = "Target",
    .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
    .extent = {.width = 64, .height = 64},
    .initialState = asharia::RenderGraphImageState::Undefined,
    .finalState = asharia::RenderGraphImageState::Present,
});

graph.addPass("ClearTarget", asharia::kBasicTransferClearPassType)
    .writeTransfer("target", target)
    .recordCommands([](asharia::RenderGraphCommandList& commands) {
        commands.clearColor("target", {0.02F, 0.04F, 0.08F, 1.0F});
    });

auto compiled = graph.compile(schemas);
if (!compiled) {
    return compiled.error();
}
```

## Validation

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
```

Expected smoke checks:

- invalid command kind is rejected by schema validation,
- transient producer is ordered before reader,
- diagnostics snapshot contains pass/resource/edge data,
- executor callbacks run for compiled passes.
