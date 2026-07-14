"""Package Product Declaration and Artifact Manifest contract tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools.package_lock_verification import verify_locked_package_graph
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class PackageProductContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.manifest = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )

    def declaration(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.package-products",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "modules": [
                {
                    "moduleId": "runtime",
                    "delivery": {
                        "kind": "artifact-set",
                        "products": [
                            {"id": "runtime-library", "purpose": "link-input"},
                            {"id": "runtime-metadata", "purpose": "metadata"},
                        ],
                    },
                },
                {
                    "moduleId": "diagnostics",
                    "delivery": {"kind": "no-artifacts"},
                },
            ],
        }

    def test_valid_declaration_binds_exact_identity_and_all_modules(self) -> None:
        declaration = self.declaration()

        diagnostics = contracts.validate_package_product_declaration_binding(
            declaration,
            self.manifest,
            self.validators,
        )

        self.assertEqual([], diagnostics)

    def test_declaration_is_closed_against_build_path_and_activation_fields(self) -> None:
        for field, value in (
            ("target", "asharia-runtime"),
            ("path", "bin/runtime.dll"),
            ("factory", "createRuntime"),
            ("scope", "singleton"),
            ("stage", "runtime"),
        ):
            with self.subTest(field=field):
                declaration = self.declaration()
                declaration["modules"][0]["delivery"]["products"][0][field] = value

                diagnostics = contracts.validate_manifest_data(
                    declaration,
                    contracts.PACKAGE_PRODUCTS_NAME,
                    self.validators,
                )

                self.assertTrue(diagnostics)
                self.assertEqual("product.declaration.schema", diagnostics[0].code)

    def test_declaration_rejects_identity_coverage_and_duplicate_drift(self) -> None:
        identity = self.declaration()
        identity["package"]["version"] = "0.2.0"
        missing = self.declaration()
        missing["modules"].pop()
        duplicate = self.declaration()
        duplicate["modules"][0]["delivery"]["products"].append(
            {"id": "runtime-library", "purpose": "runtime-binary"}
        )

        self.assertIn(
            "product.declaration.package-version-mismatch",
            {
                item.code
                for item in contracts.validate_package_product_declaration_binding(
                    identity,
                    self.manifest,
                    self.validators,
                )
            },
        )
        self.assertIn(
            "product.declaration.missing-module",
            {
                item.code
                for item in contracts.validate_package_product_declaration_binding(
                    missing,
                    self.manifest,
                    self.validators,
                )
            },
        )
        self.assertIn(
            "product.declaration.duplicate-product",
            {
                item.code
                for item in contracts.validate_package_product_declaration_binding(
                    duplicate,
                    self.manifest,
                    self.validators,
                )
            },
        )

    def test_declaration_normalization_is_order_independent(self) -> None:
        first = self.declaration()
        second = copy.deepcopy(first)
        second["modules"].reverse()
        second["modules"][1]["delivery"]["products"].reverse()

        self.assertEqual(
            contracts.render_normalized_package_product_declaration(first),
            contracts.render_normalized_package_product_declaration(second),
        )

    def test_artifact_manifest_schema_is_closed_and_paths_are_portable(self) -> None:
        artifact_manifest = {
            "schema": "com.asharia.package-artifact-manifest",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "context": {
                "hostKind": "editor",
                "targetPlatform": "com.asharia.platform.windows",
                "configuration": "Debug",
            },
            "provenance": {
                "kind": "source-build",
                "hostCompositionIntegrity": {
                    "algorithm": "sha256",
                    "digest": "1" * 64,
                },
                "sourceBuildPlanIntegrity": {
                    "algorithm": "sha256",
                    "digest": "2" * 64,
                },
                "productDeclarationIntegrity": {
                    "algorithm": "sha256",
                    "digest": "3" * 64,
                },
            },
            "modules": [
                {
                    "moduleId": "runtime",
                    "delivery": {
                        "kind": "artifact-set",
                        "products": [
                            {
                                "id": "runtime-library",
                                "purpose": "link-input",
                                "files": [
                                    {
                                        "path": "lib/runtime.lib",
                                        "role": "primary",
                                        "mediaType": "application/octet-stream",
                                        "size": 7,
                                        "integrity": {
                                            "algorithm": "sha256",
                                            "digest": "4" * 64,
                                        },
                                    }
                                ],
                            }
                        ],
                    },
                }
            ],
            "integrity": {"algorithm": "sha256", "digest": "5" * 64},
        }
        self.assertEqual(
            [],
            contracts.validate_manifest_data(
                artifact_manifest,
                contracts.PACKAGE_ARTIFACT_MANIFEST_NAME,
                self.validators,
            ),
        )

        for path in (
            "C:/build/runtime.lib",
            "/runtime.lib",
            "../runtime.lib",
            "lib\\runtime.lib",
            "lib/./runtime.lib",
        ):
            with self.subTest(path=path):
                invalid = copy.deepcopy(artifact_manifest)
                invalid["modules"][0]["delivery"]["products"][0]["files"][0][
                    "path"
                ] = path
                diagnostics = contracts.validate_manifest_data(
                    invalid,
                    contracts.PACKAGE_ARTIFACT_MANIFEST_NAME,
                    self.validators,
                )
                self.assertIn(
                    "artifact.manifest.invalid-path",
                    {item.code for item in diagnostics},
                )

        closed = copy.deepcopy(artifact_manifest)
        closed["factory"] = "createRuntime"
        self.assertEqual(
            "artifact.manifest.schema",
            contracts.validate_manifest_data(
                closed,
                contracts.PACKAGE_ARTIFACT_MANIFEST_NAME,
                self.validators,
            )[0].code,
        )

    def test_discovery_captures_product_snapshot_and_locked_reuse_detects_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            root.mkdir()
            declaration = self.declaration()
            (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
                json.dumps(self.manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            declaration_path = root / contracts.PACKAGE_PRODUCTS_NAME
            declaration_path.write_text(
                json.dumps(declaration, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            discovered = discovery.load_package_candidates(
                [
                    discovery.LocalCandidateLocation(
                        source_id=self.manifest["id"],
                        payload_root=root,
                    )
                ],
                self.validators,
            )
            self.assertTrue(discovered.succeeded)
            candidate = discovered.candidates[0]
            self.assertEqual(declaration, candidate.product_declaration)
            self.assertEqual(
                contracts.compute_bytes_integrity(declaration_path.read_bytes()),
                candidate.product_declaration_integrity,
            )

            project = {
                "schema": "com.asharia.project-packages",
                "schemaVersion": 2,
                "engine": package_test_support.engine_requirement(),
                "directPackages": [
                    {
                        "id": self.manifest["id"],
                        "version": {"kind": "exact", "version": "0.1.0"},
                    }
                ],
                "directFeatureSets": [],
                "packageOptions": [],
            }
            resolution = package_resolver.resolve_package_graph(
                project,
                package_test_support.make_engine_distribution(),
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(resolution.succeeded)
            declaration["modules"][0]["delivery"]["products"][0][
                "purpose"
            ] = "runtime-binary"
            declaration_path.write_text(
                json.dumps(declaration, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )

            verified = verify_locked_package_graph(
                project,
                package_test_support.make_engine_distribution(),
                resolution.lock,
                discovered.candidates,
                self.validators,
            )

            self.assertFalse(verified.succeeded)
            self.assertIn(
                "lock.candidate.product-declaration-mismatch",
                {item.code for item in verified.diagnostics},
            )

    def test_discovery_rejects_product_declaration_toctou(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            root.mkdir()
            (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
                json.dumps(self.manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            declaration_path = root / contracts.PACKAGE_PRODUCTS_NAME
            declaration_path.write_text(
                json.dumps(self.declaration(), ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            original_compute = contracts.compute_package_tree_integrity

            def changing_compute(payload_root: Path) -> dict[str, str]:
                integrity = original_compute(payload_root)
                product_path = payload_root / contracts.PACKAGE_PRODUCTS_NAME
                product_path.write_text(
                    product_path.read_text(encoding="utf-8") + " ",
                    encoding="utf-8",
                )
                return integrity

            with mock.patch.object(
                discovery.contracts,
                "compute_package_tree_integrity",
                side_effect=changing_compute,
            ):
                result = discovery.load_package_candidates(
                    [
                        discovery.LocalCandidateLocation(
                            source_id=self.manifest["id"],
                            payload_root=root,
                        )
                    ],
                    self.validators,
                )

            self.assertFalse(result.succeeded)
            self.assertEqual((), result.candidates)
            self.assertEqual(
                ["discovery.source.changed"],
                [item.code for item in result.diagnostics],
            )


if __name__ == "__main__":
    unittest.main()
