"""Canonical static Host factory-registration snapshot tests."""

from __future__ import annotations

import json
import unittest
from dataclasses import replace
from unittest import mock

from tools import check_package_contracts as contracts
from tools import host_registration_snapshot as registration


GENERATION_ID = "sha256-" + "a" * 64
BLUEPRINT_SHA256 = "b" * 64


class HostRegistrationSnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def registrations(self) -> tuple[registration.StaticFactoryRegistration, ...]:
        return (
            registration.StaticFactoryRegistration(
                package_id="com.asharia.system.alpha",
                package_version="0.1.0",
                module_id="editor",
                factory_id="editor-panel",
                provider_entry_point="asharia::alpha::registerFactories",
                contributions=(
                    registration.StaticContributionRegistration(
                        contribution_id="com.asharia.contribution.alpha",
                        kind="com.asharia.kind.panel",
                        cardinality="single",
                    ),
                    registration.StaticContributionRegistration(
                        contribution_id="com.asharia.contribution.beta",
                        kind="com.asharia.kind.menu",
                        cardinality="multiple",
                    ),
                ),
            ),
            registration.StaticFactoryRegistration(
                package_id="com.asharia.system.zeta",
                package_version="1.2.3",
                module_id="runtime",
                factory_id="runtime-service",
                provider_entry_point="asharia::zeta::registerFactories",
                contributions=(
                    registration.StaticContributionRegistration(
                        contribution_id="com.asharia.contribution.runtime",
                        kind="com.asharia.kind.service",
                        cardinality="single",
                    ),
                ),
            ),
        )

    def snapshot(
        self,
        values: tuple[registration.StaticFactoryRegistration, ...] | None = None,
    ) -> registration.HostRegistrationSnapshot:
        return registration.HostRegistrationSnapshot(
            generation_id=GENERATION_ID,
            host_activation_blueprint_sha256=BLUEPRINT_SHA256,
            registrations=self.registrations() if values is None else values,
        )

    def parse(
        self,
        content: object,
        *,
        generation_id: object = GENERATION_ID,
        blueprint_sha256: object = BLUEPRINT_SHA256,
    ) -> registration.HostRegistrationSnapshotResult:
        return registration.parse_host_registration_snapshot_bytes(
            content,
            self.validators,
            expected_generation_id=generation_id,
            expected_host_activation_blueprint_sha256=blueprint_sha256,
        )

    def canonical_bytes(
        self,
        snapshot: registration.HostRegistrationSnapshot | None = None,
    ) -> bytes:
        return registration.render_host_registration_snapshot(
            self.snapshot() if snapshot is None else snapshot
        ).encode("utf-8")

    def test_exact_canonical_bytes_parse_to_one_owning_snapshot(self) -> None:
        content = self.canonical_bytes()

        result = self.parse(content)

        self.assertTrue(
            result.succeeded,
            [value.render() for value in result.diagnostics],
        )
        self.assertEqual(self.snapshot(), result.snapshot)
        self.assertTrue(content.endswith(b"\n"))
        self.assertNotIn(b"\r", content)
        data = json.loads(content.decode("utf-8"))
        self.assertEqual(
            [
                "schema",
                "schemaVersion",
                "generationId",
                "hostActivationBlueprintSha256",
                "registrations",
            ],
            list(data),
        )
        self.assertEqual(
            2,
            data["schemaVersion"],
        )
        self.assertEqual(
            [
                "packageId",
                "packageVersion",
                "moduleId",
                "factoryId",
                "providerEntryPoint",
                "contributions",
            ],
            list(data["registrations"][0]),
        )
        self.assertEqual(
            ["id", "kind", "cardinality"],
            list(data["registrations"][0]["contributions"][0]),
        )

    def test_renderer_uses_full_utf8_byte_registration_order(self) -> None:
        snapshot = self.snapshot(tuple(reversed(self.registrations())))

        data = registration.host_registration_snapshot_to_data(snapshot)

        self.assertEqual(
            ["com.asharia.system.alpha", "com.asharia.system.zeta"],
            [value["packageId"] for value in data["registrations"]],
        )

    def test_renderer_uses_nested_utf8_byte_contribution_order(self) -> None:
        first = self.registrations()[0]
        reversed_contributions = replace(
            first,
            contributions=tuple(reversed(first.contributions)),
        )
        snapshot = self.snapshot((reversed_contributions,))

        data = registration.host_registration_snapshot_to_data(snapshot)

        self.assertEqual(
            [
                "com.asharia.contribution.alpha",
                "com.asharia.contribution.beta",
            ],
            [
                value["id"]
                for value in data["registrations"][0]["contributions"]
            ],
        )

    def test_parser_rejects_noncanonical_registration_order_atomically(self) -> None:
        data = registration.host_registration_snapshot_to_data(self.snapshot())
        data["registrations"].reverse()
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = self.parse(content)

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["host.registration.order-invalid"],
            [value.code for value in result.diagnostics],
        )

    def test_parser_rejects_noncanonical_contribution_order_atomically(self) -> None:
        data = registration.host_registration_snapshot_to_data(self.snapshot())
        data["registrations"][0]["contributions"].reverse()
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = self.parse(content)

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["host.registration.contribution-order-invalid"],
            [value.code for value in result.diagnostics],
        )

    def test_parser_rejects_conflicting_cardinality_for_one_kind(self) -> None:
        first, second = self.registrations()
        conflicting = replace(
            second,
            contributions=(
                registration.StaticContributionRegistration(
                    contribution_id="com.asharia.contribution.runtime",
                    kind="com.asharia.kind.panel",
                    cardinality="multiple",
                ),
            ),
        )
        content = self.canonical_bytes(self.snapshot((first, conflicting)))

        result = self.parse(content)

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["host.registration.contribution-cardinality-conflict"],
            [value.code for value in result.diagnostics],
        )

    def test_parser_rejects_noncanonical_json_bytes(self) -> None:
        canonical = self.canonical_bytes()
        data = json.loads(canonical.decode("utf-8"))
        mutations = {
            "missing-final-lf": canonical.removesuffix(b"\n"),
            "crlf": canonical.replace(b"\n", b"\r\n"),
            "compact": json.dumps(
                data,
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8"),
            "sorted-fields": (json.dumps(
                data,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            ) + "\n").encode("utf-8"),
        }
        for name, content in mutations.items():
            with self.subTest(name=name):
                result = self.parse(content)

                self.assertIsNone(result.snapshot)
                self.assertEqual(
                    ["host.registration.canonical-bytes-mismatch"],
                    [value.code for value in result.diagnostics],
                )

    def test_closed_schema_rejects_extra_root_and_registration_fields(self) -> None:
        mutations = {
            "root": lambda data: data.update({"artifactSha256": "c" * 64}),
            "registration": lambda data: data["registrations"][0].update(
                {"callback": "forbidden"}
            ),
            "contribution-type-key": lambda data: data["registrations"][0][
                "contributions"
            ][0].update({"typeKey": "forbidden"}),
            "contribution-payload-accessor": lambda data: data["registrations"][0][
                "contributions"
            ][0].update({"payloadAccessor": "forbidden"}),
            "contribution-accessor": lambda data: data["registrations"][0][
                "contributions"
            ][0].update({"accessor": "forbidden"}),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                data = registration.host_registration_snapshot_to_data(self.snapshot())
                mutate(data)
                content = (
                    json.dumps(data, ensure_ascii=False, indent=2) + "\n"
                ).encode("utf-8")

                result = self.parse(content)

                self.assertIsNone(result.snapshot)
                self.assertEqual(
                    {"factory.registration-snapshot.schema"},
                    {value.code for value in result.diagnostics},
                )

    def test_duplicate_registration_is_rejected_without_partial_snapshot(self) -> None:
        data = registration.host_registration_snapshot_to_data(self.snapshot())
        data["registrations"].append(dict(data["registrations"][0]))
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = self.parse(content)

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["factory.registration-snapshot.schema"],
            [value.code for value in result.diagnostics],
        )

    def test_invalid_contribution_cardinality_is_rejected_by_schema(self) -> None:
        data = registration.host_registration_snapshot_to_data(self.snapshot())
        data["registrations"][0]["contributions"][0]["cardinality"] = "many"
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = self.parse(content)

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["factory.registration-snapshot.schema"],
            [value.code for value in result.diagnostics],
        )

    def test_v1_snapshot_is_rejected_by_the_v2_schema(self) -> None:
        data = registration.host_registration_snapshot_to_data(self.snapshot())
        data["schemaVersion"] = 1
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = self.parse(content)

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["factory.registration-snapshot.schema"],
            [value.code for value in result.diagnostics],
        )

    def test_invalid_utf8_and_json_have_stable_diagnostics(self) -> None:
        cases = {
            "utf8": (b"\xff", "host.registration.utf8-invalid"),
            "json": (b"{not-json}\n", "host.registration.json-invalid"),
        }
        for name, (content, expected_code) in cases.items():
            with self.subTest(name=name):
                first = self.parse(content)
                second = self.parse(content)

                self.assertIsNone(first.snapshot)
                self.assertEqual(first.diagnostics, second.diagnostics)
                self.assertEqual(
                    [expected_code],
                    [value.code for value in first.diagnostics],
                )

    def test_expected_generation_and_blueprint_are_exactly_reverified(self) -> None:
        result = self.parse(
            self.canonical_bytes(),
            generation_id="sha256-" + "c" * 64,
            blueprint_sha256="d" * 64,
        )

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            [
                "host.registration.generation-mismatch",
                "host.registration.blueprint-mismatch",
            ],
            [value.code for value in result.diagnostics],
        )

    def test_invalid_expectations_and_non_bytes_fail_before_parsing(self) -> None:
        result = self.parse(
            "not-bytes",
            generation_id="current",
            blueprint_sha256="ABC",
        )

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            {
                "host.registration.bytes-required",
                "host.registration.expected-blueprint-invalid",
                "host.registration.expected-generation-invalid",
            },
            {value.code for value in result.diagnostics},
        )

    def test_empty_registration_snapshot_is_valid_and_does_not_hash(self) -> None:
        snapshot = self.snapshot(())
        content = self.canonical_bytes(snapshot)

        with mock.patch.object(
            contracts,
            "compute_bytes_integrity",
            side_effect=AssertionError("registration handoff must not hash artifacts"),
        ):
            result = self.parse(content)

        self.assertTrue(result.succeeded)
        self.assertEqual(snapshot, result.snapshot)


if __name__ == "__main__":
    unittest.main()
