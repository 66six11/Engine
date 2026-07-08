# Detailed Design: Scene World And Runtime Resource Registry

## Background

`scene-core` provides the current minimal runtime world: entity id, name, and transform. `resource-runtime` bridges asset products to runtime resource state. Both keep CPU-side data ownership and do not directly hold renderer, Vulkan, or editor UI state.

## Goals

- Use `World` to manage entity lifetime, generation, and transform.
- Use `EntityId` to prevent destroyed entities from being accessed through old handles.
- Use `RuntimeResourceRegistry` to manage pending/ready/failed state from asset handles to runtime products.
- Let renderer consume explicit draw/resource packets instead of querying editor state directly.

## Non-Goals

- Do not implement a full ECS.
- Do not store GPU buffers in `World`.
- Do not let `RuntimeResourceRegistry` read product bytes.
- Do not make scene-core depend on asset pipeline or renderer.

## Current Constraints

- `scene-core` only depends on `core`.
- `resource-runtime` only depends on `asset-core`.
- `World::tryGetTransform()` returns an immediate access pointer; the code comment says it must not be cached across World mutation.
- `RuntimeResourceTicket` contains key and generation.

## Overall Design

`World` uses a slot vector and free list for entities. `EntityId` is made from index and generation, and destroy releases a slot while generation prevents stale handles. Each live entity currently owns name and `TransformComponent`.

The runtime resource registry is an independent state machine. Callers create `RuntimeResourceKey` from asset handle and asset type, then request an expected product key. Load completion calls `markReady()` or `markFailed()` with the ticket. When manifest records are available, `resolveProductRecords()` can complete ready/fail from expected product key.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `packages/scene-core/include/asharia/scene/entity_id.hpp` | entity id structure |
| `packages/scene-core/include/asharia/scene/transform.hpp` | transform component |
| `packages/scene-core/include/asharia/scene/world.hpp` | entity create/destroy/query/update |
| `packages/resource-runtime/include/asharia/resource_runtime/runtime_resource_registry.hpp` | runtime resource state machine |
| `packages/renderer-basic/include/asharia/renderer_basic/draw_item.hpp` | renderer draw packet data shape |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `EntityId` | index, generation | stale handle detection |
| `TransformComponent` | position/rotation/scale fields | CPU-side transform |
| `World::EntitySlot` | generation, alive, name, transform | internal storage |
| `RuntimeResourceKey` | `guid`, `assetType` | runtime resource identity |
| `RuntimeResourceRecord` | `state`, `generation`, `expectedProductKey`, `product`, `failure` | load state |

## API Design

- `World::createEntity(name)` returns `Result<EntityId>`.
- `World::destroyEntity(entity)` invalidates entity generation.
- `World::setEntityName/setTransform()` return `VoidResult`.
- `World::tryGetTransform()` is only valid for the current call scope.
- `RuntimeResourceRegistry::request(key, expectedProductKey)` returns a ticket.
- `markReady/markFailed/resolveProductRecords()` require a current-generation ticket.

## Key Flows

### Normal Flow

1. Create entity.
2. Set name and transform.
3. Create runtime resource key from asset handle.
4. Request runtime resource.
5. Mark ready when product record is available.
6. Renderer builds draw list from scene/draw packet data.

### Failure Flow

- Invalid entity: World API returns `SceneErrorCode::InvalidEntity`.
- Capacity exceeded: create returns `SceneErrorCode::EntityCapacityExceeded`.
- Invalid resource key/product key: registry returns `Result<T>` errors keyed by `RuntimeResourceDiagnosticCode`.
- Product record mismatch: registry returns a product-key mismatch error.

### Boundary Flow

- `tryGetTransform()` pointer must not be cached across `createEntity()` / `destroyEntity()` / mutation.
- Runtime resource failed state should retain reason and message.
- Renderer does not directly own `World` mutation permission.

## Lifetime

World owns entity slots. After entity destroy, the old `EntityId` is no longer alive. Runtime registry records move from pending to ready or failed; a new request increments generation.

## Error Handling

Scene errors use `ErrorDomain::Scene` or project error mapping. Runtime resource errors preserve expected/actual generation and product key for stale completion debugging.

## Test Plan

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/scene-core -B build/cmake/package-scene-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-scene-core-tests-msvc-debug && ctest --test-dir build/cmake/package-scene-core-tests-msvc-debug --output-on-failure"
```

Runtime draw packet smoke:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-scene-draw-packet
```

## Risks

- Cached transform pointers can be invalidated by vector mutation. Mitigation: API comments require immediate access only.
- If runtime resource generation is ignored, old loader completion can overwrite a newer product. Mitigation: ticket-based completion.
- Direct scene/renderer coupling would block future multi-world or multi-viewport flows. Mitigation: pass explicit view/resource data instead of renderer-owned scene objects.
