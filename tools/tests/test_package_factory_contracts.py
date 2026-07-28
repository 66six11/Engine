"""Package Factory Declaration v1 contract and evidence-chain tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import effective_session
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools.package_lock_verification import verify_locked_package_graph
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class PackageFactoryContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.manifest = json.loads(
            (FIXTURE_ROOT / "valid-host-package.json").read_text(encoding="utf-8")
        )
        cls.editor_profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-editor.json").read_text(
                encoding="utf-8"
            )
        )

    def declaration(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.package-factories",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "lifecycleModel": "create-activate-quiesce-deactivate-destroy-v1",
            "modules": [
                {
                    "moduleId": "contract",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "runtime",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "implementation",
                    "activation": {
                        "kind": "factory-set",
                        "factories": [
                            {
                                "id": "runtime-service",
                                "scope": "project",
                                "requires": [],
                                "contributions": [
                                    "com.asharia.contribution.synthetic-runtime"
                                ],
                            }
                        ],
                    },
                },
                {
                    "moduleId": "editor",
                    "activation": {
                        "kind": "factory-set",
                        "factories": [
                            {
                                "id": "editor-panel",
                                "scope": "editor",
                                "requires": [
                                    {
                                        "packageId": self.manifest["id"],
                                        "factoryId": "runtime-service",
                                    }
                                ],
                                "contributions": [
                                    "com.asharia.contribution.synthetic-editor"
                                ],
                            }
                        ],
                    },
                },
                {
                    "moduleId": "tool",
                    "activation": {
                        "kind": "factory-set",
                        "factories": [
                            {
                                "id": "asset-importer",
                                "scope": "tool-job",
                                "requires": [],
                                "contributions": [
                                    "com.asharia.contribution.synthetic-asset"
                                ],
                            }
                        ],
                    },
                },
                {
                    "moduleId": "cook",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "diagnostics",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "content",
                    "activation": {"kind": "no-factories"},
                },
            ],
        }

    def write_package(
        self,
        root: Path,
        declaration: dict[str, object] | None = None,
    ) -> None:
        root.mkdir(parents=True)
        (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(self.manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        if declaration is not None:
            (root / contracts.PACKAGE_FACTORIES_NAME).write_text(
                json.dumps(declaration, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )

    def discover(self, root: Path) -> discovery.CandidateDiscoveryResult:
        return discovery.load_package_candidates(
            [
                discovery.LocalCandidateLocation(
                    source_id=self.manifest["id"],
                    payload_root=root,
                )
            ],
            self.validators,
        )

    def project(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [
                {
                    "id": self.manifest["id"],
                    "version": {
                        "kind": "exact",
                        "version": self.manifest["version"],
                    },
                }
            ],
            "directFeatureSets": [],
            "packageOptions": [],
        }

    def resolve(self, candidates):
        result = package_resolver.resolve_package_graph(
            self.project(),
            package_test_support.make_engine_distribution(),
            candidates,
            self.validators,
        )
        self.assertTrue(
            result.succeeded,
            "\n".join(value.render() for value in result.diagnostics),
        )
        return result.lock

    def test_valid_declaration_binds_modules_contributions_and_scopes(self) -> None:
        diagnostics = contracts.validate_package_factory_declaration_binding(
            self.declaration(),
            self.manifest,
            self.validators,
        )

        self.assertEqual([], diagnostics)

    def test_schema_is_closed_against_execution_and_dynamic_loading_details(self) -> None:
        mutations = {
            "symbol": lambda value: value["modules"][2]["activation"][
                "factories"
            ][0].update({"symbol": "createRuntime"}),
            "library": lambda value: value["modules"][2]["activation"][
                "factories"
            ][0].update({"library": "runtime.dll"}),
            "phase": lambda value: value.update({"activationPhase": "startup"}),
            "failure-policy": lambda value: value.update(
                {"failurePolicy": "continue"}
            ),
            "lifetime": lambda value: value["modules"][2]["activation"][
                "factories"
            ][0].update({"lifetime": "singleton"}),
            "thread-affinity": lambda value: value["modules"][2]["activation"][
                "factories"
            ][0].update({"threadAffinity": "main"}),
            "optional-dependency": lambda value: value["modules"][3][
                "activation"
            ]["factories"][0]["requires"][0].update({"optional": True}),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                declaration = self.declaration()
                mutate(declaration)

                diagnostics = contracts.validate_manifest_data(
                    declaration,
                    contracts.PACKAGE_FACTORIES_NAME,
                    self.validators,
                )

                self.assertTrue(diagnostics)
                self.assertEqual(
                    {"factory.declaration.schema"},
                    {item.code for item in diagnostics},
                )

    def test_identity_module_and_factory_uniqueness_are_fail_closed(self) -> None:
        identity = self.declaration()
        identity["package"]["version"] = "0.2.0"
        missing = self.declaration()
        missing["modules"].pop()
        unknown = self.declaration()
        unknown["modules"][-1]["moduleId"] = "unknown"
        duplicate = self.declaration()
        duplicate["modules"][4]["activation"]["factories"][0][
            "id"
        ] = "runtime-service"

        cases = (
            (identity, "factory.declaration.package-version-mismatch"),
            (missing, "factory.declaration.missing-module"),
            (unknown, "factory.declaration.unknown-module"),
            (duplicate, "factory.declaration.duplicate-factory"),
        )
        for declaration, expected_code in cases:
            with self.subTest(expected_code=expected_code):
                diagnostics = contracts.validate_package_factory_declaration_binding(
                    declaration,
                    self.manifest,
                    self.validators,
                )
                self.assertIn(expected_code, {item.code for item in diagnostics})

    def test_local_requirements_must_exist_be_acyclic_and_point_to_ancestors(self) -> None:
        self_dependency = self.declaration()
        self_dependency["modules"][3]["activation"]["factories"][0]["requires"] = [
            {
                "packageId": self.manifest["id"],
                "factoryId": "editor-panel",
            }
        ]
        unknown = self.declaration()
        unknown["modules"][3]["activation"]["factories"][0]["requires"][0][
            "factoryId"
        ] = "missing"

        cycle = self.declaration()
        cycle["modules"][1]["activation"] = {
            "kind": "factory-set",
            "factories": [
                {
                    "id": "runtime-api",
                    "scope": "project",
                    "requires": [
                        {
                            "packageId": self.manifest["id"],
                            "factoryId": "runtime-service",
                        }
                    ],
                    "contributions": [],
                }
            ],
        }
        cycle["modules"][2]["activation"]["factories"][0]["requires"] = [
            {
                "packageId": self.manifest["id"],
                "factoryId": "runtime-api",
            }
        ]

        wrong_scope = self.declaration()
        wrong_scope["modules"][3]["activation"]["factories"][0][
            "requires"
        ] = []
        wrong_scope["modules"][2]["activation"]["factories"][0]["requires"] = [
            {
                "packageId": self.manifest["id"],
                "factoryId": "editor-panel",
            }
        ]

        cases = (
            (self_dependency, "factory.declaration.self-dependency"),
            (unknown, "factory.declaration.unknown-local-factory"),
            (cycle, "factory.declaration.cycle"),
            (wrong_scope, "factory.declaration.scope-direction"),
        )
        for declaration, expected_code in cases:
            with self.subTest(expected_code=expected_code):
                diagnostics = contracts.validate_manifest_data(
                    declaration,
                    contracts.PACKAGE_FACTORIES_NAME,
                    self.validators,
                )
                self.assertIn(expected_code, {item.code for item in diagnostics})

    def test_external_requirements_must_name_a_direct_package_dependency(self) -> None:
        declaration = self.declaration()
        declaration["modules"][3]["activation"]["factories"][0][
            "requires"
        ].append(
            {
                "packageId": "com.asharia.system.external",
                "factoryId": "external-service",
            }
        )

        rejected = contracts.validate_package_factory_declaration_binding(
            declaration,
            self.manifest,
            self.validators,
        )
        manifest_with_dependency = copy.deepcopy(self.manifest)
        manifest_with_dependency["dependencies"].append(
            {
                "id": "com.asharia.system.external",
                "version": {"kind": "exact", "version": "1.0.0"},
            }
        )
        accepted = contracts.validate_package_factory_declaration_binding(
            declaration,
            manifest_with_dependency,
            self.validators,
        )

        self.assertIn(
            "factory.declaration.undeclared-package-dependency",
            {item.code for item in rejected},
        )
        self.assertEqual([], accepted)

    def test_contributions_are_known_owned_and_claimed_exactly_once(self) -> None:
        unclaimed = self.declaration()
        unclaimed["modules"][2]["activation"]["factories"][0][
            "contributions"
        ] = []
        wrong_owner = self.declaration()
        wrong_owner["modules"][2]["activation"]["factories"][0][
            "contributions"
        ] = []
        wrong_owner["modules"][3]["activation"]["factories"][0][
            "contributions"
        ].append("com.asharia.contribution.synthetic-runtime")
        unknown = self.declaration()
        unknown["modules"][2]["activation"]["factories"][0][
            "contributions"
        ].append("com.asharia.contribution.unknown")
        duplicate = self.declaration()
        duplicate["modules"][3]["activation"]["factories"][0][
            "contributions"
        ].append("com.asharia.contribution.synthetic-runtime")

        cases = (
            (unclaimed, "factory.declaration.unclaimed-contribution"),
            (wrong_owner, "factory.declaration.contribution-owner-mismatch"),
            (unknown, "factory.declaration.unknown-contribution"),
            (duplicate, "factory.declaration.duplicate-contribution-claim"),
        )
        for declaration, expected_code in cases:
            with self.subTest(expected_code=expected_code):
                diagnostics = contracts.validate_package_factory_declaration_binding(
                    declaration,
                    self.manifest,
                    self.validators,
                )
                self.assertIn(expected_code, {item.code for item in diagnostics})

    def test_normalization_is_independent_of_author_collection_order(self) -> None:
        first = self.declaration()
        first["modules"][3]["activation"]["factories"][0]["requires"].append(
            {
                "packageId": "com.asharia.system.external",
                "factoryId": "external-service",
            }
        )
        second = copy.deepcopy(first)
        second["modules"].reverse()
        editor_module = next(
            module for module in second["modules"] if module["moduleId"] == "editor"
        )
        editor_module["activation"]["factories"][0]["requires"].reverse()

        self.assertEqual(
            contracts.render_normalized_package_factory_declaration(first),
            contracts.render_normalized_package_factory_declaration(second),
        )

    def test_repository_gate_cross_validates_sibling_author_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            declaration = self.declaration()
            declaration["package"]["version"] = "0.2.0"
            self.write_package(root, declaration)

            diagnostics = contracts.validate_manifest_files(
                [
                    root / contracts.PACKAGE_MANIFEST_NAME,
                    root / contracts.PACKAGE_FACTORIES_NAME,
                ],
                self.validators,
            )

            self.assertIn(
                "factory.declaration.package-version-mismatch",
                {item.code for item in diagnostics},
            )

    def test_discovery_captures_optional_exact_snapshot_and_rejects_toctou(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            declaration = self.declaration()
            self.write_package(root, declaration)

            result = self.discover(root)

            self.assertTrue(result.succeeded)
            candidate = result.candidates[0]
            declaration_path = root / contracts.PACKAGE_FACTORIES_NAME
            self.assertEqual(declaration, candidate.factory_declaration)
            self.assertEqual(
                declaration_path.read_bytes(), candidate.factory_declaration_bytes
            )
            self.assertEqual(
                contracts.compute_bytes_integrity(declaration_path.read_bytes()),
                candidate.factory_declaration_integrity,
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root)

            absent = self.discover(root)

            self.assertTrue(absent.succeeded)
            self.assertIsNone(absent.candidates[0].factory_declaration)
            self.assertIsNone(absent.candidates[0].factory_declaration_integrity)
            self.assertIsNone(absent.candidates[0].factory_declaration_bytes)

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root, self.declaration())
            original_compute = contracts.compute_package_tree_integrity

            def changing_compute(payload_root: Path) -> dict[str, str]:
                integrity = original_compute(payload_root)
                declaration_path = payload_root / contracts.PACKAGE_FACTORIES_NAME
                declaration_path.write_text(
                    declaration_path.read_text(encoding="utf-8") + " ",
                    encoding="utf-8",
                )
                return integrity

            with mock.patch.object(
                discovery.contracts,
                "compute_package_tree_integrity",
                side_effect=changing_compute,
            ):
                changed = self.discover(root)

            self.assertFalse(changed.succeeded)
            self.assertEqual((), changed.candidates)
            self.assertEqual(
                ["discovery.source.changed"],
                [item.code for item in changed.diagnostics],
            )

    def test_locked_verification_rejects_missing_incomplete_and_rebound_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root, self.declaration())
            discovered = self.discover(root)
            self.assertTrue(discovered.succeeded)
            candidate = discovered.candidates[0]
            lock = self.resolve(discovered.candidates)

            (root / contracts.PACKAGE_FACTORIES_NAME).unlink()
            missing = verify_locked_package_graph(
                self.project(),
                package_test_support.make_engine_distribution(),
                lock,
                discovered.candidates,
                self.validators,
            )
            self.assertIn(
                "lock.candidate.factory-declaration-mismatch",
                {item.code for item in missing.diagnostics},
            )

            changed_declaration = self.declaration()
            changed_declaration["modules"][2]["activation"]["factories"][0][
                "id"
            ] = "changed-service"
            (root / contracts.PACKAGE_FACTORIES_NAME).write_text(
                json.dumps(changed_declaration, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
            changed = verify_locked_package_graph(
                self.project(),
                package_test_support.make_engine_distribution(),
                lock,
                discovered.candidates,
                self.validators,
            )
            self.assertIn(
                "lock.candidate.factory-declaration-mismatch",
                {item.code for item in changed.diagnostics},
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root, self.declaration())
            discovered = self.discover(root)
            candidate = discovered.candidates[0]
            lock = self.resolve(discovered.candidates)

            incomplete_candidate = replace(
                candidate,
                factory_declaration_integrity=None,
            )
            incomplete = verify_locked_package_graph(
                self.project(),
                package_test_support.make_engine_distribution(),
                lock,
                [incomplete_candidate],
                self.validators,
            )
            self.assertIn(
                "lock.candidate.factory-declaration-incomplete",
                {item.code for item in incomplete.diagnostics},
            )

            rebound_declaration = self.declaration()
            rebound_declaration["package"]["version"] = "0.2.0"
            rebound_bytes = (
                json.dumps(rebound_declaration, ensure_ascii=False, indent=2) + "\n"
            ).encode("utf-8")
            (root / contracts.PACKAGE_FACTORIES_NAME).write_bytes(rebound_bytes)
            rebound_candidate = replace(
                candidate,
                factory_declaration=rebound_declaration,
                factory_declaration_integrity=contracts.compute_bytes_integrity(
                    rebound_bytes
                ),
                factory_declaration_bytes=rebound_bytes,
            )
            rebound = verify_locked_package_graph(
                self.project(),
                package_test_support.make_engine_distribution(),
                lock,
                [rebound_candidate],
                self.validators,
            )
            self.assertIn(
                "factory.declaration.package-version-mismatch",
                {item.code for item in rebound.diagnostics},
            )

    def test_effective_session_preserves_a_deep_copied_verified_declaration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            declaration = self.declaration()
            self.write_package(root, declaration)
            discovered = self.discover(root)
            self.assertTrue(discovered.succeeded)
            candidate = discovered.candidates[0]
            profile_snapshot = package_test_support.make_host_profile_snapshot(
                self.editor_profile
            )
            distribution = package_test_support.make_engine_distribution(
                host_profile_snapshots=[profile_snapshot]
            )
            resolution = package_resolver.resolve_package_graph(
                self.project(),
                distribution,
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(resolution.succeeded)

            result = effective_session.plan_effective_session(
                distribution,
                self.project(),
                resolution.lock,
                discovered.candidates,
                profile_snapshot,
                self.validators,
            )

            self.assertTrue(result.succeeded)
            assert result.plan is not None
            captured = result.plan.verified_graph.selected_candidates[0]
            self.assertEqual(declaration, captured.factory_declaration)
            assert candidate.factory_declaration is not None
            candidate.factory_declaration["modules"].clear()
            self.assertEqual(declaration, captured.factory_declaration)
            assert captured.factory_declaration is not None
            captured.factory_declaration["modules"].clear()
            diagnostics = effective_session.validate_ready_effective_session(
                result.plan,
                self.validators,
            )
            self.assertIn(
                "session.snapshot.fingerprint-mismatch",
                {item.code for item in diagnostics},
            )


if __name__ == "__main__":
    unittest.main()
