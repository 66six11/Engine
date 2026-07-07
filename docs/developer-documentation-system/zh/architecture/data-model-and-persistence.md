# 架构：Data Model And Persistence

## 目的

本文说明当前 data-model、archive、schema、reflection、serialization、persistence、asset metadata、project metadata、material 和 shader-authoring 边界。它回答谁拥有可存储状态、哪个 package 可以转换这些状态、验证发生在哪里。

本文不描述 UI workflow 或 renderer command recording。未来工作只用 `future` 标注。

## 分层

| 层 | Packages | 当前职责 |
|---|---|---|
| Archive values | `packages/archive` | asset/project/material IO 使用的 JSON-like `ArchiveValue` data 和 JSON read/write helpers。 |
| Schema contracts | `packages/schema` | `TypeSchema`、fields、registry、built-in schema registration、schema document validation。 |
| C++ binding contracts | `packages/cpp-binding` | 把编译期 C++ type 绑定到 schema fields 的 C++ reflected-type metadata。 |
| Persistence | `packages/persistence` | schema-aware archive loading、saving、migration、field validation。 |
| Runtime reflection | `packages/reflection` | 与 archive persistence 分离的 runtime `TypeRegistry` 和 type metadata。 |
| Serialization | `packages/serialization` | reflection-based text/archive serialization 和 migration helpers。 |
| Asset/project metadata | `packages/asset-core`、`packages/project-core` | 稳定 asset/project identifiers、metadata documents、catalogs、IO targets。 |
| Material/shader authoring | `packages/material-core`、`packages/material-instance`、`packages/shader-authoring`、`packages/shader-slang`、`packages/shader-material-adapter` | material signatures、material instances、`.ashader` parsing、generated Slang、Slang reflection、reflection-to-material translation。 |
| Runtime resource resolution | `packages/resource-runtime` | 从 asset product records 派生 runtime tickets 和 status。 |

## 状态 Owner

| 状态 | Owner | Format 或 API | 说明 |
|---|---|---|---|
| Archive tree | `packages/archive` | `asharia::archive::ArchiveValue` | document IO 需要 JSON-like tree 且不使用 reflection 时使用。 |
| Schema registry | `packages/schema` | `asharia::schema::SchemaRegistry` | 拥有 type/field declarations 和 built-in schemas。 |
| C++ binding table | `packages/cpp-binding` | `asharia::cpp_binding::ReflectedType<T>` specializations 和 binding helpers | 把 concrete C++ types 绑定到 schema fields。 |
| Persistent object migration | `packages/persistence` | migration API 和 smoke-tested migration path | 读取旧 archive version，并产出当前 object shape。 |
| Runtime type registry | `packages/reflection` | `asharia::reflection::TypeRegistry` | runtime serialization 使用，不由 Vulkan 或 RenderGraph 使用。 |
| Serialization archive | `packages/serialization` | package-local archive/text archive APIs | 与 `packages/archive` 分离；绑定 reflection serialization。 |
| Asset GUID 和 source metadata | `packages/asset-core` | `AssetGuid`、`AssetReference`、`AssetMetadata`、`AssetCatalog` | 不拥有 importer-specific texture profile 解释。 |
| Project descriptor | `packages/project-core` | project document 和 IO target | tool 和 editor input 通过该 package 读取 project。 |
| Product manifest 和 product blob | `packages/asset-pipeline` | product manifest IO 和 product execution APIs | 拥有具体 import/product execution diagnostics。 |
| Runtime resource status | `packages/resource-runtime` | `RuntimeResourceRegistry` | 拥有 runtime handle 的 pending/ready/failed status。 |
| Material resource signature | `packages/material-core` | `MaterialResourceSignature` 和 `MaterialPipelineKey` | renderer 消费；shader tool 适配到这里。 |
| Authored shader document | `packages/shader-authoring` | `.ashader` parser 和 generated Slang model | 不依赖 Vulkan 或 renderer packages。 |
| Slang reflection signature | `packages/shader-slang` | reflection model 和 `asharia-slang-reflect` | toolchain-facing data，由 adapter/tests 消费。 |

## 依赖方向

- `archive` 是低层 document value package。`asset-core` IO、`project-core` IO、`material-instance`、`asset-pipeline`、`persistence` 可以依赖它。
- `schema` 拥有 schema contracts。`cpp-binding` 和 `persistence` 依赖 schema；schema 不依赖它们。
- `persistence` 依赖 `archive`、`schema`、`cpp-binding`。它不依赖 `reflection` 或 renderer packages。
- `serialization` 依赖 `reflection`；当前 manifest 中它不依赖 `schema` 或 `persistence`。
- `asset-core` 拥有 identity 和 catalog state。`asset-pipeline` 拥有具体 import decisions 和 product payload construction。
- `material-core` 是公共 material contract。它不依赖 shader-authoring、shader-slang、RenderGraph、Vulkan 或 editor hosts。
- `shader-material-adapter` 是从 Slang reflection 转换到 material-core signatures 的边界。
- `resource-runtime` 依赖 asset-core product records；它不运行 asset pipeline。

