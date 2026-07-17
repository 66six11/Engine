"""Pure Bootstrap project-open state reduction tests."""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools import bootstrap_session as bootstrap
from tools import effective_session
from tools import engine_distribution_repair_verifier as distribution_verifier


class BootstrapSessionTests(unittest.TestCase):
    def setUp(self) -> None:
        integrity = effective_session.SessionIntegrity("sha256", "1" * 64)
        graph = effective_session.VerifiedResolvedGraph(
            distribution={},
            project={},
            lock={},
            selected_candidates=(),
            engine_generation_id="sha256-" + "2" * 64,
            distribution_manifest_integrity=integrity,
            project_manifest_integrity=integrity,
            locked_graph_integrity=integrity,
            candidate_bindings_integrity=integrity,
        )
        self.plan = effective_session.EffectiveSessionPlan(
            verified_graph=graph,
            host_profile_path="profiles/editor/asharia.host-profile.json",
            host_profile={
                "hostKind": "editor",
                "targetPlatform": "com.asharia.platform.windows",
            },
            host_profile_bytes=b"{}\n",
            host_profile_integrity=integrity,
            session_fingerprint=effective_session.SessionIntegrity(
                "sha256", "3" * 64
            ),
        )
        verified = distribution_verifier.VerifiedInstalledDistribution(
            engine_generation_id=graph.engine_generation_id,
            generation_root=Path("distribution"),
            manifest={},
            manifest_bytes=b"{}\n",
            manifest_integrity={"algorithm": "sha256", "digest": "4" * 64},
        )
        self.request = bootstrap.ProjectOpenRequestV1(
            project_root=Path("C:/private/bootstrap-session-project-root"),
            verified_distribution=verified,
            host_profile_snapshot=effective_session.HostProfileSnapshot(
                self.plan.host_profile_path,
                self.plan.host_profile,
                self.plan.host_profile_bytes,
            ),
        )
        self.ready_session = effective_session.EffectiveSessionResult(
            effective_session.EffectiveSessionState.READY, self.plan, ()
        )

    def evidence(
        self,
        *,
        inspection: bootstrap.ProjectOpenInspectionV1 | None = None,
        current: bootstrap.CurrentImageObservationV1 | None = None,
        run: bootstrap.ProjectBootstrapRunObservationV1 | None = None,
    ) -> bootstrap.BootstrapSessionEvidenceV1:
        return bootstrap.BootstrapSessionEvidenceV1(
            request=self.request,
            inspection=inspection,
            current_image=current,
            project_bootstrap=run,
        )

    def inspection(
        self,
        session: effective_session.EffectiveSessionResult | None = None,
        owners: tuple[bootstrap.InspectionFailureOwnerV1, ...] = (),
    ) -> bootstrap.ProjectOpenInspectionV1:
        return bootstrap.ProjectOpenInspectionV1(
            package_snapshot=None,
            effective_session=session,
            failure_owners=owners,
            diagnostics=(),
        )

    def current(
        self, disposition: bootstrap.CurrentImageDispositionV1
    ) -> bootstrap.CurrentImageObservationV1:
        return bootstrap.CurrentImageObservationV1(
            disposition=disposition,
            current_session_integrity=(
                self.plan.session_fingerprint
                if disposition is bootstrap.CurrentImageDispositionV1.CURRENT
                else None
            ),
            diagnostics=(),
        )

    def run_observation(
        self, disposition: bootstrap.ProjectBootstrapDispositionV1
    ) -> bootstrap.ProjectBootstrapRunObservationV1:
        summary = None
        if disposition is bootstrap.ProjectBootstrapDispositionV1.SUCCEEDED:
            summary = bootstrap.ProjectBootstrapSummaryV1(
                "Example", "6ad468bb-e099-46d4-a91b-911e86cf7188", 1
            )
        return bootstrap.ProjectBootstrapRunObservationV1(
            disposition=disposition,
            summary=summary,
            exit_code=0 if summary is not None else 65,
            stdout_size=1 if summary is not None else 0,
            stderr_size=0,
            diagnostics=(),
        )

    def test_no_request_and_uninspected_request_are_distinct(self) -> None:
        no_project = bootstrap.derive_bootstrap_session(
            bootstrap.BootstrapSessionEvidenceV1(request=None)
        )
        opening = bootstrap.derive_bootstrap_session(self.evidence())

        self.assertEqual(bootstrap.BootstrapSessionState.NO_PROJECT, no_project.state)
        self.assertEqual(
            bootstrap.BootstrapNextAction.SELECT_PROJECT, no_project.next_action
        )
        self.assertEqual(bootstrap.BootstrapSessionState.OPENING, opening.state)

    def test_failure_ownership_controls_recovery_surface(self) -> None:
        cases = {
            bootstrap.InspectionFailureOwnerV1.CONTROL_PLANE: (
                bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR
            ),
            bootstrap.InspectionFailureOwnerV1.DISTRIBUTION: (
                bootstrap.BootstrapSessionState.REPAIR_REQUIRED
            ),
            bootstrap.InspectionFailureOwnerV1.PROJECT: (
                bootstrap.BootstrapSessionState.SAFE_MODE
            ),
        }
        for owner, expected in cases.items():
            with self.subTest(owner=owner):
                result = bootstrap.derive_bootstrap_session(
                    self.evidence(inspection=self.inspection(owners=(owner,)))
                )
                self.assertEqual(expected, result.state)

    def test_effective_session_failure_states_are_preserved(self) -> None:
        cases = {
            effective_session.EffectiveSessionState.REPAIR_REQUIRED: (
                bootstrap.BootstrapSessionState.REPAIR_REQUIRED
            ),
            effective_session.EffectiveSessionState.UPGRADE_REQUIRED: (
                bootstrap.BootstrapSessionState.UPGRADE_REQUIRED
            ),
            effective_session.EffectiveSessionState.SAFE_MODE: (
                bootstrap.BootstrapSessionState.SAFE_MODE
            ),
        }
        for session_state, expected in cases.items():
            failed = effective_session.EffectiveSessionResult(
                session_state,
                None,
                (
                    effective_session.EffectiveSessionDiagnostic(
                        "test.failure", "asharia.effective-session.json", "/", "x"
                    ),
                ),
            )
            with self.subTest(session_state=session_state):
                result = bootstrap.derive_bootstrap_session(
                    self.evidence(inspection=self.inspection(failed))
                )
                self.assertEqual(expected, result.state)
                self.assertEqual(["test.failure"], [item.code for item in result.diagnostics])

    def test_ready_plan_requires_current_image_then_successful_host(self) -> None:
        inspection = self.inspection(self.ready_session)
        states = (
            bootstrap.derive_bootstrap_session(self.evidence(inspection=inspection)),
            bootstrap.derive_bootstrap_session(
                self.evidence(
                    inspection=inspection,
                    current=self.current(bootstrap.CurrentImageDispositionV1.STALE),
                )
            ),
            bootstrap.derive_bootstrap_session(
                self.evidence(
                    inspection=inspection,
                    current=self.current(bootstrap.CurrentImageDispositionV1.CURRENT),
                )
            ),
            bootstrap.derive_bootstrap_session(
                self.evidence(
                    inspection=inspection,
                    current=self.current(bootstrap.CurrentImageDispositionV1.CURRENT),
                    run=self.run_observation(
                        bootstrap.ProjectBootstrapDispositionV1.PROJECT_REJECTED
                    ),
                )
            ),
            bootstrap.derive_bootstrap_session(
                self.evidence(
                    inspection=inspection,
                    current=self.current(bootstrap.CurrentImageDispositionV1.CURRENT),
                    run=self.run_observation(
                        bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED
                    ),
                )
            ),
            bootstrap.derive_bootstrap_session(
                self.evidence(
                    inspection=inspection,
                    current=self.current(bootstrap.CurrentImageDispositionV1.CURRENT),
                    run=self.run_observation(
                        bootstrap.ProjectBootstrapDispositionV1.SUCCEEDED
                    ),
                )
            ),
        )

        self.assertEqual(
            [
                bootstrap.BootstrapSessionState.OPENING,
                bootstrap.BootstrapSessionState.PENDING_BUILD,
                bootstrap.BootstrapSessionState.OPENING,
                bootstrap.BootstrapSessionState.SAFE_MODE,
                bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
                bootstrap.BootstrapSessionState.READY,
            ],
            [value.state for value in states],
        )
        self.assertNotIn(
            bootstrap.BootstrapSessionState.PENDING_RESTART,
            [value.state for value in states],
        )
        self.assertEqual("Example", states[-1].project.project_name)

    def test_render_is_versioned_deterministic_and_contains_no_project_path(self) -> None:
        result = bootstrap.derive_bootstrap_session(
            self.evidence(
                inspection=self.inspection(self.ready_session),
                current=self.current(bootstrap.CurrentImageDispositionV1.CURRENT),
                run=self.run_observation(
                    bootstrap.ProjectBootstrapDispositionV1.SUCCEEDED
                ),
            )
        )

        first = bootstrap.render_bootstrap_session_result(result)
        second = bootstrap.render_bootstrap_session_result(result)
        data = json.loads(first)

        self.assertEqual(first, second)
        self.assertEqual(bootstrap.BOOTSTRAP_SESSION_SCHEMA, data["schema"])
        self.assertEqual(1, data["schemaVersion"])
        self.assertEqual("Ready", data["state"])
        self.assertNotIn(str(self.request.project_root), first.decode("utf-8"))

    def test_mixed_failure_owners_follow_the_frozen_gate_priority(self) -> None:
        cases = (
            (
                (
                    bootstrap.InspectionFailureOwnerV1.PROJECT,
                    bootstrap.InspectionFailureOwnerV1.DISTRIBUTION,
                ),
                bootstrap.BootstrapSessionState.REPAIR_REQUIRED,
            ),
            (
                (
                    bootstrap.InspectionFailureOwnerV1.PROJECT,
                    bootstrap.InspectionFailureOwnerV1.CONTROL_PLANE,
                    bootstrap.InspectionFailureOwnerV1.DISTRIBUTION,
                ),
                bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
            ),
        )
        for owners, expected in cases:
            with self.subTest(owners=owners):
                result = bootstrap.derive_bootstrap_session(
                    self.evidence(inspection=self.inspection(owners=owners))
                )
                self.assertEqual(expected, result.state)

        upgrade = effective_session.EffectiveSessionResult(
            effective_session.EffectiveSessionState.UPGRADE_REQUIRED,
            None,
            (
                effective_session.EffectiveSessionDiagnostic(
                    "test.upgrade",
                    "asharia.effective-session.json",
                    "/engine",
                    "upgrade",
                ),
            ),
        )
        project_and_upgrade = bootstrap.derive_bootstrap_session(
            self.evidence(
                inspection=self.inspection(
                    upgrade,
                    (bootstrap.InspectionFailureOwnerV1.PROJECT,),
                )
            )
        )
        self.assertEqual(
            bootstrap.BootstrapSessionState.UPGRADE_REQUIRED,
            project_and_upgrade.state,
        )

    def test_diagnostic_permutations_render_identical_ordered_bytes(self) -> None:
        first = bootstrap.BootstrapSessionDiagnostic(
            "test.z",
            "z.json",
            "/z",
            "z",
        )
        second = bootstrap.BootstrapSessionDiagnostic(
            "test.a",
            "a.json",
            "/a",
            "a",
        )

        def reduced(
            diagnostics: tuple[bootstrap.BootstrapSessionDiagnostic, ...],
            owners: tuple[bootstrap.InspectionFailureOwnerV1, ...],
        ) -> bootstrap.BootstrapSessionResultV1:
            return bootstrap.derive_bootstrap_session(
                self.evidence(
                    inspection=bootstrap.ProjectOpenInspectionV1(
                        package_snapshot=None,
                        effective_session=None,
                        failure_owners=owners,
                        diagnostics=diagnostics,
                    )
                )
            )

        left = reduced(
            (first, second, first),
            (
                bootstrap.InspectionFailureOwnerV1.PROJECT,
                bootstrap.InspectionFailureOwnerV1.CONTROL_PLANE,
            ),
        )
        right = reduced(
            (second, first),
            (
                bootstrap.InspectionFailureOwnerV1.CONTROL_PLANE,
                bootstrap.InspectionFailureOwnerV1.PROJECT,
            ),
        )

        self.assertEqual(
            bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
            left.state,
        )
        self.assertEqual((second, first), left.diagnostics)
        self.assertEqual(
            bootstrap.render_bootstrap_session_result(left),
            bootstrap.render_bootstrap_session_result(right),
        )


if __name__ == "__main__":
    unittest.main()
