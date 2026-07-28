"""Focused Bootstrap Host orchestration tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import bootstrap_current_host
from tools import bootstrap_host_session
from tools import bootstrap_project_host
from tools import bootstrap_project_inspection
from tools import bootstrap_session as bootstrap
from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_process
from tools.tests import bootstrap_host_test_support as support


class BootstrapHostSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.fixture = support.make_fixture(
            Path(self.temporary_directory.name), self.validators
        )
        self.context = bootstrap_host_session.BootstrapHostAdapterContextV1(
            self.fixture.static_composition,
            self.fixture.verified_binding,
            (),
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def advance(
        self,
        completed: host_process.BoundedHostProcessResult,
    ) -> bootstrap.BootstrapSessionResultV1:
        with mock.patch.object(
            host_process,
            "run_bounded_host_process",
            return_value=completed,
        ):
            return bootstrap_host_session._advance_inspected_bootstrap_session(
                self.fixture.request,
                self.fixture.inspection,
                self.context,
                self.validators,
            )

    def test_orchestration_reduces_ready_rejection_and_early_safe_mode(self) -> None:
        ready = self.advance(support.completed(0, support.summary()))
        rejected = self.advance(
            support.completed(65, b"", b"invalid project")
        )

        self.assertEqual(bootstrap.BootstrapSessionState.READY, ready.state)
        self.assertEqual("Bootstrap Project", ready.project.project_name)
        self.assertEqual(
            bootstrap.BootstrapSessionState.SAFE_MODE,
            rejected.state,
        )

        safe_inspection = replace(
            self.fixture.inspection,
            effective_session=None,
            failure_owners=(bootstrap.InspectionFailureOwnerV1.PROJECT,),
        )
        with mock.patch.object(
            bootstrap_project_host,
            "run_project_bootstrap_host",
        ) as run:
            safe = bootstrap_host_session._advance_inspected_bootstrap_session(
                self.fixture.request,
                safe_inspection,
                self.context,
                self.validators,
            )
        self.assertEqual(bootstrap.BootstrapSessionState.SAFE_MODE, safe.state)
        run.assert_not_called()

    def test_no_project_precedes_context_validation_and_inspection(self) -> None:
        with mock.patch.object(
            bootstrap_project_inspection,
            "inspect_project_open_request",
        ) as inspect:
            result = bootstrap_host_session.open_bootstrap_session(
                None,
                None,  # type: ignore[arg-type]
                None,  # type: ignore[arg-type]
            )

        self.assertEqual(bootstrap.BootstrapSessionState.NO_PROJECT, result.state)
        inspect.assert_not_called()

    def test_orchestrator_does_not_reexport_adapter_ownership(self) -> None:
        for moved_name in (
            "CurrentHostPayloadV1",
            "observe_current_host_image",
            "run_project_bootstrap_host",
            "PROJECT_BOOTSTRAP_SUMMARY_SCHEMA",
            "advance_inspected_bootstrap_session",
        ):
            with self.subTest(moved_name=moved_name):
                self.assertFalse(hasattr(bootstrap_host_session, moved_name))

    def test_invalid_orchestration_inputs_fail_before_inspection_or_launch(self) -> None:
        malformed_environment = replace(
            self.context,
            environment=(("Path", "one"), ("PATH", "two")),
        )
        malformed_timeout = replace(self.context, timeout_seconds=float("nan"))
        malformed_image = replace(
            self.context,
            static_composition=object(),  # type: ignore[arg-type]
        )
        malformed_validators = replace(
            self.validators,
            installable=None,  # type: ignore[arg-type]
        )
        cases = (
            (None, self.validators, "bootstrap.host.context-invalid"),
            (
                malformed_environment,
                self.validators,
                "bootstrap.host.context-invalid",
            ),
            (
                malformed_timeout,
                self.validators,
                "bootstrap.host.context-invalid",
            ),
            (
                malformed_image,
                self.validators,
                "bootstrap.host.context-invalid",
            ),
            (self.context, None, "bootstrap.host.validators-invalid"),
            (
                self.context,
                malformed_validators,
                "bootstrap.host.validators-invalid",
            ),
        )
        for context, validators, expected_code in cases:
            with (
                self.subTest(expected_code=expected_code),
                mock.patch.object(
                    bootstrap_project_inspection,
                    "inspect_project_open_request",
                ) as inspect,
                mock.patch.object(
                    bootstrap_current_host,
                    "observe_current_host_image",
                ) as observe,
                mock.patch.object(
                    bootstrap_project_host,
                    "run_project_bootstrap_host",
                ) as run,
            ):
                result = bootstrap_host_session.open_bootstrap_session(
                    self.fixture.request,
                    context,  # type: ignore[arg-type]
                    validators,  # type: ignore[arg-type]
                )
                advanced = (
                    bootstrap_host_session._advance_inspected_bootstrap_session(
                        self.fixture.request,
                        self.fixture.inspection,
                        context,  # type: ignore[arg-type]
                        validators,  # type: ignore[arg-type]
                    )
                )

            for value in (result, advanced):
                self.assertEqual(
                    bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
                    value.state,
                )
                self.assertEqual(
                    [expected_code],
                    [item.code for item in value.diagnostics],
                )
            inspect.assert_not_called()
            observe.assert_not_called()
            run.assert_not_called()

    def test_invalid_request_or_inspection_handoffs_never_launch(self) -> None:
        malformed_inspection = replace(
            self.fixture.inspection,
            diagnostics=None,  # type: ignore[arg-type]
        )
        cases = (
            (
                object(),
                self.fixture.inspection,
                "bootstrap.host.request-invalid",
            ),
            (
                self.fixture.request,
                object(),
                "bootstrap.host.inspection-invalid",
            ),
            (
                self.fixture.request,
                malformed_inspection,
                "bootstrap.host.inspection-invalid",
            ),
        )
        for request, inspection, expected_code in cases:
            with (
                self.subTest(expected_code=expected_code),
                mock.patch.object(
                    bootstrap_current_host,
                    "observe_current_host_image",
                ) as observe,
                mock.patch.object(
                    bootstrap_project_host,
                    "run_project_bootstrap_host",
                ) as run,
            ):
                result = (
                    bootstrap_host_session._advance_inspected_bootstrap_session(
                        request,  # type: ignore[arg-type]
                        inspection,  # type: ignore[arg-type]
                        self.context,
                        self.validators,
                    )
                )

            self.assertEqual(
                bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
                result.state,
            )
            self.assertEqual(
                [expected_code],
                [item.code for item in result.diagnostics],
            )
            observe.assert_not_called()
            run.assert_not_called()

    def test_request_and_inspection_must_name_the_same_canonical_root(self) -> None:
        other_root = Path(self.temporary_directory.name) / "other-project"
        other_root.mkdir()
        mismatched_request = replace(
            self.fixture.request,
            project_root=other_root,
        )

        with (
            mock.patch.object(
                bootstrap_current_host,
                "observe_current_host_image",
            ) as observe,
            mock.patch.object(
                bootstrap_project_host,
                "run_project_bootstrap_host",
            ) as run,
        ):
            result = bootstrap_host_session._advance_inspected_bootstrap_session(
                mismatched_request,
                self.fixture.inspection,
                self.context,
                self.validators,
            )

        self.assertEqual(
            bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
            result.state,
        )
        self.assertEqual(
            ["bootstrap.host.request-inspection-mismatch"],
            [item.code for item in result.diagnostics],
        )
        observe.assert_not_called()
        run.assert_not_called()

    def test_each_current_identity_mismatch_prevents_host_launch(self) -> None:
        plan = self.fixture.plan
        graph = plan.verified_graph
        other_integrity = effective_session.SessionIntegrity(
            "sha256",
            "f" * 64,
        )
        configuration_distribution = {
            **graph.distribution,
            "context": {
                **graph.distribution["context"],
                "configuration": "Release",
            },
        }
        plan_cases = {
            "session-fingerprint": replace(
                plan,
                session_fingerprint=other_integrity,
            ),
            "engine-generation": replace(
                plan,
                verified_graph=replace(
                    graph,
                    engine_generation_id="sha256-" + "f" * 64,
                ),
            ),
            "host-kind": replace(
                plan,
                host_profile={**plan.host_profile, "hostKind": "editor"},
            ),
            "target-platform": replace(
                plan,
                host_profile={
                    **plan.host_profile,
                    "targetPlatform": "com.asharia.platform.other",
                },
            ),
            "configuration": replace(
                plan,
                verified_graph=replace(
                    graph,
                    distribution=configuration_distribution,
                ),
            ),
        }
        cases = [
            (
                name,
                replace(
                    self.fixture.inspection,
                    effective_session=effective_session.EffectiveSessionResult(
                        effective_session.EffectiveSessionState.READY,
                        changed_plan,
                        (),
                    ),
                ),
                self.context,
            )
            for name, changed_plan in plan_cases.items()
        ]
        mismatched_binding = replace(
            self.fixture.verified_binding,
            generation_path=(
                self.fixture.verified_binding.generation_path.parent
                / "wrong-binding-generation"
            ),
        )
        cases.append(
            (
                "binding-publication",
                self.fixture.inspection,
                replace(self.context, verified_binding=mismatched_binding),
            )
        )

        for name, inspection, context in cases:
            with (
                self.subTest(name=name),
                mock.patch.object(
                    bootstrap_project_host,
                    "run_project_bootstrap_host",
                ) as run,
            ):
                result = (
                    bootstrap_host_session._advance_inspected_bootstrap_session(
                        self.fixture.request,
                        inspection,
                        context,
                        self.validators,
                    )
                )

            self.assertEqual(
                bootstrap.BootstrapSessionState.PENDING_BUILD,
                result.state,
            )
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
