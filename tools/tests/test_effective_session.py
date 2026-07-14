"""Effective Session v1 state, evidence, and mutation tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import effective_session
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class EffectiveSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.package_template = json.loads(
            (FIXTURE_ROOT / "valid-host-package.json").read_text(encoding="utf-8")
        )
        cls.editor_profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-editor.json").read_text(
                encoding="utf-8"
            )
        )

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def project(self, identities: list[str]) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [
                {
                    "id": identity,
                    "version": {"kind": "exact", "version": "0.1.0"},
                }
                for identity in identities
            ],
            "directFeatureSets": [],
            "packageOptions": [],
        }

    def package(self, identity: str) -> dict[str, object]:
        manifest = copy.deepcopy(self.package_template)
        manifest["id"] = identity
        manifest["displayName"] = identity
        manifest["dependencies"] = []
        return manifest

    def write_package(self, identity: str, *, distributed: bool = False):
        package_root = self.root / ("distribution" if distributed else "local") / identity
        package_root.mkdir(parents=True)
        (package_root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(self.package(identity), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        if distributed:
            distribution_root = self.root / "distribution"
            relative_path = identity
            location = discovery.EngineDistributedCandidateLocation(
                distribution_root,
                relative_path,
            )
        else:
            location = discovery.LocalCandidateLocation(identity, package_root)
        result = discovery.load_package_candidates([location], self.validators)
        self.assertTrue(
            result.succeeded,
            "\n".join(value.render() for value in result.diagnostics),
        )
        return result.candidates[0]

    def prepare(
        self,
        identities: list[str] | None = None,
        *,
        distributed: bool = False,
    ):
        package_ids = identities or ["com.asharia.system.session-test"]
        candidates = [
            self.write_package(identity, distributed=distributed)
            for identity in package_ids
        ]
        profile_snapshot = package_test_support.make_host_profile_snapshot(
            self.editor_profile
        )
        distribution = package_test_support.make_engine_distribution(
            candidates if distributed else (),
            host_profile_snapshots=[profile_snapshot],
        )
        project = self.project(package_ids)
        resolved = package_resolver.resolve_package_graph(
            project,
            distribution,
            candidates,
            self.validators,
        )
        self.assertTrue(
            resolved.succeeded,
            "\n".join(value.render() for value in resolved.diagnostics),
        )
        return distribution, project, resolved.lock, candidates, profile_snapshot

    def plan(self, prepared):
        distribution, project, lock, candidates, profile_snapshot = prepared
        return effective_session.plan_effective_session(
            distribution,
            project,
            lock,
            candidates,
            profile_snapshot,
            self.validators,
        )

    def test_ready_session_is_canonical_and_deep_copies_inputs(self) -> None:
        prepared = self.prepare(
            ["com.asharia.system.zeta", "com.asharia.system.alpha"]
        )
        distribution, project, lock, candidates, snapshot = prepared

        first = self.plan(prepared)
        reordered_project = {key: project[key] for key in reversed(tuple(project))}
        reordered_profile = {
            key: snapshot.manifest[key]
            for key in reversed(tuple(snapshot.manifest))
        }
        second = effective_session.plan_effective_session(
            distribution,
            reordered_project,
            lock,
            reversed(candidates),
            effective_session.HostProfileSnapshot(
                snapshot.path,
                reordered_profile,
                snapshot.exact_bytes,
            ),
            self.validators,
        )

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        assert first.plan is not None
        assert second.plan is not None
        self.assertEqual(
            effective_session.render_effective_session_plan(first.plan),
            effective_session.render_effective_session_plan(second.plan),
        )
        project["directPackages"].clear()
        snapshot.manifest["grantedCapabilities"].append("mutated-after-compose")
        self.assertEqual(
            (),
            effective_session.validate_ready_effective_session(
                first.plan,
                self.validators,
            ),
        )

    def test_engine_family_api_and_generation_mismatch_require_upgrade(self) -> None:
        distribution, project, lock, candidates, snapshot = self.prepare()
        incompatible_api = package_test_support.make_engine_distribution(
            engine_api_version="0.2.0",
            host_profile_snapshots=[snapshot],
        )
        incompatible_family = package_test_support.make_engine_distribution(
            distribution_id="com.asharia.distribution.other-engine",
            host_profile_snapshots=[snapshot],
        )
        stale_generation = copy.deepcopy(distribution)
        stale_generation["context"]["configuration"] = "Release"
        stale_generation["engineGenerationId"] = contracts.compute_engine_generation_id(
            stale_generation
        )

        for name, incompatible, expected_code in (
            ("api", incompatible_api, "lock.engine.api-incompatible"),
            ("family", incompatible_family, "lock.engine.distribution-mismatch"),
            ("generation", stale_generation, "lock.input.engine-generation-stale"),
        ):
            with self.subTest(name):
                result = effective_session.plan_effective_session(
                    incompatible,
                    project,
                    lock,
                    candidates,
                    snapshot,
                    self.validators,
                )
                self.assertEqual(
                    effective_session.EffectiveSessionState.UPGRADE_REQUIRED,
                    result.state,
                )
                self.assertIsNone(result.plan)
                self.assertIn(
                    expected_code,
                    {diagnostic.code for diagnostic in result.diagnostics},
                )

    def test_project_graph_failure_is_safe_mode(self) -> None:
        distribution, project, lock, candidates, snapshot = self.prepare()
        stale_project = copy.deepcopy(project)
        stale_project["directPackages"] = []

        result = effective_session.plan_effective_session(
            distribution,
            stale_project,
            lock,
            candidates,
            snapshot,
            self.validators,
        )

        self.assertEqual(effective_session.EffectiveSessionState.SAFE_MODE, result.state)
        self.assertIn(
            "lock.input.project-manifest-stale",
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_profile_reference_and_exact_snapshot_fail_as_repair(self) -> None:
        distribution, project, lock, candidates, snapshot = self.prepare()
        runtime_profile = copy.deepcopy(snapshot.manifest)
        runtime_profile["hostKind"] = "runtime"
        other_platform_profile = copy.deepcopy(snapshot.manifest)
        other_platform_profile["targetPlatform"] = "com.asharia.platform.linux"
        compact_profile_bytes = (
            json.dumps(snapshot.manifest, ensure_ascii=False, separators=(",", ":"))
            + "\n"
        ).encode("utf-8")
        cases = {
            "path": effective_session.HostProfileSnapshot(
                "profiles/editor/missing.json",
                snapshot.manifest,
                snapshot.exact_bytes,
            ),
            "parsed-data": effective_session.HostProfileSnapshot(
                snapshot.path,
                {**snapshot.manifest, "hostKind": "runtime"},
                snapshot.exact_bytes,
            ),
            "kind": package_test_support.make_host_profile_snapshot(
                runtime_profile,
                path=snapshot.path,
            ),
            "platform": package_test_support.make_host_profile_snapshot(
                other_platform_profile,
                path=snapshot.path,
            ),
            "exact-integrity": effective_session.HostProfileSnapshot(
                snapshot.path,
                snapshot.manifest,
                compact_profile_bytes,
            ),
            "bytes": effective_session.HostProfileSnapshot(
                snapshot.path,
                snapshot.manifest,
                b"not-json",
            ),
        }
        for name, invalid_snapshot in cases.items():
            with self.subTest(name):
                result = effective_session.plan_effective_session(
                    distribution,
                    project,
                    lock,
                    candidates,
                    invalid_snapshot,
                    self.validators,
                )
                self.assertEqual(
                    effective_session.EffectiveSessionState.REPAIR_REQUIRED,
                    result.state,
                )
                self.assertIsNone(result.plan)

        invalid_distribution = copy.deepcopy(distribution)
        invalid_distribution["hostProfiles"][0]["integrity"]["digest"] = "f" * 64
        invalid_distribution["engineGenerationId"] = contracts.compute_engine_generation_id(
            invalid_distribution
        )
        result = effective_session.plan_effective_session(
            invalid_distribution,
            project,
            lock,
            candidates,
            snapshot,
            self.validators,
        )
        self.assertEqual(
            effective_session.EffectiveSessionState.REPAIR_REQUIRED,
            result.state,
        )

    def test_project_owned_payload_drift_is_safe_mode(self) -> None:
        distribution, project, lock, candidates, snapshot = self.prepare()
        payload_root = candidates[0].payload_location
        assert isinstance(payload_root, Path)
        (payload_root / "drift.txt").write_text("changed", encoding="utf-8")

        result = effective_session.plan_effective_session(
            distribution,
            project,
            lock,
            candidates,
            snapshot,
            self.validators,
        )

        self.assertEqual(effective_session.EffectiveSessionState.SAFE_MODE, result.state)
        self.assertIn(
            "lock.integrity.payload-mismatch",
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_distributed_payload_drift_is_repair_required(self) -> None:
        distribution, project, lock, candidates, snapshot = self.prepare(
            distributed=True
        )
        payload_root = candidates[0].payload_location
        assert isinstance(payload_root, Path)
        (payload_root / "drift.txt").write_text("changed", encoding="utf-8")

        result = effective_session.plan_effective_session(
            distribution,
            project,
            lock,
            candidates,
            snapshot,
            self.validators,
        )

        self.assertEqual(
            effective_session.EffectiveSessionState.REPAIR_REQUIRED,
            result.state,
        )
        self.assertIn(
            "lock.engine.distribution-candidate-mismatch",
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_composer_never_resolves_writes_or_emits_reserved_states(self) -> None:
        prepared = self.prepare()
        with (
            mock.patch.object(
                package_resolver,
                "resolve_package_graph",
                side_effect=AssertionError("session composer must not resolve"),
            ),
            mock.patch.object(
                Path,
                "write_text",
                side_effect=AssertionError("session composer must not write text"),
            ),
            mock.patch.object(
                Path,
                "write_bytes",
                side_effect=AssertionError("session composer must not write bytes"),
            ),
        ):
            result = self.plan(prepared)

        self.assertTrue(result.succeeded)
        self.assertNotEqual(
            effective_session.EffectiveSessionState.PENDING_BUILD,
            result.state,
        )
        self.assertNotEqual(
            effective_session.EffectiveSessionState.PENDING_RESTART,
            result.state,
        )

    def test_ready_session_nested_mutation_fails_closed(self) -> None:
        result = self.plan(self.prepare())
        self.assertTrue(result.succeeded)
        assert result.plan is not None
        result.plan.verified_graph.project["directPackages"].clear()

        diagnostics = effective_session.validate_ready_effective_session(
            result.plan,
            self.validators,
        )

        self.assertTrue(diagnostics)
        self.assertIn(
            "session.snapshot.fingerprint-mismatch",
            {diagnostic.code for diagnostic in diagnostics},
        )

        forged_graph = replace(
            result.plan.verified_graph,
            selected_candidates=None,  # type: ignore[arg-type]
        )
        forged_plan = replace(result.plan, verified_graph=forged_graph)
        forged_diagnostics = effective_session.validate_ready_effective_session(
            forged_plan,
            self.validators,
        )
        self.assertEqual(
            {"session.snapshot.candidates-invalid"},
            {diagnostic.code for diagnostic in forged_diagnostics},
        )


if __name__ == "__main__":
    unittest.main()
