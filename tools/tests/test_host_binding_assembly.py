"""Focused pure Host executable binding assembly tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tools import check_package_contracts as contracts
from tools import host_artifact_collection
from tools import host_binding_assembly
from tools import host_cmake_target
from tools import host_cmake_toolchain
from tools import host_executable_binding
from tools import host_executable_template
from tools import host_registration_snapshot
from tools.tests import host_template_test_support as support


class HostBindingAssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.build_root = self.root / "build"
        self.staging_root = self.root / "staging"
        self.build_root.mkdir()
        self.staging_root.mkdir()
        self.composition = support.composition_generation(self.validators)
        template = (
            host_executable_template.generate_windows_development_host_template(
                self.composition.manifest,
                "asharia-generated-host",
                self.validators,
            )
        )
        assert template.generation is not None
        self.template = template.generation
        self.artifact_path = self.build_root / "bin/Debug/host.exe"
        self.artifact_path.parent.mkdir(parents=True)
        self.artifact_path.write_bytes(b"synthetic-host-executable")
        self.target = host_cmake_target.HostCMakeTargetEvidence(
            build_root=self.build_root.resolve(),
            reply_index_path=self.build_root / "reply/index-001.json",
            configuration="Debug",
            target_name="asharia-generated-host",
            target_type="EXECUTABLE",
            name_on_disk="host.exe",
            artifact_relative_path="bin/Debug/host.exe",
            artifact_path=self.artifact_path.resolve(),
            codemodel_major=2,
            codemodel_minor=6,
        )
        self.configured = host_cmake_toolchain.HostCMakeConfiguredTargetEvidence(
            build_root=self.build_root.resolve(),
            reply_index_path=self.target.reply_index_path,
            generator=host_cmake_toolchain.HostCMakeGeneratorEvidence(
                self.composition.manifest.generator_name,
                self.composition.manifest.generator_multi_config,
            ),
            target=self.target,
            configured_compiler=(
                host_cmake_toolchain.ConfiguredCxxCompilerEvidence(
                    "CXX",
                    self.composition.manifest.compiler_id,
                    self.composition.manifest.compiler_version,
                    None,
                )
            ),
            toolchains_major=1,
            toolchains_minor=0,
        )
        collected = host_artifact_collection.collect_host_artifact(
            self.target, self.staging_root
        )
        assert collected.artifact is not None
        self.artifact = collected.artifact
        provider = self.composition.manifest.providers[0]
        self.snapshot = host_registration_snapshot.HostRegistrationSnapshot(
            self.composition.manifest.generation_id,
            self.composition.manifest.inputs.host_activation_blueprint_integrity.digest,
            tuple(
                host_registration_snapshot.StaticFactoryRegistration(
                    provider.package_id,
                    provider.package_version,
                    provider.module_id,
                    factory_id,
                    provider.entry_point.function,
                )
                for factory_id in provider.factory_ids
            ),
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def assemble(
        self,
        *,
        configured: object | None = None,
        snapshot: object | None = None,
    ) -> host_binding_assembly.HostExecutableBindingAssemblyResult:
        return host_binding_assembly.assemble_host_executable_binding(
            self.composition,
            self.template,
            self.configured if configured is None else configured,
            self.artifact,
            self.snapshot if snapshot is None else snapshot,
            self.validators,
        )

    def test_assembles_exact_receipt_and_canonical_snapshot(self) -> None:
        result = self.assemble()

        self.assertTrue(result.succeeded, result.diagnostics)
        assert result.assembly is not None
        receipt = result.assembly.receipt
        self.assertEqual(
            self.artifact.integrity.digest,
            receipt.artifact.integrity.digest,
        )
        self.assertEqual(
            host_executable_binding.HOST_REGISTRATION_SNAPSHOT_PATH,
            receipt.registration_snapshot.path,
        )
        parsed = host_registration_snapshot.parse_host_registration_snapshot_bytes(
            result.assembly.snapshot_bytes,
            self.validators,
            expected_generation_id=self.composition.manifest.generation_id,
            expected_host_activation_blueprint_sha256=(
                self.composition.manifest.inputs.host_activation_blueprint_integrity.digest
            ),
        )
        self.assertTrue(parsed.succeeded, parsed.diagnostics)

    def test_equivalent_evidence_produces_identical_receipt_bytes(self) -> None:
        first = self.assemble()
        second = self.assemble()
        assert first.assembly is not None
        assert second.assembly is not None

        self.assertEqual(
            host_executable_binding.render_host_executable_binding_receipt(
                first.assembly.receipt
            ),
            host_executable_binding.render_host_executable_binding_receipt(
                second.assembly.receipt
            ),
        )

    def test_configured_compiler_mismatch_fails_closed(self) -> None:
        wrong_compiler = replace(
            self.configured.configured_compiler,
            compiler_id="MSVC",
        )

        result = self.assemble(
            configured=replace(
                self.configured,
                configured_compiler=wrong_compiler,
            )
        )

        self.assertIsNone(result.assembly)
        self.assertEqual(
            ["host-binding.cmake.compiler-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_target_and_template_mismatch_fails_closed(self) -> None:
        wrong_target = replace(self.target, target_name="other-host")

        result = self.assemble(
            configured=replace(self.configured, target=wrong_target)
        )

        self.assertIsNone(result.assembly)
        self.assertEqual(
            ["host-binding.cmake.target-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_registration_mismatch_propagates_without_partial_receipt(self) -> None:
        result = self.assemble(
            snapshot=replace(self.snapshot, registrations=())
        )

        self.assertIsNone(result.assembly)
        self.assertEqual(
            ["host-binding.registration.missing"],
            [item.code for item in result.diagnostics],
        )


if __name__ == "__main__":
    unittest.main()
