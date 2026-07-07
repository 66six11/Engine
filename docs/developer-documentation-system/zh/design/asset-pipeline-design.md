# 详细设计：资产目录、导入计划与产品执行

## 背景

当前资产系统由 `asset-core`、`project-core`、`asset-pipeline`、`resource-runtime` 和 `tools/asset-processor` 共同组成。代码目标是把 source assets、`.ameta` metadata、project descriptor、product manifest 和 runtime resource state 分开，避免 editor 或 renderer 直接拥有导入产物生命周期。

## 目标

- 用 `AssetCatalog` 维护 source asset records。
- 用 `scanAssetSourceTree()` 从 source root 找到 source/metadata pairs。
- 用 `planAssetImports()` 判断 missing product、source/settings/importer/dependency/profile drift。
- 用 `executeAssetProducts()` 写入 product files 和 product manifest。
- 用 `RuntimeResourceRegistry` 追踪 runtime resource 的 pending/ready/failed 状态。

## 非目标

- 不在 `asset-core` 里读取项目目录。
- 不在 `asset-pipeline` 里持有 GPU resource。
- 不让 editor panel 直接修改 product manifest。
- 不让 runtime registry 重新执行 importer。

## 当前约束

- `asset-core` 依赖 `core` 和 `archive`；`asset-pipeline` 依赖 `archive`、`asset-core`、`material-instance`、`shader-authoring`。
- `project-core` 只描述 project descriptor 和 IO。
- `resource-runtime` 只依赖 `asset-core`。
- `asharia-asset-processor` 是命令行入口，支持 `dry-run`、`execute` 和 smoke。

## 总体方案

资产流程分为四段：

1. `project-core` 读取 `asharia.project.json`，得到 source roots 和 discovery 设置。
2. `asset-pipeline` 扫描 source tree，读取 `.ameta`，生成 discovered sources 和 snapshots。
3. `planAssetImports()` 与现有 product manifest 对比，输出 import requests 和 cache hits。
4. `executeAssetProducts()` 读取 source bytes/dependency product bytes，写 product files，并产出新的 manifest。

Runtime 阶段只消费 `AssetProductRecord`。`RuntimeResourceRegistry::request()` 创建 ticket，`markReady()` 或 `markFailed()` 关闭该 ticket，generation 用来防止 stale completion 覆盖新请求。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `packages/asset-core/include/asharia/asset_core/asset_metadata.hpp` | `SourceAssetRecord`、importer id、settings hash 和 source path validation |
| `packages/asset-core/include/asharia/asset_core/asset_catalog.hpp` | source record 增删改查 |
| `packages/asset-core/include/asharia/asset_core/asset_product.hpp` | product key、dependency、product record |
| `packages/project-core/include/asharia/project/project_descriptor.hpp` | project id、source root、discovery descriptor |
| `packages/asset-pipeline/include/asharia/asset_pipeline/asset_source_scan.hpp` | source tree scan |
| `packages/asset-pipeline/include/asharia/asset_pipeline/asset_import_planning.hpp` | import plan 和 cache hit |
| `packages/asset-pipeline/include/asharia/asset_pipeline/asset_product_execution.hpp` | product write 和 manifest update |
| `packages/resource-runtime/include/asharia/resource_runtime/runtime_resource_registry.hpp` | runtime request/ready/failed state |
| `tools/asset-processor/src/main.cpp` | CLI argument parsing 和 command dispatch |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `SourceAssetRecord` | `guid`、`assetType`、`sourcePath`、`importerId`、`sourceHash`、`settingsHash` | source asset 的稳定 metadata |
| `AssetImportRequest` | `source`、`settings`、`dependencies`、`productKey`、`reason` | 需要重新导入的工作项 |
| `AssetImportCacheHit` | `source`、`dependencies`、`product` | manifest 仍可复用的产物 |
| `AssetProductExecutionRequest` | `plan`、`sourceBytes`、`dependencyProductBytes`、`productOutputRoot` | product execution 输入 |
| `RuntimeResourceRecord` | `key`、`state`、`generation`、`expectedProductKey`、`product`、`failure` | runtime resource 状态 |

## API 设计

公开 API 使用 value structs 和 `Result`/diagnostics，而不是异常。

- `AssetCatalog::addSource/updateSource/removeSource()` 改变 source catalog。
- `scanAssetSourceTree(request)` 返回 entries 和 diagnostics。
- `planAssetImports(sources, snapshots, manifest, targetProfile, options)` 返回 plan；`succeeded()` 只在无 error diagnostics 时为 true。
- `executeAssetProducts(request)` 返回 written products、cache hits、新 manifest 和 diagnostics。
- `RuntimeResourceRegistry::request/markReady/markFailed/resolveProductRecords()` 维护 generation-safe 状态机。

## 关键流程

### 正常流程

1. 读取 project descriptor。
2. 扫描 source root 和 `.ameta`。
3. 从 metadata 建立 `SourceAssetRecord`。
4. 读取现有 product manifest。
5. 生成 import plan。
6. 对 requests 执行 importer。
7. 写 product files 和 manifest。
8. runtime 请求 product 并标记 ready。

### 失败流程

- source path 非法：`validateAssetSourcePath()` 或 scan diagnostics 报错。
- metadata 缺失或 orphan：`scanAssetSourceTree()` 返回 diagnostics。
- target profile 非法：`planAssetImports()` 返回 `InvalidTargetProfile`。
- source bytes 缺失或 hash drift：`executeAssetProducts()` 返回 diagnostics。
- stale runtime ticket：`RuntimeResourceRegistry` 返回 generation mismatch error。

### 边界流程

- `AssetCatalogRelocationPolicy::RejectPathChange` 防止同一 GUID 被静默移动。
- `planAssetImports()` 允许 cache hit，不强制重导入。
- runtime registry 不从磁盘读 product；调用方负责提供 product records。

## 生命周期

`AssetCatalog` 是内存 catalog；project descriptor、metadata 和 product manifest 由 IO API 读写。Import plan 是一次性 value result。Runtime resource ticket 只对当前 generation 有效，ready/failed 后不能重复完成。

## 错误处理

资产规划和执行使用 diagnostics vectors；catalog、descriptor、metadata、runtime registry 使用 `Result`/`VoidResult`。错误消息应包含 source path、relative product path、target profile 或 resource key。

## 测试方案

```powershell
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-dry-run
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-product-execution
```

Package-local tests：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/asset-core -B build/cmake/package-asset-core-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-asset-core-tests-msvc-debug && ctest --test-dir build/cmake/package-asset-core-tests-msvc-debug --output-on-failure"
```

## 风险

- Product manifest 和 source metadata drift 容易导致 stale cache。缓解：plan diagnostics 必须保留 drift reason。
- Runtime generation 若被忽略，会让旧请求覆盖新请求。缓解：所有 completion API 都要求 ticket。
- Editor reimport 若直接写 manifest，会绕过 plan/execute diagnostics。缓解：editor 只能发 request，由 pipeline 执行。
