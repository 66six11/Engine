# Detailed Design: Reflection, Serialization, Schema, And Persistence

## Background

The current data-description stack consists of `schema`, `cpp-binding`, `reflection`, `serialization`, `archive`, and `persistence`. It supports inspectable editor/runtime data, text archives, migration, and future persistence flows without binding directly to gameplay or renderer types.

## Goals

- Use `schema` to describe type schemas independently from C++ objects.
- Use `cpp-binding` to bind C++ fields/properties to schema-like metadata.
- Use `reflection::TypeRegistry` to store runtime type info.
- Use `serialization::serializeObject/deserializeObject()` to convert between reflected objects and archive values.
- Use migration registry for older archives and newer type versions.

## Non-Goals

- Do not automatically scan C++ source to generate reflection.
- Do not read project files from serialization.
- Do not make archive understand reflected types.
- Do not put editor selection or UI state into schema registry.

## Current Constraints

- `archive` depends on `core` and provides `ArchiveValue` plus JSON IO.
- `schema` depends on `core`.
- `cpp-binding` depends on `core` and `schema`.
- `persistence` depends on `core`, `schema`, `archive`, and `cpp-binding`.
- `serialization` depends on `core` and `reflection`.
- `reflection::TypeRegistry::freeze()` prevents further type registration.

## Overall Design

The schema stack has a static-description path and a runtime-reflection path. `schema` and `cpp-binding` describe persistable type shapes; `reflection` uses `TypeInfo`, field accessors, attributes, and `TypeRegistry` for runtime access. `serialization` only queries registry metadata and does not depend on concrete object types.

The archive layer stores generic values: null, bool, integer, floating, string, array, and object. JSON IO lives in `archive`. Migration is applied before or during deserialize, and policy controls unknown/missing field behavior.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `packages/archive/include/asharia/archive/archive_value.hpp` | generic archive value tree |
| `packages/archive/include/asharia/archive/json_archive.hpp` | JSON read/write |
| `packages/schema/include/asharia/schema/type_schema.hpp` | schema type/field document model |
| `packages/cpp-binding/include/asharia/cpp_binding/type_binding.hpp` | C++ binding model |
| `packages/reflection/include/asharia/reflection/type_info.hpp` | runtime type info and field accessor |
| `packages/reflection/include/asharia/reflection/type_builder.hpp` | typed registration builder |
| `packages/reflection/include/asharia/reflection/type_registry.hpp` | type registry and freeze |
| `packages/serialization/include/asharia/serialization/serializer.hpp` | object/archive conversion |
| `packages/serialization/include/asharia/serialization/migration.hpp` | migration registry |
| `packages/persistence/include/asharia/persistence/persistence.hpp` | persistence-facing API |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `ArchiveValue` | `kind`, scalar values, array/object members | storage-neutral value tree |
| `TypeInfo` | type id/name/version/kind/fields | runtime reflection type |
| `FieldAccessor` | read/write/construct/validate callbacks | field-level object access |
| `SerializationPolicy` | `unknownFields`, `missingFields`, `migrations`, `archivePath` | serialization behavior |
| `MigrationRegistry` | registered migrations | old archive shape to current type |

## API Design

- `TypeBuilder<T>` registers fields, properties, default values, validators, and attributes.
- `TypeRegistry::registerType()` registers complete types; `freeze()` locks the registry.
- `serializeObject(registry, type, object, policy)` returns `serialization::ArchiveValue`.
- `deserializeObject(registry, type, value, object, policy)` writes into an existing object.
- `UnknownFieldPolicy` declares `Error`, `Ignore`, and `Preserve`; current implementation rejects `Preserve` with an unsupported-preserve error.
- `MissingFieldPolicy` supports `Error`, `KeepConstructedValue`, and `UseDefault`.

## Key Flows

### Normal Flow

1. Register types with `TypeBuilder<T>`.
2. Register builtin types.
3. Call `TypeRegistry::freeze()`.
4. Serialize object to archive value.
5. Write archive through JSON IO.
6. Deserialize by type header, field metadata, and policy.

### Failure Flow

- Duplicate type: registry returns an error.
- Register after freeze: registry returns an error.
- Unknown field with `Error` policy: deserialize returns an error.
- Missing field with `Error` policy: deserialize returns an error.
- Unknown field with `Preserve` policy: deserialize returns an unsupported-preserve error.
- Field validator failure: deserialize returns an error.

### Boundary Flow

- Accessors are valid only during reflection/serialization operations; do not cache field addresses.
- Serialization requires a frozen `reflection::TypeRegistry`.
- Only fields tagged `storage_attributes::persistent()` and not tagged `storage_attributes::transient()` are serialized.
- Archive must not depend on reflection.
- Reflection registry owns type metadata, not object instances.

## Lifetime

The registry is built during initialization and then frozen. Archive values are ordinary value trees and can be copied or written. Migration registry should be built before deserialize. Reflected accessor object addresses are valid only for the current call.

## Error Handling

Errors use `ErrorDomain::Reflection` or `ErrorDomain::Serialization`. Messages should include type name/id, field name, archive path, and policy.

## Test Plan

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-registry
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-transform
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-reflection-attributes
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-roundtrip
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-json-archive
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-serialization-migration
```

## Risks

- A bad reflection accessor can corrupt object memory. Mitigation: typed builder constrains getter/setter/validator signatures.
- Misusing unknown field policy can hide forward compatibility bugs. Mitigation: default to `Error`.
- Entering runtime before registry freeze makes the type set unstable. Mitigation: freeze during initialization and test late registration failure.
