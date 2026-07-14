"""Deterministic Host Composition Plan v1 tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_package_composition as composition
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools.package_candidates import PackageCandidate
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
ENGINE_API_VERSION = "0.1.0"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


def requirement(identity: str, version: str = "0.1.0") -> dict[str, object]:
    return {"id": identity, "version": exact(version)}


class HostPackageCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-host-package.json").read_text(encoding="utf-8")
        )
        cls.feature_set_template = json.loads(
            (FIXTURE_ROOT / "valid-feature-set.json").read_text(encoding="utf-8")
        )
        cls.editor_profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-editor.json").read_text(
                encoding="utf-8"
            )
        )

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.candidate_root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def project(
        self,
        *,
        packages: list[dict[str, object]] | None = None,
        feature_sets: list[dict[str, object]] | None = None,
        options: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": packages or [],
            "directFeatureSets": feature_sets or [],
            "packageOptions": options or [],
        }

    def installable(
        self,
        identity: str,
        *,
        dependencies: list[dict[str, object]] | None = None,
        with_options: bool = False,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["displayName"] = identity
        manifest["dependencies"] = dependencies or []
        if with_options:
            manifest["options"] = [
                {
                    "id": "validation",
                    "type": "boolean",
                    "default": True,
                    "affects": ["activation", "build"],
                },
                {
                    "id": "frames-in-flight",
                    "type": "integer",
                    "default": 2,
                    "minimum": 1,
                    "maximum": 4,
                    "affects": ["build", "cook"],
                },
                {
                    "id": "quality",
                    "type": "enum",
                    "default": "balanced",
                    "values": ["fast", "balanced"],
                    "affects": ["activation"],
                },
            ]
        return manifest

    def feature_set(
        self,
        identity: str,
        *,
        packages: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.feature_set_template)
        manifest["id"] = identity
        manifest["displayName"] = identity
        manifest["packages"] = packages or []
        manifest["featureSets"] = []
        return manifest

    def candidate(self, manifest: dict[str, object]) -> PackageCandidate:
        identity = str(manifest["id"])
        payload_root = self.candidate_root / identity
        payload_root.mkdir(parents=True)
        (payload_root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        discovered = discovery.load_package_candidates(
            [discovery.LocalCandidateLocation(identity, payload_root)],
            self.validators,
        )
        self.assertTrue(
            discovered.succeeded,
            "\n".join(value.render() for value in discovered.diagnostics),
        )
        return discovered.candidates[0]

    def verified(
        self,
        project: dict[str, object],
        candidates: list[PackageCandidate],
        profile: dict[str, object] | None = None,
    ) -> effective_session.EffectiveSessionPlan:
        profile_snapshot = package_test_support.make_host_profile_snapshot(
            profile or self.editor_profile
        )
        distribution = package_test_support.make_engine_distribution(
            host_profile_snapshots=[profile_snapshot]
        )
        resolution = package_resolver.resolve_package_graph(
            project,
            distribution,
            candidates,
            self.validators,
        )
        self.assertTrue(
            resolution.succeeded,
            "\n".join(diagnostic.render() for diagnostic in resolution.diagnostics),
        )
        session = effective_session.plan_effective_session(
            distribution,
            project,
            resolution.lock,
            candidates,
            profile_snapshot,
            self.validators,
        )
        self.assertTrue(
            session.succeeded,
            "\n".join(diagnostic.render() for diagnostic in session.diagnostics),
        )
        assert session.plan is not None
        return session.plan

    def plan(
        self,
        session: effective_session.EffectiveSessionPlan,
    ) -> composition.HostCompositionPlanResult:
        return composition.plan_host_package_composition(
            session,
            self.validators,
        )

    def test_plan_contains_effective_options_entries_and_contributions(self) -> None:
        identity = "com.asharia.system.hosted"
        manifest = self.installable(identity, with_options=True)
        project = self.project(
            packages=[requirement(identity)],
            options=[
                {
                    "packageId": identity,
                    "values": [
                        {"id": "quality", "value": "fast"},
                        {"id": "validation", "value": False},
                    ],
                }
            ],
        )
        verified = self.verified(project, [self.candidate(manifest)])

        result = self.plan(verified)

        self.assertTrue(
            result.succeeded,
            "\n".join(diagnostic.render() for diagnostic in result.diagnostics),
        )
        assert result.plan is not None
        data = composition.host_composition_plan_to_data(result.plan)
        self.assertEqual([], composition.validate_host_composition_plan_data(
            data,
            self.validators,
        ))
        self.assertEqual(
            [
                {
                    "id": "frames-in-flight",
                    "type": "integer",
                    "affects": ["build", "cook"],
                    "value": 2,
                },
                {
                    "id": "quality",
                    "type": "enum",
                    "affects": ["activation"],
                    "value": "fast",
                },
                {
                    "id": "validation",
                    "type": "boolean",
                    "affects": ["build", "activation"],
                    "value": False,
                },
            ],
            data["packages"][0]["options"],
        )
        module_ids = [module["id"] for module in data["packages"][0]["modules"]]
        self.assertLess(module_ids.index("contract"), module_ids.index("runtime"))
        self.assertLess(module_ids.index("runtime"), module_ids.index("editor"))
        self.assertEqual(
            ["runtime", "authoring", "diagnostics"],
            [entry["dimension"] for entry in data["entries"]],
        )
        self.assertEqual(
            [
                "com.asharia.contribution.synthetic-runtime",
                "com.asharia.contribution.synthetic-editor",
            ],
            [value["id"] for value in data["contributions"]],
        )
        rendered = composition.render_host_composition_plan(result.plan)
        self.assertTrue(rendered.endswith("\n"))
        self.assertNotIn("\r", rendered)

    def test_dependency_order_keeps_feature_sets_as_graph_evidence_only(self) -> None:
        dependency_id = "com.asharia.system.dependency"
        package_id = "com.asharia.system.root"
        feature_id = "com.asharia.features.root"
        dependency = self.installable(dependency_id)
        package = self.installable(
            package_id,
            dependencies=[requirement(dependency_id)],
        )
        feature_set = self.feature_set(
            feature_id,
            packages=[requirement(package_id)],
        )
        project = self.project(feature_sets=[requirement(feature_id)])
        candidates = [
            self.candidate(feature_set),
            self.candidate(package),
            self.candidate(dependency),
        ]
        verified = self.verified(project, candidates)

        result = self.plan(verified)

        self.assertTrue(result.succeeded)
        assert result.plan is not None
        data = composition.host_composition_plan_to_data(result.plan)
        self.assertEqual(
            [dependency_id, package_id, feature_id],
            [value["id"] for value in data["resolvedGraph"]["packages"]],
        )
        self.assertEqual(
            [dependency_id, package_id],
            [value["id"] for value in data["packages"]],
        )
        self.assertEqual(
            "feature-set",
            data["resolvedGraph"]["directFeatureSets"][0]["packageKind"],
        )

    def test_installable_with_no_selected_modules_remains_in_plan(self) -> None:
        identity = "com.asharia.system.runtime-only"
        manifest = copy.deepcopy(
            json.loads((FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8"))
        )
        manifest["id"] = identity
        manifest["displayName"] = identity
        project = self.project(packages=[requirement(identity)])
        profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-asset-worker.json").read_text(
                encoding="utf-8"
            )
        )
        verified = self.verified(project, [self.candidate(manifest)], profile)

        result = self.plan(verified)

        self.assertTrue(result.succeeded)
        assert result.plan is not None
        self.assertEqual(identity, result.plan.packages[0].package.package_id)
        self.assertEqual((), result.plan.packages[0].modules)
        self.assertEqual((), result.plan.entries)
        self.assertEqual((), result.plan.contributions)

    def test_stale_project_and_incomplete_verified_snapshot_fail_atomically(self) -> None:
        identity = "com.asharia.system.hosted"
        project = self.project(packages=[requirement(identity)])
        verified = self.verified(project, [self.candidate(self.installable(identity))])

        stale_session = copy.deepcopy(verified)
        stale_session.verified_graph.project["directPackages"].clear()
        incomplete_session = copy.deepcopy(verified)
        incomplete_session.verified_graph.lock["nodes"].clear()
        stale = self.plan(stale_session)
        incomplete = self.plan(incomplete_session)

        self.assertIsNone(stale.plan)
        self.assertIn(
            "lock.input.project-manifest-stale",
            {diagnostic.code for diagnostic in stale.diagnostics},
        )
        self.assertIsNone(incomplete.plan)
        self.assertTrue(
            {
                diagnostic.code
                for diagnostic in incomplete.diagnostics
                if diagnostic.code.startswith(("lock.", "session."))
            }
        )

    def test_unsupported_and_forged_verification_results_fail_closed(self) -> None:
        project = self.project()
        unsupported = composition.plan_host_package_composition(
            object(),
            self.validators,
        )
        forged = copy.deepcopy(self.verified(project, []))
        forged.host_profile["hostKind"] = "runtime"
        forged_diagnostics = self.plan(forged)

        self.assertIsNone(unsupported.plan)
        self.assertEqual("plan.input.unverified", unsupported.diagnostics[0].code)
        self.assertIsNone(forged_diagnostics.plan)
        self.assertTrue(
            any(
                diagnostic.code.startswith(("session.", "profile."))
                for diagnostic in forged_diagnostics.diagnostics
            )
        )

    def test_contribution_without_selected_owner_fails_atomically(self) -> None:
        identity = "com.asharia.system.hosted"
        manifest = self.installable(identity)
        project = self.project(packages=[requirement(identity)])
        verified = self.verified(project, [self.candidate(manifest)])
        projection, diagnostics = contracts.select_host_profile_data(
            verified.verified_graph.lock,
            [manifest],
            self.editor_profile,
            self.validators,
        )
        self.assertEqual([], diagnostics)
        assert projection is not None
        selection = projection.contributions[0]
        forged_projection = contracts.HostProjection(
            projection.host_kind,
            projection.target_platform,
            projection.modules,
            (
                contracts.HostContributionSelection(
                    selection.package_id,
                    selection.package_version,
                    selection.contribution_id,
                    selection.contribution_kind,
                    "missing-owner",
                ),
            ),
        )

        with mock.patch.object(
            contracts,
            "select_host_profile_data",
            return_value=(forged_projection, []),
        ):
            result = self.plan(verified)

        self.assertIsNone(result.plan)
        self.assertEqual(
            {"plan.contribution.owner-missing"},
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_unordered_inputs_render_byte_identically(self) -> None:
        dependency_id = "com.asharia.system.dependency"
        package_id = "com.asharia.system.root"
        dependency = self.installable(dependency_id, with_options=True)
        package = self.installable(
            package_id,
            dependencies=[requirement(dependency_id)],
            with_options=True,
        )
        options = [
            {
                "packageId": dependency_id,
                "values": [
                    {"id": "validation", "value": False},
                    {"id": "quality", "value": "fast"},
                ],
            },
            {
                "packageId": package_id,
                "values": [
                    {"id": "quality", "value": "fast"},
                    {"id": "validation", "value": False},
                ],
            },
        ]
        project = self.project(packages=[requirement(package_id)], options=options)
        candidates = [self.candidate(package), self.candidate(dependency)]
        reordered_project = copy.deepcopy(project)
        reordered_project["packageOptions"].reverse()
        for group in reordered_project["packageOptions"]:
            group["values"].reverse()
        profile = copy.deepcopy(self.editor_profile)
        verified = self.verified(project, candidates, profile)
        reordered_verified = self.verified(
            reordered_project,
            list(reversed(candidates)),
            profile,
        )

        first = self.plan(verified)
        second = self.plan(reordered_verified)

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        assert first.plan is not None
        assert second.plan is not None
        self.assertEqual(
            composition.render_host_composition_plan(first.plan),
            composition.render_host_composition_plan(second.plan),
        )

    def test_planner_does_not_resolve_write_or_mutate_inputs(self) -> None:
        identity = "com.asharia.system.hosted"
        project = self.project(packages=[requirement(identity)])
        profile = copy.deepcopy(self.editor_profile)
        verified = self.verified(project, [self.candidate(self.installable(identity))])
        project_before = copy.deepcopy(project)
        profile_before = copy.deepcopy(profile)
        verified_before = copy.deepcopy(verified)

        with (
            mock.patch.object(
                package_resolver,
                "resolve_package_graph",
                side_effect=AssertionError("planner must not resolve"),
            ),
            mock.patch.object(
                Path,
                "write_text",
                side_effect=AssertionError("planner must not write text"),
            ),
            mock.patch.object(
                Path,
                "write_bytes",
                side_effect=AssertionError("planner must not write bytes"),
            ),
        ):
            result = self.plan(verified)

        self.assertTrue(result.succeeded)
        self.assertEqual(project_before, project)
        self.assertEqual(profile_before, profile)
        self.assertEqual(verified_before, verified)

    def test_closed_schema_rejects_build_and_activation_adapter_fields(self) -> None:
        identity = "com.asharia.system.hosted"
        project = self.project(packages=[requirement(identity)])
        verified = self.verified(project, [self.candidate(self.installable(identity))])
        result = self.plan(verified)
        self.assertTrue(result.succeeded)
        assert result.plan is not None
        data = composition.host_composition_plan_to_data(result.plan)

        for field in ("artifactPath", "activationPhase"):
            with self.subTest(field=field):
                invalid = copy.deepcopy(data)
                invalid["packages"][0][field] = "adapter-owned"
                self.assertTrue(
                    composition.validate_host_composition_plan_data(
                        invalid,
                        self.validators,
                    )
                )

    def test_discover_resolve_verify_compose_chain(self) -> None:
        identity = "com.asharia.system.discovered"
        manifest = self.installable(identity)
        project = self.project(packages=[requirement(identity)])
        with tempfile.TemporaryDirectory() as temporary_directory:
            package_root = Path(temporary_directory) / "package"
            package_root.mkdir()
            (package_root / contracts.PACKAGE_MANIFEST_NAME).write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
            discovered = discovery.load_package_candidates(
                [discovery.LocalCandidateLocation(identity, package_root)],
                self.validators,
            )
            self.assertTrue(discovered.succeeded)
            profile_snapshot = package_test_support.make_host_profile_snapshot(
                self.editor_profile
            )
            distribution = package_test_support.make_engine_distribution(
                host_profile_snapshots=[profile_snapshot]
            )
            resolved = package_resolver.resolve_package_graph(
                project,
                distribution,
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(resolved.succeeded)
            session = effective_session.plan_effective_session(
                distribution,
                project,
                resolved.lock,
                discovered.candidates,
                profile_snapshot,
                self.validators,
            )
            self.assertTrue(session.succeeded)
            assert session.plan is not None

            planned = self.plan(session.plan)

        self.assertTrue(
            planned.succeeded,
            "\n".join(diagnostic.render() for diagnostic in planned.diagnostics),
        )
        assert planned.plan is not None
        self.assertEqual(identity, planned.plan.packages[0].package.package_id)


if __name__ == "__main__":
    unittest.main()
