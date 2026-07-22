"""Focused deterministic Package Lock update planning tests."""

from __future__ import annotations

import copy
import hashlib
import json
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import package_lock_update_plan
from tools import package_resolver
from tools.package_candidates import PackageCandidate
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


def version_range(minimum: str, maximum: str) -> dict[str, object]:
    return {
        "kind": "range",
        "minimumInclusive": minimum,
        "maximumExclusive": maximum,
        "allowPrerelease": False,
    }


def requirement(identity: str, constraint: dict[str, object]) -> dict[str, object]:
    return {"id": identity, "version": constraint}


class PackageLockUpdatePlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )

    def project(
        self,
        *,
        packages: list[dict[str, object]] | None = None,
        options: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": packages or [],
            "directFeatureSets": [],
            "packageOptions": options or [],
        }

    def installable(
        self,
        identity: str,
        version: str,
        *,
        dependencies: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["version"] = version
        manifest["displayName"] = identity
        manifest["dependencies"] = dependencies or []
        return manifest

    def candidate(
        self,
        manifest: dict[str, object],
        *,
        origin: str | None = None,
        source_id: str = "com.asharia.source.synthetic",
    ) -> PackageCandidate:
        stable_origin = origin or f"catalog/{manifest['id']}/{manifest['version']}"
        manifest_bytes = json.dumps(
            manifest,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        payload_bytes = f"payload:{stable_origin}".encode("utf-8")
        return PackageCandidate(
            identity=str(manifest["id"]),
            version=str(manifest["version"]),
            package_kind=str(manifest["packageKind"]),
            origin=stable_origin,
            source={"kind": "local", "sourceId": source_id},
            manifest_integrity={
                "algorithm": "sha256",
                "digest": hashlib.sha256(manifest_bytes).hexdigest(),
            },
            payload_integrity={
                "algorithm": "sha256",
                "digest": hashlib.sha256(payload_bytes).hexdigest(),
            },
            manifest=manifest,
        )

    def resolve_base(
        self,
        project: dict[str, object],
        candidates: list[PackageCandidate],
        distribution: dict[str, object] | None = None,
    ) -> tuple[dict[str, object], dict[str, object]]:
        current_distribution = (
            distribution
            if distribution is not None
            else package_test_support.make_engine_distribution()
        )
        result = package_resolver.resolve_package_graph(
            project,
            current_distribution,
            candidates,
            self.validators,
        )
        self.assertTrue(
            result.succeeded,
            [diagnostic.render() for diagnostic in result.diagnostics],
        )
        assert result.lock is not None
        return result.lock, current_distribution

    def plan(
        self,
        base_project: dict[str, object],
        proposed_project: dict[str, object],
        base_lock: dict[str, object],
        distribution: dict[str, object],
        candidates: object,
        request: package_lock_update_plan.LockUpdateRequest,
    ) -> package_lock_update_plan.PackageLockUpdatePlanResult:
        return package_lock_update_plan.plan_package_lock_update(
            base_project,
            proposed_project,
            base_lock,
            distribution,
            candidates,
            self.validators,
            request,
        )

    def assert_atomic_failure(
        self,
        result: package_lock_update_plan.PackageLockUpdatePlanResult,
        code: str,
    ) -> None:
        self.assertFalse(result.succeeded)
        self.assertIsNone(result.plan)
        self.assertIn(code, {diagnostic.code for diagnostic in result.diagnostics})

    def test_targeted_update_retains_unrelated_lock_and_moves_required_transitive(self) -> None:
        dependency = "com.asharia.system.a-dependency"
        unrelated = "com.asharia.system.m-unrelated"
        target = "com.asharia.system.z-target"
        broad = version_range("1.0.0", "3.0.0")
        project = self.project(
            packages=[requirement(unrelated, broad), requirement(target, broad)]
        )
        dependency_v1 = self.candidate(self.installable(dependency, "1.0.0"))
        dependency_v2 = self.candidate(self.installable(dependency, "2.0.0"))
        unrelated_v1 = self.candidate(self.installable(unrelated, "1.0.0"))
        unrelated_v2 = self.candidate(self.installable(unrelated, "2.0.0"))
        target_v1 = self.candidate(
            self.installable(
                target,
                "1.0.0",
                dependencies=[requirement(dependency, exact("1.0.0"))],
            )
        )
        target_v2 = self.candidate(
            self.installable(
                target,
                "2.0.0",
                dependencies=[requirement(dependency, exact("2.0.0"))],
            )
        )
        base_candidates = [dependency_v1, unrelated_v1, target_v1]
        base_lock, distribution = self.resolve_base(project, base_candidates)

        result = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [unrelated_v2, target_v2, dependency_v2, *reversed(base_candidates)],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(target,),
            ),
        )

        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        assert result.plan is not None
        versions = {
            node["id"]: node["version"] for node in result.plan.proposed_lock["nodes"]
        }
        self.assertEqual("2.0.0", versions[target])
        self.assertEqual("2.0.0", versions[dependency])
        self.assertEqual("1.0.0", versions[unrelated])
        impacts = {impact.identity: impact for impact in result.plan.impacts}
        self.assertEqual("requested-target", impacts[target].cause)
        self.assertEqual(
            {"upgraded", "dependencies-changed", "evidence-refreshed"},
            set(impacts[target].changes),
        )
        self.assertEqual("transitive", impacts[dependency].cause)
        self.assertEqual(
            {"upgraded", "evidence-refreshed"},
            set(impacts[dependency].changes),
        )
        self.assertNotIn(unrelated, impacts)
        self.assertLess(
            tuple(impact.identity for impact in result.plan.impacts).index(dependency),
            tuple(impact.identity for impact in result.plan.impacts).index(target),
        )

    def test_intent_only_option_change_does_not_unlock_the_package(self) -> None:
        identity = "com.asharia.system.optioned"
        project = self.project(
            packages=[
                requirement(identity, version_range("1.0.0", "3.0.0"))
            ]
        )
        proposed = copy.deepcopy(project)
        proposed["packageOptions"] = [
            {
                "packageId": identity,
                "values": [{"id": "validation", "value": False}],
            }
        ]
        version_one = self.candidate(self.installable(identity, "1.0.0"))
        version_two = self.candidate(self.installable(identity, "2.0.0"))
        base_lock, distribution = self.resolve_base(project, [version_one])
        request = package_lock_update_plan.LockUpdateRequest(
            package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
            intent_only_targets=(identity,),
        )

        retained = self.plan(
            project,
            proposed,
            base_lock,
            distribution,
            [version_two, version_one],
            request,
        )
        unavailable = self.plan(
            project,
            proposed,
            base_lock,
            distribution,
            [version_two],
            request,
        )

        self.assertTrue(
            retained.succeeded,
            [item.render() for item in retained.diagnostics],
        )
        assert retained.plan is not None
        self.assertEqual("1.0.0", retained.plan.proposed_lock["nodes"][0]["version"])
        impact = next(item for item in retained.plan.impacts if item.identity == identity)
        self.assertEqual("direct-intent", impact.cause)
        self.assertEqual(("package-options-changed",), impact.changes)
        self.assert_atomic_failure(unavailable, "resolver.preference.unavailable")

    def test_distribution_generation_change_requires_full_update(self) -> None:
        identity = "com.asharia.system.engine-bound"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [candidate])
        changed_distribution = copy.deepcopy(distribution)
        changed_distribution["context"]["toolchain"]["compilerVersion"] = "0.2.0"
        changed_distribution["engineGenerationId"] = (
            contracts.compute_engine_generation_id(changed_distribution)
        )

        targeted = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            changed_distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                intent_only_targets=(identity,),
            ),
        )
        full = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            changed_distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.FULL_UPDATE_MODE
            ),
        )

        self.assert_atomic_failure(
            targeted,
            "update.request.engine-input-change-requires-full",
        )
        self.assertTrue(full.succeeded, [item.render() for item in full.diagnostics])
        assert full.plan is not None
        self.assertTrue(full.plan.engine_input_changed)
        self.assertEqual(
            changed_distribution["engineGenerationId"],
            full.plan.proposed_lock["inputs"]["engine"]["engineGenerationId"],
        )

    def test_direct_and_engine_intent_changes_enforce_distinct_authorization(self) -> None:
        identity = "com.asharia.system.intent-bound"
        project = self.project(
            packages=[
                requirement(identity, version_range("1.0.0", "2.0.0"))
            ]
        )
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [candidate])
        direct_change = copy.deepcopy(project)
        direct_change["directPackages"][0]["version"] = exact("1.0.0")

        intent_only = self.plan(
            project,
            direct_change,
            base_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                intent_only_targets=(identity,),
            ),
        )
        unlocked = self.plan(
            project,
            direct_change,
            base_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(identity,),
            ),
        )

        self.assert_atomic_failure(
            intent_only,
            "update.request.direct-intent-change-untargeted",
        )
        self.assertTrue(unlocked.succeeded, [item.render() for item in unlocked.diagnostics])
        assert unlocked.plan is not None
        direct_impact = next(
            item for item in unlocked.plan.impacts if item.identity == identity
        )
        self.assertEqual("requested-target", direct_impact.cause)
        self.assertEqual(("direct-requirement-changed",), direct_impact.changes)

        engine_change = copy.deepcopy(project)
        engine_change["engine"]["apiVersion"] = version_range("0.1.0", "0.2.0")
        targeted_engine = self.plan(
            project,
            engine_change,
            base_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                intent_only_targets=(identity,),
            ),
        )
        full_engine = self.plan(
            project,
            engine_change,
            base_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.FULL_UPDATE_MODE
            ),
        )

        self.assert_atomic_failure(
            targeted_engine,
            "update.request.engine-change-requires-full",
        )
        self.assertTrue(
            full_engine.succeeded,
            [item.render() for item in full_engine.diagnostics],
        )
        assert full_engine.plan is not None
        self.assertTrue(full_engine.plan.project_manifest_changed)

    def test_unreachable_old_transitive_preference_does_not_fail(self) -> None:
        dependency = "com.asharia.system.old-dependency"
        root = "com.asharia.system.old-root"
        project = self.project(packages=[requirement(root, exact("1.0.0"))])
        root_candidate = self.candidate(
            self.installable(
                root,
                "1.0.0",
                dependencies=[requirement(dependency, exact("1.0.0"))],
            )
        )
        dependency_candidate = self.candidate(
            self.installable(dependency, "1.0.0")
        )
        base_lock, distribution = self.resolve_base(
            project,
            [root_candidate, dependency_candidate],
        )

        result = self.plan(
            project,
            self.project(),
            base_lock,
            distribution,
            [],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(root,),
            ),
        )

        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        assert result.plan is not None
        self.assertEqual([], result.plan.proposed_lock["nodes"])
        impacts = {impact.identity: impact for impact in result.plan.impacts}
        self.assertEqual("requested-target", impacts[root].cause)
        self.assertEqual("transitive", impacts[dependency].cause)
        self.assertIn("removed", impacts[root].changes)
        self.assertIn("removed", impacts[dependency].changes)

    def test_no_op_preserves_normalized_base_lock_bytes(self) -> None:
        identity = "com.asharia.system.stable"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [candidate])

        result = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.FULL_UPDATE_MODE
            ),
        )

        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        assert result.plan is not None
        self.assertEqual("no-changes", result.plan.status)
        self.assertEqual((), result.plan.impacts)
        self.assertEqual(result.plan.base_lock, result.plan.proposed_lock)
        self.assertEqual(
            contracts.render_normalized_lock_manifest(base_lock),
            contracts.render_normalized_lock_manifest(result.plan.proposed_lock),
        )

    def test_input_permutations_produce_identical_plan_and_preview(self) -> None:
        alpha = "com.asharia.system.alpha"
        omega = "com.asharia.system.omega"
        project = self.project(
            packages=[
                requirement(omega, exact("1.0.0")),
                requirement(alpha, exact("1.0.0")),
            ]
        )
        candidates = [
            self.candidate(self.installable(omega, "1.0.0")),
            self.candidate(self.installable(alpha, "1.0.0")),
        ]
        base_lock, distribution = self.resolve_base(project, candidates)
        reordered_project = copy.deepcopy(project)
        reordered_project["directPackages"].reverse()
        request = package_lock_update_plan.LockUpdateRequest(
            package_lock_update_plan.FULL_UPDATE_MODE
        )

        first = self.plan(
            project,
            project,
            base_lock,
            distribution,
            candidates,
            request,
        )
        second = self.plan(
            reordered_project,
            copy.deepcopy(reordered_project),
            copy.deepcopy(base_lock),
            dict(reversed(list(distribution.items()))),
            list(reversed(candidates)),
            request,
        )

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        assert first.plan is not None and second.plan is not None
        self.assertEqual(first.plan.proposed_lock, second.plan.proposed_lock)
        self.assertEqual(first.plan.plan_integrity, second.plan.plan_integrity)
        self.assertEqual(
            package_lock_update_plan.render_package_lock_update_preview(first.plan),
            package_lock_update_plan.render_package_lock_update_preview(second.plan),
        )

    def test_candidate_set_integrity_preserves_duplicate_multiset_entries(self) -> None:
        project = self.project()
        base_lock, distribution = self.resolve_base(project, [])
        unreferenced = self.candidate(
            self.installable("com.asharia.system.unreferenced", "1.0.0")
        )
        request = package_lock_update_plan.LockUpdateRequest(
            package_lock_update_plan.FULL_UPDATE_MODE
        )

        once = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [unreferenced],
            request,
        )
        twice = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [unreferenced, copy.deepcopy(unreferenced)],
            request,
        )

        self.assertTrue(once.succeeded)
        self.assertTrue(twice.succeeded)
        assert once.plan is not None and twice.plan is not None
        self.assertEqual(once.plan.proposed_lock, twice.plan.proposed_lock)
        self.assertNotEqual(
            once.plan.candidate_set_integrity,
            twice.plan.candidate_set_integrity,
        )
        self.assertNotEqual(once.plan.plan_integrity, twice.plan.plan_integrity)

    def test_candidate_fingerprints_ignore_mapping_order_and_adapter_origin(self) -> None:
        identity = "com.asharia.system.portable-candidate"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(
            self.installable(identity, "1.0.0"),
            origin=r"D:\adapter-cache\portable-candidate",
        )
        variant = replace(
            candidate,
            origin=r"E:\other-cache\portable-candidate",
            source=dict(reversed(list(candidate.source.items()))),
            manifest=dict(reversed(list(candidate.manifest.items()))),
        )
        base_lock, distribution = self.resolve_base(project, [candidate])
        request = package_lock_update_plan.LockUpdateRequest(
            package_lock_update_plan.FULL_UPDATE_MODE
        )

        first = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [candidate],
            request,
        )
        second = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [variant],
            request,
        )

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        assert first.plan is not None and second.plan is not None
        self.assertEqual(first.plan.candidate_set_integrity, second.plan.candidate_set_integrity)
        self.assertEqual(
            first.plan.selected_candidate_set_integrity,
            second.plan.selected_candidate_set_integrity,
        )
        self.assertEqual(first.plan.plan_integrity, second.plan.plan_integrity)
        self.assertEqual(
            package_lock_update_plan.render_package_lock_update_preview(first.plan),
            package_lock_update_plan.render_package_lock_update_preview(second.plan),
        )
        self.assertEqual(
            "local:com.asharia.source.synthetic",
            first.plan.selected_candidates[0].origin,
        )
        self.assertNotIn("adapter-cache", repr(first.plan.selected_candidates))

    def test_forged_resolver_selection_fails_atomically(self) -> None:
        identity = "com.asharia.system.output-bound"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [candidate])
        forged = replace(
            candidate,
            build_descriptor={"forged": True},
        )
        forged_resolution = package_resolver.ResolutionResult(
            lock=copy.deepcopy(base_lock),
            selected_candidates=(forged,),
            diagnostics=(),
        )

        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            return_value=forged_resolution,
        ):
            result = self.plan(
                project,
                copy.deepcopy(project),
                base_lock,
                distribution,
                [candidate],
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.FULL_UPDATE_MODE
                ),
            )

        self.assert_atomic_failure(
            result,
            "update.output.selected-candidate-unbound",
        )

        malformed_resolution = package_resolver.ResolutionResult(
            lock=copy.deepcopy(base_lock),
            selected_candidates=(
                replace(candidate, identity=[]),  # type: ignore[arg-type]
            ),
            diagnostics=(),
        )
        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            return_value=malformed_resolution,
        ):
            malformed_result = self.plan(
                project,
                copy.deepcopy(project),
                base_lock,
                distribution,
                [candidate],
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.FULL_UPDATE_MODE
                ),
            )
        self.assert_atomic_failure(
            malformed_result,
            "update.output.selected-candidate-metadata-invalid",
        )

    def test_forged_resolver_engine_and_policy_inputs_fail_atomically(self) -> None:
        identity = "com.asharia.system.forged-engine-input"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [candidate])
        changed_distribution = copy.deepcopy(distribution)
        changed_distribution["context"]["toolchain"]["compilerVersion"] = "0.2.0"
        changed_distribution["engineGenerationId"] = (
            contracts.compute_engine_generation_id(changed_distribution)
        )
        forged_lock = copy.deepcopy(base_lock)
        forged_lock["resolver"]["policyVersion"] = (
            package_resolver.LOCKED_PREFERENCE_POLICY_VERSION
        )
        forged_resolution = package_resolver.ResolutionResult(
            lock=forged_lock,
            selected_candidates=(candidate,),
            diagnostics=(),
        )

        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            return_value=forged_resolution,
        ):
            result = self.plan(
                project,
                copy.deepcopy(project),
                base_lock,
                changed_distribution,
                [candidate],
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.FULL_UPDATE_MODE
                ),
            )

        self.assert_atomic_failure(result, "update.output.engine-input-mismatch")
        self.assertIn(
            "update.output.resolver-policy-mismatch",
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_forged_resolver_candidate_order_fails_atomically(self) -> None:
        alpha = "com.asharia.system.output-alpha"
        omega = "com.asharia.system.output-omega"
        project = self.project(
            packages=[requirement(alpha, exact("1.0.0")), requirement(omega, exact("1.0.0"))]
        )
        candidates = [
            self.candidate(self.installable(alpha, "1.0.0")),
            self.candidate(self.installable(omega, "1.0.0")),
        ]
        base_lock, distribution = self.resolve_base(project, candidates)
        real = package_resolver.resolve_package_graph(
            project,
            distribution,
            candidates,
            self.validators,
        )
        self.assertTrue(real.succeeded)
        forged_resolution = package_resolver.ResolutionResult(
            lock=copy.deepcopy(real.lock),
            selected_candidates=tuple(reversed(real.selected_candidates)),
            diagnostics=(),
        )

        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            return_value=forged_resolution,
        ):
            result = self.plan(
                project,
                copy.deepcopy(project),
                base_lock,
                distribution,
                candidates,
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.FULL_UPDATE_MODE
                ),
            )

        self.assert_atomic_failure(
            result,
            "update.output.selected-candidates-noncanonical",
        )

    def test_forged_resolver_result_envelope_fails_atomically(self) -> None:
        project = self.project()
        base_lock, distribution = self.resolve_base(project, [])
        request = package_lock_update_plan.LockUpdateRequest(
            package_lock_update_plan.FULL_UPDATE_MODE
        )
        forged_values = (
            None,
            object(),
            package_resolver.ResolutionResult(
                lock=None,
                selected_candidates=(),
                diagnostics=None,  # type: ignore[arg-type]
            ),
        )
        for forged in forged_values:
            with self.subTest(forged_type=type(forged).__name__):
                with mock.patch.object(
                    package_lock_update_plan.package_resolver,
                    "resolve_package_graph",
                    return_value=forged,
                ):
                    result = self.plan(
                        project,
                        copy.deepcopy(project),
                        base_lock,
                        distribution,
                        [],
                        request,
                    )
                self.assert_atomic_failure(
                    result,
                    "update.output.resolution-envelope-invalid",
                )

    def test_resolver_cannot_mutate_authorization_or_input_fingerprints(self) -> None:
        root = "com.asharia.system.mutable-root"
        injected = "com.asharia.system.mutable-injected"
        project = self.project(packages=[requirement(root, exact("1.0.0"))])
        proposed = copy.deepcopy(project)
        root_candidate = self.candidate(self.installable(root, "1.0.0"))
        injected_candidate = self.candidate(self.installable(injected, "1.0.0"))
        candidates = [root_candidate, injected_candidate]
        base_lock, distribution = self.resolve_base(project, [root_candidate])
        real_resolve = package_resolver.resolve_package_graph

        def mutate_project(
            resolver_project: dict[str, object],
            resolver_distribution: dict[str, object],
            resolver_candidates: tuple[PackageCandidate, ...],
            validators: contracts.ContractValidators,
            **kwargs: object,
        ) -> package_resolver.ResolutionResult:
            resolver_project["directPackages"].append(  # type: ignore[index,union-attr]
                requirement(injected, exact("1.0.0"))
            )
            return real_resolve(
                resolver_project,
                resolver_distribution,
                resolver_candidates,
                validators,
                **kwargs,
            )

        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            side_effect=mutate_project,
        ):
            result = self.plan(
                project,
                proposed,
                base_lock,
                distribution,
                candidates,
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                    unlock_targets=(root,),
                ),
            )

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.plan)
        self.assertTrue(
            any(code.startswith("update.output.") for code in {
                diagnostic.code for diagnostic in result.diagnostics
            })
        )
        self.assertEqual([root], [item["id"] for item in proposed["directPackages"]])

        original_generation = distribution["engineGenerationId"]
        original_source = copy.deepcopy(root_candidate.source)

        def mutate_distribution_and_candidate(
            resolver_project: dict[str, object],
            resolver_distribution: dict[str, object],
            resolver_candidates: tuple[PackageCandidate, ...],
            validators: contracts.ContractValidators,
            **kwargs: object,
        ) -> package_resolver.ResolutionResult:
            resolver_distribution["context"]["toolchain"]["compilerVersion"] = "9.9.9"  # type: ignore[index]
            resolver_distribution["engineGenerationId"] = (
                contracts.compute_engine_generation_id(resolver_distribution)
            )
            resolver_candidates[0].source["sourceId"] = "com.asharia.source.mutated"
            return real_resolve(
                resolver_project,
                resolver_distribution,
                resolver_candidates,
                validators,
                **kwargs,
            )

        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            side_effect=mutate_distribution_and_candidate,
        ):
            mutated_result = self.plan(
                project,
                copy.deepcopy(project),
                base_lock,
                distribution,
                [root_candidate],
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                    unlock_targets=(root,),
                ),
            )

        self.assert_atomic_failure(
            mutated_result,
            "update.output.engine-input-mismatch",
        )
        self.assertIn(
            "update.output.selected-candidate-unbound",
            {diagnostic.code for diagnostic in mutated_result.diagnostics},
        )
        self.assertEqual(original_generation, distribution["engineGenerationId"])
        self.assertEqual(original_source, root_candidate.source)

    def test_equal_precedence_version_string_change_is_not_an_upgrade(self) -> None:
        identity = "com.asharia.system.metadata-version"
        project = self.project(
            packages=[
                requirement(identity, version_range("1.0.0", "2.0.0"))
            ]
        )
        base_candidate = self.candidate(
            self.installable(identity, "1.0.0+z-base")
        )
        replacement = self.candidate(
            self.installable(identity, "1.0.0+a-replacement")
        )
        base_lock, distribution = self.resolve_base(project, [base_candidate])

        result = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [replacement],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(identity,),
            ),
        )

        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        assert result.plan is not None
        impact = next(item for item in result.plan.impacts if item.identity == identity)
        self.assertIn("version-changed", impact.changes)
        self.assertIn("evidence-refreshed", impact.changes)
        self.assertNotIn("upgraded", impact.changes)
        self.assertNotIn("downgraded", impact.changes)

    def test_request_validation_is_atomic_and_role_aware(self) -> None:
        project = self.project()
        base_lock, distribution = self.resolve_base(project, [])
        alpha = "com.asharia.system.alpha"
        omega = "com.asharia.system.omega"
        cases = (
            (
                "mutable",
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                    unlock_targets=[alpha],  # type: ignore[arg-type]
                ),
                "update.request.targets-invalid",
            ),
            (
                "duplicate",
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                    unlock_targets=(alpha, alpha),
                ),
                "update.request.target-duplicate",
            ),
            (
                "noncanonical",
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                    unlock_targets=(omega, alpha),
                ),
                "update.request.targets-noncanonical",
            ),
            (
                "overlap",
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                    unlock_targets=(alpha,),
                    intent_only_targets=(alpha,),
                ),
                "update.request.target-role-conflict",
            ),
            (
                "full-with-target",
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.FULL_UPDATE_MODE,
                    unlock_targets=(alpha,),
                ),
                "update.request.full-targets-forbidden",
            ),
            (
                "targeted-empty",
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.TARGETED_CONSERVATIVE_MODE
                ),
                "update.request.targets-required",
            ),
        )
        for name, request, expected_code in cases:
            with self.subTest(name=name):
                result = self.plan(
                    project,
                    copy.deepcopy(project),
                    base_lock,
                    distribution,
                    [],
                    request,
                )
                self.assert_atomic_failure(result, expected_code)

        unknown = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(alpha,),
            ),
        )
        self.assert_atomic_failure(unknown, "update.request.target-unknown")

    def test_intent_only_targets_require_a_real_option_change_and_locked_baseline(self) -> None:
        identity = "com.asharia.system.intent-only"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [candidate])

        unused = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                intent_only_targets=(identity,),
            ),
        )
        self.assert_atomic_failure(
            unused,
            "update.request.intent-only-target-unused",
        )

        empty_project = self.project()
        empty_lock, _ = self.resolve_base(empty_project, [])
        proposed = copy.deepcopy(empty_project)
        proposed["packageOptions"] = [
            {
                "packageId": identity,
                "values": [{"id": "validation", "value": False}],
            }
        ]
        missing_baseline = self.plan(
            empty_project,
            proposed,
            empty_lock,
            distribution,
            [candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                intent_only_targets=(identity,),
            ),
        )
        self.assert_atomic_failure(
            missing_baseline,
            "update.request.intent-only-baseline-missing",
        )

    def test_resolver_diagnostics_ignore_adapter_candidate_origins(self) -> None:
        identity = "com.asharia.system.redacted"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        base_candidate = self.candidate(self.installable(identity, "1.0.0"))
        base_lock, distribution = self.resolve_base(project, [base_candidate])
        windows_candidate = self.candidate(
            self.installable(identity, "1.0.0"),
            origin=r"D:\private dir\catalog\redacted",
            source_id="com.asharia.source.windows",
        )
        posix_candidate = self.candidate(
            self.installable(identity, "1.0.0"),
            origin="/srv/private dir/catalog/redacted",
            source_id="com.asharia.source.posix",
        )

        result = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [posix_candidate, windows_candidate],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(identity,),
            ),
        )
        swapped = self.plan(
            project,
            copy.deepcopy(project),
            base_lock,
            distribution,
            [
                replace(posix_candidate, origin=windows_candidate.origin),
                replace(windows_candidate, origin=posix_candidate.origin),
            ],
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.TARGETED_CONSERVATIVE_MODE,
                unlock_targets=(identity,),
            ),
        )

        self.assert_atomic_failure(result, "resolver.candidate.ambiguous")
        self.assert_atomic_failure(swapped, "resolver.candidate.ambiguous")
        rendered = "\n".join(item.render() for item in result.diagnostics)
        swapped_rendered = "\n".join(item.render() for item in swapped.diagnostics)
        self.assertEqual(rendered, swapped_rendered)
        self.assertNotIn(r"D:\private", rendered)
        self.assertNotIn("/srv/private", rendered)
        self.assertNotIn(r"dir\catalog\redacted", rendered)
        self.assertNotIn("dir/catalog/redacted", rendered)

    def test_resolver_diagnostic_identity_is_path_redacted(self) -> None:
        project = self.project()
        base_lock, distribution = self.resolve_base(project, [])
        forged_failure = package_resolver.ResolutionResult(
            lock=None,
            selected_candidates=(),
            diagnostics=(
                package_resolver.ResolverDiagnostic(
                    code="resolver.test.failure",
                    identity=r"D:\private dir\identity",
                    message="synthetic resolver failure",
                ),
            ),
        )

        with mock.patch.object(
            package_lock_update_plan.package_resolver,
            "resolve_package_graph",
            return_value=forged_failure,
        ):
            result = self.plan(
                project,
                copy.deepcopy(project),
                base_lock,
                distribution,
                [],
                package_lock_update_plan.LockUpdateRequest(
                    package_lock_update_plan.FULL_UPDATE_MODE
                ),
            )

        self.assert_atomic_failure(result, "resolver.test.failure")
        rendered = "\n".join(item.render() for item in result.diagnostics)
        self.assertIn("<redacted-path>", rendered)
        self.assertNotIn("private dir", rendered)

    def test_returned_plan_is_detached_from_caller_mutation(self) -> None:
        identity = "com.asharia.system.detached"
        base_project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        proposed_project = copy.deepcopy(base_project)
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        candidates = [candidate]
        base_lock, distribution = self.resolve_base(base_project, candidates)
        result = self.plan(
            base_project,
            proposed_project,
            base_lock,
            distribution,
            candidates,
            package_lock_update_plan.LockUpdateRequest(
                package_lock_update_plan.FULL_UPDATE_MODE
            ),
        )
        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        assert result.plan is not None
        preview_before = package_lock_update_plan.render_package_lock_update_preview(
            result.plan
        )
        selected_before = result.plan.selected_candidates

        base_project["directPackages"].clear()
        proposed_project["directPackages"].clear()
        base_lock["nodes"].clear()
        distribution["distribution"]["id"] = "com.asharia.distribution.mutated"
        candidate.source["sourceId"] = "com.asharia.source.mutated"
        candidate.manifest["id"] = "com.asharia.system.mutated"
        candidate.payload_integrity["digest"] = "0" * 64
        exposed = result.plan.selected_candidates
        exposed[0].source["sourceId"] = "com.asharia.source.exposed-mutation"

        self.assertEqual(preview_before, package_lock_update_plan.render_package_lock_update_preview(result.plan))
        self.assertEqual(identity, result.plan.base_project["directPackages"][0]["id"])
        self.assertEqual(identity, result.plan.proposed_lock["nodes"][0]["id"])
        self.assertEqual(selected_before, result.plan.selected_candidates)


if __name__ == "__main__":
    unittest.main()
