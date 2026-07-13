"""Deterministic Source Build Plan v1 planner tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import host_package_composition as composition
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools import source_build_plan
from tools.cmake_file_api import (
    CMakeCodemodelSnapshot,
    CMakeGeneratorEvidence,
    CMakeTargetEvidence,
    CMakeToolchainEvidence,
)
from tools.package_candidates import PackageCandidate
from tools.package_lock_verification import (
    LockedGraphVerificationResult,
    verify_locked_package_graph,
)


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
ENGINE_API_VERSION = "0.1.0"
PACKAGE_ID = "com.asharia.system.synthetic-build"
BOUNDARY_ID = "com.asharia.synthetic-build-source"
ROOT_TARGET = "asharia-synthetic-build"


def exact(version: str = "0.1.0") -> dict[str, object]:
    return {"kind": "exact", "version": version}


class SourceBuildPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.manifest_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )
        cls.editor_profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-editor.json").read_text(
                encoding="utf-8"
            )
        )

    def manifest(self) -> dict[str, object]:
        manifest = copy.deepcopy(self.manifest_template)
        manifest["id"] = PACKAGE_ID
        manifest["displayName"] = "Synthetic Build System"
        return manifest

    def descriptor(
        self,
        *,
        boundary: str = BOUNDARY_ID,
        target_name: str = ROOT_TARGET,
        target_type: str = "STATIC_LIBRARY",
    ) -> dict[str, object]:
        return {
            "schema": "com.asharia.package-source-build",
            "schemaVersion": 1,
            "package": {"id": PACKAGE_ID, "version": "0.1.0"},
            "modules": [
                {
                    "moduleId": "runtime",
                    "sourceBoundaries": [boundary],
                    "build": {
                        "kind": "target-roots",
                        "targets": [
                            {"name": target_name, "type": target_type}
                        ],
                    },
                },
                {
                    "moduleId": "diagnostics",
                    "sourceBoundaries": [boundary],
                    "build": {"kind": "no-build"},
                },
            ],
        }

    def candidate(
        self,
        manifest: dict[str, object],
        descriptor: dict[str, object] | None,
        *,
        payload_location: Path | None = None,
    ) -> PackageCandidate:
        manifest_bytes = json.dumps(
            manifest,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        descriptor_bytes = (
            json.dumps(descriptor, ensure_ascii=False, indent=2).encode("utf-8")
            + b"\n"
            if descriptor is not None
            else None
        )
        return PackageCandidate(
            identity=PACKAGE_ID,
            version="0.1.0",
            package_kind="installable-capability",
            origin=f"local:{PACKAGE_ID}",
            source={"kind": "local", "sourceId": PACKAGE_ID},
            manifest_integrity=contracts.compute_bytes_integrity(manifest_bytes),
            payload_integrity=contracts.compute_bytes_integrity(
                f"payload:{PACKAGE_ID}".encode("utf-8")
            ),
            manifest=manifest,
            build_descriptor=descriptor,
            build_descriptor_integrity=(
                contracts.compute_bytes_integrity(descriptor_bytes)
                if descriptor_bytes is not None
                else None
            ),
            build_descriptor_bytes=descriptor_bytes,
            payload_location=payload_location or Path("synthetic") / PACKAGE_ID,
        )

    def project(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 1,
            "directPackages": [{"id": PACKAGE_ID, "version": exact()}],
            "directFeatureSets": [],
            "packageOptions": [],
        }

    def topology(
        self,
        *,
        planned_root: str = PACKAGE_ID,
        include_boundary: bool = True,
        reverse: bool = False,
    ) -> dict[str, object]:
        packages: list[dict[str, object]] = [
            {
                "path": "engine/core/asharia.package.json",
                "name": "com.asharia.synthetic-core-source",
                "packageKind": "source-boundary",
                "sourceRole": "engine-component",
                "ownerDomain": "foundation",
                "plannedOwnershipRoot": "engine/core",
                "selectable": False,
                "catalogVisible": False,
                "dependencies": [],
                "targets": [
                    {
                        "name": "asharia-core",
                        "role": "runtime",
                        "test": False,
                        "dependencies": [],
                    }
                ],
            }
        ]
        if include_boundary:
            packages.append(
                {
                    "path": "packages/synthetic/asharia.package.json",
                    "name": BOUNDARY_ID,
                    "packageKind": "source-boundary",
                    "sourceRole": "module-group",
                    "ownerDomain": "foundation",
                    "plannedOwnershipRoot": planned_root,
                    "selectable": False,
                    "catalogVisible": False,
                    "dependencies": ["com.asharia.synthetic-core-source"],
                    "targets": [
                        {
                            "name": ROOT_TARGET,
                            "role": "runtime",
                            "test": False,
                            "dependencies": ["asharia-core"],
                        }
                    ],
                }
            )
        if reverse:
            packages.reverse()
        owner_domains: dict[str, int] = {}
        roots: dict[str, int] = {}
        for package in packages:
            owner = package["ownerDomain"]
            planned = package["plannedOwnershipRoot"]
            owner_domains[owner] = owner_domains.get(owner, 0) + 1
            roots[planned] = roots.get(planned, 0) + 1
        return {
            "schema": "com.asharia.source-topology-snapshot",
            "schemaVersion": 1,
            "summary": {
                "packageCount": len(packages),
                "targetCount": sum(len(package["targets"]) for package in packages),
                "ownerDomains": dict(sorted(owner_domains.items())),
                "plannedOwnershipRoots": dict(sorted(roots.items())),
            },
            "packages": packages,
        }

    def codemodel(
        self,
        *,
        root_type: str = "STATIC_LIBRARY",
        reverse: bool = False,
    ) -> CMakeCodemodelSnapshot:
        targets = [
            CMakeTargetEvidence("asharia-core", "STATIC_LIBRARY", (), ()),
            CMakeTargetEvidence(
                ROOT_TARGET,
                root_type,
                ("asharia-core",),
                ("packages/synthetic/asharia-synthetic-build.lib",),
            ),
        ]
        if reverse:
            targets.reverse()
        return CMakeCodemodelSnapshot(
            configuration="Debug",
            generator=CMakeGeneratorEvidence("Ninja", False),
            toolchain=CMakeToolchainEvidence(
                "MSVC", "19.44", "Windows", "x86_64"
            ),
            targets=tuple(targets),
        )

    def prepare(
        self,
        *,
        descriptor: dict[str, object] | None = None,
    ) -> tuple[
        composition.HostCompositionPlan,
        LockedGraphVerificationResult,
        PackageCandidate,
    ]:
        manifest = self.manifest()
        candidate = self.candidate(
            manifest,
            self.descriptor() if descriptor is None else descriptor,
        )
        project = self.project()
        resolution = package_resolver.resolve_package_graph(
            project,
            ENGINE_API_VERSION,
            [candidate],
            self.validators,
        )
        self.assertTrue(
            resolution.succeeded,
            [item.render() for item in resolution.diagnostics],
        )
        verified = LockedGraphVerificationResult(
            lock=resolution.lock,
            selected_candidates=resolution.selected_candidates,
            diagnostics=(),
        )
        host_result = composition.plan_host_package_composition(
            verified,
            project,
            self.editor_profile,
            self.validators,
        )
        self.assertTrue(
            host_result.succeeded,
            [item.render() for item in host_result.diagnostics],
        )
        assert host_result.plan is not None
        return host_result.plan, verified, candidate

    def plan(
        self,
        host_plan: composition.HostCompositionPlan,
        verified: LockedGraphVerificationResult,
        topology: dict[str, object] | None = None,
        codemodel: CMakeCodemodelSnapshot | None = None,
    ) -> source_build_plan.SourceBuildPlanResult:
        return source_build_plan.plan_source_build(
            host_plan,
            verified,
            topology or self.topology(),
            codemodel or self.codemodel(),
            self.validators,
        )

    def test_plan_contains_bindings_roots_closure_build_options_and_fingerprints(self) -> None:
        host_plan, verified, _ = self.prepare()

        result = self.plan(host_plan, verified)

        self.assertTrue(
            result.succeeded,
            [item.render() for item in result.diagnostics],
        )
        assert result.plan is not None
        self.assertEqual([ROOT_TARGET], [target.name for target in result.plan.build_roots])
        self.assertEqual(
            ["asharia-core", ROOT_TARGET],
            [target.name for target in result.plan.target_closure],
        )
        self.assertEqual(
            ["runtime", "diagnostics"],
            [module.module_id for module in result.plan.packages[0].modules],
        )
        self.assertEqual(
            ["target-roots", "no-build"],
            [module.build_kind for module in result.plan.packages[0].modules],
        )
        self.assertEqual(
            [(PACKAGE_ID, "validation", True)],
            [
                (option.package_id, option.option_id, option.value)
                for option in result.plan.build_options
            ],
        )
        data = source_build_plan.source_build_plan_to_data(result.plan)
        self.assertEqual(
            [],
            source_build_plan.validate_source_build_plan_data(
                data,
                self.validators,
            ),
        )
        self.assertTrue(source_build_plan.render_source_build_plan(result.plan).endswith("\n"))
        self.assertNotIn(
            "packages/synthetic/asharia-synthetic-build.lib",
            source_build_plan.render_source_build_plan(result.plan),
        )

    def test_semantically_unordered_topology_and_codemodel_are_deterministic(self) -> None:
        host_plan, verified, _ = self.prepare()

        first = self.plan(host_plan, verified)
        second = self.plan(
            host_plan,
            verified,
            self.topology(reverse=True),
            self.codemodel(reverse=True),
        )

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        assert first.plan is not None
        assert second.plan is not None
        self.assertEqual(
            source_build_plan.render_source_build_plan(first.plan),
            source_build_plan.render_source_build_plan(second.plan),
        )

    def test_missing_descriptor_fails_atomically(self) -> None:
        host_plan, verified, _ = self.prepare()
        candidate = verified.selected_candidates[0]
        without_descriptor = replace(
            candidate,
            build_descriptor=None,
            build_descriptor_integrity=None,
            build_descriptor_bytes=None,
        )
        forged = replace(verified, selected_candidates=(without_descriptor,))

        result = self.plan(host_plan, forged)

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.plan)
        self.assertIn("build.descriptor.missing", {item.code for item in result.diagnostics})

    def test_stale_descriptor_snapshot_fails_atomically(self) -> None:
        host_plan, verified, _ = self.prepare()
        candidate = verified.selected_candidates[0]
        assert candidate.build_descriptor_bytes is not None
        stale_candidate = replace(
            candidate,
            build_descriptor_bytes=candidate.build_descriptor_bytes + b" ",
        )
        stale_verified = replace(
            verified,
            selected_candidates=(stale_candidate,),
        )

        result = self.plan(host_plan, stale_verified)

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.plan)
        self.assertEqual(
            {"build.descriptor.integrity-mismatch"},
            {item.code for item in result.diagnostics},
        )

    def test_stale_host_unknown_boundary_and_wrong_target_type_fail_closed(self) -> None:
        host_plan, verified, _ = self.prepare()
        stale = replace(
            host_plan,
            locked_graph_integrity=composition.IntegrityRecord("sha256", "0" * 64),
        )
        stale_result = self.plan(stale, verified)
        self.assertEqual(
            ["build.input.host-composition-stale"],
            [item.code for item in stale_result.diagnostics],
        )

        unknown_descriptor = self.descriptor(boundary="com.asharia.unknown-source")
        unknown_host, unknown_verified, _ = self.prepare(descriptor=unknown_descriptor)
        unknown_result = self.plan(unknown_host, unknown_verified)
        self.assertIn(
            "build.descriptor.unknown-boundary",
            {item.code for item in unknown_result.diagnostics},
        )
        self.assertIsNone(unknown_result.plan)

        wrong_descriptor = self.descriptor(target_type="SHARED_LIBRARY")
        wrong_host, wrong_verified, _ = self.prepare(descriptor=wrong_descriptor)
        wrong_result = self.plan(wrong_host, wrong_verified)
        self.assertIn(
            "build.target.type-mismatch",
            {item.code for item in wrong_result.diagnostics},
        )
        self.assertIsNone(wrong_result.plan)

        missing_descriptor = self.descriptor(target_name="asharia-missing")
        missing_host, missing_verified, _ = self.prepare(
            descriptor=missing_descriptor
        )
        missing_result = self.plan(missing_host, missing_verified)
        self.assertIn(
            "build.target.missing",
            {item.code for item in missing_result.diagnostics},
        )
        self.assertIsNone(missing_result.plan)

    def test_boundary_owner_and_target_owner_are_verified_by_topology(self) -> None:
        host_plan, verified, _ = self.prepare()

        owner_result = self.plan(
            host_plan,
            verified,
            self.topology(planned_root="com.asharia.system.other"),
        )
        missing_result = self.plan(
            host_plan,
            verified,
            self.topology(include_boundary=False),
        )

        self.assertIn(
            "build.descriptor.boundary-owner-mismatch",
            {item.code for item in owner_result.diagnostics},
        )
        self.assertIn(
            "build.descriptor.unknown-boundary",
            {item.code for item in missing_result.diagnostics},
        )
        self.assertIn(
            "build.target.owner-mismatch",
            {item.code for item in missing_result.diagnostics},
        )

    def test_plan_schema_rejects_artifact_factory_and_activation_fields(self) -> None:
        host_plan, verified, _ = self.prepare()
        result = self.plan(host_plan, verified)
        self.assertTrue(result.succeeded)
        assert result.plan is not None
        baseline = source_build_plan.source_build_plan_to_data(result.plan)
        mutations = {
            "artifact": lambda value: value.update({"artifact": "runtime.dll"}),
            "factory": lambda value: value["packages"][0]["modules"][0][
                "build"
            ].update({"factory": "CreateRuntime"}),
            "activation": lambda value: value.update({"activationPhase": "startup"}),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                data = copy.deepcopy(baseline)
                mutate(data)

                diagnostics = source_build_plan.validate_source_build_plan_data(
                    data,
                    self.validators,
                )

                self.assertTrue(diagnostics)
                self.assertEqual(
                    {"build.plan.schema"},
                    {item.code for item in diagnostics},
                )

    def test_planner_is_read_only_and_does_not_mutate_inputs(self) -> None:
        host_plan, verified, _ = self.prepare()
        topology = self.topology()
        topology_before = copy.deepcopy(topology)
        descriptor_before = copy.deepcopy(
            verified.selected_candidates[0].build_descriptor
        )
        codemodel = self.codemodel()

        with mock.patch.object(
            Path,
            "read_bytes",
            side_effect=AssertionError("planner performed filesystem IO"),
        ):
            result = self.plan(host_plan, verified, topology, codemodel)

        self.assertTrue(result.succeeded)
        self.assertEqual(topology_before, topology)
        self.assertEqual(
            descriptor_before,
            verified.selected_candidates[0].build_descriptor,
        )

    def test_discovery_to_locked_reuse_to_composition_to_build_handoff(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            root.mkdir()
            manifest = self.manifest()
            descriptor = self.descriptor()
            (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            (root / contracts.PACKAGE_SOURCE_BUILD_NAME).write_text(
                json.dumps(descriptor, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            discovered = discovery.load_package_candidates(
                [
                    discovery.LocalCandidateLocation(
                        source_id=PACKAGE_ID,
                        payload_root=root,
                    )
                ],
                self.validators,
            )
            self.assertTrue(discovered.succeeded)
            project = self.project()
            resolution = package_resolver.resolve_package_graph(
                project,
                ENGINE_API_VERSION,
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(resolution.succeeded)
            verified = verify_locked_package_graph(
                project,
                ENGINE_API_VERSION,
                resolution.lock,
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(
                verified.succeeded,
                [item.render() for item in verified.diagnostics],
            )
            host_result = composition.plan_host_package_composition(
                verified,
                project,
                self.editor_profile,
                self.validators,
            )
            self.assertTrue(host_result.succeeded)
            assert host_result.plan is not None

            build_result = self.plan(host_result.plan, verified)

            self.assertTrue(
                build_result.succeeded,
                [item.render() for item in build_result.diagnostics],
            )


if __name__ == "__main__":
    unittest.main()
