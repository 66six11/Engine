"""Verified Engine Distribution package catalog projection tests."""

from __future__ import annotations

import copy
import json
import shutil
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import engine_distribution_package_catalog as package_catalog
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import package_candidate_discovery as discovery
from tools import package_lock_verification
from tools import package_resolver
from tools.tests import package_test_support
from tools.tests import test_engine_distribution_assembly as assembly_tests


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


class EngineDistributionPackageCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assembly_tests.EngineDistributionAssemblyTests.setUpClass()
        cls.validators = contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )

    def installable(
        self,
        identity: str,
        version: str = "1.0.0",
        *,
        dependencies: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["version"] = version
        manifest["displayName"] = identity
        manifest["dependencies"] = dependencies or []
        return manifest

    def write_package(
        self,
        generation_root: Path,
        logical_root: str,
        manifest: dict[str, object],
        *,
        availability: str = "default",
    ) -> dict[str, object]:
        package_root = generation_root.joinpath(*logical_root.split("/"))
        package_root.mkdir(parents=True)
        manifest_path = package_root / contracts.PACKAGE_MANIFEST_NAME
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (package_root / "payload.bin").write_bytes(
            f"payload:{manifest['id']}".encode("utf-8")
        )
        return {
            "id": manifest["id"],
            "version": manifest["version"],
            "packageKind": manifest["packageKind"],
            "availability": availability,
            "root": logical_root,
            "manifestIntegrity": contracts.compute_manifest_file_integrity(
                manifest_path
            ),
            "payloadIntegrity": contracts.compute_package_tree_integrity(package_root),
        }

    def handoff(
        self,
        generation_root: Path,
        inventory: list[dict[str, object]],
    ) -> distribution_verifier.VerifiedInstalledDistribution:
        manifest = package_test_support.make_engine_distribution()
        manifest["bundledPackages"] = copy.deepcopy(inventory)
        manifest["engineGenerationId"] = contracts.compute_engine_generation_id(
            manifest
        )
        manifest_bytes = contracts.render_normalized_engine_distribution_manifest(
            manifest
        ).encode("utf-8")
        captured = json.loads(manifest_bytes.decode("utf-8"))
        verified_root = generation_root / captured["engineGenerationId"]
        verified_root.mkdir(exist_ok=True)
        fixture_packages = generation_root / "packages"
        if fixture_packages.is_dir():
            shutil.copytree(
                fixture_packages,
                verified_root / "packages",
                dirs_exist_ok=True,
            )
        return distribution_verifier.VerifiedInstalledDistribution(
            engine_generation_id=captured["engineGenerationId"],
            generation_root=verified_root.resolve(),
            manifest=captured,
            manifest_bytes=manifest_bytes,
            manifest_integrity=contracts.compute_bytes_integrity(manifest_bytes),
        )

    def derive(
        self,
        handoff: object,
    ) -> package_catalog.EngineDistributionPackageCatalogResult:
        return package_catalog.derive_engine_distribution_package_catalog(
            handoff,
            self.validators,
        )

    def assert_atomic_failure(
        self,
        result: package_catalog.EngineDistributionPackageCatalogResult,
    ) -> None:
        self.assertFalse(result.succeeded)
        self.assertIsNone(result.snapshot)
        self.assertTrue(result.diagnostics)

    def test_verified_inventory_derives_stable_detached_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            zulu = self.write_package(
                generation_root,
                "packages/zulu",
                self.installable("com.asharia.system.zulu", "2.0.0"),
                availability="optional",
            )
            alpha = self.write_package(
                generation_root,
                "packages/alpha",
                self.installable("com.asharia.system.alpha"),
                availability="required",
            )
            verified = self.handoff(generation_root, [zulu, alpha])

            result = self.derive(verified)
            reordered = self.derive(self.handoff(generation_root, [alpha, zulu]))

            self.assertTrue(
                result.succeeded,
                [diagnostic.render() for diagnostic in result.diagnostics],
            )
            self.assertTrue(reordered.succeeded)
            assert result.snapshot is not None
            assert reordered.snapshot is not None
            self.assertEqual(
                ["com.asharia.system.alpha", "com.asharia.system.zulu"],
                [entry.identity for entry in result.snapshot.entries],
            )
            self.assertEqual(
                ["required", "optional"],
                [entry.availability for entry in result.snapshot.entries],
            )
            self.assertEqual(
                ["packages/alpha", "packages/zulu"],
                [entry.logical_root for entry in result.snapshot.entries],
            )
            self.assertEqual(
                ["com.asharia.system.alpha", "com.asharia.system.zulu"],
                [candidate.identity for candidate in result.snapshot.candidates],
            )
            self.assertEqual(result.snapshot.entries, reordered.snapshot.entries)
            self.assertEqual(
                [candidate.identity for candidate in result.snapshot.candidates],
                [candidate.identity for candidate in reordered.snapshot.candidates],
            )
            self.assertEqual(
                result.snapshot.distribution_manifest,
                reordered.snapshot.distribution_manifest,
            )
            self.assertEqual(result.snapshot, reordered.snapshot)
            self.assertNotEqual(
                result.snapshot,
                replace(result.snapshot, _distribution_manifest_bytes=b"{}\n"),
            )
            changed_candidates = list(result.snapshot.candidates)
            changed_candidates[0] = replace(
                changed_candidates[0],
                payload_integrity={"algorithm": "sha256", "digest": "0" * 64},
            )
            self.assertNotEqual(
                result.snapshot,
                replace(
                    result.snapshot,
                    _candidate_snapshot=tuple(changed_candidates),
                ),
            )

            verified.manifest["bundledPackages"].clear()
            verified.manifest_integrity["digest"] = "0" * 64
            first_manifest = result.snapshot.distribution_manifest
            first_candidates = result.snapshot.candidates
            first_manifest["bundledPackages"].clear()
            first_candidates[0].manifest["id"] = "com.asharia.mutated"

            self.assertEqual(2, len(result.snapshot.distribution_manifest["bundledPackages"]))
            self.assertEqual(
                "com.asharia.system.alpha",
                result.snapshot.candidates[0].manifest["id"],
            )

    def test_real_repair_verifier_handoff_derives_catalog(self) -> None:
        fixture = assembly_tests.EngineDistributionAssemblyTests(
            methodName="runTest"
        )
        fixture.setUp()
        try:
            assembled = fixture.assemble()
            self.assertTrue(
                assembled.succeeded,
                [diagnostic.render() for diagnostic in assembled.diagnostics],
            )
            assert assembled.receipt is not None
            verification = (
                distribution_verifier.verify_installed_engine_distribution(
                    distribution_verifier.InstalledDistributionVerificationRequest(
                        generation_root=assembled.receipt.engine_generation_path,
                        expected_engine_generation_id=(
                            assembled.receipt.engine_generation_id
                        ),
                    ),
                    self.validators,
                )
            )
            self.assertTrue(verification.succeeded)
            assert verification.report is not None
            verified = verification.report.verified_distribution
            self.assertIsNotNone(verified)

            result = self.derive(verified)

            self.assertTrue(
                result.succeeded,
                [diagnostic.render() for diagnostic in result.diagnostics],
            )
            assert result.snapshot is not None
            self.assertEqual(
                len(verified.manifest["bundledPackages"]),
                len(result.snapshot.entries),
            )
        finally:
            fixture.tearDown()

    def test_malformed_or_mutated_handoff_is_not_treated_as_source_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            inventory = [
                self.write_package(
                    generation_root,
                    "packages/rendering",
                    self.installable("com.asharia.system.rendering"),
                )
            ]
            verified = self.handoff(generation_root, inventory)
            verified.manifest["bundledPackages"][0]["version"] = "9.0.0"

            result = self.derive(verified)
            unsupported = self.derive({})

        self.assert_atomic_failure(result)
        self.assert_atomic_failure(unsupported)
        self.assertEqual(
            ["catalog.handoff.manifest-snapshot-mismatch"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )
        self.assertEqual(
            ["catalog.handoff.invalid"],
            [diagnostic.code for diagnostic in unsupported.diagnostics],
        )
        self.assertNotIn(str(generation_root), result.diagnostics[0].render())

    def test_handoff_exact_bytes_integrity_and_generation_are_all_bound(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            inventory = [
                self.write_package(
                    generation_root,
                    "packages/rendering",
                    self.installable("com.asharia.system.rendering"),
                )
            ]
            baseline = self.handoff(generation_root, inventory)
            cases = {
                "bytes": replace(baseline, manifest_bytes=b"{}\n"),
                "integrity": replace(
                    baseline,
                    manifest_integrity={"algorithm": "sha256", "digest": "0" * 64},
                ),
                "generation": replace(
                    baseline,
                    engine_generation_id=f"sha256-{'f' * 64}",
                ),
                "root-id": replace(
                    baseline,
                    generation_root=generation_root.resolve(),
                ),
            }

            results = {name: self.derive(value) for name, value in cases.items()}

        for result in results.values():
            self.assert_atomic_failure(result)
        self.assertIn(
            "catalog.handoff.manifest-snapshot-mismatch",
            {value.code for value in results["bytes"].diagnostics},
        )
        self.assertEqual(
            ["catalog.handoff.manifest-integrity-mismatch"],
            [value.code for value in results["integrity"].diagnostics],
        )
        self.assertEqual(
            {
                "catalog.handoff.generation-id-mismatch",
                "catalog.handoff.generation-root-id-mismatch",
            },
            {value.code for value in results["generation"].diagnostics},
        )
        self.assertEqual(
            ["catalog.handoff.generation-root-id-mismatch"],
            [value.code for value in results["root-id"].diagnostics],
        )

    def test_duplicate_identity_version_and_root_fail_as_invalid_handoff(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            first = self.write_package(
                generation_root,
                "packages/first",
                self.installable("com.asharia.system.first"),
            )
            duplicate_identity = copy.deepcopy(first)
            duplicate_identity["root"] = "packages/second"
            multiple_version = copy.deepcopy(duplicate_identity)
            multiple_version["version"] = "2.0.0"
            duplicate_root = copy.deepcopy(first)
            duplicate_root["id"] = "com.asharia.system.second"

            identity_result = self.derive(
                self.handoff(generation_root, [first, duplicate_identity])
            )
            reordered_identity_result = self.derive(
                self.handoff(generation_root, [duplicate_identity, first])
            )
            multiple_version_result = self.derive(
                self.handoff(generation_root, [first, multiple_version])
            )
            root_result = self.derive(
                self.handoff(generation_root, [first, duplicate_root])
            )

        self.assert_atomic_failure(identity_result)
        self.assert_atomic_failure(multiple_version_result)
        self.assert_atomic_failure(root_result)
        self.assertIn(
            "catalog.handoff.inventory.duplicate-identity-version",
            {value.code for value in identity_result.diagnostics},
        )
        self.assertEqual(
            [value.render() for value in identity_result.diagnostics],
            [value.render() for value in reordered_identity_result.diagnostics],
        )
        self.assertIn(
            "catalog.handoff.inventory.multiple-versions",
            {value.code for value in multiple_version_result.diagnostics},
        )
        self.assertIn(
            "catalog.handoff.inventory.root-collision",
            {value.code for value in root_result.diagnostics},
        )

    def test_unavailable_root_and_loader_mutation_fail_without_absolute_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            missing_inventory = [
                {
                    "id": "com.asharia.system.missing",
                    "version": "1.0.0",
                    "packageKind": "installable-capability",
                    "availability": "default",
                    "root": "packages/missing",
                    "manifestIntegrity": {
                        "algorithm": "sha256",
                        "digest": "1" * 64,
                    },
                    "payloadIntegrity": {
                        "algorithm": "sha256",
                        "digest": "2" * 64,
                    },
                }
            ]
            verified = self.handoff(generation_root, missing_inventory)
            unavailable = self.derive(verified)
            changed_diagnostic = discovery.CandidateDiscoveryDiagnostic(
                code="discovery.source.changed",
                message=f"source changed at {generation_root}",
                source_key="engine-distribution:packages/missing",
                location="payload.bin",
            )
            with mock.patch.object(
                package_catalog.discovery,
                "load_package_candidates",
                return_value=discovery.CandidateDiscoveryResult(
                    candidates=(),
                    diagnostics=(changed_diagnostic,),
                ),
            ):
                changed = self.derive(verified)

        for result in (unavailable, changed):
            self.assert_atomic_failure(result)
            self.assertNotIn(str(generation_root), "\n".join(
                diagnostic.render() for diagnostic in result.diagnostics
            ))
        self.assertEqual("discovery.source.unavailable", unavailable.diagnostics[0].code)
        self.assertIn(
            "package source path is unavailable",
            unavailable.diagnostics[0].message,
        )
        self.assertEqual("discovery.source.changed", changed.diagnostics[0].code)
        self.assertEqual(
            "strict bundled-package candidate loading failed",
            changed.diagnostics[0].message,
        )

    def test_wrapped_discovery_failures_keep_stable_order_and_logical_context(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            inventory = [
                {
                    "id": "com.asharia.system.invalid-source",
                    "version": "1.0.0",
                    "packageKind": "installable-capability",
                    "availability": "default",
                    "root": "packages/invalid-source",
                    "manifestIntegrity": {
                        "algorithm": "sha256",
                        "digest": "1" * 64,
                    },
                    "payloadIntegrity": {
                        "algorithm": "sha256",
                        "digest": "2" * 64,
                    },
                }
            ]
            verified = self.handoff(generation_root, inventory)
            source_key = "engine-distribution:packages/invalid-source"
            failures = (
                discovery.CandidateDiscoveryDiagnostic(
                    code="discovery.source.link",
                    message="package root cannot be a link or junction",
                    source_key=source_key,
                ),
                discovery.CandidateDiscoveryDiagnostic(
                    code="discovery.source.alias",
                    message="two logical sources resolve to one physical root",
                    source_key=source_key,
                ),
                discovery.CandidateDiscoveryDiagnostic(
                    code="discovery.source.changed",
                    message=f"source changed at {generation_root}",
                    source_key=source_key,
                    location="payload.bin",
                ),
            )

            rendered_results: list[list[str]] = []
            for ordered in (failures, tuple(reversed(failures))):
                with mock.patch.object(
                    package_catalog.discovery,
                    "load_package_candidates",
                    return_value=discovery.CandidateDiscoveryResult(
                        candidates=(),
                        diagnostics=ordered,
                    ),
                ):
                    result = self.derive(verified)
                self.assert_atomic_failure(result)
                rendered_results.append(
                    [diagnostic.render() for diagnostic in result.diagnostics]
                )
                self.assertEqual(
                    {
                        "discovery.source.alias",
                        "discovery.source.changed",
                        "discovery.source.link",
                    },
                    {diagnostic.code for diagnostic in result.diagnostics},
                )

        self.assertEqual(rendered_results[0], rendered_results[1])
        self.assertNotIn(str(generation_root), "\n".join(rendered_results[0]))

    def test_each_loaded_candidate_must_match_exact_inventory_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            package = self.write_package(
                generation_root,
                "packages/rendering",
                self.installable("com.asharia.system.rendering"),
            )
            verified = self.handoff(generation_root, [package])
            loaded = discovery.load_package_candidates(
                [
                    discovery.EngineDistributedCandidateLocation(
                        verified.generation_root,
                        "packages/rendering",
                    )
                ],
                self.validators,
            )
            self.assertTrue(loaded.succeeded)
            candidate = loaded.candidates[0]
            other_root = generation_root / "other-root"
            other_root.mkdir()
            cases = (
                (
                    "identity",
                    "catalog.inventory.identity-mismatch",
                    replace(
                        candidate,
                        identity="com.asharia.system.other",
                    ),
                ),
                (
                    "version",
                    "catalog.inventory.version-mismatch",
                    replace(
                        candidate,
                        version="2.0.0",
                    ),
                ),
                (
                    "kind",
                    "catalog.inventory.kind-mismatch",
                    replace(
                        candidate,
                        package_kind="feature-set",
                    ),
                ),
                (
                    "origin",
                    "catalog.inventory.root-mismatch",
                    replace(
                        candidate,
                        origin="engine-distribution:packages/other",
                    ),
                ),
                (
                    "payload-location",
                    "catalog.inventory.root-mismatch",
                    replace(candidate, payload_location=other_root),
                ),
                (
                    "source",
                    "catalog.inventory.source-mismatch",
                    replace(
                        candidate,
                        source={
                            "kind": "local",
                            "sourceId": "com.asharia.source.unexpected",
                        },
                    ),
                ),
                (
                    "manifest-integrity",
                    "catalog.inventory.manifest-integrity-mismatch",
                    replace(
                        candidate,
                        manifest_integrity={
                            "algorithm": "sha256",
                            "digest": "0" * 64,
                        },
                    ),
                ),
                (
                    "payload-integrity",
                    "catalog.inventory.payload-integrity-mismatch",
                    replace(
                        candidate,
                        payload_integrity={
                            "algorithm": "sha256",
                            "digest": "0" * 64,
                        },
                    ),
                ),
            )

            for label, expected_code, changed_candidate in cases:
                with self.subTest(label=label), mock.patch.object(
                    package_catalog.discovery,
                    "load_package_candidates",
                    return_value=discovery.CandidateDiscoveryResult(
                        candidates=(changed_candidate,),
                        diagnostics=(),
                    ),
                ):
                    result = self.derive(verified)
                    self.assert_atomic_failure(result)
                    self.assertIn(
                        expected_code,
                        {diagnostic.code for diagnostic in result.diagnostics},
                    )

    def test_candidate_cardinality_is_exact_and_atomic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            package = self.write_package(
                generation_root,
                "packages/rendering",
                self.installable("com.asharia.system.rendering"),
            )
            verified = self.handoff(generation_root, [package])
            loaded = discovery.load_package_candidates(
                [
                    discovery.EngineDistributedCandidateLocation(
                        verified.generation_root,
                        "packages/rendering",
                    )
                ],
                self.validators,
            )
            self.assertTrue(loaded.succeeded)
            candidate = loaded.candidates[0]
            cases = {
                "missing": (
                    (),
                    {"catalog.inventory.candidate-missing"},
                ),
                "duplicate": (
                    (candidate, candidate),
                    {"catalog.inventory.candidate-duplicate"},
                ),
                "unexpected": (
                    (
                        replace(
                            candidate,
                            identity="com.asharia.system.unexpected",
                            origin="engine-distribution:packages/unexpected",
                        ),
                    ),
                    {
                        "catalog.inventory.candidate-missing",
                        "catalog.inventory.unexpected-candidate",
                    },
                ),
            }

            for label, (candidates, expected_codes) in cases.items():
                with self.subTest(label=label), mock.patch.object(
                    package_catalog.discovery,
                    "load_package_candidates",
                    return_value=discovery.CandidateDiscoveryResult(
                        candidates=candidates,
                        diagnostics=(),
                    ),
                ):
                    result = self.derive(verified)
                    self.assert_atomic_failure(result)
                    self.assertEqual(
                        expected_codes,
                        {diagnostic.code for diagnostic in result.diagnostics},
                    )

    def test_catalog_resolves_canonical_lock_and_reuses_it_headlessly(self) -> None:
        dependency_id = "com.asharia.system.catalog-dependency"
        root_id = "com.asharia.system.catalog-root"
        with tempfile.TemporaryDirectory() as temporary_directory:
            generation_root = Path(temporary_directory)
            dependency = self.write_package(
                generation_root,
                "packages/dependency",
                self.installable(dependency_id),
                availability="required",
            )
            root = self.write_package(
                generation_root,
                "packages/root",
                self.installable(
                    root_id,
                    dependencies=[
                        {"id": dependency_id, "version": exact("1.0.0")}
                    ],
                ),
            )
            catalog_result = self.derive(
                self.handoff(generation_root, [root, dependency])
            )
            self.assertTrue(catalog_result.succeeded)
            assert catalog_result.snapshot is not None
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
                catalog_result.snapshot.distribution_manifest,
                catalog_result.snapshot.candidates,
                self.validators,
            )
            self.assertTrue(
                resolution.succeeded,
                [diagnostic.render() for diagnostic in resolution.diagnostics],
            )
            verification = package_lock_verification.verify_locked_package_graph(
                project,
                catalog_result.snapshot.distribution_manifest,
                resolution.lock,
                catalog_result.snapshot.candidates,
                self.validators,
            )

            # Catalog captures fresh evidence, while locked verification closes
            # ordinary filesystem drift that happens after that capture.
            root_candidate = next(
                candidate
                for candidate in catalog_result.snapshot.candidates
                if candidate.identity == root_id
            )
            payload_path = root_candidate.payload_location / "payload.bin"
            payload_path.write_bytes(b"changed-after-catalog-capture")
            drifted = package_lock_verification.verify_locked_package_graph(
                project,
                catalog_result.snapshot.distribution_manifest,
                resolution.lock,
                catalog_result.snapshot.candidates,
                self.validators,
            )

        self.assertTrue(
            verification.succeeded,
            [
                f"{diagnostic.code}: {diagnostic.message}"
                for diagnostic in verification.diagnostics
            ],
        )
        self.assertEqual(resolution.lock, verification.lock)
        self.assertEqual(
            [dependency_id, root_id],
            [candidate.identity for candidate in verification.selected_candidates],
        )
        self.assertFalse(drifted.succeeded)
        self.assertIn(
            "lock.engine.distribution-candidate-mismatch",
            {diagnostic.code for diagnostic in drifted.diagnostics},
        )


if __name__ == "__main__":
    unittest.main()
