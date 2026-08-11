# Scene rendering validation model

`directional-wedge.obj` is a source-controlled, deliberately asymmetric model used only to prove
that Scene rendering consumes real indexed mesh data. It is not a fallback mesh and its generator
is not a production OBJ importer.

Generate the compile-time product into the build tree:

```powershell
python tools/generate_validation_mesh_product.py `
  --source assets/fixtures/scene-rendering/directional-wedge.obj `
  --metadata assets/fixtures/scene-rendering/directional-wedge.obj.ameta `
  --output-header build/generated/asharia/validation/directional_wedge_mesh_product.hpp `
  --output-manifest build/generated/asharia/validation/directional_wedge_mesh_product.json
```

The generated header uses namespace `asharia::validation` and exposes
`directionalWedgeValidationMeshProduct()`. The returned view contains immutable vertex/index spans,
stable asset identity, product hash, and bounds. It intentionally contains no source path.

The generated header and manifest remain under `build/generated/`, which is ignored. The committed
`directional-wedge.expected.json` is the CPU-test oracle for the normalized product contract.
