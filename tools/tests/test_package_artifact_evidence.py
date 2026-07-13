"""Pure Package Product & Artifact Evidence v1 handoff tests."""

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
from tools import package_artifact_evidence as artifacts
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
PACKAGE_ID = "com.asharia.system.synthetic-artifacts"
BOUNDARY_ID = "com.asharia.synthetic-artifacts-source"
ROOT_TARGET = "asharia-synthetic-artifacts"


class PackageArtifactEvidenceTests(unittest.TestCase):
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
        manifest["displayName"] = "Synthetic Artifact System"
        return manifest

    def descriptor(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.package-source-build",
            "schemaVersion": 1,
            "package": {"id": PACKAGE_ID, "version": "0.1.0"},
            "modules": [
                {
                    "moduleId": "runtime",
                    "sourceBoundaries": [BOUNDARY_ID],
                    "build": {
                        "kind": "target-roots",
                        "targets": [
                            {"name": ROOT_TARGET, "type": "STATIC_LIBRARY"}
                        ],
                    },
                },
                {
                    "moduleId": "diagnostics",
                    "sourceBoundaries": [BOUNDARY_ID],
                    "build": {"kind": "no-build"},
                },
            ],
        }

    def product_declaration(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.package-products",
            "schemaVersion": 1,
            "package": {"id": PACKAGE_ID, "version": "0.1.0"},
            "modules": [
                {
                    "moduleId": "runtime",
                    "delivery": {
                        "kind": "artifact-set",
                        "products": [
                            {"id": "runtime-library", "purpose": "link-input"},
                            {"id": "runtime-metadata", "purpose": "metadata"},
                        ],
                    },
                },
                {
                    "moduleId": "diagnostics",
                    "delivery": {"kind": "no-artifacts"},
                },
            ],
        }

    def candidate(
        self,
        manifest: dict[str, object],
        descriptor: dict[str, object],
        declaration: dict[str, object],
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
        )
        declaration_bytes = (
            json.dumps(declaration, ensure_ascii=False, indent=2).encode("utf-8")
            + b"\n"
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
            build_descriptor_integrity=contracts.compute_bytes_integrity(
                descriptor_bytes
            ),
            build_descriptor_bytes=descriptor_bytes,
            product_declaration=declaration,
            product_declaration_integrity=contracts.compute_bytes_integrity(
                declaration_bytes
            ),
            product_declaration_bytes=declaration_bytes,
            payload_location=payload_location or Path("synthetic") / PACKAGE_ID,
        )

    def project(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 1,
            "directPackages": [
                {
                    "id": PACKAGE_ID,
                    "version": {"kind": "exact", "version": "0.1.0"},
                }
            ],
            "directFeatureSets": [],
            "packageOptions": [],
        }

    def topology(self) -> dict[str, object]:
        packages = [
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
            },
            {
                "path": "packages/synthetic/asharia.package.json",
                "name": BOUNDARY_ID,
                "packageKind": "source-boundary",
                "sourceRole": "module-group",
                "ownerDomain": "foundation",
                "plannedOwnershipRoot": PACKAGE_ID,
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
            },
        ]
        return {
            "schema": "com.asharia.source-topology-snapshot",
            "schemaVersion": 1,
            "summary": {
                "packageCount": 2,
                "targetCount": 2,
                "ownerDomains": {"foundation": 2},
                "plannedOwnershipRoots": {
                    PACKAGE_ID: 1,
                    "engine/core": 1,
                },
            },
            "packages": packages,
        }

    def codemodel(self) -> CMakeCodemodelSnapshot:
        return CMakeCodemodelSnapshot(
            configuration="Debug",
            generator=CMakeGeneratorEvidence("Ninja", False),
            toolchain=CMakeToolchainEvidence(
                "MSVC", "19.44", "Windows", "x86_64"
            ),
            targets=(
                CMakeTargetEvidence("asharia-core", "STATIC_LIBRARY", (), ()),
                CMakeTargetEvidence(
                    ROOT_TARGET,
                    "STATIC_LIBRARY",
                    ("asharia-core",),
                    ("packages/synthetic/asharia-synthetic-artifacts.lib",),
                ),
            ),
        )

    def prepare(
        self,
    ) -> tuple[
        composition.HostCompositionPlan,
        source_build_plan.SourceBuildPlan,
        LockedGraphVerificationResult,
    ]:
        candidate = self.candidate(
            self.manifest(),
            self.descriptor(),
            self.product_declaration(),
        )
        project = self.project()
        resolution = package_resolver.resolve_package_graph(
            project,
            ENGINE_API_VERSION,
            [candidate],
            self.validators,
        )
        self.assertTrue(resolution.succeeded)
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
        self.assertTrue(host_result.succeeded)
        assert host_result.plan is not None
        build_result = source_build_plan.plan_source_build(
            host_result.plan,
            verified,
            self.topology(),
            self.codemodel(),
            self.validators,
        )
        self.assertTrue(
            build_result.succeeded,
            [item.render() for item in build_result.diagnostics],
        )
        assert build_result.plan is not None
        return host_result.plan, build_result.plan, verified

    def file(
        self,
        path: str,
        role: str,
        content: bytes,
        *,
        media_type: str = "application/octet-stream",
    ) -> artifacts.ArtifactFileObservation:
        integrity = contracts.compute_bytes_integrity(content)
        return artifacts.ArtifactFileObservation(
            path=path,
            role=role,
            media_type=media_type,
            size=len(content),
            integrity=artifacts.IntegrityRecord(
                integrity["algorithm"], integrity["digest"]
            ),
            content=content,
        )

    def observations(self) -> tuple[artifacts.ProductArtifactObservation, ...]:
        return (
            artifacts.ProductArtifactObservation(
                package_id=PACKAGE_ID,
                package_version="0.1.0",
                module_id="runtime",
                product_id="runtime-library",
                target_platform="com.asharia.platform.windows",
                configuration="Debug",
                files=(
                    self.file("lib/runtime.lib", "primary", b"library"),
                    self.file("symbols/runtime.pdb", "debug-symbol", b"symbols"),
                ),
            ),
            artifacts.ProductArtifactObservation(
                package_id=PACKAGE_ID,
                package_version="0.1.0",
                module_id="runtime",
                product_id="runtime-metadata",
                target_platform="com.asharia.platform.windows",
                configuration="Debug",
                files=(
                    self.file(
                        "metadata/runtime.json",
                        "primary",
                        b"{}",
                        media_type="application/json",
                    ),
                ),
            ),
        )

    def verify(
        self,
        host_plan: composition.HostCompositionPlan,
        source_plan: source_build_plan.SourceBuildPlan,
        verified: LockedGraphVerificationResult,
        observations: tuple[artifacts.ProductArtifactObservation, ...],
    ) -> artifacts.PackageArtifactVerificationResult:
        return artifacts.verify_package_artifacts(
            host_plan,
            source_plan,
            verified,
            observations,
            self.validators,
        )

    def test_verified_manifests_are_complete_canonical_and_deterministic(self) -> None:
        host_plan, source_plan, verified = self.prepare()
        observations = self.observations()
        reordered = tuple(
            replace(observation, files=tuple(reversed(observation.files)))
            for observation in reversed(observations)
        )

        first = self.verify(host_plan, source_plan, verified, observations)
        second = self.verify(host_plan, source_plan, verified, reordered)

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        self.assertEqual(first.manifest_set_integrity, second.manifest_set_integrity)
        self.assertEqual(
            artifacts.render_package_artifact_manifest_set(first.manifests),
            artifacts.render_package_artifact_manifest_set(second.manifests),
        )
        self.assertEqual(1, len(first.manifests))
        manifest = first.manifests[0]
        self.assertEqual(
            ["diagnostics", "runtime"],
            [module.module_id for module in manifest.modules],
        )
        self.assertEqual(
            [],
            artifacts.validate_package_artifact_manifest_data(
                manifest,
                self.validators,
            ),
        )
        rendered = artifacts.render_package_artifact_manifest(manifest)
        self.assertNotIn("packages/synthetic", rendered)
        self.assertNotIn("asharia-synthetic-artifacts", rendered)
        self.assertNotIn("factory", rendered)
        self.assertNotIn("stage", rendered)

    def test_missing_duplicate_and_unknown_products_fail_atomically(self) -> None:
        host_plan, source_plan, verified = self.prepare()
        valid = self.observations()
        cases = {
            "artifact.product.missing": valid[1:],
            "artifact.product.duplicate": valid + (valid[0],),
            "artifact.product.unknown": valid
            + (replace(valid[0], product_id="unknown-product"),),
        }

        for expected_code, observations in cases.items():
            with self.subTest(expected_code=expected_code):
                result = self.verify(
                    host_plan,
                    source_plan,
                    verified,
                    observations,
                )
                self.assertFalse(result.succeeded)
                self.assertEqual((), result.manifests)
                self.assertIsNone(result.manifest_set_integrity)
                self.assertIn(
                    expected_code,
                    {item.code for item in result.diagnostics},
                )

    def test_size_digest_context_version_and_primary_drift_fail_closed(self) -> None:
        host_plan, source_plan, verified = self.prepare()
        valid = self.observations()
        first_file = valid[0].files[0]
        cases = {
            "artifact.size.mismatch": replace(
                valid[0],
                files=(replace(first_file, size=999),) + valid[0].files[1:],
            ),
            "artifact.integrity.mismatch": replace(
                valid[0],
                files=(
                    replace(
                        first_file,
                        integrity=artifacts.IntegrityRecord("sha256", "0" * 64),
                    ),
                )
                + valid[0].files[1:],
            ),
            "artifact.observation.context-mismatch": replace(
                valid[0],
                configuration="Release",
            ),
            "artifact.product.unknown": replace(
                valid[0],
                package_version="0.2.0",
            ),
            "artifact.primary.cardinality": replace(
                valid[0],
                files=(replace(first_file, role="metadata"),)
                + valid[0].files[1:],
            ),
        }

        for expected_code, invalid in cases.items():
            with self.subTest(expected_code=expected_code):
                result = self.verify(
                    host_plan,
                    source_plan,
                    verified,
                    (invalid, valid[1]),
                )
                self.assertFalse(result.succeeded)
                self.assertIn(
                    expected_code,
                    {item.code for item in result.diagnostics},
                )

    def test_absolute_escape_nfc_and_case_colliding_paths_are_rejected(self) -> None:
        host_plan, source_plan, verified = self.prepare()
        valid = self.observations()
        first_file = valid[0].files[0]
        for path in (
            "C:/runtime.lib",
            "/runtime.lib",
            "../runtime.lib",
            "lib\\runtime.lib",
            "lib/e\u0301.lib",
        ):
            with self.subTest(path=path):
                invalid = replace(
                    valid[0],
                    files=(replace(first_file, path=path),) + valid[0].files[1:],
                )
                result = self.verify(
                    host_plan,
                    source_plan,
                    verified,
                    (invalid, valid[1]),
                )
                self.assertFalse(result.succeeded)
                self.assertIn(
                    "artifact.path.invalid",
                    {item.code for item in result.diagnostics},
                )

        collision = replace(
            valid[0],
            files=valid[0].files
            + (self.file("LIB/RUNTIME.LIB", "metadata", b"duplicate"),),
        )
        result = self.verify(
            host_plan,
            source_plan,
            verified,
            (collision, valid[1]),
        )
        self.assertFalse(result.succeeded)
        self.assertIn(
            "artifact.path.collision",
            {item.code for item in result.diagnostics},
        )

    def test_stale_host_source_plan_and_product_snapshot_are_rejected(self) -> None:
        host_plan, source_plan, verified = self.prepare()
        observations = self.observations()

        stale_host = replace(
            host_plan,
            target_platform="com.asharia.platform.linux",
        )
        host_result = self.verify(
            stale_host,
            source_plan,
            verified,
            observations,
        )
        self.assertFalse(host_result.succeeded)
        self.assertIn(
            "artifact.input.host-composition-stale",
            {item.code for item in host_result.diagnostics},
        )

        stale_plan = replace(source_plan, configuration="Release")
        plan_result = self.verify(
            host_plan,
            stale_plan,
            verified,
            observations,
        )
        self.assertFalse(plan_result.succeeded)
        self.assertIn(
            "build.plan.integrity-mismatch",
            {item.code for item in plan_result.diagnostics},
        )

        candidate = verified.selected_candidates[0]
        assert candidate.product_declaration_bytes is not None
        stale_candidate = replace(
            candidate,
            product_declaration_bytes=candidate.product_declaration_bytes + b" ",
        )
        stale_verified = replace(
            verified,
            selected_candidates=(stale_candidate,),
        )
        declaration_result = self.verify(
            host_plan,
            source_plan,
            stale_verified,
            observations,
        )
        self.assertFalse(declaration_result.succeeded)
        self.assertIn(
            "artifact.product-declaration.integrity-mismatch",
            {item.code for item in declaration_result.diagnostics},
        )

    def test_verifier_is_pure_and_preserves_inputs(self) -> None:
        host_plan, source_plan, verified = self.prepare()
        observations = self.observations()
        before = copy.deepcopy((host_plan, source_plan, verified, observations))

        with (
            mock.patch.object(
                Path,
                "read_bytes",
                side_effect=AssertionError("artifact verifier performed filesystem IO"),
            ),
            mock.patch.object(
                package_resolver,
                "resolve_package_graph",
                side_effect=AssertionError("artifact verifier called resolver"),
            ),
        ):
            result = self.verify(
                host_plan,
                source_plan,
                verified,
                observations,
            )

        self.assertTrue(result.succeeded)
        self.assertEqual(before, (host_plan, source_plan, verified, observations))

    def test_full_discovery_to_artifact_manifest_handoff(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            root.mkdir()
            manifest = self.manifest()
            descriptor = self.descriptor()
            declaration = self.product_declaration()
            for name, value in (
                (contracts.PACKAGE_MANIFEST_NAME, manifest),
                (contracts.PACKAGE_SOURCE_BUILD_NAME, descriptor),
                (contracts.PACKAGE_PRODUCTS_NAME, declaration),
            ):
                (root / name).write_text(
                    json.dumps(value, ensure_ascii=False, indent=2) + "\n",
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
            build_result = source_build_plan.plan_source_build(
                host_result.plan,
                verified,
                self.topology(),
                self.codemodel(),
                self.validators,
            )
            self.assertTrue(build_result.succeeded)
            assert build_result.plan is not None

            result = self.verify(
                host_result.plan,
                build_result.plan,
                verified,
                self.observations(),
            )

            self.assertTrue(
                result.succeeded,
                [item.render() for item in result.diagnostics],
            )
            self.assertEqual(1, len(result.manifests))


if __name__ == "__main__":
    unittest.main()
