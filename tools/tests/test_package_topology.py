from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools import check_package_topology


class PackageTopologyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_package(
        self,
        relative_path: str,
        name: str,
        *,
        dependencies: list[str] | None = None,
        cmake_target: str | None = None,
        selectable: bool = False,
        catalog_visible: bool = False,
    ) -> Path:
        package_root = self.root / relative_path
        package_root.mkdir(parents=True)
        target = f"{name.rsplit('.', maxsplit=1)[-1]}-runtime"
        manifest = {
            "schemaVersion": 1,
            "packageKind": "source-boundary",
            "sourceRole": "module-group",
            "ownerDomain": "foundation",
            "plannedOwnershipRoot": "com.asharia.system.foundation-test",
            "selectable": selectable,
            "catalogVisible": catalog_visible,
            "name": name,
            "version": "0.1.0",
            "displayName": name,
            "description": "fixture",
            "dependencies": dependencies or [],
            "targets": [target],
            "targetRoles": {target: "runtime"},
            "targetDependencies": {target: []},
        }
        (package_root / "asharia.package.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        actual_target = cmake_target or target
        (package_root / "CMakeLists.txt").write_text(
            f"add_library({actual_target} STATIC source.cpp)\n", encoding="utf-8"
        )
        return package_root

    def inspect(self) -> tuple[dict[str, object], list[str]]:
        return check_package_topology.inspect_repository(self.root)

    def test_valid_source_boundary_produces_inventory(self) -> None:
        self.write_package("packages/example", "com.asharia.example")

        inventory, errors = self.inspect()

        self.assertEqual([], errors)
        self.assertEqual("com.asharia.source-topology-snapshot", inventory["schema"])
        self.assertEqual(1, inventory["summary"]["packageCount"])
        self.assertEqual("packages/example/asharia.package.json", inventory["packages"][0]["path"])
        self.assertEqual([], inventory["packages"][0]["targets"][0]["dependencies"])

    def test_installable_v2_manifest_is_left_to_the_contract_validator(self) -> None:
        self.write_package("packages/example", "com.asharia.example")
        installable_root = self.root / "packages/installable"
        installable_root.mkdir(parents=True)
        (installable_root / "asharia.package.json").write_text(
            json.dumps(
                {
                    "schemaVersion": 2,
                    "packageKind": "installable-capability",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

        inventory, errors = self.inspect()

        self.assertEqual([], errors)
        self.assertEqual(1, inventory["summary"]["packageCount"])

    def test_unknown_dependency_is_rejected(self) -> None:
        self.write_package(
            "packages/example",
            "com.asharia.example",
            dependencies=["com.asharia.missing"],
        )

        _, errors = self.inspect()

        self.assertTrue(any("unknown package dependency com.asharia.missing" in item for item in errors))

    def test_dependency_cycle_is_rejected(self) -> None:
        self.write_package("packages/a", "com.asharia.a", dependencies=["com.asharia.b"])
        self.write_package("packages/b", "com.asharia.b", dependencies=["com.asharia.a"])

        _, errors = self.inspect()

        self.assertTrue(any("dependency cycle:" in item for item in errors))

    def test_duplicate_identity_is_rejected(self) -> None:
        self.write_package("packages/a", "com.asharia.same")
        self.write_package("packages/b", "com.asharia.same")

        _, errors = self.inspect()

        self.assertTrue(any("duplicate package identity" in item for item in errors))

    def test_undeclared_direct_cmake_target_is_rejected(self) -> None:
        self.write_package(
            "packages/example",
            "com.asharia.example",
            cmake_target="unexpected-target",
        )

        _, errors = self.inspect()

        self.assertTrue(any("direct CMake targets are not declared" in item for item in errors))

    def test_source_boundary_cannot_be_catalog_visible_or_selectable(self) -> None:
        self.write_package(
            "packages/example",
            "com.asharia.example",
            selectable=True,
            catalog_visible=True,
        )

        _, errors = self.inspect()

        self.assertTrue(any("selectable to false" in item for item in errors))
        self.assertTrue(any("catalogVisible to false" in item for item in errors))

    def test_wrong_field_types_are_reported_without_crashing(self) -> None:
        package_root = self.write_package("packages/example", "com.asharia.example")
        manifest_path = package_root / "asharia.package.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        target = manifest["targets"][0]
        manifest["ownerDomain"] = []
        manifest["plannedOwnershipRoot"] = {}
        manifest["targetRoles"][target] = []
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

        _, errors = self.inspect()

        self.assertTrue(any("ownerDomain must be one of" in item for item in errors))
        self.assertTrue(any("plannedOwnershipRoot must be a non-empty string" in item for item in errors))
        self.assertTrue(any("target role for" in item for item in errors))


if __name__ == "__main__":
    unittest.main()
