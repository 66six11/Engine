"""Focused bounded Project Bootstrap Host protocol tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import bootstrap_project_host
from tools import bootstrap_session as bootstrap
from tools import check_package_contracts as contracts
from tools import host_process
from tools.tests import bootstrap_host_test_support as support


class BootstrapProjectHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.fixture = support.make_fixture(
            Path(self.temporary_directory.name), self.validators
        )
        self.current = self.fixture.observe(self.validators)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def execute(
        self,
        completed: host_process.BoundedHostProcessResult,
    ) -> bootstrap.ProjectBootstrapRunObservationV1:
        with mock.patch.object(
            host_process,
            "run_bounded_host_process",
            return_value=completed,
        ):
            return bootstrap_project_host.run_project_bootstrap_host(
                self.fixture.project_root,
                self.current,
                (),
            )

    def test_success_uses_fixed_argv_and_strict_summary(self) -> None:
        expected = support.completed(0, support.summary())
        with mock.patch.object(
            host_process,
            "run_bounded_host_process",
            return_value=expected,
        ) as run:
            result = bootstrap_project_host.run_project_bootstrap_host(
                self.fixture.project_root,
                self.current,
                (("PATH", "synthetic"),),
            )

        self.assertEqual(
            bootstrap.ProjectBootstrapDispositionV1.SUCCEEDED,
            result.disposition,
        )
        self.assertEqual("Bootstrap Project", result.summary.project_name)
        run.assert_called_once_with(
            (
                str(self.current.payload.artifact_path),
                "--asharia-project-root",
                str(self.fixture.project_root),
            ),
            {"PATH": "synthetic"},
            60.0,
            64 * 1024,
            1024 * 1024,
        )

    def test_exit_65_is_project_rejection_and_does_not_parse_stderr(self) -> None:
        result = self.execute(
            support.completed(65, b"", b"project details")
        )

        self.assertEqual(
            bootstrap.ProjectBootstrapDispositionV1.PROJECT_REJECTED,
            result.disposition,
        )
        self.assertIsNone(result.summary)
        self.assertEqual(65, result.exit_code)

    def test_process_and_protocol_failures_are_host_failures(self) -> None:
        cases = {
            "overflow": support.completed(
                0, support.summary(), limit_exceeded="stdout"
            ),
            "nonzero": support.completed(79, b"", b"failure"),
            "exit65-with-stdout": support.completed(
                65, b"unexpected", b"failure"
            ),
            "stderr-on-success": support.completed(
                0, support.summary(), b"warning"
            ),
            "invalid-json": support.completed(0, b"{"),
            "extra-field": support.completed(0, support.summary(extra=True)),
            "boolean-version": support.completed(
                0, support.summary(schemaVersion=True)
            ),
            "floating-version": support.completed(
                0, support.summary(schemaVersion=1.0)
            ),
            "exponent-version": support.completed(
                0,
                support.summary().replace(
                    b'"schemaVersion": 1',
                    b'"schemaVersion": 1e0',
                ),
            ),
            "noncanonical-id": support.completed(
                0, support.summary(projectId=support.PROJECT_ID.upper())
            ),
            "surrogate-name": support.completed(
                0,
                support.summary().replace(
                    b'"Bootstrap Project"',
                    b'"\\ud800"',
                ),
            ),
            "duplicate-key": support.completed(
                0,
                (
                    '{"schema":"com.asharia.project-bootstrap-summary",'
                    '"schemaVersion":1,"projectName":"one",'
                    '"projectName":"two","projectId":"'
                    + support.PROJECT_ID
                    + '","assetSourceRootCount":0}'
                ).encode("utf-8"),
            ),
        }
        for name, completed in cases.items():
            with self.subTest(name=name):
                result = self.execute(completed)
                self.assertEqual(
                    bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
                    result.disposition,
                )

    def test_timeout_spawn_and_invalid_inputs_never_escape(self) -> None:
        for error in (
            host_process.HostProcessTimeout(),
            host_process.HostProcessSpawnFailure(),
        ):
            with self.subTest(error=type(error).__name__), mock.patch.object(
                host_process,
                "run_bounded_host_process",
                side_effect=error,
            ):
                result = bootstrap_project_host.run_project_bootstrap_host(
                    self.fixture.project_root,
                    self.current,
                    (),
                )
                self.assertEqual(
                    bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
                    result.disposition,
                )

        with mock.patch.object(host_process, "run_bounded_host_process") as run:
            invalid = bootstrap_project_host.run_project_bootstrap_host(
                self.fixture.project_root,
                self.current,
                (("Path", "one"), ("PATH", "two")),
            )
        self.assertEqual(
            bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
            invalid.disposition,
        )
        run.assert_not_called()

    def test_forged_payload_artifact_path_never_launches(self) -> None:
        unbound_artifact = self.fixture.project_root / "unbound.exe"
        unbound_artifact.write_bytes(b"unbound")
        forged_payload = replace(
            self.current.payload,
            artifact_path=unbound_artifact,
        )
        forged_current = replace(self.current, payload=forged_payload)

        with mock.patch.object(host_process, "run_bounded_host_process") as run:
            result = bootstrap_project_host.run_project_bootstrap_host(
                self.fixture.project_root,
                forged_current,
                (),
            )

        self.assertEqual(
            bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
            result.disposition,
        )
        self.assertEqual(
            ["bootstrap.host.current-image-invalid"],
            [item.code for item in result.diagnostics],
        )
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
