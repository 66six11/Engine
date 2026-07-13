"""Package Lockfile v1 contract and selected-result tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class PackageLockContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()

    def load(self, name: str) -> dict[str, object]:
        return json.loads((FIXTURE_ROOT / name).read_text(encoding="utf-8"))

    def validate_data(self, lock: dict[str, object]) -> list[check_package_contracts.Diagnostic]:
        return check_package_contracts.validate_manifest_data(
            lock,
            "asharia.packages.lock.json",
            self.validators,
        )

    def author_manifests(self) -> list[dict[str, object]]:
        feature_set = self.load("valid-feature-set.json")
        rendering = self.load("valid-system.json")
        rendering["id"] = "com.asharia.system.rendering-vulkan"
        input_system = self.load("valid-system.json")
        input_system["id"] = "com.asharia.system.input"
        input_system["options"] = []
        return [feature_set, rendering, input_system]

    def validate_selected_result(
        self,
        lock: dict[str, object],
        project: dict[str, object],
        manifests: list[dict[str, object]] | None = None,
    ) -> list[check_package_contracts.Diagnostic]:
        return check_package_contracts.validate_locked_result_data(
            lock,
            project,
            manifests if manifests is not None else self.author_manifests(),
            self.validators,
        )

    def test_valid_empty_and_transitive_locks_are_accepted(self) -> None:
        self.assertEqual([], self.validate_data(self.load("valid-empty-lock.json")))
        self.assertEqual([], self.validate_data(self.load("valid-lock.json")))

    def test_lock_schema_rejects_unknown_fields_invalid_integrity_and_free_source(self) -> None:
        lock = self.load("valid-lock.json")
        lock["generatedAt"] = "2026-07-13T00:00:00Z"
        lock["nodes"][0]["manifestIntegrity"]["digest"] = "SHA256:abc"
        lock["nodes"][1]["source"] = {"kind": "registry", "url": "https://example.test"}

        diagnostics = self.validate_data(lock)

        self.assertTrue(diagnostics)
        self.assertEqual({"lock.manifest.schema"}, {item.code for item in diagnostics})

    def test_duplicate_and_multiple_versions_are_rejected(self) -> None:
        duplicate = self.load("valid-lock.json")
        duplicate["nodes"].append(copy.deepcopy(duplicate["nodes"][2]))
        self.assertIn("lock.node.duplicate-id", {item.code for item in self.validate_data(duplicate)})

        multiple = self.load("valid-lock.json")
        other_version = copy.deepcopy(multiple["nodes"][2])
        other_version["version"] = "0.1.1"
        multiple["nodes"].append(other_version)
        self.assertIn(
            "lock.node.multiple-versions",
            {item.code for item in self.validate_data(multiple)},
        )

    def test_exact_reference_failures_are_rejected(self) -> None:
        mutations = {
            "missing": (
                lambda lock: lock["nodes"][0]["dependencies"].append(
                    {
                        "id": "com.asharia.system.missing",
                        "version": "0.1.0",
                        "packageKind": "installable-capability",
                    }
                ),
                "lock.reference.missing-node",
            ),
            "version": (
                lambda lock: lock["nodes"][0]["dependencies"][0].update(
                    {"version": "0.1.1"}
                ),
                "lock.reference.version-mismatch",
            ),
            "kind": (
                lambda lock: lock["nodes"][0]["dependencies"][0].update(
                    {"packageKind": "feature-set"}
                ),
                "lock.reference.kind-mismatch",
            ),
        }
        for name, (mutate, expected_code) in mutations.items():
            with self.subTest(name=name):
                lock = self.load("valid-lock.json")
                mutate(lock)
                self.assertIn(expected_code, {item.code for item in self.validate_data(lock)})

    def test_cycles_unreachable_nodes_and_invalid_paths_are_rejected(self) -> None:
        cycle = self.load("valid-lock.json")
        cycle["nodes"][2]["dependencies"].append(
            {
                "id": "com.asharia.features.standard3d",
                "version": "0.1.0",
                "packageKind": "feature-set",
            }
        )
        cycle_codes = {item.code for item in self.validate_data(cycle)}
        self.assertIn("lock.dependency.invalid-kind", cycle_codes)
        self.assertIn("lock.dependency.cycle", cycle_codes)

        unreachable = self.load("valid-lock.json")
        node = copy.deepcopy(unreachable["nodes"][2])
        node["id"] = "com.asharia.system.unreachable"
        unreachable["nodes"].append(node)
        self.assertIn(
            "lock.node.unreachable",
            {item.code for item in self.validate_data(unreachable)},
        )

        invalid_path = self.load("valid-lock.json")
        invalid_path["nodes"][0]["source"]["relativePath"] = "../outside"
        self.assertEqual(
            ["lock.source.invalid-relative-path"],
            [item.code for item in self.validate_data(invalid_path)],
        )

    def test_normalized_lock_writer_is_order_independent(self) -> None:
        lock = self.load("valid-lock.json")
        reordered = copy.deepcopy(lock)
        reordered["nodes"].reverse()
        reordered["nodes"][2]["dependencies"].reverse()

        first = check_package_contracts.render_normalized_lock_manifest(lock)
        second = check_package_contracts.render_normalized_lock_manifest(reordered)

        self.assertEqual(first, second)
        self.assertEqual(
            (FIXTURE_ROOT / "valid-lock.json").read_text(encoding="utf-8"),
            first,
        )
        self.assertEqual(first, check_package_contracts.render_normalized_lock_manifest(json.loads(first)))
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\r", first)

    def test_project_manifest_integrity_uses_normalized_bytes(self) -> None:
        project = self.load("valid-project.json")
        reordered = copy.deepcopy(project)
        reordered["directPackages"].reverse()
        reordered["packageOptions"].reverse()

        first = check_package_contracts.compute_project_manifest_integrity(project)
        second = check_package_contracts.compute_project_manifest_integrity(reordered)

        self.assertEqual(first, second)
        self.assertRegex(first["digest"], r"^[0-9a-f]{64}$")

    def test_package_tree_integrity_ignores_enumeration_and_root_build_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            first_root = temporary_root / "first"
            second_root = temporary_root / "second"
            for root, file_order in (
                (first_root, ("alpha.txt", "nested/beta.bin")),
                (second_root, ("nested/beta.bin", "alpha.txt")),
            ):
                root.mkdir()
                (root / "asharia.package.json").write_text("{}\n", encoding="utf-8")
                for relative_path in file_order:
                    path = root / relative_path
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(relative_path.encode("utf-8"))
                (root / "build").mkdir()
                (root / "build/ignored.bin").write_bytes(root.name.encode("utf-8"))

            first = check_package_contracts.compute_package_tree_integrity(first_root)
            second = check_package_contracts.compute_package_tree_integrity(second_root)
            (second_root / "alpha.txt").write_bytes(b"changed")
            changed = check_package_contracts.compute_package_tree_integrity(second_root)

        self.assertEqual(first, second)
        self.assertNotEqual(first, changed)

    def test_package_tree_requires_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            with self.assertRaisesRegex(ValueError, "must contain regular file"):
                check_package_contracts.compute_package_tree_integrity(root)

    def test_package_tree_rejects_a_link_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            original_is_symlink = Path.is_symlink

            def mark_root_as_link(path: Path) -> bool:
                return path == root or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", new=mark_root_as_link):
                with self.assertRaises(check_package_contracts.PackageTreeIntegrityError) as raised:
                    check_package_contracts.compute_package_tree_integrity(root)

        self.assertEqual("link", raised.exception.code)
        self.assertEqual("", raised.exception.relative_path)

    def test_locked_candidate_integrity_detects_missing_and_changed_payloads(self) -> None:
        lock = self.load("valid-lock.json")
        manifests = {manifest["id"]: manifest for manifest in self.author_manifests()}
        with tempfile.TemporaryDirectory() as temporary_directory:
            candidate_roots: dict[str, Path] = {}
            for node in lock["nodes"]:
                identity = node["id"]
                root = Path(temporary_directory) / identity
                root.mkdir()
                (root / "asharia.package.json").write_text(
                    json.dumps(manifests[identity], ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8",
                )
                (root / "payload.bin").write_bytes(identity.encode("utf-8"))
                node["manifestIntegrity"] = (
                    check_package_contracts.compute_manifest_file_integrity(
                        root / "asharia.package.json"
                    )
                )
                node["payloadIntegrity"] = (
                    check_package_contracts.compute_package_tree_integrity(root)
                )
                candidate_roots[identity] = root

            self.assertEqual(
                [],
                check_package_contracts.validate_locked_candidate_integrity(
                    lock,
                    candidate_roots,
                ),
            )
            changed_identity = lock["nodes"][0]["id"]
            (candidate_roots[changed_identity] / "payload.bin").write_bytes(b"changed")
            self.assertEqual(
                ["lock.integrity.payload-mismatch"],
                [
                    item.code
                    for item in check_package_contracts.validate_locked_candidate_integrity(
                        lock,
                        candidate_roots,
                    )
                ],
            )
            del candidate_roots[lock["nodes"][1]["id"]]
            self.assertIn(
                "lock.source.unavailable",
                {
                    item.code
                    for item in check_package_contracts.validate_locked_candidate_integrity(
                        lock,
                        candidate_roots,
                    )
                },
            )

    def test_selected_result_matches_project_and_author_manifests(self) -> None:
        diagnostics = self.validate_selected_result(
            self.load("valid-lock.json"),
            self.load("valid-lock-project.json"),
        )

        self.assertEqual([], diagnostics)

    def test_selected_result_detects_stale_project_and_option_errors(self) -> None:
        stale_project = self.load("valid-lock-project.json")
        stale_project["packageOptions"][0]["values"][0]["value"] = True
        self.assertEqual(
            ["lock.input.project-manifest-stale"],
            [
                item.code
                for item in self.validate_selected_result(
                    self.load("valid-lock.json"),
                    stale_project,
                )
            ],
        )

        invalid_option_project = self.load("valid-lock-project.json")
        invalid_option_project["packageOptions"][0]["values"][0]["value"] = "enabled"
        lock = self.load("valid-lock.json")
        lock["inputs"]["projectManifestIntegrity"] = (
            check_package_contracts.compute_project_manifest_integrity(invalid_option_project)
        )
        self.assertEqual(
            ["lock.option.invalid-value"],
            [
                item.code
                for item in self.validate_selected_result(lock, invalid_option_project)
            ],
        )

    def test_selected_result_detects_author_constraint_and_edge_mismatches(self) -> None:
        manifests = self.author_manifests()
        manifests[0]["packages"][0]["version"]["minimumInclusive"] = "0.1.1"
        self.assertIn(
            "lock.edge.constraint-mismatch",
            {
                item.code
                for item in self.validate_selected_result(
                    self.load("valid-lock.json"),
                    self.load("valid-lock-project.json"),
                    manifests,
                )
            },
        )

        undeclared_edge_lock = self.load("valid-lock.json")
        undeclared_edge_lock["nodes"][1]["dependencies"].append(
            {
                "id": "com.asharia.system.rendering-vulkan",
                "version": "0.1.0",
                "packageKind": "installable-capability",
            }
        )
        self.assertIn(
            "lock.edge.undeclared-author-dependency",
            {
                item.code
                for item in self.validate_selected_result(
                    undeclared_edge_lock,
                    self.load("valid-lock-project.json"),
                )
            },
        )

    def test_selected_result_detects_engine_and_candidate_mismatches(self) -> None:
        manifests = self.author_manifests()
        manifests[1]["engineApi"]["minimumInclusive"] = "0.1.1"
        self.assertIn(
            "lock.candidate.engine-api-mismatch",
            {
                item.code
                for item in self.validate_selected_result(
                    self.load("valid-lock.json"),
                    self.load("valid-lock-project.json"),
                    manifests,
                )
            },
        )

        manifests = self.author_manifests()
        manifests[1]["version"] = "0.1.1"
        self.assertIn(
            "lock.candidate.version-mismatch",
            {
                item.code
                for item in self.validate_selected_result(
                    self.load("valid-lock.json"),
                    self.load("valid-lock-project.json"),
                    manifests,
                )
            },
        )


if __name__ == "__main__":
    unittest.main()
