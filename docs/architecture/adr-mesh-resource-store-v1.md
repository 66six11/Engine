# ADR：Mesh Resource Store v1

## 状态

Accepted and implemented for #394。

## 背景

#386 已冻结 Mesh Product v1、受限 `.glb` importer、deterministic publication 与 runtime-safe reader，但旧 `RuntimeResourceRegistry` 的 `Ready` 只表示找到 exact `AssetProductRecord`。它没有读取 artifact、创建 typed CPU payload、分离 handle/request generation，也不能在 reload 失败时保留旧资源。

当前最小需求是为后继 renderer GPU mesh owner 提供一份可安全租借的 immutable `MeshProductV1`。它必须满足：

- runtime 不依赖 source importer、`.ameta`、editor 或 Vulkan；
- IO/parse 可在 worker 执行，但 live store 只能在 owner thread mutation；
- stale handle 与 stale async completion 分别拒绝；
- reload 失败不能破坏当前 active payload；
- diagnostics 不泄露机器上的绝对 artifact cache root；
- 当前 manifest 仍只有 relative path、byte size 与 64-bit product hash，不能伪装成最终 SHA-256 ArtifactId。

## 先例与 Asharia 取舍

| 先例 | 采用的行为 | 拒绝或延期 |
| --- | --- | --- |
| Unreal [`FStreamableManager`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FStreamableManager) / streamable handles | logical request 与已加载对象 lifetime 分开；完成后由 handle/owner 保持对象，而不是让临时 IO callback 暴露悬空借用 | 不复制 global asset registry、soft-object path、GC/UObject 或通用 streamable graph |
| O3DE [Asset System](https://docs.o3de.org/docs/user-guide/assets/asset-system/) | Runtime 消费 Asset Processor 生成的 cache product；source/import processing 与 runtime asset ownership 分层 | 不实现 Asset Bus、catalog network service、全资产 dependency notification 或 background processor |
| Godot [`ResourceLoader`](https://docs.godotengine.org/en/stable/classes/class_resourceloader.html) | Runtime loader 消费 imported resource，并让已加载 resource 由引用计数对象安全共享 | 不复制路径作为最终持久身份、全局 cache mode 或动态脚本 loader |
| Bevy [`Assets<T>`](https://docs.rs/bevy/latest/bevy/asset/struct.Assets.html) | typed asset storage 和 generation-aware handle 比公开 `void*`/巨大 variant 更容易保持类型与 stale-handle 安全 | 当前只实现 Mesh-specific store，不提前抽象通用 `Assets<T>`、server、events 或 hot reload |

这些先例只用于校准 owner/lifetime 分层。Asharia 的具体合同由 package-first、headless C++23、当前 manifest v1 与 Vulkan/Avalonia 边界决定。

## 决策

### 1. 抽出 runtime-safe artifact verification

新增 `asharia::asset_artifact`：

- `AssetArtifactLocatorV1` 只含 manifest-relative path、expected bytes 与 expected hash；
- `readVerifiedAssetArtifactV1()` 先校验 path/limit，再读取，随后精确校验 size 与 V1 FNV-1a hash；
- 底层 `readFileBytes()` 的绝对路径 diagnostic 不向上转发；
- `asset-pipeline` 的 product-path validator 复用同一路径规则。

这不是最终 content-addressed store。SHA-256 `ArtifactId`、immutable object path 与 runtime manifest snapshot 继续作为独立 versioned migration。

### 2. 采用 Mesh-specific typed store

旧 tri-state registry 被 `MeshResourceStore` 硬切替换。公开 payload 固定为 `shared_ptr<const MeshProductV1>`，不使用裸 owning pointer、`void*`、巨大 `variant` 或 speculative template framework。

Store 使用两种 generation：

```text
MeshResourceHandle     = slot + slotGeneration
MeshResourceLoadTicket = handle + requestGeneration + expectedProductHash
```

`slotGeneration` 只处理 unload/reuse；`requestGeneration` 只处理异步 selection/load 新鲜度。二者不可合并。

### 3. 分离 active 与 candidate

每个 slot 同时允许：

- 一个可选 `active` immutable revision；
- 一个可选 `candidate` load ticket；
- 一个可选 `lastFailure`。

成功 publish 递增 `activeRevision` 并原子替换 active shared pointer。Candidate failure 只清除 candidate、记录 failure；如果已有 active，仍可 acquire。旧 lease 独立保持旧 payload，直到最后一个 consumer 释放。

### 4. Worker 只产生 owning completion

`loadMeshResourceCandidate(plan)` 读取/校验 artifact，再从 bytes 调用 `readMeshProductV1()`。它只返回 owning completion，不保存 store pointer，也不直接 publish。

`request()`、`publish()`、`unload()` 必须在 store create thread 调用。Host 决定 worker/thread pool 与 owner-thread safe point；v1 不内置队列、mutex、callback dispatcher 或 job system。

### 5. Selection failure 是资源状态

Missing、stale 或 invalid exact product record 返回 `MeshResourceRequestResult` 的 `FailedNoActive` / `KeptActiveAfterFailure`，并携带 typed failure。Invalid key/type/handle、wrong owner thread、stale generation 与 forged completion 才是 API diagnostic error。

这样 asset database 变化不会被误报成程序崩溃条件，同时程序合同错误仍 fail closed。

## 备选方案

### 继续扩展旧 `RuntimeResourceRegistry`

拒绝。它的 `Ready` 已绑定“record resolved”语义，单 generation 也无法同时表达 slot lifetime 与 request freshness。继续加 IO/payload 会保留含混状态并扩大迁移面。

### 直接让 `resource-runtime` 依赖 `asset-pipeline`

拒绝。这样会把 fastgltf、source/importer/settings 与 tool-side publication 带进 runtime，违反 package 依赖方向。

### 现在建立通用 `ResourceManager<T>` 与内置 thread pool

拒绝。当前只有 Mesh Product v1 证明了 typed load/reload 需求；texture/material/shader 的依赖、fallback 与 GPU handoff 尚不同。先验证一个完整 vertical slice，再从真实重复中提取共性。

### 成功 reload 时使旧 handle/lease 失效

拒绝。Renderer 或并发 consumer 可能仍在读取旧 CPU revision；立即失效会把替换安全转嫁给调用方。shared immutable lease 是当前最小安全 ownership。

### 让 Runtime 直接读取 source `.glb`

拒绝。它绕过 deterministic import/cook/publication，污染 headless runtime 依赖，并使 Editor/renderer 对 source-format parser 形成隐式依赖。

## 后果

收益：

- `Ready` 现在真实表示 typed CPU mesh 已创建并可租借；
- artifact IO、product parse、store mutation 与 GPU ownership 边界明确；
- stale handle/completion、reload fallback 和 old-lease lifetime 可独立测试；
- 后继 renderer 可以只消费 lease/immutable upload packet，不读取 source 或 manifest path。

代价与限制：

- Store 当前按 key 线性查找，适合 baseline，尚无规模/eviction policy；
- `MeshResourceLoadPlan` 暂时携带 artifact root 给 host-owned worker，直到 ArtifactId/store interface 落地；
- v1 product hash 仍是兼容校验，不具备最终内容身份的 collision resistance；
- CPU lease 不解决 GPU fence/deferred retirement；该责任仍在 renderer/RHI backend；
- 自动 watcher/reimport、runtime dependency graph、streaming 与 thumbnail 明确延期。

## 验证

- `asharia-asset-artifact-tests`：路径、limit、missing、size、hash、成功读取与 root redaction；
- `asharia-resource-runtime-smoke-tests`：selection states、dedup、双 generation、stale completion、reload success/failure、unload/reuse、old lease 与 owner thread；
- `asharia-asset-processor --smoke-mesh-resource`：真实 restricted GLB 经 product publication 后进入 typed lease，验证 11 vertices、9 indices、3 submeshes、3 material slots 与固定 bounds；
- repository encoding/doc/package/dependency/build/CTest/changed-clang-tidy gates。

## 后继工作

1. renderer-specific GPU mesh owner 消费 `MeshResourceLease`，建立 vertex/index upload、revision swap 与 fence-based deferred retirement；
2. Scene draw binding 从 validation product 切换为 runtime/GPU resource handle；
3. 在真实第二种 typed resource 证明共性后，再评估共享 store primitives；
4. 以独立 schema migration 引入 SHA-256 ArtifactId/runtime manifest snapshot；
5. manual reload 闭环完成后再引入 watcher/debounce/automatic reimport 与 thumbnail consumer。
