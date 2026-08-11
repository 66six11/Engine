"""Generate the deterministic Scene validation mesh product.

This intentionally narrow tool accepts only the repository's DirectionalWedge OBJ fixture
contract. It is build/test input normalization, not a general-purpose OBJ importer.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence


_FNV1A64_OFFSET = 14695981039346656037
_FNV1A64_PRIME = 1099511628211
_HASH_MASK = (1 << 64) - 1
_PRODUCT_HASH_PREFIX = b"asharia.validation.mesh-product.v1\0"
_EXPECTED_OBJECT_NAME = "DirectionalWedge"
_EXPECTED_ASSET_TYPE = "com.asharia.asset.Mesh"
_EXPECTED_IMPORTER = "com.asharia.importer.validation-mesh-fixture"
_EXPECTED_FIXTURE_CONTRACT = "directional-wedge-v1"
_EXPECTED_SOURCE_PATH = "assets/fixtures/scene-rendering/directional-wedge.obj"
_MAX_VERTEX_COUNT = 65535
_MAX_INDEX_COUNT = 65535 * 3


class ValidationMeshError(ValueError):
    """A typed, user-facing validation fixture generation failure."""


@dataclass(frozen=True)
class ValidationVertex:
    position: tuple[float, float, float]
    color: tuple[float, float, float]


@dataclass(frozen=True)
class ValidationBounds:
    minimum: tuple[float, float, float]
    maximum: tuple[float, float, float]


@dataclass(frozen=True)
class ValidationMeshProduct:
    asset_id: str
    product_hash: int
    vertices: tuple[ValidationVertex, ...]
    indices: tuple[int, ...]
    bounds: ValidationBounds


def _hash_bytes(data: bytes, initial: int = _FNV1A64_OFFSET) -> int:
    value = initial
    for byte in data:
        value ^= byte
        value = (value * _FNV1A64_PRIME) & _HASH_MASK
    return value


def _hash_uint64(value: int, initial: int) -> int:
    return _hash_bytes(value.to_bytes(8, byteorder="little"), initial)


def _hash_text(text: str, initial: int) -> int:
    encoded = text.encode("utf-8")
    value = _hash_uint64(len(encoded), initial)
    return _hash_bytes(encoded, value)


def _hash_settings(settings: Mapping[str, object]) -> int:
    value = _hash_uint64(len(settings), _FNV1A64_OFFSET)
    for key, setting in settings.items():
        if not isinstance(key, str) or not isinstance(setting, str):
            raise ValidationMeshError("metadata settings must contain only string values")
        value = _hash_text(key, value)
        value = _hash_text(setting, value)
    return value


def _float32(token: str, line_number: int, field: str) -> float:
    try:
        parsed = float(token)
    except ValueError as error:
        raise ValidationMeshError(
            f"line {line_number}: {field} '{token}' is not a number"
        ) from error
    if not math.isfinite(parsed):
        raise ValidationMeshError(
            f"line {line_number}: {field} must be finite, observed '{token}'"
        )
    try:
        normalized = struct.unpack("<f", struct.pack("<f", parsed))[0]
    except OverflowError as error:
        raise ValidationMeshError(
            f"line {line_number}: {field} is outside finite float32 range"
        ) from error
    if not math.isfinite(normalized):
        raise ValidationMeshError(
            f"line {line_number}: {field} is outside finite float32 range"
        )
    return 0.0 if normalized == 0.0 else normalized


def _positive_obj_index(token: str, line_number: int) -> int:
    if not token.isascii() or not token.isdecimal() or token.startswith("0"):
        raise ValidationMeshError(
            f"line {line_number}: face index '{token}' must be a positive absolute integer"
        )
    value = int(token)
    if value > _MAX_VERTEX_COUNT:
        raise ValidationMeshError(
            f"line {line_number}: face index {value} exceeds uint16 fixture bounds"
        )
    return value - 1


def _triangle_area_squared(
    first: ValidationVertex, second: ValidationVertex, third: ValidationVertex
) -> float:
    edge_a = tuple(second.position[index] - first.position[index] for index in range(3))
    edge_b = tuple(third.position[index] - first.position[index] for index in range(3))
    cross = (
        edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
        edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
        edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0],
    )
    return sum(component * component for component in cross)


def parse_validation_obj(source_text: str) -> tuple[tuple[ValidationVertex, ...], tuple[int, ...]]:
    """Parse the closed validation-only OBJ subset into normalized float32/index data."""

    vertices: list[ValidationVertex] = []
    face_records: list[tuple[int, tuple[int, int, int]]] = []
    object_name: str | None = None

    for line_number, raw_line in enumerate(source_text.splitlines(), start=1):
        line = raw_line.partition("#")[0].strip()
        if not line:
            continue
        fields = line.split()
        directive = fields[0]
        if directive == "o":
            if len(fields) != 2 or fields[1] != _EXPECTED_OBJECT_NAME:
                raise ValidationMeshError(
                    f"line {line_number}: fixture object must be '{_EXPECTED_OBJECT_NAME}'"
                )
            if object_name is not None:
                raise ValidationMeshError(
                    f"line {line_number}: fixture must contain exactly one object declaration"
                )
            object_name = fields[1]
            continue
        if directive == "v":
            if len(fields) != 7:
                raise ValidationMeshError(
                    f"line {line_number}: fixture vertex requires position3 and color3"
                )
            values = tuple(
                _float32(token, line_number, f"vertex component {index}")
                for index, token in enumerate(fields[1:], start=1)
            )
            color = values[3:]
            if any(component < 0.0 or component > 1.0 for component in color):
                raise ValidationMeshError(
                    f"line {line_number}: vertex colors must be in the inclusive [0, 1] range"
                )
            vertices.append(
                ValidationVertex(
                    position=(values[0], values[1], values[2]),
                    color=(color[0], color[1], color[2]),
                )
            )
            if len(vertices) > _MAX_VERTEX_COUNT:
                raise ValidationMeshError("fixture vertex count exceeds uint16 index bounds")
            continue
        if directive == "f":
            if len(fields) != 4:
                raise ValidationMeshError(
                    f"line {line_number}: fixture faces must contain exactly one triangle"
                )
            indices = tuple(_positive_obj_index(token, line_number) for token in fields[1:])
            if len(set(indices)) != 3:
                raise ValidationMeshError(
                    f"line {line_number}: fixture face contains a repeated index"
                )
            face_records.append((line_number, (indices[0], indices[1], indices[2])))
            if len(face_records) * 3 > _MAX_INDEX_COUNT:
                raise ValidationMeshError("fixture index count exceeds validation product bounds")
            continue
        raise ValidationMeshError(
            f"line {line_number}: unsupported validation OBJ directive '{directive}'"
        )

    if object_name is None:
        raise ValidationMeshError("fixture is missing its object declaration")
    if len(vertices) < 3:
        raise ValidationMeshError("fixture requires at least three vertices")
    if not face_records:
        raise ValidationMeshError("fixture requires at least one triangle")

    referenced: set[int] = set()
    indices: list[int] = []
    for line_number, face in face_records:
        for index in face:
            if index >= len(vertices):
                raise ValidationMeshError(
                    f"line {line_number}: face index {index + 1} exceeds vertex count "
                    f"{len(vertices)}"
                )
        if _triangle_area_squared(
            vertices[face[0]], vertices[face[1]], vertices[face[2]]
        ) <= 1.0e-12:
            raise ValidationMeshError(
                f"line {line_number}: fixture face is geometrically degenerate"
            )
        referenced.update(face)
        indices.extend(face)

    if len(referenced) != len(vertices):
        raise ValidationMeshError("fixture contains vertices that are not referenced by any face")
    return tuple(vertices), tuple(indices)


def _closed_object(value: object, expected_keys: set[str], context: str) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected_keys:
        raise ValidationMeshError(f"{context} must use the closed validation fixture shape")
    return value


def _load_metadata(metadata_text: str, source_bytes: bytes) -> str:
    try:
        root_value = json.loads(metadata_text)
    except json.JSONDecodeError as error:
        raise ValidationMeshError(f"metadata JSON is invalid: {error.msg}") from error
    root = _closed_object(
        root_value,
        {
            "schema",
            "schemaVersion",
            "guid",
            "assetType",
            "sourcePath",
            "sourceHash",
            "settingsHash",
            "importer",
            "settings",
        },
        "metadata",
    )
    importer = _closed_object(root["importer"], {"id", "version"}, "metadata importer")
    settings = _closed_object(root["settings"], {"fixtureContract"}, "metadata settings")

    expected_values = {
        "schema": "com.asharia.asset.metadata",
        "schemaVersion": 1,
        "assetType": _EXPECTED_ASSET_TYPE,
        "sourcePath": _EXPECTED_SOURCE_PATH,
    }
    for field, expected in expected_values.items():
        if root[field] != expected:
            raise ValidationMeshError(
                f"metadata field '{field}' must equal {expected!r}, observed {root[field]!r}"
            )
    if importer != {"id": _EXPECTED_IMPORTER, "version": 1}:
        raise ValidationMeshError("metadata importer does not match validation fixture contract v1")
    if settings != {"fixtureContract": _EXPECTED_FIXTURE_CONTRACT}:
        raise ValidationMeshError("metadata settings do not match validation fixture contract v1")

    asset_id_value = root["guid"]
    if not isinstance(asset_id_value, str):
        raise ValidationMeshError("metadata guid must be a canonical UUID string")
    try:
        asset_uuid = uuid.UUID(asset_id_value)
    except (ValueError, AttributeError) as error:
        raise ValidationMeshError("metadata guid must be a canonical UUID string") from error
    if str(asset_uuid) != asset_id_value or asset_uuid.int == 0:
        raise ValidationMeshError("metadata guid must be a nonzero canonical lowercase UUID")

    source_hash = f"{_hash_bytes(source_bytes):016x}"
    if root["sourceHash"] != source_hash:
        raise ValidationMeshError(
            f"metadata sourceHash mismatch: expected '{source_hash}'"
        )
    settings_hash = f"{_hash_settings(settings):016x}"
    if root["settingsHash"] != settings_hash:
        raise ValidationMeshError(
            f"metadata settingsHash mismatch: expected '{settings_hash}'"
        )
    return asset_id_value


def _mesh_bounds(vertices: Sequence[ValidationVertex]) -> ValidationBounds:
    return ValidationBounds(
        minimum=tuple(min(vertex.position[axis] for vertex in vertices) for axis in range(3)),
        maximum=tuple(max(vertex.position[axis] for vertex in vertices) for axis in range(3)),
    )


def _canonical_product_bytes(
    asset_id: str,
    vertices: Sequence[ValidationVertex],
    indices: Sequence[int],
) -> bytes:
    payload = bytearray(_PRODUCT_HASH_PREFIX)
    payload.extend(uuid.UUID(asset_id).bytes)
    payload.extend(struct.pack("<II", len(vertices), len(indices)))
    for vertex in vertices:
        payload.extend(struct.pack("<6f", *(vertex.position + vertex.color)))
    for index in indices:
        payload.extend(struct.pack("<H", index))
    return bytes(payload)


def build_validation_mesh_product(
    source_bytes: bytes, metadata_text: str
) -> ValidationMeshProduct:
    try:
        source_text = source_bytes.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValidationMeshError("validation OBJ source must be UTF-8 without BOM") from error
    if source_text.startswith("\ufeff"):
        raise ValidationMeshError("validation OBJ source must be UTF-8 without BOM")
    asset_id = _load_metadata(metadata_text, source_bytes)
    vertices, indices = parse_validation_obj(source_text)
    canonical_bytes = _canonical_product_bytes(asset_id, vertices, indices)
    product_hash = _hash_bytes(canonical_bytes)
    return ValidationMeshProduct(
        asset_id=asset_id,
        product_hash=product_hash,
        vertices=vertices,
        indices=indices,
        bounds=_mesh_bounds(vertices),
    )


def product_manifest(product: ValidationMeshProduct) -> dict[str, object]:
    return {
        "schema": "com.asharia.validation.mesh-product",
        "schemaVersion": 1,
        "assetId": product.asset_id,
        "productHash": f"{product.product_hash:016x}",
        "vertexCount": len(product.vertices),
        "indexCount": len(product.indices),
        "vertexFormat": "position3f-color3f",
        "indexFormat": "uint16",
        "bounds": {
            "min": list(product.bounds.minimum),
            "max": list(product.bounds.maximum),
        },
    }


def _cpp_float(value: float) -> str:
    text = format(value, ".9g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "F"


def product_header(product: ValidationMeshProduct) -> bytes:
    vertex_lines = []
    for vertex in product.vertices:
        position = ", ".join(_cpp_float(component) for component in vertex.position)
        color = ", ".join(_cpp_float(component) for component in vertex.color)
        vertex_lines.append(
            "        ValidationMeshVertex{.position = {"
            + position
            + "}, .color = {"
            + color
            + "}},"
        )
    index_lines = []
    for offset in range(0, len(product.indices), 12):
        index_lines.append(
            "        "
            + ", ".join(str(index) for index in product.indices[offset : offset + 12])
            + ","
        )
    minimum = ", ".join(_cpp_float(value) for value in product.bounds.minimum)
    maximum = ", ".join(_cpp_float(value) for value in product.bounds.maximum)
    text = f"""#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace asharia::validation {{

    struct ValidationMeshVertex {{
        std::array<float, 3> position{{}};
        std::array<float, 3> color{{}};
    }};

    struct ValidationMeshBounds {{
        std::array<float, 3> minimum{{}};
        std::array<float, 3> maximum{{}};
    }};

    struct ValidationMeshProductView {{
        std::string_view assetId;
        std::uint64_t productHash{{}};
        std::span<const ValidationMeshVertex> vertices;
        std::span<const std::uint16_t> indices;
        ValidationMeshBounds bounds;
    }};

    inline constexpr std::string_view kDirectionalWedgeValidationAssetId =
        \"{product.asset_id}\";
    inline constexpr std::uint64_t kDirectionalWedgeValidationProductHash =
        0x{product.product_hash:016x}ULL;
    inline constexpr std::array<ValidationMeshVertex, {len(product.vertices)}>
        kDirectionalWedgeValidationVertices{{{{
{chr(10).join(vertex_lines)}
    }}}};
    inline constexpr std::array<std::uint16_t, {len(product.indices)}>
        kDirectionalWedgeValidationIndices{{{{
{chr(10).join(index_lines)}
    }}}};
    inline constexpr ValidationMeshBounds kDirectionalWedgeValidationBounds{{
        .minimum = {{{minimum}}},
        .maximum = {{{maximum}}},
    }};

    [[nodiscard]] inline constexpr ValidationMeshProductView
    directionalWedgeValidationMeshProduct() noexcept {{
        return ValidationMeshProductView{{
            .assetId = kDirectionalWedgeValidationAssetId,
            .productHash = kDirectionalWedgeValidationProductHash,
            .vertices = std::span<const ValidationMeshVertex>{{
                kDirectionalWedgeValidationVertices}},
            .indices = std::span<const std::uint16_t>{{kDirectionalWedgeValidationIndices}},
            .bounds = kDirectionalWedgeValidationBounds,
        }};
    }}

}} // namespace asharia::validation
"""
    return b"\xef\xbb\xbf" + text.encode("utf-8")


def _write_if_changed(path: Path, data: bytes) -> None:
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def generate_product_files(
    source_path: Path,
    metadata_path: Path,
    output_header_path: Path,
    output_manifest_path: Path,
) -> ValidationMeshProduct:
    paths = [source_path, metadata_path, output_header_path, output_manifest_path]
    if len({path.resolve() for path in paths}) != len(paths):
        raise ValidationMeshError("source, metadata, header, and manifest paths must be distinct")
    if output_header_path.suffix != ".hpp":
        raise ValidationMeshError("generated validation mesh header must use the .hpp extension")
    if output_manifest_path.suffix != ".json":
        raise ValidationMeshError("generated validation mesh manifest must use the .json extension")

    source_bytes = source_path.read_bytes()
    metadata_text = metadata_path.read_text(encoding="utf-8")
    product = build_validation_mesh_product(source_bytes, metadata_text)
    manifest_bytes = (
        json.dumps(product_manifest(product), indent=2, ensure_ascii=True) + "\n"
    ).encode("utf-8")
    _write_if_changed(output_header_path, product_header(product))
    _write_if_changed(output_manifest_path, manifest_bytes)
    return product


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate the repository's validation-only DirectionalWedge mesh product."
    )
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    parser.add_argument("--output-header", required=True, type=Path)
    parser.add_argument("--output-manifest", required=True, type=Path)
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        product = generate_product_files(
            arguments.source,
            arguments.metadata,
            arguments.output_header,
            arguments.output_manifest,
        )
    except (OSError, ValidationMeshError) as error:
        print(f"validation mesh product generation failed: {error}", file=sys.stderr)
        return 1
    print(
        "generated validation mesh product "
        f"assetId={product.asset_id} productHash={product.product_hash:016x} "
        f"vertices={len(product.vertices)} indices={len(product.indices)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
