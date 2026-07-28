"""Locked Package Graph Verification & Reuse tests for Package Lock v2."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts
from tools import package_candidate_discovery
from tools import package_lock_verification
from tools import package_resolver
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
ENGINE_API_VERSION = "0.1.0"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


class PackageLockVerificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )

    def installable(
        self,
        identity: str,
        *,
        dependencies: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["version"] = "1.0.0"
        manifest["displayName"] = identity
        manifest["dependencies"] = dependencies or []
        return manifest

    def write_package(self, root: Path, manifest: dict[str, object]) -> Path:
        root.mkdir(parents=True)
        (root / check_package_contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (root / "payload.bin").write_bytes(str(manifest["id"]).encode("utf-8"))
        return root

    def resolved_graph(
        self,
        temporary_root: Path,
    ) -> tuple[
        dict[str, object],
        list[package_resolver.PackageCandidate],
        dict[str, object],
        dict[str, Path],
    ]:
        dependency_id = "com.asharia.system.locked-dependency"
        root_id = "com.asharia.system.locked-root"
        roots = {
            dependency_id: self.write_package(
                temporary_root / "dependency",
                self.installable(dependency_id),
            ),
            root_id: self.write_package(
                temporary_root / "root",
                self.installable(
                    root_id,
                    dependencies=[{"id": dependency_id, "version": exact("1.0.0")}],
                ),
            ),
        }
        locations = [
            package_candidate_discovery.LocalCandidateLocation(
                f"com.asharia.source.{identity.rsplit('.', 1)[-1]}",
                root,
            )
            for identity, root in roots.items()
        ]
        discovery = package_candidate_discovery.load_package_candidates(
            locations,
            self.validators,
        )
        self.assertTrue(discovery.succeeded, [item.render() for item in discovery.diagnostics])
        project = {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [{"id": root_id, "version": exact("1.0.0")}],
            "directFeatureSets": [],
            "packageOptions": [],
        }
        resolution = package_resolver.resolve_package_graph(
            project,
            package_test_support.make_engine_distribution(),
            discovery.candidates,
            self.validators,
        )
        self.assertTrue(resolution.succeeded, [item.render() for item in resolution.diagnostics])
        return project, list(discovery.candidates), resolution.lock, roots

    def verify(
        self,
        project: dict[str, object],
        lock: dict[str, object] | None,
        candidates: object,
        *,
        engine_api_version: str = ENGINE_API_VERSION,
    ) -> package_lock_verification.LockedGraphVerificationResult:
        return package_lock_verification.verify_locked_package_graph(
            project,
            package_test_support.make_engine_distribution(
                engine_api_version=engine_api_version
            ),
            lock,
            candidates,
            self.validators,
        )

    def assert_atomic_failure(
        self,
        result: package_lock_verification.LockedGraphVerificationResult,
    ) -> None:
        self.assertFalse(result.succeeded)
        self.assertIsNone(result.lock)
        self.assertEqual((), result.selected_candidates)
        self.assertTrue(result.diagnostics)

    def test_exact_graph_is_reused_canonically_and_inputs_stay_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            reordered_lock = copy.deepcopy(lock)
            reordered_lock["nodes"].reverse()
            project_before = copy.deepcopy(project)
            lock_before = copy.deepcopy(reordered_lock)
            candidates_before = copy.deepcopy(candidates)

            first = self.verify(project, reordered_lock, candidates)
            reordered_candidates = [
                replace(candidate, source=dict(reversed(list(candidate.source.items()))))
                for candidate in reversed(candidates)
            ]
            second = self.verify(project, lock, reordered_candidates)

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        self.assertEqual(
            check_package_contracts.render_normalized_lock_manifest(lock),
            check_package_contracts.render_normalized_lock_manifest(first.lock),
        )
        self.assertEqual(first.lock, second.lock)
        self.assertEqual(
            tuple(sorted(candidate.identity for candidate in candidates)),
            tuple(candidate.identity for candidate in first.selected_candidates),
        )
        self.assertEqual(project_before, project)
        self.assertEqual(lock_before, reordered_lock)
        self.assertEqual(candidates_before, candidates)

    def test_distributed_lock_binding_uses_inventory_and_rejects_shadowing(self) -> None:
        identity = "com.asharia.system.distributed-locked"
        with tempfile.TemporaryDirectory() as temporary_directory:
            distribution_root = Path(temporary_directory) / "distribution"
            self.write_package(
                distribution_root / "packages/distributed-locked",
                self.installable(identity),
            )
            discovery = package_candidate_discovery.load_package_candidates(
                [
                    package_candidate_discovery.EngineDistributedCandidateLocation(
                        distribution_root,
                        "packages/distributed-locked",
                    )
                ],
                self.validators,
            )
            self.assertTrue(discovery.succeeded)
            candidate = discovery.candidates[0]
            distribution = package_test_support.make_engine_distribution(
                discovery.candidates
            )
            project = {
                "schema": "com.asharia.project-packages",
                "schemaVersion": 2,
                "engine": package_test_support.engine_requirement(),
                "directPackages": [{"id": identity, "version": exact("1.0.0")}],
                "directFeatureSets": [],
                "packageOptions": [],
            }
            resolution = package_resolver.resolve_package_graph(
                project,
                distribution,
                discovery.candidates,
                self.validators,
            )
            self.assertTrue(resolution.succeeded)
            verified = package_lock_verification.verify_locked_package_graph(
                project,
                distribution,
                resolution.lock,
                discovery.candidates,
                self.validators,
            )

            payload_path = candidate.payload_location / "payload.bin"
            original_payload = payload_path.read_bytes()
            payload_path.write_bytes(b"changed-after-distribution-discovery")
            drifted = package_lock_verification.verify_locked_package_graph(
                project,
                distribution,
                resolution.lock,
                discovery.candidates,
                self.validators,
            )
            payload_path.write_bytes(original_payload)

            changed = replace(
                candidate,
                payload_integrity={"algorithm": "sha256", "digest": "0" * 64},
            )
            mismatched = package_lock_verification.verify_locked_package_graph(
                project,
                distribution,
                resolution.lock,
                [changed],
                self.validators,
            )
            shadow = replace(
                candidate,
                origin="local:com.asharia.source.shadow",
                source={"kind": "local", "sourceId": "com.asharia.source.shadow"},
            )
            shadowed = package_lock_verification.verify_locked_package_graph(
                project,
                distribution,
                resolution.lock,
                [candidate, shadow],
                self.validators,
            )
            stale_distribution = copy.deepcopy(distribution)
            stale_distribution["context"]["toolchain"]["compilerVersion"] = "0.2.0"
            stale_distribution["engineGenerationId"] = (
                check_package_contracts.compute_engine_generation_id(stale_distribution)
            )
            stale = package_lock_verification.verify_locked_package_graph(
                project,
                stale_distribution,
                resolution.lock,
                discovery.candidates,
                self.validators,
            )

        self.assertTrue(verified.succeeded)
        self.assertIn(
            "lock.engine.distribution-candidate-mismatch",
            {item.code for item in drifted.diagnostics},
        )
        self.assertIn(
            "lock.engine.distribution-candidate-mismatch",
            {item.code for item in mismatched.diagnostics},
        )
        self.assertIn(
            "lock.engine.distribution-shadowed",
            {item.code for item in shadowed.diagnostics},
        )
        self.assertIn(
            "lock.input.engine-generation-stale",
            {item.code for item in stale.diagnostics},
        )

    def test_missing_invalid_and_stale_inputs_fail_before_binding(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            missing = self.verify(project, None, candidates)
            invalid_engine = self.verify(
                project,
                lock,
                candidates,
                engine_api_version="not-semver",
            )
            stale_engine = self.verify(
                project,
                lock,
                candidates,
                engine_api_version="0.1.1",
            )
            stale_project = copy.deepcopy(project)
            stale_project["directPackages"][0]["version"] = exact("1.0.1")
            stale_manifest = self.verify(stale_project, lock, candidates)
            invalid_lock = copy.deepcopy(lock)
            invalid_lock["unexpected"] = True
            invalid_contract = self.verify(project, invalid_lock, candidates)

        for result in (
            missing,
            invalid_engine,
            stale_engine,
            stale_manifest,
            invalid_contract,
        ):
            self.assert_atomic_failure(result)
        self.assertIn("lock.input.missing", {item.code for item in missing.diagnostics})
        self.assertIn(
            "distribution.manifest.schema",
            {item.code for item in invalid_engine.diagnostics},
        )
        self.assertIn(
            "lock.input.engine-api-stale",
            {item.code for item in stale_engine.diagnostics},
        )
        self.assertIn(
            "lock.input.engine-generation-stale",
            {item.code for item in stale_engine.diagnostics},
        )
        self.assertEqual(
            ["lock.input.project-manifest-stale"],
            [item.code for item in stale_manifest.diagnostics],
        )
        self.assertIn("lock.manifest.schema", {item.code for item in invalid_contract.diagnostics})

    def test_locked_source_does_not_fall_back_to_another_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            selected = candidates[0]
            alternate = replace(
                selected,
                origin="local:com.asharia.source.alternate",
                source={"kind": "local", "sourceId": "com.asharia.source.alternate"},
            )
            result = self.verify(
                project,
                lock,
                [alternate, *candidates[1:]],
            )

        self.assert_atomic_failure(result)
        self.assertEqual(
            ["lock.source.unavailable"],
            [item.code for item in result.diagnostics],
        )

    def test_one_locked_source_must_bind_exactly_one_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            first = self.verify(project, lock, [*candidates, candidates[0]])
            second = self.verify(project, lock, [candidates[0], *reversed(candidates)])

        for result in (first, second):
            self.assert_atomic_failure(result)
        self.assertEqual(
            [item.render() for item in first.diagnostics],
            [item.render() for item in second.diagnostics],
        )
        self.assertEqual(
            ["lock.candidate.ambiguous-binding"],
            [item.code for item in first.diagnostics],
        )

    def test_locked_source_metadata_mismatches_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            cases = (
                ("identity", {"identity": "com.asharia.system.other"}, "identity-mismatch"),
                ("version", {"version": "1.0.1"}, "version-mismatch"),
                ("kind", {"package_kind": "feature-set"}, "kind-mismatch"),
            )
            results = [
                (
                    name,
                    expected,
                    self.verify(
                        project,
                        lock,
                        [replace(candidates[0], **changes), *candidates[1:]],
                    ),
                )
                for name, changes, expected in cases
            ]

        for name, expected, result in results:
            with self.subTest(name=name):
                self.assert_atomic_failure(result)
                codes = {item.code for item in result.diagnostics}
                self.assertIn(f"lock.candidate.{expected}", codes)
                self.assertIn("lock.candidate.metadata-mismatch", codes)

    def test_candidate_evidence_mismatch_is_rejected_before_rehash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            changed = replace(
                candidates[0],
                payload_integrity={"algorithm": "sha256", "digest": "0" * 64},
            )
            result = self.verify(project, lock, [changed, *candidates[1:]])

        self.assert_atomic_failure(result)
        self.assertEqual(
            ["lock.integrity.payload-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_author_graph_drift_is_rejected_by_cross_document_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            root_index = next(
                index
                for index, candidate in enumerate(candidates)
                if candidate.identity == "com.asharia.system.locked-root"
            )
            changed_manifest = copy.deepcopy(candidates[root_index].manifest)
            changed_manifest["dependencies"] = []
            changed = replace(candidates[root_index], manifest=changed_manifest)
            changed_candidates = [*candidates]
            changed_candidates[root_index] = changed
            result = self.verify(project, lock, changed_candidates)

        self.assert_atomic_failure(result)
        self.assertIn(
            "lock.edge.undeclared-author-dependency",
            {item.code for item in result.diagnostics},
        )

    def test_payload_change_after_discovery_is_caught_by_rehash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, roots = self.resolved_graph(Path(temporary_directory))
            (roots["com.asharia.system.locked-dependency"] / "payload.bin").write_bytes(
                b"changed-after-discovery"
            )
            result = self.verify(project, lock, candidates)

        self.assert_atomic_failure(result)
        self.assertEqual(
            ["lock.integrity.payload-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_manifest_change_after_discovery_is_caught_by_rehash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, roots = self.resolved_graph(Path(temporary_directory))
            manifest_path = (
                roots["com.asharia.system.locked-root"]
                / check_package_contracts.PACKAGE_MANIFEST_NAME
            )
            changed_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            changed_manifest["description"] = "changed after discovery"
            manifest_path.write_text(
                json.dumps(changed_manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
            result = self.verify(project, lock, candidates)

        self.assert_atomic_failure(result)
        self.assertEqual(
            {"lock.integrity.manifest-mismatch", "lock.integrity.payload-mismatch"},
            {item.code for item in result.diagnostics},
        )

    def test_unreferenced_candidates_do_not_change_reuse_result(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            project, candidates, lock, _ = self.resolved_graph(temporary_root)
            extra_root = self.write_package(
                temporary_root / "extra",
                self.installable("com.asharia.system.unreferenced"),
            )
            extra_discovery = package_candidate_discovery.load_package_candidates(
                [
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.unreferenced",
                        extra_root,
                    )
                ],
                self.validators,
            )
            self.assertTrue(extra_discovery.succeeded)
            baseline = self.verify(project, lock, candidates)
            with_extra = self.verify(
                project,
                lock,
                [extra_discovery.candidates[0], *reversed(candidates)],
            )

        self.assertTrue(baseline.succeeded)
        self.assertTrue(with_extra.succeeded)
        self.assertEqual(baseline.lock, with_extra.lock)
        self.assertEqual(
            tuple(candidate.identity for candidate in baseline.selected_candidates),
            tuple(candidate.identity for candidate in with_extra.selected_candidates),
        )

    def test_verification_never_resolves_or_writes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project, candidates, lock, _ = self.resolved_graph(Path(temporary_directory))
            with (
                mock.patch.object(
                    package_resolver,
                    "resolve_package_graph",
                    side_effect=AssertionError("resolver must not be called"),
                ),
                mock.patch.object(
                    Path,
                    "write_text",
                    side_effect=AssertionError("verification must not write text"),
                ),
                mock.patch.object(
                    Path,
                    "write_bytes",
                    side_effect=AssertionError("verification must not write bytes"),
                ),
            ):
                result = self.verify(project, lock, candidates)

        self.assertTrue(result.succeeded)

    def test_invalid_candidate_collection_fails_atomically(self) -> None:
        project = {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [],
            "directFeatureSets": [],
            "packageOptions": [],
        }
        empty_lock = package_resolver.resolve_package_graph(
            project,
            package_test_support.make_engine_distribution(),
            [],
            self.validators,
        ).lock

        not_iterable = self.verify(project, empty_lock, None)
        wrong_item = self.verify(project, empty_lock, [object()])

        for result in (not_iterable, wrong_item):
            self.assert_atomic_failure(result)
            self.assertEqual(
                ["lock.input.candidates-invalid"],
                [item.code for item in result.diagnostics],
            )


if __name__ == "__main__":
    unittest.main()
