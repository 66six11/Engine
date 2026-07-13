"""Package Source Build Descriptor v1 contract and discovery tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import package_candidate_discovery as discovery


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class PackageSourceBuildContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.manifest = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )

    def descriptor(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.package-source-build",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "modules": [
                {
                    "moduleId": "runtime",
                    "sourceBoundaries": ["com.asharia.synthetic-runtime"],
                    "build": {
                        "kind": "target-roots",
                        "targets": [
                            {"name": "asharia-synthetic-runtime", "type": "STATIC_LIBRARY"}
                        ],
                    },
                },
                {
                    "moduleId": "diagnostics",
                    "sourceBoundaries": ["com.asharia.synthetic-runtime"],
                    "build": {"kind": "no-build"},
                },
            ],
        }

    def write_package(
        self,
        root: Path,
        descriptor: dict[str, object] | None,
    ) -> None:
        root.mkdir(parents=True)
        (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(self.manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        if descriptor is not None:
            (root / contracts.PACKAGE_SOURCE_BUILD_NAME).write_text(
                json.dumps(descriptor, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )

    def load(self, root: Path) -> discovery.CandidateDiscoveryResult:
        return discovery.load_package_candidates(
            [
                discovery.LocalCandidateLocation(
                    source_id="com.asharia.source.synthetic",
                    payload_root=root,
                )
            ],
            self.validators,
        )

    def test_valid_descriptor_binding_and_normalization(self) -> None:
        descriptor = self.descriptor()

        diagnostics = contracts.validate_package_source_build_binding(
            descriptor,
            self.manifest,
            self.validators,
        )

        self.assertEqual([], diagnostics)
        reordered = copy.deepcopy(descriptor)
        reordered["modules"].reverse()
        reordered["modules"][1]["sourceBoundaries"].reverse()
        self.assertEqual(
            contracts.render_normalized_package_source_build_descriptor(descriptor),
            contracts.render_normalized_package_source_build_descriptor(reordered),
        )

    def test_descriptor_requires_exact_identity_and_complete_module_coverage(self) -> None:
        descriptor = self.descriptor()
        descriptor["package"]["version"] = "0.1.1"
        descriptor["modules"].pop()
        descriptor["modules"].append(
            {
                "moduleId": "unknown",
                "sourceBoundaries": ["com.asharia.synthetic-runtime"],
                "build": {"kind": "no-build"},
            }
        )

        codes = {
            item.code
            for item in contracts.validate_package_source_build_binding(
                descriptor,
                self.manifest,
                self.validators,
            )
        }

        self.assertEqual(
            {
                "build.descriptor.package-version-mismatch",
                "build.descriptor.missing-module",
                "build.descriptor.unknown-module",
            },
            codes,
        )

    def test_repository_gate_cross_validates_sibling_author_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            descriptor = self.descriptor()
            descriptor["package"]["version"] = "0.1.1"
            self.write_package(root, descriptor)

            diagnostics = contracts.validate_manifest_files(
                [
                    root / contracts.PACKAGE_MANIFEST_NAME,
                    root / contracts.PACKAGE_SOURCE_BUILD_NAME,
                ],
                self.validators,
            )

            self.assertIn(
                "build.descriptor.package-version-mismatch",
                {item.code for item in diagnostics},
            )

    def test_closed_descriptor_rejects_commands_artifacts_and_lifecycle(self) -> None:
        mutations = {
            "command": lambda value: value["modules"][0]["build"].update(
                {"command": ["cmake", "--build"]}
            ),
            "artifact": lambda value: value["modules"][0]["build"].update(
                {"artifact": "runtime.dll"}
            ),
            "factory": lambda value: value.update({"factory": "CreateRuntime"}),
            "phase": lambda value: value.update({"activationPhase": "startup"}),
            "alias": lambda value: value["modules"][0]["build"]["targets"][0].update(
                {"name": "asharia::synthetic-runtime"}
            ),
            "interface": lambda value: value["modules"][0]["build"]["targets"][0].update(
                {"type": "INTERFACE_LIBRARY"}
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                descriptor = self.descriptor()
                mutate(descriptor)

                diagnostics = contracts.validate_manifest_data(
                    descriptor,
                    contracts.PACKAGE_SOURCE_BUILD_NAME,
                    self.validators,
                )

                self.assertTrue(diagnostics)
                self.assertEqual(
                    {"build.descriptor.schema"},
                    {item.code for item in diagnostics},
                )

    def test_discovery_captures_exact_optional_descriptor_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root, self.descriptor())

            result = self.load(root)

            self.assertTrue(
                result.succeeded,
                [item.render() for item in result.diagnostics],
            )
            candidate = result.candidates[0]
            descriptor_path = root / contracts.PACKAGE_SOURCE_BUILD_NAME
            self.assertEqual(self.descriptor(), candidate.build_descriptor)
            self.assertEqual(descriptor_path.read_bytes(), candidate.build_descriptor_bytes)
            self.assertEqual(
                contracts.compute_bytes_integrity(descriptor_path.read_bytes()),
                candidate.build_descriptor_integrity,
            )
            self.assertEqual(
                contracts.compute_package_tree_integrity(root),
                candidate.payload_integrity,
            )

    def test_discovery_allows_source_candidate_without_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root, None)

            result = self.load(root)

            self.assertTrue(result.succeeded)
            candidate = result.candidates[0]
            self.assertIsNone(candidate.build_descriptor)
            self.assertIsNone(candidate.build_descriptor_integrity)
            self.assertIsNone(candidate.build_descriptor_bytes)

    def test_discovery_fails_atomically_for_invalid_or_changing_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            invalid = self.descriptor()
            invalid["package"]["id"] = "com.asharia.system.other"
            self.write_package(root, invalid)

            invalid_result = self.load(root)

            self.assertFalse(invalid_result.succeeded)
            self.assertEqual((), invalid_result.candidates)
            self.assertIn(
                "build.descriptor.package-id-mismatch",
                {item.code for item in invalid_result.diagnostics},
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root, self.descriptor())
            original_compute = contracts.compute_package_tree_integrity

            def changing_compute(payload_root: Path) -> dict[str, str]:
                integrity = original_compute(payload_root)
                descriptor_path = payload_root / contracts.PACKAGE_SOURCE_BUILD_NAME
                descriptor_path.write_text(
                    descriptor_path.read_text(encoding="utf-8") + " ",
                    encoding="utf-8",
                )
                return integrity

            with mock.patch.object(
                discovery.contracts,
                "compute_package_tree_integrity",
                side_effect=changing_compute,
            ):
                changed_result = self.load(root)

            self.assertFalse(changed_result.succeeded)
            self.assertEqual((), changed_result.candidates)
            self.assertEqual(
                ["discovery.source.changed"],
                [item.code for item in changed_result.diagnostics],
            )


if __name__ == "__main__":
    unittest.main()
