"""Pure Host executable binding receipt v1 contract tests."""

from __future__ import annotations

import json
import unittest
from dataclasses import replace

from tools import check_package_contracts as contracts
from tools import host_executable_binding as binding


def _integrity(marker: str) -> binding.IntegrityRecord:
    return binding.IntegrityRecord("sha256", marker * 64)


class HostExecutableBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def receipt(
        self,
        *,
        build: binding.HostBuildIdentity | None = None,
        target: binding.HostTargetEvidence | None = None,
        artifact: binding.BoundFileEvidence | None = None,
        snapshot: binding.BoundFileEvidence | None = None,
    ) -> binding.HostExecutableBindingReceiptV1:
        target = target or binding.HostTargetEvidence(
            name="asharia-generated-host",
            target_type="EXECUTABLE",
            name_on_disk="asharia-generated-host.exe",
            build_artifact_relative_path=(
                "asharia-host/bin/Debug/asharia-generated-host.exe"
            ),
            codemodel_major=2,
            codemodel_minor=6,
            toolchains_major=1,
            toolchains_minor=0,
        )
        return binding.create_host_executable_binding_receipt(
            inputs=binding.HostExecutableBindingInputs(
                static_composition=binding.GeneratedInputIdentity(
                    "sha256-" + "1" * 64, _integrity("2")
                ),
                host_template=binding.GeneratedInputIdentity(
                    "sha256-" + "3" * 64, _integrity("4")
                ),
                host_activation_blueprint_integrity=_integrity("5"),
            ),
            host=binding.HostIdentity(
                "sha256-" + "6" * 64,
                "editor",
                "com.asharia.platform.windows",
            ),
            build=build
            or binding.HostBuildIdentity(
                configuration="Debug",
                generator=binding.BuildGeneratorEvidence("Ninja", False),
                configured_compiler=binding.ConfiguredCompilerEvidence(
                    "CXX", "MSVC", "19.44.35217.0", None
                ),
            ),
            target=target,
            artifact=artifact
            or binding.BoundFileEvidence(
                "host/asharia-generated-host.exe",
                binding.HOST_EXECUTABLE_MEDIA_TYPE,
                4096,
                _integrity("7"),
            ),
            registration_snapshot=snapshot
            or binding.BoundFileEvidence(
                binding.HOST_REGISTRATION_SNAPSHOT_PATH,
                binding.HOST_REGISTRATION_SNAPSHOT_MEDIA_TYPE,
                512,
                _integrity("8"),
            ),
        )

    def canonical_bytes(
        self, receipt: binding.HostExecutableBindingReceiptV1 | None = None
    ) -> bytes:
        return binding.render_host_executable_binding_receipt(
            self.receipt() if receipt is None else receipt
        ).encode("utf-8")

    def test_canonical_receipt_round_trips_with_fixed_field_order(self) -> None:
        receipt = self.receipt()
        content = self.canonical_bytes(receipt)

        result = binding.parse_host_executable_binding_receipt_bytes(
            content, self.validators
        )

        self.assertTrue(
            result.succeeded,
            [diagnostic.render() for diagnostic in result.diagnostics],
        )
        self.assertEqual(receipt, result.receipt)
        self.assertTrue(content.endswith(b"\n"))
        self.assertNotIn(b"\r", content)
        data = json.loads(content.decode("utf-8"))
        self.assertEqual(
            [
                "schema",
                "schemaVersion",
                "bindingGenerationId",
                "inputs",
                "host",
                "build",
                "target",
                "artifact",
                "registrationSnapshot",
                "integrity",
            ],
            list(data),
        )
        self.assertEqual(
            ["configuration", "generator", "configuredCompiler"],
            list(data["build"]),
        )
        self.assertEqual(
            ["language", "compilerId", "compilerVersion"],
            list(data["build"]["configuredCompiler"]),
        )

    def test_equivalent_inputs_repeat_exact_identity_and_bytes(self) -> None:
        first = self.receipt()
        second = self.receipt()

        self.assertEqual(first, second)
        self.assertEqual(
            first.binding_generation_id, second.binding_generation_id
        )
        self.assertEqual(self.canonical_bytes(first), self.canonical_bytes(second))

    def test_exact_artifact_bytes_change_binding_identity(self) -> None:
        first = self.receipt()
        artifact = replace(first.artifact, integrity=_integrity("9"))
        second = self.receipt(artifact=artifact)

        self.assertNotEqual(
            first.binding_generation_id, second.binding_generation_id
        )
        self.assertNotEqual(first.integrity, second.integrity)

    def test_optional_configured_compiler_target_is_exactly_bound(self) -> None:
        first = self.receipt()
        compiler = replace(
            first.build.configured_compiler,
            target="x86_64-pc-windows-msvc",
        )
        second = self.receipt(
            build=replace(first.build, configured_compiler=compiler)
        )

        data = binding.host_executable_binding_receipt_to_data(second)

        self.assertEqual(
            "x86_64-pc-windows-msvc",
            data["build"]["configuredCompiler"]["target"],
        )
        self.assertNotEqual(
            first.binding_generation_id, second.binding_generation_id
        )

    def test_backward_compatible_file_api_minor_is_exactly_recorded(self) -> None:
        first = self.receipt()
        second = self.receipt(
            target=replace(
                first.target,
                codemodel_minor=7,
                toolchains_minor=1,
            )
        )

        diagnostics = binding.validate_host_executable_binding_receipt_data(
            second, self.validators
        )

        self.assertEqual([], diagnostics)
        self.assertNotEqual(
            first.binding_generation_id, second.binding_generation_id
        )

    def test_canonical_json_variants_are_rejected_atomically(self) -> None:
        canonical = self.canonical_bytes()
        data = json.loads(canonical.decode("utf-8"))
        variants = {
            "missing-final-lf": canonical.removesuffix(b"\n"),
            "crlf": canonical.replace(b"\n", b"\r\n"),
            "compact": json.dumps(
                data, ensure_ascii=False, separators=(",", ":")
            ).encode("utf-8"),
            "sorted-fields": (
                json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
            ).encode("utf-8"),
        }
        for name, content in variants.items():
            with self.subTest(name=name):
                result = binding.parse_host_executable_binding_receipt_bytes(
                    content, self.validators
                )

                self.assertIsNone(result.receipt)
                self.assertEqual(
                    ["host-binding.canonical-bytes-mismatch"],
                    [diagnostic.code for diagnostic in result.diagnostics],
                )

    def test_tampered_artifact_digest_fails_identity_and_integrity(self) -> None:
        data = binding.host_executable_binding_receipt_to_data(self.receipt())
        data["artifact"]["integrity"]["digest"] = "a" * 64
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = binding.parse_host_executable_binding_receipt_bytes(
            content, self.validators
        )

        self.assertIsNone(result.receipt)
        self.assertEqual(
            {
                "host-binding.generation-id-mismatch",
                "host-binding.integrity-mismatch",
            },
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_tampered_self_integrity_fails_closed(self) -> None:
        data = binding.host_executable_binding_receipt_to_data(self.receipt())
        data["integrity"]["digest"] = "b" * 64
        content = (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
            "utf-8"
        )

        result = binding.parse_host_executable_binding_receipt_bytes(
            content, self.validators
        )

        self.assertIsNone(result.receipt)
        self.assertEqual(
            ["host-binding.integrity-mismatch"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_closed_schema_rejects_lifecycle_time_and_absolute_paths(self) -> None:
        mutations = {
            "lifecycle": lambda data: data.update({"activated": True}),
            "time": lambda data: data.update({"createdAt": "2026-07-15T00:00:00Z"}),
            "absolute-build-path": lambda data: data["target"].update(
                {"buildArtifactRelativePath": "D:/build/host.exe"}
            ),
            "absolute-artifact-path": lambda data: data["artifact"].update(
                {"path": "D:/generation/host.exe"}
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                data = binding.host_executable_binding_receipt_to_data(self.receipt())
                mutate(data)
                diagnostics = binding.validate_host_executable_binding_receipt_data(
                    data, self.validators
                )

                self.assertEqual(
                    {"host-binding.schema"},
                    {diagnostic.code for diagnostic in diagnostics},
                )

    def test_self_consistent_wrong_artifact_names_fail_deep_validation(self) -> None:
        receipt = self.receipt()
        cases = {
            "file-api": self.receipt(
                target=replace(
                    receipt.target,
                    build_artifact_relative_path="bin/not-the-host.exe",
                )
            ),
            "published": self.receipt(
                artifact=replace(receipt.artifact, path="host/not-the-host.exe")
            ),
            "noncanonical-path": self.receipt(
                target=replace(
                    receipt.target,
                    build_artifact_relative_path=(
                        "asharia-host//bin/asharia-generated-host.exe"
                    ),
                )
            ),
        }
        expected = {
            "file-api": "host-binding.target-artifact-mismatch",
            "published": "host-binding.artifact-path-mismatch",
            "noncanonical-path": "host-binding.target-artifact-mismatch",
        }
        for name, value in cases.items():
            with self.subTest(name=name):
                diagnostics = binding.validate_host_executable_binding_receipt_data(
                    value, self.validators
                )

                self.assertEqual(
                    [expected[name]],
                    [diagnostic.code for diagnostic in diagnostics],
                )

    def test_invalid_bytes_json_and_schema_are_atomic(self) -> None:
        cases = {
            "not-bytes": ("{}", "host-binding.bytes-required"),
            "utf8": (b"\xff", "host-binding.utf8-invalid"),
            "json": (b"{broken}\n", "host-binding.json-invalid"),
        }
        for name, (content, expected) in cases.items():
            with self.subTest(name=name):
                result = binding.parse_host_executable_binding_receipt_bytes(
                    content, self.validators
                )

                self.assertIsNone(result.receipt)
                self.assertEqual(
                    [expected], [diagnostic.code for diagnostic in result.diagnostics]
                )


if __name__ == "__main__":
    unittest.main()
