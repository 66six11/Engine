from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools import check_package_contracts


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class PackageContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validator = check_package_contracts.load_schema_validator()

    def validate(self, name: str) -> list[check_package_contracts.Diagnostic]:
        return check_package_contracts.validate_manifest_file(FIXTURE_ROOT / name, self.validator)

    def test_valid_portable_manifests_are_accepted(self) -> None:
        self.assertEqual([], self.validate("valid-system.json"))
        self.assertEqual([], self.validate("valid-integration.json"))

    def test_semver_accepts_alphanumeric_prerelease_starting_with_a_digit(self) -> None:
        manifest = json.loads((FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8"))
        manifest["version"] = "1.0.0-1alpha"

        diagnostics = check_package_contracts.validate_manifest_data(
            manifest,
            "synthetic-semver.json",
            self.validator,
        )

        self.assertEqual([], diagnostics)

    def test_build_system_fields_are_rejected_by_the_closed_schema(self) -> None:
        diagnostics = self.validate("invalid-schema.json")

        self.assertEqual(["package.manifest.schema"], [item.code for item in diagnostics])
        self.assertIn("sourceBoundaries", diagnostics[0].message)

    def test_missing_fields_invalid_roles_and_malformed_capabilities_are_rejected(self) -> None:
        mutations = {
            "missing-field": lambda manifest: manifest.pop("description"),
            "invalid-role": lambda manifest: manifest["modules"][0].update({"role": "backend"}),
            "malformed-capability": lambda manifest: manifest["modules"][0].update(
                {"requiredCapabilities": ["filesystem.read"]}
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                manifest = json.loads((FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8"))
                mutate(manifest)

                diagnostics = check_package_contracts.validate_manifest_data(
                    manifest,
                    f"synthetic-{name}.json",
                    self.validator,
                )

                self.assertTrue(diagnostics)
                self.assertEqual({"package.manifest.schema"}, {item.code for item in diagnostics})

    def test_duplicate_logical_module_identity_is_rejected(self) -> None:
        manifest = json.loads((FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8"))
        manifest["modules"].append(dict(manifest["modules"][0]))

        diagnostics = check_package_contracts.validate_manifest_data(
            manifest,
            "synthetic-duplicate-module.json",
            self.validator,
        )

        self.assertEqual(["package.module.duplicate-id"], [item.code for item in diagnostics])

    def test_unresolved_local_references_are_rejected(self) -> None:
        diagnostics = self.validate("invalid-references.json")

        self.assertEqual(
            [
                "package.contribution.unknown-module",
                "package.entry.unknown-module",
                "package.module.unknown-dependency",
            ],
            [item.code for item in diagnostics],
        )

    def test_module_cycles_are_rejected(self) -> None:
        diagnostics = self.validate("invalid-cycle.json")

        self.assertEqual(["package.module.cycle"], [item.code for item in diagnostics])
        self.assertIn("runtime -> diagnostics -> runtime", diagnostics[0].message)

    def test_module_shipping_and_host_closure_are_rejected(self) -> None:
        diagnostics = self.validate("invalid-module-closure.json")

        self.assertEqual(
            [
                "package.module.host-closure",
                "package.module.platform-closure",
                "package.module.shipping-closure",
            ],
            [item.code for item in diagnostics],
        )

    def test_platform_any_cannot_be_combined_with_specific_platforms(self) -> None:
        manifest = json.loads((FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8"))
        manifest["modules"][0]["platforms"].append("com.asharia.platform.windows")

        diagnostics = check_package_contracts.validate_manifest_data(
            manifest,
            "synthetic-platform-any.json",
            self.validator,
        )

        self.assertEqual(
            ["package.platform.invalid-any-combination"],
            [item.code for item in diagnostics],
        )

    def test_module_and_contribution_shipping_metadata_must_match_their_owners(self) -> None:
        system = json.loads((FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8"))
        system["modules"][0]["shippingClass"] = "editor"

        module_diagnostics = check_package_contracts.validate_manifest_data(
            system,
            "synthetic-module-shipping.json",
            self.validator,
        )

        self.assertEqual(
            ["package.module.invalid-shipping-class"],
            [item.code for item in module_diagnostics],
        )

        integration = json.loads((FIXTURE_ROOT / "valid-integration.json").read_text(encoding="utf-8"))
        integration["contributions"][0]["shippingClass"] = "editor"
        integration["contributions"][0]["hostKinds"] = ["dedicated-server"]

        contribution_diagnostics = check_package_contracts.validate_manifest_data(
            integration,
            "synthetic-contribution-shipping.json",
            self.validator,
        )

        self.assertEqual(
            [
                "package.contribution.host-mismatch",
                "package.contribution.shipping-mismatch",
            ],
            [item.code for item in contribution_diagnostics],
        )

    def test_cross_field_values_are_rejected(self) -> None:
        diagnostics = self.validate("invalid-values.json")

        self.assertEqual(
            [
                "package.content-root.invalid-path",
                "package.version.invalid-range",
                "package.option.default-not-allowed",
            ],
            [item.code for item in diagnostics],
        )

    def test_catalog_type_policy_is_derived_from_declared_structure(self) -> None:
        diagnostics = self.validate("invalid-integration.json")

        self.assertEqual(
            [
                "package.catalog.integration-missing-contribution",
                "package.catalog.integration-missing-dependencies",
            ],
            [item.code for item in diagnostics],
        )

    def test_diagnostics_are_deterministic(self) -> None:
        first = [item.render() for item in self.validate("invalid-references.json")]
        second = [item.render() for item in self.validate("invalid-references.json")]

        self.assertEqual(first, second)
        self.assertEqual(first, sorted(first))


if __name__ == "__main__":
    unittest.main()