## 读写流程

当前 asset/project/material authoring flow：

1. Source files 和 metadata 由 `asset-pipeline` discovery，或由 editor/tool code 读取。
2. `asset-core` 拥有 stable asset identity、source records、product keys、catalog entries、metadata IO。
3. `project-core` 拥有 tool 和 host 使用的 project descriptors。
4. `.ashader` documents 由 `shader-authoring` 解析，generated Slang 和 metadata 交给 shader tooling 消费。
5. `.amat` material instances 由 `material-instance` 读取，使用 asset references 和 shader-authoring contracts。
6. `asset-pipeline` 规划 imports 并执行 products。Diagnostics 留在 plan/execution result。
7. `resource-runtime` 根据 product records 解析 runtime handles，并为 host code 产出 pending/ready/failed status。

当前 schema/persistence flow：

1. `schema` 注册或加载 type declarations。
2. `cpp-binding` 把 C++ field access 映射到 schema fields。
3. `persistence` 读取 archive object，验证 type/version/fields，需要时应用已注册 migration，然后写入 bound C++ storage。
4. 失败时返回带上下文的项目 error types；测试覆盖 unsupported fields、unknown versions、missing required fields、migration。

## 生命周期

- Document model package 用普通 C++ object 拥有 parsed values。可失败 IO 返回 `Result<T>` 或 package-specific diagnostics。
- Schema 和 reflection registry 是显式 object，不是隐式 global state。
- Asset catalog data 从 source records 和 product records 构建，再通过 catalog views 暴露。
- Runtime resource registry tickets 通过解析 product records 推进；stale generation 或 mismatched product keys 会变成 diagnostics，而不是静默产出 ready handle。
- Material/shader contracts 在 renderer binding 前创建。Renderer code 消费 signatures 和 pipeline keys；它不解析 `.amat` 或 `.ashader` documents。

## 错误处理

| 失败 | Owner | 预期行为 |
|---|---|---|
| Malformed JSON/archive input | 读取文件的 IO package | 返回带上下文的 error 或 diagnostic；不创建 partial ready state。 |
| Unsupported schema version | `packages/persistence` | 有 migration 时尝试迁移；否则带 version context 失败。 |
| Unknown 或 unsupported field kind | `packages/persistence` 或 `packages/schema` | 在 validation 或 load 阶段拒绝。 |
| Missing asset product record | `packages/asset-core` view 或 `packages/resource-runtime` | 标记 missing/stale/failed status，并保留 diagnostics。 |
| Product key mismatch | `packages/resource-runtime` | 拒绝 stale product records，并保持 handle unresolved。 |
| Shader reflection mismatch | `packages/shader-material-adapter` | 在产出不兼容 material signature 前失败。 |

## 未来工作

`future`: asset、project、material、shader、schema documents 可以在 schemas 成为 document validation 单一事实源后共享 generated document-schema index。在那之前，package-local tests 和 smoke commands 仍是验证事实源。

`future`: reflection serialization 和 schema persistence 只有在保留当前边界时才可以合并：`schema/persistence` 验证 stored document shape；`reflection/serialization` 描述 runtime C++ metadata。

## 验证方式

运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked -RunCheapGates
```

这些区域改变时运行 package-local gates：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\persistence -B build\cmake\package-persistence-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-persistence-tests-msvc-debug && ctest --test-dir build\cmake\package-persistence-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\asset-pipeline -B build\cmake\package-asset-pipeline-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-asset-pipeline-tests-msvc-debug && ctest --test-dir build\cmake\package-asset-pipeline-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\resource-runtime -B build\cmake\package-resource-runtime-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-resource-runtime-tests-msvc-debug && ctest --test-dir build\cmake\package-resource-runtime-tests-msvc-debug --output-on-failure"
```

检查点：

- `material-core` 不链接 Slang、Vulkan、RenderGraph、asset-pipeline 或 editor targets。
- `asset-core` 不包含 texture importer 或 decoder-specific policy。
- `resource-runtime` status transitions 覆盖 pending、ready、failed、stale generation、product key mismatch。
- `persistence` tests 覆盖 migration 和 negative validation paths。
