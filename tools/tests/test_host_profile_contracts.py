"""Host Profile v1 contract and deterministic logical projection tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools import check_package_contracts


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
PRODUCTION_EDITOR_PROFILE = (
    Path(__file__).parents[2]
    / "apps/studio/Distribution/profiles/editor/asharia.host-profile.json"
)
PROFILE_NAMES = (
    "minimal",
    "editor",
    "runtime",
    "dedicated-server",
    "asset-worker",
)


class HostProfileContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()

    def load(self, name: str) -> dict[str, object]:
        return json.loads((FIXTURE_ROOT / name).read_text(encoding="utf-8"))

    def profile(self, host_kind: str) -> dict[str, object]:
        return self.load(f"valid-host-profile-{host_kind}.json")

    def select(
        self,
        host_kind: str,
        package: dict[str, object] | None = None,
        profile: dict[str, object] | None = None,
    ) -> tuple[
        check_package_contracts.HostProjection | None,
        list[check_package_contracts.Diagnostic],
    ]:
        return check_package_contracts.select_host_profile_data(
            self.load("valid-host-lock.json"),
            [package or self.load("valid-host-package.json")],
            profile or self.profile(host_kind),
            self.validators,
        )

    def test_all_standard_profiles_are_accepted(self) -> None:
        for host_kind in PROFILE_NAMES:
            with self.subTest(host_kind=host_kind):
                path = FIXTURE_ROOT / f"valid-host-profile-{host_kind}.json"
                profile = self.profile(host_kind)
                self.assertEqual(
                    [],
                    check_package_contracts.validate_manifest_data(
                        profile,
                        check_package_contracts.HOST_PROFILE_NAME,
                        self.validators,
                    ),
                )
                self.assertEqual(
                    path.read_text(encoding="utf-8"),
                    check_package_contracts.render_normalized_host_profile(profile),
                )

    def test_production_editor_profile_matches_the_canonical_contract_oracle(self) -> None:
        production_bytes = PRODUCTION_EDITOR_PROFILE.read_bytes()
        fixture_bytes = (
            FIXTURE_ROOT / "valid-host-profile-editor.json"
        ).read_bytes()
        profile = json.loads(production_bytes.decode("utf-8"))

        self.assertEqual(fixture_bytes, production_bytes)
        self.assertEqual(
            [],
            check_package_contracts.validate_manifest_data(
                profile,
                check_package_contracts.HOST_PROFILE_NAME,
                self.validators,
            ),
        )
        self.assertEqual(
            production_bytes,
            check_package_contracts.render_normalized_host_profile(profile).encode(
                "utf-8"
            ),
        )

    def test_schema_is_closed_and_requires_a_concrete_target_platform(self) -> None:
        profile = self.profile("runtime")
        profile["targetPlatform"] = "com.asharia.platform.any"
        profile["packageIds"] = []

        diagnostics = check_package_contracts.validate_manifest_data(
            profile,
            check_package_contracts.HOST_PROFILE_NAME,
            self.validators,
        )

        self.assertTrue(diagnostics)
        self.assertEqual({"host-profile.manifest.schema"}, {item.code for item in diagnostics})

    def test_standard_profile_policy_cannot_drift(self) -> None:
        profile = self.profile("runtime")
        profile["requiredRoles"].remove("content")
        profile["allowedRoles"].append("editor")
        profile["allowedShippingClasses"].append("editor")
        profile["contributionFilter"]["mode"] = "deny-all"

        diagnostics = check_package_contracts.validate_manifest_data(
            profile,
            check_package_contracts.HOST_PROFILE_NAME,
            self.validators,
        )

        self.assertEqual(
            {
                "host.profile.allowed-roles-mismatch",
                "host.profile.contribution-policy-mismatch",
                "host.profile.required-roles-mismatch",
                "host.profile.shipping-policy-mismatch",
            },
            {item.code for item in diagnostics},
        )

    def test_normalized_profile_is_order_and_whitespace_independent(self) -> None:
        profile = self.profile("editor")
        reordered = copy.deepcopy(profile)
        reordered["requiredRoles"].reverse()
        reordered["allowedRoles"].reverse()
        reordered["allowedShippingClasses"].reverse()
        profile["grantedCapabilities"] = [
            "com.asharia.capability.process-launch",
            "com.asharia.capability.filesystem-read",
        ]
        reordered["grantedCapabilities"] = list(reversed(profile["grantedCapabilities"]))

        first = check_package_contracts.render_normalized_host_profile(profile)
        second = check_package_contracts.render_normalized_host_profile(reordered)

        self.assertEqual(first, second)
        self.assertEqual(
            first,
            check_package_contracts.render_normalized_host_profile(json.loads(first)),
        )
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\r", first)

    def test_normalized_writer_uses_utf8_without_bom(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / check_package_contracts.HOST_PROFILE_NAME
            check_package_contracts.write_normalized_host_profile(
                output,
                self.profile("asset-worker"),
            )
            data = output.read_bytes()

        self.assertFalse(data.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\r\n", data)

    def test_profiles_select_expected_modules_and_contributions(self) -> None:
        expected_modules = {
            "minimal": {"contract"},
            "editor": {
                "contract",
                "runtime",
                "implementation",
                "editor",
                "diagnostics",
                "content",
            },
            "runtime": {"contract", "runtime", "implementation", "content"},
            "dedicated-server": {"contract", "runtime", "implementation", "content"},
            "asset-worker": {"contract", "tool", "cook", "diagnostics", "content"},
        }
        expected_contributions = {
            "minimal": set(),
            "editor": {
                "com.asharia.contribution.synthetic-editor",
                "com.asharia.contribution.synthetic-runtime",
            },
            "runtime": {"com.asharia.contribution.synthetic-runtime"},
            "dedicated-server": {"com.asharia.contribution.synthetic-runtime"},
            "asset-worker": {"com.asharia.contribution.synthetic-asset"},
        }

        for host_kind in PROFILE_NAMES:
            with self.subTest(host_kind=host_kind):
                projection, diagnostics = self.select(host_kind)
                self.assertEqual([], diagnostics)
                self.assertIsNotNone(projection)
                assert projection is not None
                self.assertEqual(
                    expected_modules[host_kind],
                    {selection.module_id for selection in projection.modules},
                )
                self.assertEqual(
                    expected_contributions[host_kind],
                    {
                        selection.contribution_id
                        for selection in projection.contributions
                    },
                )

    def test_projection_is_deterministic_under_manifest_array_reordering(self) -> None:
        package = self.load("valid-host-package.json")
        reordered = copy.deepcopy(package)
        reordered["modules"].reverse()
        reordered["contributions"].reverse()
        for module in reordered["modules"]:
            module["dependsOn"].reverse()

        first, first_diagnostics = self.select("editor", package=package)
        second, second_diagnostics = self.select("editor", package=reordered)

        self.assertEqual([], first_diagnostics)
        self.assertEqual([], second_diagnostics)
        self.assertEqual(first, second)

    def test_required_module_capability_must_be_explicitly_granted(self) -> None:
        package = self.load("valid-host-package.json")
        runtime_module = next(module for module in package["modules"] if module["id"] == "runtime")
        runtime_module["requiredCapabilities"] = [
            "com.asharia.capability.synthetic-runtime"
        ]

        projection, diagnostics = self.select("runtime", package=package)

        self.assertIsNone(projection)
        self.assertEqual(["host.module.capability-denied"], [item.code for item in diagnostics])

        profile = self.profile("runtime")
        profile["grantedCapabilities"] = ["com.asharia.capability.synthetic-runtime"]
        projection, diagnostics = self.select("runtime", package=package, profile=profile)
        self.assertEqual([], diagnostics)
        self.assertIsNotNone(projection)

    def test_asset_worker_rejects_editor_shipping_dependency_closure(self) -> None:
        package = self.load("valid-host-package.json")
        package["modules"].append(
            {
                "id": "editor-support",
                "role": "editor",
                "dependsOn": [],
                "hostKinds": ["editor", "asset-worker"],
                "platforms": ["com.asharia.platform.any"],
                "shippingClass": "editor",
                "requiredCapabilities": [],
            }
        )
        diagnostics_module = next(
            module for module in package["modules"] if module["id"] == "diagnostics"
        )
        diagnostics_module["dependsOn"].append("editor-support")

        projection, diagnostics = self.select("asset-worker", package=package)

        self.assertIsNone(projection)
        self.assertEqual(
            {"host.module.role-closure", "host.module.shipping-closure"},
            {item.code for item in diagnostics},
        )

    def test_platform_dependency_mismatch_is_rejected_before_projection(self) -> None:
        package = self.load("valid-host-package.json")
        tool_module = next(module for module in package["modules"] if module["id"] == "tool")
        cook_module = next(module for module in package["modules"] if module["id"] == "cook")
        tool_module["platforms"] = ["com.asharia.platform.windows"]
        cook_module["platforms"] = ["com.asharia.platform.linux"]

        projection, diagnostics = self.select("asset-worker", package=package)

        self.assertIsNone(projection)
        self.assertEqual(["package.module.platform-closure"], [item.code for item in diagnostics])

    def test_missing_and_mismatched_pinned_manifests_are_rejected(self) -> None:
        lock = self.load("valid-host-lock.json")
        profile = self.profile("runtime")

        projection, diagnostics = check_package_contracts.select_host_profile_data(
            lock,
            [],
            profile,
            self.validators,
        )
        self.assertIsNone(projection)
        self.assertEqual(["host.package.missing-manifest"], [item.code for item in diagnostics])

        package = self.load("valid-host-package.json")
        package["version"] = "0.2.0"
        projection, diagnostics = check_package_contracts.select_host_profile_data(
            lock,
            [package],
            profile,
            self.validators,
        )
        self.assertIsNone(projection)
        self.assertEqual(["host.package.version-mismatch"], [item.code for item in diagnostics])

    def test_projection_diagnostics_are_deterministic(self) -> None:
        package = self.load("valid-host-package.json")
        runtime_module = next(module for module in package["modules"] if module["id"] == "runtime")
        runtime_module["requiredCapabilities"] = [
            "com.asharia.capability.zeta",
            "com.asharia.capability.alpha",
        ]

        _, first = self.select("runtime", package=package)
        _, second = self.select("runtime", package=package)

        self.assertEqual([item.render() for item in first], [item.render() for item in second])
        self.assertEqual(
            [item.render() for item in first],
            sorted(item.render() for item in first),
        )


if __name__ == "__main__":
    unittest.main()
