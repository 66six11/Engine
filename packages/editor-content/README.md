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
