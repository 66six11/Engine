# Mesh Product v1 与受限 GLB 导入

资料核对日期：2026-08-14
状态：Current（#386/#394）；以本文件、`mesh-product`、`asset-artifact`、`resource-runtime` 与真实 smoke 为合同真相。

## 目的与所有权

Mesh Product v1 把外部模型源格式归一化成稳定、确定、可在无 Editor/Vulkan 环境独立校验的 CPU mesh
事实。它有意停在 product reader：不把 source-format parser 放入 Runtime，也不让 Asset Inspector、
ResourceRuntime 或 renderer 成为 importer 的依赖。

| 阶段 | Owner | 当前输入与输出 |
| --- | --- | --- |
| source/import | `asset-pipeline` | 受限 glTF 2.0 `.glb` bytes → `MeshProductBuildInputV1` |
| cooked format/write | `mesh-product` 的 tool-side `asharia::mesh_product_writer` | 归一化 facts → canonical Mesh Product v1 bytes |
| product read | runtime-safe `asharia::mesh_product` | product bytes/file → immutable owning `MeshProductV1` |
| publication | `asset-pipeline` | 发布前以内存 bounded reader 校验 product；staging 复读 size/hash 后原子替换，并发布 manifest record/hash |
| artifact verification | runtime-safe `asharia::asset_artifact` | manifest-relative path/limit/size/V1 hash → owning verified bytes |
| runtime CPU resource | `asharia::resource_runtime` | exact product selection → generation-safe `MeshResourceLease`；reload failure 保留旧 active |
| GPU/preview | 后续 Slice | 当前不创建 GPU buffer、Scene View binding 或 thumbnail |

`mesh-product` 不保存 source path、importer/settings、editor state、runtime generation、renderer key 或
Vulkan object。artifact 内容身份与 publication 完整性由外层 manifest/product hash 负责，v1 不在容器内再定义
第二套 checksum。

## 成熟引擎依据与取舍

