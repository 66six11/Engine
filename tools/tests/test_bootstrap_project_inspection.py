"""Bootstrap project-open package inspection adapter tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import bootstrap_project_inspection as inspection
from tools import bootstrap_host_session
from tools import bootstrap_project_host
from tools import bootstrap_session
from tools import check_package_contracts as contracts
from tools import effective_session
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class BootstrapProjectInspectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.package_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )
        profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-editor.json").read_text(
                encoding="utf-8"
            )
        )
        cls.profile_snapshot = package_test_support.make_host_profile_snapshot(
            profile
        )

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.project_root = self.root / "project"
        self.distribution_root = self.root / "distribution"
        self.project_root.mkdir()
        self.distribution_root.mkdir()

        self.engine_id = "com.asharia.system.bootstrap-engine"
        self.embedded_id = "com.asharia.system.bootstrap-embedded"
        self.local_id = "com.asharia.system.bootstrap-local"
        self.local_source_id = "com.asharia.source.bootstrap-local"
        self.embedded_relative_path = "Packages/bootstrap-embedded"
        self.engine_relative_path = "packages/bootstrap-engine"
        self.local_root = self.root / "workspace" / "bootstrap-local"

        locations: list[discovery.CandidateLocation] = [
            discovery.EngineDistributedCandidateLocation(
                self.distribution_root, self.engine_relative_path
            ),
            discovery.ProjectEmbeddedCandidateLocation(
                self.project_root, self.embedded_relative_path
            ),
            discovery.LocalCandidateLocation(
                self.local_source_id, self.local_root
            ),
        ]
        for identity, package_root in (
            (self.engine_id, self.distribution_root / self.engine_relative_path),
            (self.embedded_id, self.project_root / self.embedded_relative_path),
            (self.local_id, self.local_root),
        ):
            self._write_package(package_root, identity)

        discovered = discovery.load_package_candidates(
            locations, self.validators
        )
        self.assertTrue(
            discovered.succeeded,
            "\n".join(value.render() for value in discovered.diagnostics),
        )
        self.distribution = package_test_support.make_engine_distribution(
            discovered.candidates,
            host_profile_snapshots=(self.profile_snapshot,),
        )
        self.project = {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [
                {
                    "id": identity,
                    "version": {"kind": "exact", "version": "0.1.0"},
                }
                for identity in (self.engine_id, self.embedded_id, self.local_id)
            ],
            "directFeatureSets": [],
            "packageOptions": [],
        }
        resolved = package_resolver.resolve_package_graph(
            self.project,
            self.distribution,
            discovered.candidates,
            self.validators,
        )
        self.assertTrue(
            resolved.succeeded,
            "\n".join(value.render() for value in resolved.diagnostics),
        )
        self.lock = resolved.lock
        assert self.lock is not None
        self.project_bytes = self._write_json(
            self.project_root / contracts.PROJECT_MANIFEST_NAME, self.project
        )
        self.lock_bytes = self._write_json(
            self.project_root / contracts.PACKAGE_LOCK_NAME, self.lock
        )
        # Project Bootstrap owns this descriptor; package inspection must ignore it.
        (self.project_root / "asharia.project.json").write_text(
            "{not-json", encoding="utf-8", newline="\n"
        )

        distribution_bytes = (
            json.dumps(self.distribution, ensure_ascii=False, indent=2) + "\n"
        ).encode("utf-8")
        verified = distribution_verifier.VerifiedInstalledDistribution(
            engine_generation_id=self.distribution["engineGenerationId"],
            generation_root=self.distribution_root,
            manifest=copy.deepcopy(self.distribution),
            manifest_bytes=distribution_bytes,
            manifest_integrity=contracts.compute_bytes_integrity(
                distribution_bytes
            ),
        )
        self.request = bootstrap_session.ProjectOpenRequestV1(
            project_root=self.project_root,
            verified_distribution=verified,
            host_profile_snapshot=self.profile_snapshot,
            local_sources=(
                discovery.LocalCandidateLocation(
                    self.local_source_id, self.local_root
                ),
            ),
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _write_package(self, root: Path, identity: str) -> None:
        root.mkdir(parents=True)
        manifest = copy.deepcopy(self.package_template)
        manifest["id"] = identity
        manifest["displayName"] = identity
        manifest["dependencies"] = []
        self._write_json(root / contracts.PACKAGE_MANIFEST_NAME, manifest)
        (root / "payload.bin").write_bytes(identity.encode("utf-8"))

    @staticmethod
    def _write_json(path: Path, value: object) -> bytes:
        exact_bytes = (
            json.dumps(value, ensure_ascii=False, indent=2) + "\n"
        ).encode("utf-8")
        path.write_bytes(exact_bytes)
        return exact_bytes

    def test_ready_uses_one_canonical_root_and_only_locked_sources(self) -> None:
        request = replace(
            self.request,
            project_root=self.project_root / ".",
            local_sources=(
                *self.request.local_sources,
                discovery.LocalCandidateLocation(
                    "com.asharia.source.unreferenced",
                    self.root / "must-not-be-discovered",
                ),
            ),
        )
        with mock.patch.object(
            package_resolver,
            "resolve_package_graph",
            side_effect=AssertionError("inspection must not resolve"),
        ):
            result = inspection.inspect_project_open_request(
                request, self.validators
            )

        self.assertEqual((), result.failure_owners)
        self.assertEqual((), result.diagnostics)
        self.assertIsNotNone(result.package_snapshot)
        self.assertIsNotNone(result.effective_session)
        assert result.package_snapshot is not None
        assert result.effective_session is not None
        self.assertEqual(
            self.project_root.resolve(), result.package_snapshot.project_root
        )
        self.assertEqual(
            self.project_bytes, result.package_snapshot.project_manifest_bytes
        )
        self.assertEqual(self.lock_bytes, result.package_snapshot.lock_bytes)
        self.assertTrue(result.effective_session.succeeded)
        assert result.effective_session.plan is not None
        self.assertEqual(
            {
                f"engine-distribution:{self.engine_relative_path}",
                f"project-embedded:{self.embedded_relative_path}",
                f"local:{self.local_source_id}",
            },
            {
                value.origin
                for value in (
                    result.effective_session.plan.verified_graph.selected_candidates
                )
            },
        )

    def test_invalid_contract_and_unmapped_local_source_are_project_owned(
        self,
    ) -> None:
        (self.project_root / contracts.PROJECT_MANIFEST_NAME).write_bytes(b"{")
        invalid = inspection.inspect_project_open_request(
            self.request, self.validators
        )
        self.assertEqual(
            (bootstrap_session.InspectionFailureOwnerV1.PROJECT,),
            invalid.failure_owners,
        )
        self.assertIn(
            "contract.manifest.json",
            [item.code for item in invalid.diagnostics],
        )
        self.assertIsNone(invalid.package_snapshot)

        (self.project_root / contracts.PROJECT_MANIFEST_NAME).write_bytes(
            self.project_bytes
        )
        unmapped = inspection.inspect_project_open_request(
            replace(self.request, local_sources=()), self.validators
        )
        self.assertEqual(
            (bootstrap_session.InspectionFailureOwnerV1.PROJECT,),
            unmapped.failure_owners,
        )
        self.assertIn(
            "bootstrap.project.local-source-unmapped",
            [item.code for item in unmapped.diagnostics],
        )
        self.assertIsNone(unmapped.effective_session)

    def test_distribution_inventory_failures_are_distribution_owned(self) -> None:
        manifest = copy.deepcopy(self.request.verified_distribution.manifest)
        manifest["bundledPackages"] = []
        manifest["engineGenerationId"] = contracts.compute_engine_generation_id(
            manifest
        )
        exact_bytes = (
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
        ).encode("utf-8")
        verified = distribution_verifier.VerifiedInstalledDistribution(
            engine_generation_id=manifest["engineGenerationId"],
            generation_root=self.distribution_root,
            manifest=manifest,
            manifest_bytes=exact_bytes,
            manifest_integrity=contracts.compute_bytes_integrity(exact_bytes),
        )

        result = inspection.inspect_project_open_request(
            replace(self.request, verified_distribution=verified),
            self.validators,
        )

        self.assertEqual(
            (bootstrap_session.InspectionFailureOwnerV1.DISTRIBUTION,),
            result.failure_owners,
        )
        self.assertIn(
            "bootstrap.distribution.package-inventory-mismatch",
            [item.code for item in result.diagnostics],
        )

    def test_contract_mutation_during_planning_invalidates_session(self) -> None:
        real_planner = effective_session.plan_effective_session

        def mutate_after_planning(*args, **kwargs):
            result = real_planner(*args, **kwargs)
            (self.project_root / contracts.PACKAGE_LOCK_NAME).write_bytes(
                self.lock_bytes + b" "
            )
            return result

        with mock.patch.object(
            inspection.effective_session,
            "plan_effective_session",
            side_effect=mutate_after_planning,
        ):
            result = inspection.inspect_project_open_request(
                self.request, self.validators
            )

        self.assertEqual(
            (bootstrap_session.InspectionFailureOwnerV1.PROJECT,),
            result.failure_owners,
        )
        self.assertIsNone(result.effective_session)
        self.assertIn(
            "bootstrap.project.contract-changed",
            [item.code for item in result.diagnostics],
        )

    def test_unsupported_request_is_control_plane_owned(self) -> None:
        result = inspection.inspect_project_open_request(object(), self.validators)

        self.assertEqual(
            (bootstrap_session.InspectionFailureOwnerV1.CONTROL_PLANE,),
            result.failure_owners,
        )
        self.assertEqual(
            ["bootstrap.inspection.request-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_manifest_lock_and_candidate_changes_never_launch_old_host(
        self,
    ) -> None:
        local_payload = self.local_root / "payload.bin"
        original_payload = local_payload.read_bytes()

        def change_manifest() -> None:
            changed = copy.deepcopy(self.project)
            changed["directPackages"] = [
                value
                for value in changed["directPackages"]
                if value["id"] != self.local_id
            ]
            self._write_json(
                self.project_root / contracts.PROJECT_MANIFEST_NAME,
                changed,
            )

        def change_lock() -> None:
            changed = copy.deepcopy(self.lock)
            node = next(
                value
                for value in changed["nodes"]
                if value["id"] == self.local_id
            )
            node["payloadIntegrity"]["digest"] = "f" * 64
            self._write_json(
                self.project_root / contracts.PACKAGE_LOCK_NAME,
                changed,
            )

        def change_candidate() -> None:
            local_payload.write_bytes(b"changed-candidate-payload")

        context = bootstrap_host_session.BootstrapHostAdapterContextV1(
            None,
            None,
            (),
        )
        for name, mutate in (
            ("manifest", change_manifest),
            ("lock", change_lock),
            ("candidate", change_candidate),
        ):
            self._write_json(
                self.project_root / contracts.PROJECT_MANIFEST_NAME,
                self.project,
            )
            self._write_json(
                self.project_root / contracts.PACKAGE_LOCK_NAME,
                self.lock,
            )
            local_payload.write_bytes(original_payload)
            mutate()

            with (
                self.subTest(name=name),
                mock.patch.object(
                    bootstrap_project_host,
                    "run_project_bootstrap_host",
                ) as run,
            ):
                result = bootstrap_host_session.open_bootstrap_session(
                    self.request,
                    context,
                    self.validators,
                )

            self.assertEqual(
                bootstrap_session.BootstrapSessionState.SAFE_MODE,
                result.state,
            )
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
