"""Generate the repository-owned restricted GLB Mesh Product v1 fixture.

The geometry and container bytes are assembled entirely from constants in this file. No
third-party model or binary payload is copied into the repository.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path


_GLB_MAGIC = 0x46546C67
_GLB_VERSION = 2
_JSON_CHUNK_TYPE = 0x4E4F534A
_BIN_CHUNK_TYPE = 0x004E4942
_FNV1A64_OFFSET = 14695981039346656037
_FNV1A64_PRIME = 1099511628211
_SOURCE_PATH = "assets/fixtures/mesh-product-v1/restricted-static-mesh.glb"
_ASSET_GUID = "6f5299ad-9c29-47b2-9366-159c00ebfe9c"
_IMPORTER_ID = "com.asharia.importer.mesh.glb-static"


def _align_four(payload: bytearray, padding: int = 0) -> None:
    while len(payload) % 4:
        payload.append(padding)


def _append_structs(
    payload: bytearray,
    format_text: str,
    values: tuple[tuple[object, ...], ...],
) -> tuple[int, int]:
    _align_four(payload)
    offset = len(payload)
    for value in values:
        payload.extend(struct.pack(format_text, *value))
    length = len(payload) - offset
    _align_four(payload)
    return offset, length


def _hash_bytes(data: bytes) -> int:
    value = _FNV1A64_OFFSET
    for byte in data:
        value ^= byte
        value = (value * _FNV1A64_PRIME) & ((1 << 64) - 1)
    return value


def _hash_uint64(value: int, initial: int = _FNV1A64_OFFSET) -> int:
    result = initial
    for byte in value.to_bytes(8, byteorder="little"):
        result ^= byte
        result = (result * _FNV1A64_PRIME) & ((1 << 64) - 1)
    return result


def build_glb() -> bytes:
    binary = bytearray()
    positions_offset, positions_length = _append_structs(
        binary,
        "<3f",
        (
            (0.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            (1.0, 1.0, 0.0),
            (0.0, 1.0, 0.0),
        ),
    )
    normals_offset, normals_length = _append_structs(
        binary,
        "<3f",
        ((0.0, 0.0, 1.0),) * 4,
    )
    uv_offset, uv_length = _append_structs(
        binary,
        "<2f",
        ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)),
    )
    first_indices_offset, first_indices_length = _append_structs(
        binary, "<H", ((0,), (1,), (2,))
    )
    second_indices_offset, second_indices_length = _append_structs(
        binary, "<H", ((0,), (2,), (3,))
    )
    generated_positions_offset, generated_positions_length = _append_structs(
        binary,
        "<3f",
        ((0.0, 0.0, 1.0), (1.0, 0.0, 1.0), (0.0, 1.0, 1.0)),
    )

    document = {
        "asset": {
            "version": "2.0",
            "generator": "Asharia deterministic Mesh Product v1 fixture generator",
        },
        "scene": 0,
        "scenes": [{"name": "FixtureScene", "nodes": [0, 1]}],
        "nodes": [
            {
                "name": "IndexedAuthoredAttributes",
                "mesh": 0,
                "translation": [1.0, 0.0, 0.0],
            },
            {
                "name": "NonIndexedGeneratedAttributesMirrored",
                "mesh": 1,
                "translation": [-1.0, 0.0, 0.0],
                "scale": [-1.0, 1.0, 1.0],
            },
        ],
        "meshes": [
            {
                "name": "IndexedTwoPrimitiveMesh",
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 3,
                        "material": 0,
                        "mode": 4,
                    },
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 4,
                        "material": 1,
                        "mode": 4,
                    },
                ],
            },
            {
                "name": "NonIndexedMissingNormalUvMesh",
                "primitives": [{"attributes": {"POSITION": 5}, "mode": 4}],
            },
        ],
        "materials": [{"name": "FixtureMaterialA"}, {"name": "FixtureMaterialB"}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {
                "buffer": 0,
                "byteOffset": positions_offset,
                "byteLength": positions_length,
                "target": 34962,
            },
            {
                "buffer": 0,
                "byteOffset": normals_offset,
                "byteLength": normals_length,
                "target": 34962,
            },
            {
                "buffer": 0,
                "byteOffset": uv_offset,
                "byteLength": uv_length,
                "target": 34962,
            },
            {
                "buffer": 0,
                "byteOffset": first_indices_offset,
                "byteLength": first_indices_length,
                "target": 34963,
            },
            {
                "buffer": 0,
                "byteOffset": second_indices_offset,
                "byteLength": second_indices_length,
                "target": 34963,
            },
            {
                "buffer": 0,
                "byteOffset": generated_positions_offset,
                "byteLength": generated_positions_length,
                "target": 34962,
            },
        ],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": 4,
                "type": "VEC3",
                "min": [0.0, 0.0, 0.0],
                "max": [1.0, 1.0, 0.0],
            },
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {
                "bufferView": 5,
                "componentType": 5126,
                "count": 3,
                "type": "VEC3",
                "min": [0.0, 0.0, 1.0],
                "max": [1.0, 1.0, 1.0],
            },
        ],
    }

    json_bytes = bytearray(
        json.dumps(document, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
    )
    _align_four(json_bytes, ord(" "))
    bin_bytes = bytearray(binary)
    _align_four(bin_bytes)
    total_length = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    return b"".join(
        (
            struct.pack("<III", _GLB_MAGIC, _GLB_VERSION, total_length),
            struct.pack("<II", len(json_bytes), _JSON_CHUNK_TYPE),
            json_bytes,
            struct.pack("<II", len(bin_bytes), _BIN_CHUNK_TYPE),
            bin_bytes,
        )
    )


def build_metadata(source_bytes: bytes) -> bytes:
    document = {
        "schema": "com.asharia.asset.metadata",
        "schemaVersion": 1,
        "guid": _ASSET_GUID,
        "assetType": "com.asharia.asset.Mesh",
        "sourcePath": _SOURCE_PATH,
        "sourceHash": f"{_hash_bytes(source_bytes):016x}",
        "settingsHash": f"{_hash_uint64(0):016x}",
        "importer": {"id": _IMPORTER_ID, "version": 1},
        "settings": {},
    }
    return (json.dumps(document, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def main() -> None:
    fixture_root = Path(__file__).resolve().parent
    glb_bytes = build_glb()
    (fixture_root / "restricted-static-mesh.glb").write_bytes(glb_bytes)
    (fixture_root / "restricted-static-mesh.glb.ameta").write_bytes(build_metadata(glb_bytes))


if __name__ == "__main__":
    main()
