"""Focused immutable Host executable binding publication tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import host_artifact_io
from tools import host_binding_generation_verifier
from tools import host_binding_publication
from tools import host_cmake_target
from tools import host_cmake_toolchain
from tools import host_executable_binding
from tools import host_executable_template
from tools import host_registration_snapshot
from tools import host_registration_verification
from tools import host_template_publication
from tools import static_composition_root
from tools.tests import host_template_test_support as support


class HostBindingPublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.composition = support.composition_generation(self.validators)
        template_result = (
            host_executable_template.generate_windows_development_host_template(
                self.composition.manifest,
                "asharia-generated-host",
                self.validators,
            )
        )
        assert template_result.generation is not None
        self.template = template_result.generation
        composition_publication = (
            static_composition_root.publish_static_composition_root(
                self.composition,
                self.root / "static-compositions",
                self.validators,
            )
        )
        template_publication = (
            host_template_publication.publish_windows_development_host_template(
                self.template,
                self.root / "host-templates",
                self.validators,
            )
        )
        assert composition_publication.receipt is not None
        assert template_publication.receipt is not None
        self.composition_root = composition_publication.receipt.generation_path
        self.template_root = template_publication.receipt.generation_path

        self.build_root = self.root / "build"
        self.build_root.mkdir()
        self.artifact_path = self.build_root / "bin/Debug/host.exe"
        self.artifact_path.parent.mkdir(parents=True)
        self.artifact_path.write_bytes(b"synthetic-final-host")
        self.reply_index = self.build_root / ".cmake/api/v1/reply/index-001.json"
        self.target = host_cmake_target.HostCMakeTargetEvidence(
            build_root=self.build_root.resolve(),
            reply_index_path=self.reply_index,
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
            reply_index_path=self.reply_index,
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
        self.request = (
            host_binding_publication.HostExecutableBindingPublicationRequestV1(
                composition_generation=self.composition,
                composition_root=self.composition_root,
                template_generation=self.template,
                template_root=self.template_root,
                target=self.target,
                registration_snapshot=self.snapshot,
                environment=(("PATH", "synthetic"),),
            )
        )
        self.publication_root = self.root / "bindings"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def configured_result(
        self,
        evidence: host_cmake_toolchain.HostCMakeConfiguredTargetEvidence | None = None,
    ) -> host_cmake_toolchain.HostCMakeConfiguredTargetResult:
        return host_cmake_toolchain.HostCMakeConfiguredTargetResult(
            self.configured if evidence is None else evidence,
            (),
        )

    def verification_outcome(
        self,
        snapshot: host_registration_snapshot.HostRegistrationSnapshot | None = None,
    ) -> host_registration_verification.HostRegistrationVerificationOutcomeV1:
        return host_registration_verification.HostRegistrationVerificationOutcomeV1(
            self.snapshot if snapshot is None else snapshot,
            host_registration_verification.HostRegistrationVerificationProcessEvidence(
                ("staged-host.exe", "--asharia-verify-static-registration"),
                0,
                1,
                0,
            ),
            (),
        )

    def publish(
        self,
        *,
        configured_side_effect: object | None = None,
        verification_outcome: object | None = None,
    ) -> host_binding_publication.HostExecutableBindingPublicationResult:
        configured = (
            self.configured_result()
            if configured_side_effect is None
            else configured_side_effect
        )
        verified = (
            self.verification_outcome()
            if verification_outcome is None
            else verification_outcome
        )
        with mock.patch.object(
            host_cmake_toolchain,
            "read_host_cmake_configured_target",
            side_effect=configured if isinstance(configured, list) else None,
            return_value=None if isinstance(configured, list) else configured,
        ), mock.patch.object(
            host_registration_verification,
            "run_staged_host_registration_verification",
            return_value=verified,
        ):
            return host_binding_publication.collect_and_publish_host_executable_binding(
                self.request,
                self.publication_root,
                self.validators,
            )

    def test_publishes_closed_generation_and_reuses_deeply_verified_bytes(self) -> None:
        first = self.publish()
        second = self.publish()

        self.assertTrue(first.succeeded, first.diagnostics)
        self.assertTrue(second.succeeded, second.diagnostics)
        assert first.receipt is not None
        assert second.receipt is not None
        self.assertFalse(first.receipt.reused)
        self.assertTrue(second.receipt.reused)
        self.assertEqual(
            first.receipt.binding_generation_id,
            second.receipt.binding_generation_id,
        )
        self.assertEqual(
            {
                host_executable_binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
                "host/host.exe",
                host_executable_binding.HOST_REGISTRATION_SNAPSHOT_PATH,
            },
            {
                path.relative_to(first.receipt.generation_path).as_posix()
                for path in first.receipt.generation_path.rglob("*")
                if path.is_file()
            },
        )
        verified = (
            host_binding_generation_verifier.verify_published_host_binding_generation(
                first.receipt.generation_path,
                self.composition,
                self.template,
                self.validators,
            )
        )
        self.assertTrue(verified.succeeded, verified.diagnostics)

    def test_observed_snapshot_mismatch_fails_without_partial_generation(self) -> None:
        mismatch = replace(self.snapshot, registrations=())

        result = self.publish(
            verification_outcome=self.verification_outcome(mismatch)
        )

        self.assertEqual(
            ["host-binding.registration.handoff-mismatch"],
            [item.code for item in result.diagnostics],
        )
        generations = self.publication_root / "generations"
        self.assertEqual([], list(generations.iterdir()))

    def test_file_api_drift_after_verification_fails_closed(self) -> None:
        changed = replace(
            self.configured,
            configured_compiler=replace(
                self.configured.configured_compiler,
                compiler_version="different",
            ),
        )

        result = self.publish(
            configured_side_effect=[
                self.configured_result(),
                self.configured_result(changed),
            ]
        )

        self.assertEqual(
            ["host-binding.cmake.reply-drift"],
            [item.code for item in result.diagnostics],
        )
        self.assertEqual(
            [], list((self.publication_root / "generations").iterdir())
        )

    def test_stale_target_handoff_fails_before_staging(self) -> None:
        latest_target = replace(self.target, reply_index_path=self.build_root / "new.json")
        latest = replace(
            self.configured,
            reply_index_path=latest_target.reply_index_path,
            target=latest_target,
        )

        result = self.publish(configured_side_effect=self.configured_result(latest))

        self.assertEqual(
            ["host-binding.cmake.handoff-stale"],
            [item.code for item in result.diagnostics],
        )
        self.assertFalse(self.publication_root.exists())

    def test_existing_artifact_tamper_prevents_reuse(self) -> None:
        first = self.publish()
        assert first.receipt is not None
        (first.receipt.generation_path / "host/host.exe").write_bytes(b"tampered")

        second = self.publish()

        self.assertIsNone(second.receipt)
        self.assertEqual(
            ["host-binding.publication.artifact-integrity-mismatch"],
            [item.code for item in second.diagnostics],
        )

    def test_existing_extra_file_prevents_reuse(self) -> None:
        first = self.publish()
        assert first.receipt is not None
        (first.receipt.generation_path / "extra.txt").write_text(
            "extra", encoding="utf-8"
        )

        second = self.publish()

        self.assertIsNone(second.receipt)
        self.assertEqual(
            ["host-binding.publication.layout-mismatch"],
            [item.code for item in second.diagnostics],
        )

    def test_published_snapshot_tamper_fails_deep_verification(self) -> None:
        published = self.publish()
        assert published.receipt is not None
        snapshot_path = (
            published.receipt.generation_path
            / host_executable_binding.HOST_REGISTRATION_SNAPSHOT_PATH
        )
        snapshot_path.write_bytes(b"{}\n")

        verified = (
            host_binding_generation_verifier.verify_published_host_binding_generation(
                published.receipt.generation_path,
                self.composition,
                self.template,
                self.validators,
            )
        )

        self.assertIsNone(verified.verified)
        self.assertIn(
            "host-binding.publication.snapshot-integrity-mismatch",
            [item.code for item in verified.diagnostics],
        )

    def test_published_receipt_tamper_fails_deep_verification(self) -> None:
        published = self.publish()
        assert published.receipt is not None
        receipt_path = (
            published.receipt.generation_path
            / host_executable_binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME
        )
        receipt_path.write_bytes(receipt_path.read_bytes() + b" ")

        verified = (
            host_binding_generation_verifier.verify_published_host_binding_generation(
                published.receipt.generation_path,
                self.composition,
                self.template,
                self.validators,
            )
        )

        self.assertIsNone(verified.verified)
        self.assertEqual(
            ["host-binding.canonical-bytes-mismatch"],
            [item.code for item in verified.diagnostics],
        )

    def test_generation_directory_name_is_part_of_publication_identity(self) -> None:
        published = self.publish()
        assert published.receipt is not None
        wrong_path = published.receipt.generation_path.with_name(
            "sha256-" + "f" * 64
        )
        published.receipt.generation_path.rename(wrong_path)

        verified = (
            host_binding_generation_verifier.verify_published_host_binding_generation(
                wrong_path,
                self.composition,
                self.template,
                self.validators,
            )
        )

        self.assertEqual(
            ["host-binding.publication.generation-path-mismatch"],
            [item.code for item in verified.diagnostics],
        )

    def test_deep_verifier_rejects_template_from_another_composition(self) -> None:
        published = self.publish()
        assert published.receipt is not None
        other_composition = support.composition_generation(
            self.validators,
            compiler_version="different",
        )
        other_template_result = (
            host_executable_template.generate_windows_development_host_template(
                other_composition.manifest,
                "asharia-generated-host",
                self.validators,
            )
        )
        assert other_template_result.generation is not None

        verified = (
            host_binding_generation_verifier.verify_published_host_binding_generation(
                published.receipt.generation_path,
                self.composition,
                other_template_result.generation,
                self.validators,
            )
        )

        self.assertIsNone(verified.verified)
        self.assertIn(
            "host-binding.input.composition-mismatch",
            [item.code for item in verified.diagnostics],
        )

    def test_deep_verifier_rescans_closed_layout_before_success(self) -> None:
        published = self.publish()
        assert published.receipt is not None
        original_observe = host_artifact_io.observe_host_artifact
        observations = 0

        def add_extra_file(path: object) -> object:
            nonlocal observations
            result = original_observe(path)
            observations += 1
            if observations == 1:
                (published.receipt.generation_path / "late-extra.txt").write_text(
                    "late",
                    encoding="utf-8",
                )
            return result

        with mock.patch.object(
            host_artifact_io,
            "observe_host_artifact",
            side_effect=add_extra_file,
        ):
            verified = (
                host_binding_generation_verifier.verify_published_host_binding_generation(
                    published.receipt.generation_path,
                    self.composition,
                    self.template,
                    self.validators,
                )
            )

        self.assertIsNone(verified.verified)
        self.assertEqual(
            ["host-binding.publication.layout-mismatch"],
            [item.code for item in verified.diagnostics],
        )


if __name__ == "__main__":
    unittest.main()
