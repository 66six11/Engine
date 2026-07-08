# 详细设计：Reflection、Serialization、Schema 与 Persistence

## 背景

当前数据描述栈由 `schema`、`cpp-binding`、`reflection`、`serialization`、`archive` 和 `persistence` 组成。它服务于 editor/runtime 可检查数据、文本 archive、migration 和未来持久化流程，但不直接绑定具体 gameplay 或 renderer 类型。

## 目标

- 用 `schema` 描述独立于 C++ 对象的 type schema。
- 用 `cpp-binding` 把 C++ fields/properties 绑定到 schema-like metadata。
- 用 `reflection::TypeRegistry` 保存 runtime type info。
- 用 `serialization::serializeObject/deserializeObject()` 在 reflected object 和 archive value 之间转换。
- 用 migration registry 处理旧 archive 到新 type version。

## 非目标

- 不自动扫描 C++ 源码生成 reflection。
- 不在 serialization 中读取 project files。
- 不在 archive 层理解 reflected type。
- 不把 editor selection 或 UI state 写入 schema registry。

## 当前约束

- `archive` 依赖 `core`，提供 `ArchiveValue` 和 JSON IO。
- `schema` 依赖 `core`。
- `cpp-binding` 依赖 `core` 和 `schema`。
- `persistence` 依赖 `core`、`schema`、`archive`、`cpp-binding`。
- `serialization` 依赖 `core` 和 `reflection`。
- `reflection::TypeRegistry::freeze()` 后不能再注册 type。

## 总体方案

Schema 栈分为静态描述和 runtime reflection 两条线。`schema` 和 `cpp-binding` 描述可持久化类型形状；`reflection` 用 `TypeInfo`、field accessor、attributes 和 `TypeRegistry` 管理运行时访问。`serialization` 只通过 registry 查找 type 和 field，不依赖具体对象类型。

Archive 层只保存通用值：null、bool、integer、floating、string、array、object。JSON IO 在 `archive` 中完成。Migration 在 deserialize 前或过程中应用，policy 决定 unknown/missing field 行为。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `packages/archive/include/asharia/archive/archive_value.hpp` | 通用 archive value tree |
| `packages/archive/include/asharia/archive/json_archive.hpp` | JSON read/write |
| `packages/schema/include/asharia/schema/type_schema.hpp` | schema type/field document model |
| `packages/cpp-binding/include/asharia/cpp_binding/type_binding.hpp` | C++ binding model |
| `packages/reflection/include/asharia/reflection/type_info.hpp` | runtime type info 和 field accessor |
| `packages/reflection/include/asharia/reflection/type_builder.hpp` | typed registration builder |
| `packages/reflection/include/asharia/reflection/type_registry.hpp` | type registry 和 freeze |
| `packages/serialization/include/asharia/serialization/serializer.hpp` | object/archive conversion |
| `packages/serialization/include/asharia/serialization/migration.hpp` | migration registry |
| `packages/persistence/include/asharia/persistence/persistence.hpp` | persistence-facing API |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `ArchiveValue` | `kind`、scalar values、array/object members | storage-neutral value tree |
| `TypeInfo` | type id/name/version/kind/fields | runtime reflection type |
| `FieldAccessor` | read/write/construct/validate callbacks | field-level object access |
| `SerializationPolicy` | `unknownFields`、`missingFields`、`migrations`、`archivePath` | serialization behavior |
| `MigrationRegistry` | registered migrations | old archive shape to current type |

## API 设计

- `TypeBuilder<T>` 注册 fields、properties、default values、validators 和 attributes。
- `TypeRegistry::registerType()` 注册完整 type；`freeze()` 锁定 registry。
- `serializeObject(registry, type, object, policy)` 输出 `serialization::ArchiveValue`。
- `deserializeObject(registry, type, value, object, policy)` 写入已有对象。
- `UnknownFieldPolicy` 声明 `Error`、`Ignore`、`Preserve`；当前实现会对 `Preserve` 返回 unsupported-preserve error。
- `MissingFieldPolicy` 支持 `Error`、`KeepConstructedValue`、`UseDefault`。

## 关键流程

### 正常流程

1. 用 `TypeBuilder<T>` 注册 type。
2. 注册 builtin types。
3. `TypeRegistry::freeze()`。
4. serialize object 到 archive value。
5. JSON IO 写出 archive。
6. deserialize 时按 type header、field metadata 和 policy 写入对象。

### 失败流程

- duplicate type：registry 返回 error。
- freeze 后注册：registry 返回 error。
- unknown field 且 policy 为 `Error`：deserialize 返回 error。
- missing field 且 policy 为 `Error`：deserialize 返回 error。
- unknown field 且 policy 为 `Preserve`：deserialize 返回 unsupported-preserve error。
- field validator 失败：deserialize 返回 error。

### 边界流程

- accessor 只在 serialization/reflection 操作期间使用，不能长期缓存 field address。
- Serialization 要求 `reflection::TypeRegistry` 已 freeze。
- 只有标记 `storage_attributes::persistent()` 且未标记 `storage_attributes::transient()` 的字段会被序列化。
- archive 层不能依赖 reflection。
- reflection registry 不拥有对象实例，只拥有 type metadata。

## 生命周期

Registry 在初始化阶段构建，然后 freeze。Archive value 是普通 value tree，可读写和复制。Migration registry 应在 deserialize 前构建完成。反射 accessor 对象地址只在当前调用中有效。

## 错误处理

错误使用 `ErrorDomain::Reflection` 或 `ErrorDomain::Serialization`，message 应包含 type name/id、field name、archive path 和 policy。

## 测试方案

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-registry
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-transform
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-attributes
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-roundtrip
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-json-archive
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-migration
```

## 风险

- Reflection accessor 错误会破坏对象内存。缓解：typed builder 约束 getter/setter/validator signature。
- Unknown field policy 若误用，会吞掉 forward compatibility bug。缓解：默认 `Error`。
- Registry 未 freeze 就进入 runtime 会导致 type set 不稳定。缓解：初始化阶段显式 freeze 并测试 late registration failure。
