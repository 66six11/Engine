"""CPU contract tests for the source-controlled Scene validation model."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools import generate_validation_mesh_product as generator


_REPOSITORY_ROOT = Path(__file__).parents[2]
_FIXTURE_ROOT = _REPOSITORY_ROOT / "assets" / "fixtures" / "scene-rendering"
_SOURCE_PATH = _FIXTURE_ROOT / "directional-wedge.obj"
_METADATA_PATH = _FIXTURE_ROOT / "directional-wedge.obj.ameta"
_EXPECTED_PATH = _FIXTURE_ROOT / "directional-wedge.expected.json"


class ValidationMeshProductTests(unittest.TestCase):
    def test_fixture_product_is_stable_and_matches_committed_oracle(self) -> None:
        expected = json.loads(_EXPECTED_PATH.read_text(encoding="utf-8"))

        with (
            tempfile.TemporaryDirectory() as first_root_text,
            tempfile.TemporaryDirectory() as second_root_text,
        ):
            first_root = Path(first_root_text)
            second_root = Path(second_root_text)
            first_header = first_root / "directional_wedge_mesh_product.hpp"
            first_manifest = first_root / "directional_wedge_mesh_product.json"
            second_header = second_root / "directional_wedge_mesh_product.hpp"
            second_manifest = second_root / "directional_wedge_mesh_product.json"

            first_product = generator.generate_product_files(
                _SOURCE_PATH, _METADATA_PATH, first_header, first_manifest
            )
            second_product = generator.generate_product_files(
                _SOURCE_PATH, _METADATA_PATH, second_header, second_manifest
            )

            self.assertEqual(first_product, second_product)
            self.assertEqual(first_header.read_bytes(), second_header.read_bytes())
            self.assertEqual(first_manifest.read_bytes(), second_manifest.read_bytes())
            self.assertEqual(expected, json.loads(first_manifest.read_text(encoding="utf-8")))
            self.assertEqual(expected["assetId"], first_product.asset_id)
            self.assertEqual(expected["vertexCount"], len(first_product.vertices))
            self.assertEqual(expected["indexCount"], len(first_product.indices))
            self.assertEqual(expected["bounds"]["min"], list(first_product.bounds.minimum))
            self.assertEqual(expected["bounds"]["max"], list(first_product.bounds.maximum))
            self.assertEqual(
                expected["productHash"], f"{first_product.product_hash:016x}"
            )

            header_bytes = first_header.read_bytes()
            self.assertTrue(header_bytes.startswith(b"\xef\xbb\xbf"))
            header_text = header_bytes.decode("utf-8-sig")
            self.assertIn("directionalWedgeValidationMeshProduct()", header_text)
            self.assertNotIn("sourcePath", header_text)
            self.assertNotIn("DirectionalWedge.obj", header_text)
            self.assertNotIn(str(_REPOSITORY_ROOT), header_text)

    def test_parser_rejects_non_triangle_and_out_of_range_faces(self) -> None:
        valid_vertices = """o DirectionalWedge
v 0 0 0 1 0 0
v 1 0 0 0 1 0
v 0 1 0 0 0 1
"""
        cases = {
            "non-triangle": (valid_vertices + "f 1 2 3 1\n", "exactly one triangle"),
            "out-of-range": (valid_vertices + "f 1 2 4\n", "exceeds vertex count 3"),
            "zero-index": (valid_vertices + "f 0 2 3\n", "positive absolute integer"),
        }
        for name, (source, reason) in cases.items():
            with self.subTest(name=name):
                with self.assertRaisesRegex(generator.ValidationMeshError, reason):
                    generator.parse_validation_obj(source)

    def test_parser_rejects_non_finite_vertex_components(self) -> None:
        source = """o DirectionalWedge
v nan 0 0 1 0 0
v 1 0 0 0 1 0
v 0 1 0 0 0 1
f 1 2 3
"""
        with self.assertRaisesRegex(generator.ValidationMeshError, "must be finite"):
            generator.parse_validation_obj(source)


if __name__ == "__main__":
    unittest.main()
