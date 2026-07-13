"""Project Manifest and Feature Set author-contract tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools import check_package_contracts


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class ProjectPackageContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()

    def load(self, name: str) -> dict[str, object]:
        return json.loads((FIXTURE_ROOT / name).read_text(encoding="utf-8"))

    def validate(self, name: str) -> list[check_package_contracts.Diagnostic]:
        return check_package_contracts.validate_manifest_file(
            FIXTURE_ROOT / name,
            self.validators,
        )

    def test_valid_project_manifests_are_accepted(self) -> None:
        self.assertEqual([], self.validate("valid-empty-project.json"))
        self.assertEqual([], self.validate("valid-project.json"))

    def test_valid_feature_sets_and_unresolved_external_members_are_accepted(self) -> None:
        self.assertEqual([], self.validate("valid-feature-set.json"))
        self.assertEqual([], self.validate("valid-nested-feature-set.json"))

    def test_project_schema_rejects_complex_option_values_and_unknown_fields(self) -> None:
        manifest = self.load("valid-project.json")
        manifest["packageOptions"][0]["values"][0]["value"] = {"enabled": True}
        manifest["source"] = "local"

        diagnostics = check_package_contracts.validate_manifest_data(
            manifest,
            "asharia.packages.json",
            self.validators,
        )

        self.assertTrue(diagnostics)
        self.assertEqual({"project.manifest.schema"}, {item.code for item in diagnostics})

    def test_project_duplicate_and_cross_kind_identities_are_rejected(self) -> None:
        manifest = self.load("valid-project.json")
        manifest["directPackages"].append(copy.deepcopy(manifest["directPackages"][0]))
        manifest["directFeatureSets"].append(
            {
                "id": manifest["directPackages"][0]["id"],
                "version": {"kind": "exact", "version": "0.1.0"},
            }
        )
        manifest["packageOptions"][0]["values"].append(
            copy.deepcopy(manifest["packageOptions"][0]["values"][0])
        )
        manifest["packageOptions"].append(copy.deepcopy(manifest["packageOptions"][0]))

        diagnostics = check_package_contracts.validate_manifest_data(
            manifest,
            "asharia.packages.json",
            self.validators,
        )

        self.assertEqual(
            {
                "project.option.duplicate-id",
                "project.option.duplicate-package-id",
                "project.package.duplicate-id",
                "project.selection.cross-kind-id",
            },
            {item.code for item in diagnostics},
        )

    def test_project_invalid_version_range_is_rejected(self) -> None:
        manifest = self.load("valid-project.json")
        constraint = manifest["directPackages"][0]["version"]
        constraint["minimumInclusive"] = constraint["maximumExclusive"]

        diagnostics = check_package_contracts.validate_manifest_data(
            manifest,
            "asharia.packages.json",
            self.validators,
        )

        self.assertEqual(["project.package.invalid-range"], [item.code for item in diagnostics])

    def test_feature_set_membership_rules_are_rejected(self) -> None:
        empty = self.load("valid-feature-set.json")
        empty["packages"] = []
        self.assertEqual(
            ["feature-set.member.empty"],
            [
                item.code
                for item in check_package_contracts.validate_manifest_data(
                    empty,
                    "empty-feature-set.json",
                    self.validators,
                )
            ],
        )

        self_reference = self.load("valid-nested-feature-set.json")
        self_reference["featureSets"][0]["id"] = self_reference["id"]
        self.assertEqual(
            ["feature-set.member.self"],
            [
                item.code
                for item in check_package_contracts.validate_manifest_data(
                    self_reference,
                    "self-feature-set.json",
                    self.validators,
                )
            ],
        )

        cross_kind = self.load("valid-feature-set.json")
        cross_kind["featureSets"].append(
            {
                "id": cross_kind["packages"][0]["id"],
                "version": {"kind": "exact", "version": "0.1.0"},
            }
        )
        self.assertEqual(
            ["feature-set.member.cross-kind-id"],
            [
                item.code
                for item in check_package_contracts.validate_manifest_data(
                    cross_kind,
                    "cross-kind-feature-set.json",
                    self.validators,
                )
            ],
        )

    def test_selected_feature_set_graph_cycles_are_rejected(self) -> None:
        diagnostics = check_package_contracts.validate_manifest_files(
            [FIXTURE_ROOT / "feature-set-cycle-a.json", FIXTURE_ROOT / "feature-set-cycle-b.json"],
            self.validators,
        )

        self.assertEqual(["feature-set.member.cycle"], [item.code for item in diagnostics])
        self.assertIn(
            "com.asharia.features.cycle-a -> com.asharia.features.cycle-b -> "
            "com.asharia.features.cycle-a",
            diagnostics[0].message,
        )

    def test_dispatcher_isolates_all_author_contract_kinds(self) -> None:
        self.assertEqual([], self.validate("valid-system.json"))
        self.assertEqual([], self.validate("valid-feature-set.json"))
        self.assertEqual([], self.validate("valid-project.json"))
        self.assertEqual([], self.validate("valid-host-profile-runtime.json"))

        source_boundary = {"schemaVersion": 1, "packageKind": "source-boundary"}
        self.assertEqual(
            [],
            check_package_contracts.validate_manifest_data(
                source_boundary,
                "asharia.package.json",
                self.validators,
            ),
        )
        self.assertEqual(
            ["contract.manifest.unsupported-kind"],
            [
                item.code
                for item in check_package_contracts.validate_manifest_data(
                    {"schemaVersion": 2, "packageKind": "artifact"},
                    "unknown.json",
                    self.validators,
                )
            ],
        )

    def test_discovery_routes_source_boundaries_away_from_author_contracts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifests = {
                "source/asharia.package.json": {
                    "schemaVersion": 1,
                    "packageKind": "source-boundary",
                },
                "installable/asharia.package.json": self.load("valid-system.json"),
                "feature-set/asharia.package.json": self.load("valid-feature-set.json"),
                "project/asharia.packages.json": self.load("valid-empty-project.json"),
                "project/asharia.packages.lock.json": self.load("valid-empty-lock.json"),
                "profiles/asharia.host-profile.json": self.load(
                    "valid-host-profile-runtime.json"
                ),
            }
            for relative_path, manifest in manifests.items():
                path = root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(manifest), encoding="utf-8")

            discovered = [
                path.relative_to(root).as_posix()
                for path in check_package_contracts.discover_contract_manifests(root)
            ]

        self.assertEqual(
            [
                "feature-set/asharia.package.json",
                "installable/asharia.package.json",
                "profiles/asharia.host-profile.json",
                "project/asharia.packages.json",
                "project/asharia.packages.lock.json",
            ],
            discovered,
        )

    def test_normalized_writer_is_order_and_whitespace_independent(self) -> None:
        manifest = self.load("valid-project.json")
        original = copy.deepcopy(manifest)
        reordered = copy.deepcopy(manifest)
        reordered["directPackages"].reverse()
        reordered["packageOptions"].reverse()
        next(
            item
            for item in reordered["packageOptions"]
            if item["packageId"] == "com.asharia.system.rendering-vulkan"
        )["values"].reverse()

        first = check_package_contracts.render_normalized_project_manifest(manifest)
        second = check_package_contracts.render_normalized_project_manifest(reordered)

        self.assertEqual(first, second)
        self.assertEqual(original, manifest)
        self.assertEqual(first, check_package_contracts.render_normalized_project_manifest(json.loads(first)))
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\r", first)

    def test_normalized_writer_uses_utf8_without_bom(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "asharia.packages.json"
            check_package_contracts.write_normalized_project_manifest(
                output,
                self.load("valid-project.json"),
            )
            data = output.read_bytes()

        self.assertFalse(data.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\r\n", data)


if __name__ == "__main__":
    unittest.main()