| 依据 | Adopt | Reject / Defer |
| --- | --- | --- |
| [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) 与 [Khronos glTF Validator](https://github.khronos.org/glTF-Validator/) | 遵循 GLB v2、scene/node/primitive/accessor 结构、`T * R * S`、little-endian buffer、finite/accessor/range 校验；Khronos Validator 作为 fixture 的外部规格门禁 | glTF 是 source contract，不是 runtime product；本 Slice不承诺完整 glTF 2.0、extensions 或 validator 嵌入运行时 |
| [Unreal Interchange](https://dev.epicgames.com/documentation/en-us/unreal-engine/importing-assets-using-interchange-in-unreal-engine) / Static Mesh 思路 | 采用“source 翻译 → 确定 pipeline → engine asset/facts”的 owner 分离，以及每 primitive 对应 material assignment 的做法 | 不复制 Interchange 的通用 node graph、pipeline stack、Blueprint/Python/runtime import framework；当前只有一个确定性内建 importer |
| [Unity Asset Database](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetDatabase.html) | source 与 imported artifact 分离，importer/source/settings/tool/schema 参与 product identity；重复输入产生可复现 artifact | 不把 Unity GameObject/sub-asset object graph 或 ModelImporter UI 序列化进 product |
| [Godot ArrayMesh](https://docs.godotengine.org/en/stable/classes/class_arraymesh.html) | 采用 position 必需、其他 vertex arrays 可选、index 可选、surface/submesh 独立 material slot 的最小 mesh facts | 不复制 `ArrayMesh` 动态 mutation API、blend shapes、LOD 或 renderer resource lifetime |
| [O3DE Scene Pipeline](https://docs.o3de.org/docs/user-guide/assets/scene-pipeline/) 与 [Scene Builder](https://docs.o3de.org/docs/user-guide/assets/scene-pipeline/scene-builder/) | source scene 先产生中间层次，再由 export/cook 写 game-ready product；Runtime 消费 product 而非 source parser | 不建立通用 SceneGraph、Scene Settings/manifest rules、mesh optimizer 或 ModelAsset ecosystem |

Asharia 特有决策来自当前 package-first、headless C++23 与 Vulkan/Avalonia 边界：用一个小型
`mesh-product` package 同时提供 runtime-safe reader target 与 tool-only writer target；importer 仍留在
`asset-pipeline`，而不是把 fastgltf 或 authoring state带到 Runtime。

## Mesh Product v1 二进制合同

所有多字节整数与 float 均为 little-endian。唯一 canonical layout 为：

```text
128-byte header
vertex section      16-byte aligned, count × 32 bytes
index section       16-byte aligned, count × uint32
submesh section     16-byte aligned, count × 16 bytes
material-slot       16-byte aligned, count × 16 bytes
```

Header 固定事实：

- magic 为 8 bytes `ASHMESH1`，`formatVersion == 1`；
- endian marker 为 `0x01020304`，header size 为 128；
- 唯一 vertex format 为 `P3N3Uv2F32`，stride 为 32 bytes；
- counts、四个 canonical section offsets、完整 file size 与 local AABB；
- reserved fields、header tail 与所有 alignment padding 必须为零。

Header 的 byte offset 冻结如下：

| Offset | Bytes | 字段 |
| ---: | ---: | --- |
| 0 | 8 | magic `ASHMESH1` |
| 8 | 4 | format version |
| 12 | 4 | little-endian marker |
| 16 | 4 | header bytes |
| 20 | 4 | vertex format |
| 24 | 4 | vertex stride |
| 28 | 4 | reserved = 0 |
| 32 | 16 | vertex/index/submesh/material-slot counts，各 `uint32` |
| 48 | 32 | vertex/index/submesh/material-slot offsets，各 `uint64` |
| 80 | 8 | complete file size |
| 88 | 24 | local AABB min/max，各三个 binary32 |
| 112 | 16 | reserved = 0 |

Section 事实：

- vertex 为 position.xyz、normal.xyz、UV0.xy 的 finite canonical float32；`-0.0` 不是 canonical encoding；
- index 为 triangle-list `uint32`，总数非零且为 3 的倍数，每个 index 必须落在 vertex range；
- submesh 为 `{firstIndex, indexCount, materialSlot, reserved=0}`，按顺序无 gap/overlap 覆盖完整 index buffer，
  每段自身是完整 triangle list；
- material slot 是有序的 16-byte `AssetGuid`，按 `AssetGuid::bytes[0..15]` 原样写入；zero GUID
  明确表示未绑定，重复 GUID 合法；
- AABB 必须有限、min/max 有序，并逐 bit 等于 reader 从所有 position 重算的结果。

默认 read/write limits 相同：512 MiB product、8 Mi vertices、24 Mi indices、65,536 submeshes、
65,536 material slots。reader 在按 count 分配前校验 byte budget、checked offset/size arithmetic、canonical
layout 和全部 count limits；随后才解码 payload 并校验 facts。错误通过 `MeshProductErrorCode` 保留
invalid magic/version/endian/format、truncation、budget/count、layout、non-finite、bounds、index、submesh、
material slot 与 non-canonical context。

公共读取入口是 `readMeshProductV1()` / `readMeshProductV1File()`；tool-side 写入入口是
`writeMeshProductV1()` / `writeMeshProductV1File()`。两次写入同一归一化输入必须 byte-identical。

## 坐标、单位、法线与 winding

glTF source 是右手坐标：`-X` right、`+Y` up、`+Z` forward，线性单位为 meter。Mesh Product v1 的
Asharia canonical space 是左手坐标：`+X` right、`+Y` up、`+Z` forward，`1 unit == 1 meter`。因此 importer
在 glTF global node transform 后应用 basis conversion：

```text
C = diag(-1, 1, 1)
productPosition = C * gltfGlobalTransform * sourcePosition
```

节点 local transform 遵守 glTF 的 `T * R * S`，global transform 为 `parentGlobal * local`；所有 transform
在 cook 时 bake 到 vertex，product 不保留 source node hierarchy。法线使用最终线性变换的 inverse-transpose
并 normalize；non-invertible transform、NaN/Inf 和退化三角形 fail closed。

glTF 在缺 normal 时要求生成 flat normals。因此 importer 按 face 确定性生成法线，并在共享 vertex 不能表达
flat face normal 时拆点；不得改成隐式 smooth averaging。缺 UV0 时填 `(0, 0)`。

Product 所有三角形固定为 counter-clockwise front face。在 global transform 加坐标 basis conversion 后，
若最终 linear determinant 为负，importer 交换每个 triangle 的第二、第三个 index，使 bake 后 geometry 仍为
canonical CCW。这条规则同时覆盖 glTF→Asharia handedness conversion 与 negative-scale mirror，不把 winding
修补推迟给 renderer/material。

## 受限 `.glb` 支持矩阵

Importer identity 为 `com.asharia.importer.mesh.glb-static` v1；只接受 `.glb`，当前无 import settings。
该 importer v1 的输出 schema 固定为 Mesh Product `formatVersion == 1`。任何会改变二进制合同或既有输入
cooked facts 的不兼容 schema 修改，都必须同时升级 Mesh Product format 与 importer version；不改变 schema、但会
改变既有输入 cooked bytes/semantics 的实现修改，也至少必须升级 importer version。两者都通过
`AssetProductKey.importerVersion` 令旧 cache/manifest key 失效。当前内建 importer 没有独立 tool fingerprint
作为替代失效键，因此不得让 importer v1 在同一 product key 下静默产生另一种结果，也不得依赖覆盖同 key
artifact。

| 项目 | v1 行为 |
| --- | --- |
| container | 接受合法 GLB v2 的一个 JSON chunk 与一个 BIN chunk；拒绝 `.gltf`、任何 external/data URI、所有 required extension 与额外 chunk/source dependency |
| scene | default scene 必需；整个 node graph 必须 acyclic 且每个 node 最多一个 parent，default roots 不重复；按 root/child source order 深度优先遍历，只 cook default scene 可达 mesh，允许 transform-only node |
| transform | 接受有限、可逆的 node matrix 或 TRS；按 parent/global 合成后 bake；保留 meter，转换为 Asharia canonical left-handed space |
| primitive | 只接受 `TRIANGLES`；`POSITION` 必需；indexed/non-indexed 均接受；每个可达 mesh-node primitive instance 依 source order 产生一个有序 submesh；所有退化三角形均拒绝 |
| attributes | 可达 primitive 的 `POSITION`: float `VEC3`；`NORMAL`: 可选、非零 finite float `VEC3`；`TEXCOORD_0`: 可选 float `VEC2`；count 必须匹配；其他 vertex attribute fail closed |
| indices | 可选 scalar unsigned byte/short/int source accessor；product 一律转为 `uint32` |
| accessors | 接受单一 BIN-backed buffer 中的 dense accessor 与合法 offset/stride/alignment/range；拒绝 sparse、invalid range/type/count/non-finite |
| normal/UV | normal 缺失时生成 deterministic flat normal；UV0 缺失时零填 |
| materials | material slot 0 固定 unbound；glTF material index `i` 稳定映射到 slot `i + 1`；当前 slot GUID 全为 zero，因为 material product/reference resolver 尚未实现 |
| extensions/unsupported | animation、camera/light semantics、skin/joints/weights、morph target、sparse 与 non-triangle topology fail closed；不解码 Draco/meshopt/quantized extension geometry，required extension 一律拒绝，只有满足本表的 core fallback 仍可被 cook |

Importer 默认 limits：256 MiB source、16 MiB JSON、256 JSON nesting depth、65,536 nodes、256 node depth、65,536 meshes、
65,536 primitives、65,536 material slots、8 Mi output vertices、24 Mi output indices，以及 1 GiB
Asharia-owned post-parse working/output bytes。source/JSON limits 约束第三方 parser 的输入规模；1 GiB budget
不宣称覆盖第三方 parser 自身的内部 allocation。`AssetGlbImportDiagnosticCode` 区分 request/extension/GLB/JSON、
external URI/extension/buffer layout、default scene/count、animation/scene semantic/skin/morph/sparse/topology/attribute、
accessor/index/non-finite/transform/degenerate/empty 等失败，错误必须包含 source/importer 上下文。

## 确定性、publication 与 fixture

确定性来自 closed support matrix、source-order traversal、固定 basis/winding/attribute generation、canonical
Mesh Product writer 与现有 product key。禁止依赖 absolute path、timestamp、hash-map iteration、thread completion
order 或平台 float mode 来决定 bytes。

`assets/fixtures/mesh-product-v1/restricted-static-mesh.glb` 由同目录 `generate_fixture.py` 从固定数值常量生成，
没有复制第三方模型。它覆盖 multiple node/primitive、indexed/non-indexed、authored/missing normal+UV、
node transform、negative determinant 与 material slot。真实 cook 的固定结果为 11 vertices、9 indices、
3 submeshes、3 material slots，bounds `min=(-2, 0, 0)` / `max=(2, 1, 1)`。Khronos Validator 应对该 fixture
返回 zero errors；package tests 另外用内存构造/变异覆盖所有负向路径。

写入流程必须是：在内存中 canonical encode → 用 bounded Mesh Product reader 校验同一 bytes → 写入 staging
path → 通用 publication 层复读并验证 size/hash → 原子替换 artifact → 发布 manifest。import/write/read 任一失败
都不得破坏 last-known-good artifact/manifest；当前没有声称对 staging file 再执行一次类型专属 Mesh reader。

## Runtime CPU 接入与后续顺序

本 Slice 不实现 `.gltf`、OBJ/FBX、textures/material products、LOD、tangent、skin、morph、animation、meshopt、
meshlet、streaming、watcher、Editor settings、GPU upload 或 thumbnail。

#394 已完成第一条 runtime CPU 路径：`asset-artifact` 复验当前 manifest v1 的相对路径、byte budget、size/hash，
`loadMeshResourceCandidate()` 再从 verified bytes 调用 bounded Mesh Product reader。`MeshResourceStore` 用 slot/request
两种 generation 拒绝 stale handle/completion，active/candidate 分离；成功 reload 递增 revision，失败 reload 保留旧
active lease。Store 不拥有 thread pool，worker 只产生 owning completion，publish/unload 只在 owner thread 执行。

后续必须保持以下 owner 顺序：

```text
Mesh Product v1 artifact（Current）
→ ResourceRuntime typed CPU Mesh payload / artifact IO / generation-safe lease（Current）
→ renderer-owned vertex/index GPU resource / safe swap / deferred retirement
→ Scene View consumes runtime handle (remove validation GUID special case)
→ ThumbnailService reuses the same RuntimeResource and renderer preview path
→ manual reload proven first; watcher/debounce/automatic reimport last
```

`asset-artifact` 只拥有外层 bytes/path/size/hash verification，type-specific reader 继续归 `mesh-product`；Runtime 不依赖
`asset-pipeline` 或 fastgltf。Inspector 只能显示 catalog/product facts；在 #389 前不拥有 importer settings Apply/
Revert，缩略图也不能从 source 绕过 product/runtime 链。

## 验证门禁

- `mesh-product` package：writer/reader round-trip、byte-identical determinism、header/offset/padding/count/range/
  finite/bounds/submesh/material negative tests 与 limits；
- `asset-pipeline` package：真实 GLB fixture、source-order flatten、basis/transform/winding、missing normal/UV、
  material slot，以及 malformed/truncated/oversized/unsupported failure tests；
- `asset-processor --smoke-product-execution`：真实 `.glb` → artifact + manifest → disk reader；
- `asset-processor --smoke-mesh-resource`：同一真实 artifact/manifest → verified bytes → `MeshResourceStore` → typed lease；
- Khronos glTF Validator、package topology/target dependency、MSVC CTest、changed clang-tidy、encoding、doc sync 与
  `git diff --check`。
