"""Focused restricted Host registration verification process tests."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import host_cmake_target
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

    def completed(
        self,
        *,
        exit_code: int = 0,
        stdout: bytes | None = None,
        stderr: bytes = b"",
    ) -> subprocess.CompletedProcess[bytes]:
        return subprocess.CompletedProcess(
            args=[],
            returncode=exit_code,
            stdout=(
                host_registration_snapshot.render_host_registration_snapshot(
                    self.snapshot
                ).encode("utf-8")
                if stdout is None
                else stdout
            ),
            stderr=stderr,
        )

    def run_with(
        self,
        completed: subprocess.CompletedProcess[bytes],
        request: verification.HostRegistrationVerificationRequestV1 | None = None,
    ) -> verification.HostRegistrationVerificationOutcomeV1:
        with mock.patch.object(subprocess, "run", return_value=completed) as runner:
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
        self.assertFalse(runner.call_args.kwargs["shell"])
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
            self.completed(stdout=b"x" * 11),
            self.request(max_snapshot_bytes=10),
        )

        self.assertEqual(
            ["host-verification.snapshot-too-large"],
            [item.code for item in result.diagnostics],
        )

    def test_timeout_is_stable_and_has_no_process_evidence(self) -> None:
        with mock.patch.object(
            subprocess,
            "run",
            side_effect=subprocess.TimeoutExpired(cmd=[], timeout=1),
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
        with mock.patch.object(subprocess, "run") as runner:
            result = verification.run_host_registration_verification(
                self.request(timeout_seconds=float("nan")),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            ["host-verification.timeout-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_missing_artifact_is_rejected_before_spawn(self) -> None:
        self.artifact.unlink()
        with mock.patch.object(subprocess, "run") as runner:
            result = verification.run_host_registration_verification(
                self.request(),
                self.validators,
            )

        runner.assert_not_called()
        self.assertEqual(
            ["host-verification.artifact-invalid"],
            [item.code for item in result.diagnostics],
        )


if __name__ == "__main__":
    unittest.main()
