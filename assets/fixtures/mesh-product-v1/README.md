# Mesh Product v1 restricted GLB fixture

`restricted-static-mesh.glb` is a repository-owned, programmatically generated glTF 2.0 binary
fixture for the restricted static-mesh importer. It contains no copied third-party model data or
external binary dependency. The checked-in bytes are governed by the same terms as this repository.

Regenerate the GLB and its `.ameta` sidecar from repository root:

```powershell
python assets/fixtures/mesh-product-v1/generate_fixture.py
```

The generator emits the GLB JSON and BIN chunks directly from fixed numeric constants. The fixture
contains one default scene with:

- one translated mesh node containing two indexed `TRIANGLES` primitives, authored normals/UV0,
  and two distinct glTF material indices;
- one translated, negative-determinant mesh node containing one non-indexed `TRIANGLES` primitive
  with missing normals and UV0 and no material;
- finite float32 positions and `uint16` source indices; and
- expected Asharia-canonical cooked local bounds `min=(-2, 0, 0)`, `max=(2, 1, 1)` after
  glTF node transforms and the right-handed → left-handed X reflection are baked.

The fixed cooked result is 11 vertices, 9 `uint32` indices, 3 submeshes, and 3 material
slots: slot 0 is the default unbound slot, glTF materials 0 and 1 map to slots 1 and 2, and
all three GUIDs are intentionally zero until material reference resolution exists.

This single fixture proves the positive integration path for default-scene traversal, multiple
nodes/primitives, indexed/non-indexed input, generated flat normals, zero-filled UV0, material-slot
ordering, mirrored-winding repair, and deterministic output. Importer unit tests construct or mutate
minimal GLB bytes for malformed, truncated, oversized, external-URI, sparse, required-extension,
animation, camera/light, skin, morph, topology, non-finite, degenerate, and overflow failures; those
cases are intentionally not copied into many opaque binary fixtures.

The source model is validation input only. It is not a fallback mesh, sample project asset, renderer
resource, or promise of broader glTF feature support.

Khronos glTF Validator 2.0.0-dev.3.10 reports zero errors, warnings, and hints for the generated
fixture. It reports two informational `UNUSED_OBJECT` messages for authored `TEXCOORD_0` accessors
because the intentionally minimal fixture materials do not reference textures; UVs remain present to
exercise the importer contract.
