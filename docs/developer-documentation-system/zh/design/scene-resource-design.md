# 详细设计：Scene World 与 Runtime Resource Registry

## 背景

`scene-core` 提供当前最小 runtime world：entity id、name、transform。`resource-runtime` 提供资产 product 到 runtime resource 的状态桥接。两者都保持 CPU-side data ownership，不直接持有 renderer、Vulkan 或 editor UI state。

## 目标

- 用 `World` 管理 entity lifetime、generation 和 transform。
- 用 `EntityId` 防止 destroyed entity 被旧 handle 访问。
- 用 `RuntimeResourceRegistry` 管理 asset handle 到 runtime product 的 pending/ready/failed 状态。
- 让 renderer 从明确的 draw/resource packets 消费数据，而不是直接查询 editor state。

## 非目标

- 不实现完整 ECS。
- 不在 `World` 中存储 GPU buffers。
- 不让 `RuntimeResourceRegistry` 读取 product bytes。
- 不在 scene-core 中依赖 asset pipeline 或 renderer。

## 当前约束

- `scene-core` 只依赖 `core`。
- `resource-runtime` 只依赖 `asset-core`。
- `World::tryGetTransform()` 返回 immediate access pointer；代码注释明确不能跨 World mutation 缓存。
- `RuntimeResourceTicket` 包含 key 和 generation。

## 总体方案

`World` 用 slot vector 和 free list 管理 entity。`EntityId` 由 index/generation 组成，destroy 会释放 slot 并通过 generation 防 stale handle。每个 live entity 当前拥有 name 和 `TransformComponent`。

Runtime resource registry 是独立状态机。调用方用 asset handle 和 asset type 生成 `RuntimeResourceKey`，再 request expected product key。加载完成后，用 ticket 调用 `markReady()` 或 `markFailed()`。如果有 manifest records，也可以用 `resolveProductRecords()` 根据 expected product key 完成 ready/fail。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `packages/scene-core/include/asharia/scene/entity_id.hpp` | entity id 结构 |
| `packages/scene-core/include/asharia/scene/transform.hpp` | transform component |
| `packages/scene-core/include/asharia/scene/world.hpp` | entity create/destroy/query/update |
| `packages/resource-runtime/include/asharia/resource_runtime/runtime_resource_registry.hpp` | runtime resource state machine |
| `packages/renderer-basic/include/asharia/renderer_basic/draw_item.hpp` | renderer draw packet data shape |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `EntityId` | index、generation | stale handle detection |
| `TransformComponent` | position/rotation/scale fields | CPU-side transform |
| `World::EntitySlot` | generation、alive、name、transform | internal storage |
| `RuntimeResourceKey` | `guid`、`assetType` | runtime resource identity |
| `RuntimeResourceRecord` | `state`、`generation`、`expectedProductKey`、`product`、`failure` | load state |

## API 设计

- `World::createEntity(name)` 返回 `Result<EntityId>`。
- `World::destroyEntity(entity)` invalidates entity generation.
- `World::setEntityName/setTransform()` 返回 `VoidResult`。
- `World::tryGetTransform()` 只用于当前调用范围。
- `RuntimeResourceRegistry::request(key, expectedProductKey)` 返回 ticket。
- `markReady/markFailed/resolveProductRecords()` 只能用 current generation ticket。

## 关键流程

### 正常流程

1. 创建 entity。
2. 设置 name 和 transform。
3. 从 asset handle 生成 runtime resource key。
4. request runtime resource。
5. product record 可用后 mark ready。
6. renderer 从 scene/draw packet 构建 draw list。

### 失败流程

- invalid entity：World API 返回 `SceneErrorCode::InvalidEntity`。
- capacity 超限：create 返回 `SceneErrorCode::EntityCapacityExceeded`。
- invalid resource key/product key：registry 返回以 `RuntimeResourceDiagnosticCode` 标识的 `Result<T>` errors。
- product record 不匹配：registry 返回 product-key mismatch error。

### 边界流程

- `tryGetTransform()` pointer 不能跨 `createEntity()` / `destroyEntity()` / mutation 缓存。
- Runtime resource failed state 应保留 reason 和 message。
- Renderer 不直接持有 `World` mutation 权限。

## 生命周期

World 拥有 entity slots。Entity destroy 后，旧 `EntityId` 不再 alive。Runtime registry record 从 pending 进入 ready 或 failed；新 request 会提升 generation。

## 错误处理

Scene errors 使用 `ErrorDomain::Scene` 或 project error mapping。Runtime resource errors 保留 expected/actual generation 和 product key，便于定位 stale completion。

## 测试方案

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/scene-core -B build/cmake/package-scene-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-scene-core-tests-msvc-debug && ctest --test-dir build/cmake/package-scene-core-tests-msvc-debug --output-on-failure"
```

Runtime draw packet smoke：

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-scene-draw-packet
```

## 风险

- 缓存 transform pointer 会在 vector mutation 后失效。缓解：API 注释要求 immediate access only。
- Runtime resource generation 若不检查，会出现 old loader completion 覆盖新 product。缓解：ticket-based completion。
- Scene 和 renderer 直接耦合会阻塞未来多 world/multi viewport。缓解：传递显式 view/resource data，而不是 renderer-owned scene objects。
