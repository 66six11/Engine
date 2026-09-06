# Editor Content

`editor-content` owns Asharia's UI-neutral, read-only project asset catalog query. It lets editor
frontends inspect source roots, folders, source assets, sub-assets, current product state, and
actionable catalog diagnostics without loading runtime resources.

## Current boundary

- `asharia::editor_content` reads the canonical project descriptor, source metadata, source
  snapshots, and product manifest, then produces an immutable catalog snapshot.
- `asharia_editor_content_native` projects that snapshot through the versioned
  `asharia_editor_content_query` C ABI and the strict
  `com.asharia.editor.assetCatalogSnapshot` v1 JSON schema.
- The query is deterministic and bounded. Studio defaults to 10,000 source files, 8 GiB of source
  bytes, 10,000 diagnostics, and a 16 MiB response.
- Catalog planning uses declared tool facts only. Browsing never probes `PATH`, `VULKAN_SDK`, or
  importer executables.

This package does not execute importers, mutate source files or metadata, publish products, load
`RuntimeResource` payloads, create GPU resources, or render thumbnails. Those capabilities belong
to the asset processor, resource runtime, renderer, and editor preview service respectively.

The package is intentionally a rebuildable snapshot query for the first vertical slice. A future
incremental asset index may replace its implementation without changing the catalog ownership or
turning an editor panel into a second asset database.

## Validation

With the repository MSVC test preset configured, run:

```powershell
ctest --preset msvc-debug-tests --output-on-failure -R "asharia-editor-content-native"
```

The package smoke tests cover the C header and ABI layout, strict UTF-8 and response bounds,
cross-root aggregate limits, source-root limits, and exception containment. Managed Studio tests
separately verify the closed JSON schema, query scope, lifecycle, and Resource Browser projection.

## Native product selection

`selectEditorAssetProduct(snapshot, guid, expectedType)` returns an owning product record from the
same snapshot's source-derived import-plan keys and manifest. It compares the complete key
(source/settings/dependencies/importer version/target), requires one source row and one current
record, and rejects incomplete scans, missing planning facts, wrong types, missing/stale products
and ambiguous records. Identical repeated expected keys are harmless; duplicate product records
are rejected. The query never selects by timestamps, file names or manifest order.

A snapshot is point-in-time data: after source/settings/tool facts or manifest changes, refresh
before requesting another load. Declared-only planning remains in force. Native callers can supply
`toolVersions` in `EditorAssetCatalogSnapshotRequest`; snapshots retain them for refresh. At most
256 unique importer/name pairs are accepted, with nonzero importer/hash and 1-128 byte names.
The host must obtain these fingerprints outside browsing; neither metadata nor cached candidate
records supply them. An omitted/partial Shader declaration remains unresolved without probing tools.
Compiled-product authoring dependency planning and a real cook/catalog/runtime proof are still
pending. Studio C ABI/JSON inputs remain unchanged and currently provide no tool declarations.
Any error diagnostic blocks selection for the whole snapshot (the browser can still display its
partial rows). Warnings alone do not block an otherwise uniquely matched product.

The returned record can populate a runtime read request, or its key can drive `MeshResourceStore`.
The caller still supplies the artifact root and runtime-specific entry/ABI expectations. Runtime
readers remain responsible for contained paths, size/hash checks and payload validation; selecting
a record does not prove that its file exists or is valid. This slice does not connect Studio's
Scene mesh/material consumer. Native C ABI/JSON v1 exposes no new fields.

The adopted precedent is metadata lookup before resource loading: Epic's
[Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine)
returns asset data, and O3DE's
[AssetCatalogRequests](https://docs.o3de.org/docs/api/frameworks/azcore/class_a_z_1_1_data_1_1_asset_catalog_requests.html)
provides ID-based asset info queries. Asharia keeps this lookup in the existing editor snapshot,
without a global request bus, automatic registration or eager object loading, because the host
already owns a bounded source scan and full import-plan keys. The native runtime stays independent
of editor catalog ownership.

`asharia-editor --smoke-editor-asset-browser` exercises real project/metadata/manifest snapshot
selection plus missing, stale, wrong-type, wrong-target, duplicate and incomplete rejection cases.
