# Mesh Product

`mesh-product` owns the CPU-only Mesh Product v1 contract. The runtime-safe
`asharia::mesh_product` target exposes immutable mesh facts and bounded readers. The
tool-side `asharia::mesh_product_writer` target validates build input and emits the one
canonical little-endian encoding.

## V1 facts

- vertex layout: `P3N3Uv2F32` (`position.xyz`, `normal.xyz`, `uv0.xy`), 32 bytes;
- `uint32` triangle-list indices;
- contiguous submeshes covering the complete index buffer;
- position-derived local AABB;
- ordered material slots containing an `AssetGuid`; zero means unbound;
- fixed 128-byte header and 16-byte-aligned vertex, index, submesh, and material sections;
- reserved bytes and alignment padding are zero and all offsets are canonical.

The reader rejects unknown versions, non-little-endian products, malformed layouts,
non-finite values, inconsistent bounds, out-of-range indices, invalid submeshes, and
configured count or byte-budget violations before allocating count-sized payloads.

The product does not contain source paths, importer settings, renderer handles, Vulkan
objects, editor state, or runtime-resource lifecycle state. Artifact content identity and
publication integrity are owned by the outer asset artifact pipeline, so v1 does not add a
second checksum contract inside these bytes.

## Standalone validation

After Conan bootstrap:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\mesh-product -B build\cmake\package-mesh-product-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-mesh-product-tests-msvc-debug && ctest --test-dir build\cmake\package-mesh-product-tests-msvc-debug --output-on-failure"
```
