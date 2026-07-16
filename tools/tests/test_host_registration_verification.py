"""Focused restricted Host registration verification process tests."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import host_artifact_collection
from tools import host_cmake_target
from tools import host_registration_process
from tools import host_registration_snapshot
from tools import host_registration_verification as verification


class HostRegistrationVerificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.generation_id = "sha256-" + "a" * 64
        cls.blueprint_sha256 = "b" * 64
        cls.snapshot = host_registration_snapshot.HostRegistrationSnapshot(
            generation_id=cls.generation_id,
            host_activation_blueprint_sha256=cls.blueprint_sha256,
            registrations=(
                host_registration_snapshot.StaticFactoryRegistration(
                    "com.asharia.synthetic",
                    "1.0.0",
                    "implementation",
                    "runtime-service",
                    "asharia::synthetic::provideRuntimeFactories",
                    (
                        host_registration_snapshot.StaticContributionRegistration(
                            "com.asharia.contribution.synthetic-runtime",
                            "com.asharia.contribution.synthetic-service",
                            "single",
                        ),
                    ),
                ),
            ),
        )

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.build_root = Path(self.temporary_directory.name) / "build"
        self.build_root.mkdir()
        self.artifact = self.build_root / "host.exe"
        self.artifact.write_bytes(b"synthetic-host")
        self.target = host_cmake_target.HostCMakeTargetEvidence(
            build_root=self.build_root.resolve(),
            reply_index_path=self.build_root / "index.json",
            configuration="Debug",
            target_name="asharia-generated-host",
            target_type="EXECUTABLE",
            name_on_disk="host.exe",
            artifact_relative_path="host.exe",
            artifact_path=self.artifact.resolve(),
            codemodel_major=2,
            codemodel_minor=6,
        )
        self.staging_root = Path(self.temporary_directory.name) / "staging"
        self.staging_root.mkdir()
        collected = host_artifact_collection.collect_host_artifact(
            self.target, self.staging_root
        )
        assert collected.artifact is not None
        self.staged_artifact = collected.artifact

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def request(self, **changes: object) -> verification.HostRegistrationVerificationRequestV1:
        values: dict[str, object] = {
            "target": self.target,
            "expected_generation_id": self.generation_id,
            "expected_host_activation_blueprint_sha256": self.blueprint_sha256,
            "environment": (("PATH", "synthetic"),),
        }
        values.update(changes)
        return verification.HostRegistrationVerificationRequestV1(**values)

    def staged_request(
        self, **changes: object
    ) -> verification.StagedHostRegistrationVerificationRequestV1:
        values: dict[str, object] = {
            "artifact_root": self.staging_root,
            "artifact": self.staged_artifact,
            "expected_generation_id": self.generation_id,
            "expected_host_activation_blueprint_sha256": self.blueprint_sha256,
            "environment": (("PATH", "synthetic"),),
        }
        values.update(changes)
        return verification.StagedHostRegistrationVerificationRequestV1(**values)

    def completed(
        self,
        *,
        exit_code: int = 0,
        stdout: bytes | None = None,
        stderr: bytes = b"",
        limit_exceeded: str | None = None,
    ) -> host_registration_process.BoundedHostProcessResult:
        rendered_stdout = (
            host_registration_snapshot.render_host_registration_snapshot(
                self.snapshot
            ).encode("utf-8")
            if stdout is None
            else stdout
        )
        return host_registration_process.BoundedHostProcessResult(
            return_code=exit_code,
            stdout=rendered_stdout,
            stderr=stderr,
            stdout_size=len(rendered_stdout),
            stderr_size=len(stderr),
            limit_exceeded=limit_exceeded,
        )

    def run_with(
        self,
        completed: host_registration_process.BoundedHostProcessResult,
        request: verification.HostRegistrationVerificationRequestV1 | None = None,
    ) -> verification.HostRegistrationVerificationOutcomeV1:
        with mock.patch.object(
            host_registration_process,
            "run_bounded_host_process",
            return_value=completed,
        ) as runner:
            result = verification.run_host_registration_verification(
                request or self.request(),
                self.validators,
            )
        runner.assert_called_once()
        arguments = runner.call_args.args[0]
        self.assertEqual(str(self.artifact.resolve()), arguments[0])
        self.assertEqual(
            verification.HOST_REGISTRATION_VERIFICATION_ARGUMENT,
            arguments[1],
        )
        return result

    def test_success_returns_exact_canonical_snapshot(self) -> None:
        result = self.run_with(self.completed())

        self.assertTrue(result.succeeded)
        self.assertEqual(self.snapshot, result.snapshot)
        assert result.process is not None
        self.assertEqual(0, result.process.exit_code)
        self.assertEqual(0, result.process.stderr_size)

    def test_nonzero_exit_and_stderr_fail_without_snapshot(self) -> None:
        failed = self.run_with(self.completed(exit_code=71, stdout=b""))
        noisy = self.run_with(self.completed(stderr=b"unexpected\n"))

        self.assertEqual(
            ["host-verification.process-failed"],
            [item.code for item in failed.diagnostics],
        )
        self.assertEqual(
            ["host-verification.unexpected-stderr"],
            [item.code for item in noisy.diagnostics],
        )
        self.assertIsNone(failed.snapshot)
        self.assertIsNone(noisy.snapshot)

    def test_generation_mismatch_is_reported_by_snapshot_contract(self) -> None:
        result = self.run_with(
            self.completed(),
            self.request(expected_generation_id="sha256-" + "c" * 64),
        )

        self.assertEqual(
            ["host.registration.generation-mismatch"],
            [item.code for item in result.diagnostics],
        )
        self.assertIsNone(result.snapshot)

    def test_oversized_stdout_fails_before_parsing(self) -> None:
        result = self.run_with(
            self.completed(stdout=b"x" * 11, limit_exceeded="stdout"),
            self.request(max_snapshot_bytes=10),
        )

        self.assertEqual(
            ["host-verification.snapshot-too-large"],
            [item.code for item in result.diagnostics],
        )

    def test_timeout_is_stable_and_has_no_process_evidence(self) -> None:
        with mock.patch.object(
            host_registration_process,
            "run_bounded_host_process",
            side_effect=host_registration_process.HostProcessTimeout,
        ):
            result = verification.run_host_registration_verification(
                self.request(),
                self.validators,
            )

        self.assertEqual(
            ["host-verification.timeout"],
            [item.code for item in result.diagnostics],
        )
        self.assertIsNone(result.process)

    def test_non_finite_timeout_is_rejected_before_spawn(self) -> None:
        with mock.patch.object(
            host_registration_process, "run_bounded_host_process"
        ) as runner:
            result = verification.run_host_registration_verification(
                self.request(timeout_seconds=float("nan")),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            ["host-verification.timeout-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_invalid_expected_identity_is_rejected_before_spawn(self) -> None:
        with mock.patch.object(
            host_registration_process, "run_bounded_host_process"
        ) as runner:
            result = verification.run_staged_host_registration_verification(
                self.staged_request(
                    expected_generation_id="not-a-generation",
                    expected_host_activation_blueprint_sha256="not-a-digest",
                ),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            {
                "host-verification.expected-blueprint-invalid",
                "host-verification.expected-generation-invalid",
            },
            {item.code for item in result.diagnostics},
        )

    def test_missing_artifact_is_rejected_before_spawn(self) -> None:
        self.artifact.unlink()
        with mock.patch.object(
            host_registration_process, "run_bounded_host_process"
        ) as runner:
            result = verification.run_host_registration_verification(
                self.request(),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            ["host-verification.artifact-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_staged_artifact_runs_without_fabricated_cmake_evidence(self) -> None:
        with mock.patch.object(
            host_registration_process,
            "run_bounded_host_process",
            return_value=self.completed(),
        ) as runner:
            result = verification.run_staged_host_registration_verification(
                self.staged_request(),
                self.validators,
            )

        self.assertTrue(result.succeeded)
        self.assertEqual(
            str(self.staged_artifact.staged_path.resolve()),
            runner.call_args.args[0][0],
        )

    def test_staged_artifact_must_remain_inside_owned_root(self) -> None:
        outside = Path(self.temporary_directory.name) / "outside.exe"
        outside.write_bytes(b"outside")
        with mock.patch.object(
            host_registration_process, "run_bounded_host_process"
        ) as runner:
            result = verification.run_staged_host_registration_verification(
                self.staged_request(
                    artifact=replace(
                        self.staged_artifact,
                        staged_path=outside,
                    )
                ),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            ["host-verification.staged-artifact-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_staged_artifact_rejects_lexical_parent_escape(self) -> None:
        outside = self.staging_root / ".." / "outside.exe"
        outside.write_bytes(self.staged_artifact.staged_path.read_bytes())
        with mock.patch.object(
            host_registration_process, "run_bounded_host_process"
        ) as runner:
            result = verification.run_staged_host_registration_verification(
                self.staged_request(
                    artifact=replace(
                        self.staged_artifact,
                        staged_path=outside,
                    )
                ),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            ["host-verification.staged-artifact-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_staged_artifact_changed_by_process_discards_snapshot(self) -> None:
        def mutate_artifact(
            *_args: object, **_kwargs: object
        ) -> host_registration_process.BoundedHostProcessResult:
            self.staged_artifact.staged_path.write_bytes(b"changed")
            return self.completed()

        with mock.patch.object(
            host_registration_process,
            "run_bounded_host_process",
            side_effect=mutate_artifact,
        ):
            result = verification.run_staged_host_registration_verification(
                self.staged_request(),
                self.validators,
            )

        self.assertIsNone(result.snapshot)
        self.assertEqual(
            ["host-binding.artifact.staged-hash-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_bounded_runner_terminates_stdout_overflow(self) -> None:
        result = host_registration_process.run_bounded_host_process(
            (
                sys.executable,
                "-c",
                "import sys; sys.stdout.buffer.write(b'x' * 1048576)",
            ),
            dict(os.environ),
            10.0,
            1024,
        )

        self.assertEqual("stdout", result.limit_exceeded)
        self.assertGreater(result.stdout_size, 1024)
        self.assertLessEqual(len(result.stdout), 1025)


if __name__ == "__main__":
    unittest.main()
