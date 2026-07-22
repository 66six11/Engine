"""Immutable Engine Distribution Assembly v1 tests."""

from __future__ import annotations

import copy
import json
import os
import shutil
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import engine_distribution_assembly as assembly
from tools import package_artifact_evidence as artifacts
from tools import package_artifact_publication as publication
from tools import package_candidate_discovery as discovery
from tools import product_payload_policy
from tools import stable_file_identity
from tools.tests import product_payload_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
PACKAGE_ID = "com.asharia.system.distribution-test"
PACKAGE_VERSION = "0.1.0"
TARGET_PLATFORM = "com.asharia.platform.windows"


class EngineDistributionAssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.package_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )
        cls.profile_bytes = (
            FIXTURE_ROOT / "valid-host-profile-editor.json"
        ).read_bytes()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.editor_root = self.root / "editor-source"
        self.editor_root.mkdir()
        (self.editor_root / "bin").mkdir()
        (self.editor_root / "resources").mkdir()
        (self.editor_root / "bin/asharia-editor.exe").write_bytes(b"editor-binary")
        (self.editor_root / "resources/bootstrap.json").write_bytes(b"{}\n")

        self.package_root = self.root / "package-source"
        self.package_root.mkdir()
        package = copy.deepcopy(self.package_template)
        package["id"] = PACKAGE_ID
        package["version"] = PACKAGE_VERSION
        package["displayName"] = "Distribution Test System"
        package["dependencies"] = []
        (self.package_root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(package, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (self.package_root / "include").mkdir()
        (self.package_root / "include/public.hpp").write_bytes(b"#pragma once\n")
        discovered = discovery.load_package_candidates(
            [
                discovery.LocalCandidateLocation(
                    "com.asharia.source.distribution-test",
                    self.package_root,
                )
            ],
            self.validators,
        )
        self.assertTrue(
            discovered.succeeded,
            [diagnostic.render() for diagnostic in discovered.diagnostics],
        )
        self.candidate = discovered.candidates[0]
        self.artifact_receipt = self.make_artifact_receipt()
        self.publication_root = self.root / "distribution-publication"
        self.publication_root.mkdir()

    def tearDown(self) -> None:
        if self.root.exists():
            shutil.rmtree(stable_file_identity.extended_path(self.root))
        self.temporary_directory.cleanup()

    @staticmethod
    def integrity(contents: bytes) -> artifacts.IntegrityRecord:
        value = contracts.compute_bytes_integrity(contents)
        return artifacts.IntegrityRecord(value["algorithm"], value["digest"])

    def make_artifact_receipt(
        self,
        artifact_path: str = "lib/distribution-test.lib",
        *,
        assert_valid: bool = True,
    ) -> publication.PackageArtifactPublicationReceipt:
        artifact_contents = b"static-library"
        placeholder = artifacts.IntegrityRecord("sha256", "0" * 64)
        provenance_value = self.integrity(b"provenance")
        manifest = artifacts.PackageArtifactManifest(
            package_id=PACKAGE_ID,
            package_version=PACKAGE_VERSION,
            host_kind="editor",
            target_platform=TARGET_PLATFORM,
            configuration="Debug",
            provenance=artifacts.PackageArtifactProvenance(
                host_composition_integrity=provenance_value,
                source_build_plan_integrity=provenance_value,
                product_declaration_integrity=provenance_value,
            ),
            modules=(
                artifacts.ModuleArtifactEvidence(
                    module_id="runtime",
                    delivery_kind="artifact-set",
                    products=(
                        artifacts.ProductArtifactEvidence(
                            product_id="runtime-library",
                            purpose="link-input",
                            files=(
                                artifacts.ArtifactFileEvidence(
                                    path=artifact_path,
                                    role="primary",
                                    media_type="application/octet-stream",
                                    size=len(artifact_contents),
                                    integrity=self.integrity(artifact_contents),
                                ),
                            ),
                        ),
                    ),
                ),
            ),
            integrity=placeholder,
        )
        manifest_integrity = artifacts.compute_package_artifact_manifest_integrity(
            manifest
        )
        manifest = replace(
            manifest,
            integrity=artifacts.IntegrityRecord(
                manifest_integrity["algorithm"], manifest_integrity["digest"]
            ),
        )
        set_integrity_value = contracts.compute_bytes_integrity(
            artifacts.render_package_artifact_manifest_set((manifest,)).encode("utf-8")
        )
        set_integrity = artifacts.IntegrityRecord(
            set_integrity_value["algorithm"], set_integrity_value["digest"]
        )
        generation_id = f"{set_integrity.algorithm}-{set_integrity.digest}"
        generation_root = self.root / "artifact-publication" / generation_id
        package_root = generation_root / "packages" / PACKAGE_ID
        artifact_file = package_root.joinpath(*artifact_path.split("/"))
        artifact_file.parent.mkdir(parents=True)
        artifact_file.write_bytes(artifact_contents)
        (package_root / contracts.PACKAGE_ARTIFACT_MANIFEST_NAME).write_text(
            artifacts.render_package_artifact_manifest(manifest),
            encoding="utf-8",
            newline="\n",
        )
        receipt = publication.PackageArtifactPublicationReceipt(
            artifact_generation_id=generation_id,
            artifact_generation_path=generation_root,
            manifest_set_integrity=set_integrity,
            manifests=(manifest,),
            reused=False,
        )
        if assert_valid:
            self.assertEqual(
                (),
                publication.verify_package_artifact_publication_receipt(
                    receipt, self.validators
                ),
            )
        return receipt

    def request(self) -> assembly.DistributionAssemblyRequest:
        return assembly.DistributionAssemblyRequest(
            identity=assembly.EngineDistributionIdentity(
                distribution_id="com.asharia.distribution.test",
                engine_version="0.1.0",
                engine_api_version="0.1.0",
            ),
            context=assembly.EngineDistributionContext(
                target_platform=TARGET_PLATFORM,
                configuration="Debug",
                compiler_id="MSVC",
                compiler_version="19.40",
                target_system="Windows",
                target_architecture="x86_64",
                runtime_library="MultiThreadedDebugDLL",
            ),
            editor_image=assembly.EditorImageAssembly(
                root=self.editor_root,
                entry_point="bin/asharia-editor.exe",
                files=(
                    assembly.EditorImageFileBinding(
                        path="bin/asharia-editor.exe",
                        role="executable",
                        media_type="application/vnd.microsoft.portable-executable",
                    ),
                    assembly.EditorImageFileBinding(
                        path="resources/bootstrap.json",
                        role="resource",
                        media_type="application/json",
                    ),
                ),
            ),
            bundled_packages=(
                assembly.BundledPackageAssembly(
                    candidate=self.candidate,
                    availability="required",
                    root=f"packages/systems/{PACKAGE_ID}",
                ),
            ),
            artifact_publications=(self.artifact_receipt,),
            host_profiles=(
                assembly.HostProfileAssembly(
                    path="profiles/editor/asharia.host-profile.json",
                    exact_bytes=self.profile_bytes,
                ),
            ),
        )

    def assemble(
        self, request: assembly.DistributionAssemblyRequest | None = None
    ) -> assembly.EngineDistributionAssemblyResult:
        return assembly.assemble_engine_distribution(
            request or self.request(),
            self.publication_root,
            self.validators,
        )

    def remove_added_file(self, path: Path, root: Path) -> None:
        path.unlink()
        parent = path.parent
        while parent != root:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent

    def test_shared_python_payload_fixtures_fail_preflight_without_overwrite(
        self,
    ) -> None:
        valid = self.assemble()
        self.assertTrue(
            valid.succeeded,
            [diagnostic.render() for diagnostic in valid.diagnostics],
        )
        assert valid.receipt is not None
        valid_manifest = (
            valid.receipt.engine_generation_path
            / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
        )
        manifest_before = valid_manifest.read_bytes()
        cases = product_payload_test_support.load_python_product_payload_cases()

        for relative in cases.forbidden:
            with self.subTest(relative=relative):
                source = self.editor_root.joinpath(*relative.split("/"))
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_bytes(b"repository-only Python payload")
                request = self.request()
                request = replace(
                    request,
                    editor_image=replace(
                        request.editor_image,
                        files=request.editor_image.files
                        + (
                            assembly.EditorImageFileBinding(
                                path=relative,
                                role="resource",
                                media_type="application/octet-stream",
                            ),
                        ),
                    ),
                )

                result = self.assemble(request)

                self.assertFalse(result.succeeded)
                self.assertIsNone(result.receipt)
                diagnostic = self.assert_python_policy_diagnostic(result, relative)
                self.assertNotIn(str(self.root), diagnostic.message)
                self.assertEqual(manifest_before, valid_manifest.read_bytes())
                generations = list(
                    (self.publication_root / "generations").iterdir()
                )
                self.assertEqual([valid.receipt.engine_generation_path], generations)
                staging = self.publication_root / ".asharia-distribution-staging"
                self.assertEqual([], list(staging.iterdir()))
                self.remove_added_file(source, self.editor_root)

        generation_files = (
            path.relative_to(valid.receipt.engine_generation_path).as_posix()
            for path in valid.receipt.engine_generation_path.rglob("*")
            if path.is_file()
        )
        self.assertEqual(
            (),
            product_payload_policy.find_forbidden_python_product_payloads(
                generation_files
            ),
        )

    def assert_python_policy_diagnostic(
        self,
        result: assembly.EngineDistributionAssemblyResult,
        relative: str,
    ) -> contracts.Diagnostic:
        matches = [
            diagnostic
            for diagnostic in result.diagnostics
            if diagnostic.code
            == "distribution.assembly.python-payload-forbidden"
        ]
        diagnostic = self.assert_single(matches)
        self.assertIn(relative, diagnostic.message)
        return diagnostic

    def assert_single(self, values: list[contracts.Diagnostic]) -> contracts.Diagnostic:
        self.assertEqual(1, len(values), [value.render() for value in values])
        return values[0]

    def test_bundled_package_python_payload_is_rejected_with_current_evidence(
        self,
    ) -> None:
        forbidden_relative = "Lib/site-packages/example/data.bin"
        forbidden = self.package_root.joinpath(*forbidden_relative.split("/"))
        forbidden.parent.mkdir(parents=True)
        forbidden.write_bytes(b"repository-only Python package")
        rediscovered = discovery.load_package_candidates(
            [
                discovery.LocalCandidateLocation(
                    "com.asharia.source.distribution-test",
                    self.package_root,
                )
            ],
            self.validators,
        )
        self.assertTrue(rediscovered.succeeded)
        request = self.request()
        request = replace(
            request,
            bundled_packages=(
                replace(request.bundled_packages[0], candidate=rediscovered.candidates[0]),
            ),
        )

        result = self.assemble(request)

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.receipt)
        diagnostic = self.assert_python_policy_diagnostic(
            result,
            forbidden_relative,
        )
        self.assertIn(PACKAGE_ID, diagnostic.message)
        self.assertEqual([], list(self.publication_root.iterdir()))

    def test_malformed_bundled_candidate_fails_as_a_typed_request(self) -> None:
        request = self.request()
        request = replace(
            request,
            bundled_packages=(
                replace(request.bundled_packages[0], candidate=object()),
            ),
        )

        result = self.assemble(request)

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.receipt)
        self.assertEqual(
            ["distribution.assembly.request-invalid"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )
        self.assertEqual([], list(self.publication_root.iterdir()))

    def test_self_consistent_legacy_python_artifact_receipt_is_not_assemblable(
        self,
    ) -> None:
        forbidden_relative = "runtime/pythonplugin.dll"
        legacy = self.make_artifact_receipt(
            forbidden_relative,
            assert_valid=False,
        )
        self.assertEqual(1, len(legacy.manifests))
        manifest = legacy.manifests[0]
        computed_manifest = artifacts.compute_package_artifact_manifest_integrity(
            manifest
        )
        self.assertEqual(manifest.integrity.algorithm, computed_manifest["algorithm"])
        self.assertEqual(manifest.integrity.digest, computed_manifest["digest"])
        computed_set = contracts.compute_bytes_integrity(
            artifacts.render_package_artifact_manifest_set(legacy.manifests).encode(
                "utf-8"
            )
        )
        self.assertEqual(legacy.manifest_set_integrity.algorithm, computed_set["algorithm"])
        self.assertEqual(legacy.manifest_set_integrity.digest, computed_set["digest"])
        self.assertEqual(
            legacy.artifact_generation_id,
            f"{computed_set['algorithm']}-{computed_set['digest']}",
        )
        artifact_file = (
            legacy.artifact_generation_path
            / "packages"
            / PACKAGE_ID
            / Path(*forbidden_relative.split("/"))
        )
        self.assertEqual(
            manifest.modules[0].products[0].files[0].integrity,
            self.integrity(artifact_file.read_bytes()),
        )
        request = replace(self.request(), artifact_publications=(legacy,))

        result = self.assemble(request)

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.receipt)
        self.assert_python_policy_diagnostic(result, forbidden_relative)
        self.assertEqual([], list(self.publication_root.iterdir()))

    def test_valid_assembly_is_canonical_atomic_and_idempotent(self) -> None:
        request = self.request()
        request_before = copy.deepcopy(request)

        first = self.assemble(request)
        reordered = replace(
            request,
            editor_image=replace(
                request.editor_image,
                files=tuple(reversed(request.editor_image.files)),
            ),
        )
        second = self.assemble(reordered)

        self.assertTrue(first.succeeded, [value.render() for value in first.diagnostics])
        self.assertTrue(second.succeeded, [value.render() for value in second.diagnostics])
        self.assertFalse(first.receipt.reused)
        self.assertTrue(second.receipt.reused)
        self.assertEqual(first.receipt.engine_generation_id, second.receipt.engine_generation_id)
        self.assertEqual(request_before, request)
        generation = first.receipt.engine_generation_path
        self.assertEqual(first.receipt.engine_generation_id, generation.name)
        self.assertEqual(
            self.publication_root
            / "generations"
            / first.receipt.engine_generation_id,
            generation,
        )
        self.assertEqual(
            first.receipt.manifest,
            json.loads(
                (generation / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME).read_text(
                    encoding="utf-8"
                )
            ),
        )
        rendered = contracts.render_normalized_engine_distribution_manifest(
            first.receipt.manifest
        )
        self.assertNotIn(str(self.root), rendered)
        self.assertNotIn("generatedAt", rendered)
        self.assertNotIn("cmake", rendered.casefold())
        self.assertEqual(
            [],
            [
                path
                for path in (self.publication_root / ".asharia-distribution-staging").iterdir()
            ],
        )

    @unittest.skipUnless(os.name == "nt", "Windows extended-path regression")
    def test_publication_root_with_dot_segments_uses_a_standard_receipt_path(self) -> None:
        marker = self.root / "marker"
        marker.mkdir()
        publication_with_dot_segments = marker / ".." / self.publication_root.name

        result = assembly.assemble_engine_distribution(
            self.request(),
            publication_with_dot_segments,
            self.validators,
        )

        self.assertTrue(
            result.succeeded,
            [diagnostic.render() for diagnostic in result.diagnostics],
        )
        self.assertEqual(
            self.publication_root
            / "generations"
            / result.receipt.engine_generation_id,
            result.receipt.engine_generation_path,
        )

    def test_large_payloads_are_streamed_without_path_read_bytes(self) -> None:
        large = b"x" * (assembly._COPY_CHUNK_SIZE * 3 + 17)
        (self.editor_root / "resources/bootstrap.json").write_bytes(large)
        with mock.patch.object(
            Path,
            "read_bytes",
            side_effect=AssertionError("assembly must stream payload files"),
        ):
            result = self.assemble()

        self.assertTrue(result.succeeded, [value.render() for value in result.diagnostics])
        self.assertEqual(
            len(large),
            next(
                value["size"]
                for value in result.receipt.manifest["editorImage"]["files"]
                if value["path"] == "resources/bootstrap.json"
            ),
        )

    def test_preflight_failures_do_not_create_publication_namespaces(self) -> None:
        request = self.request()
        invalid_profile = replace(
            request.host_profiles[0],
            exact_bytes=b"{not-json}\n",
        )
        result = self.assemble(replace(request, host_profiles=(invalid_profile,)))

        self.assertFalse(result.succeeded)
        self.assertEqual("distribution.assembly.host-profile-invalid", result.diagnostics[0].code)
        self.assertEqual([], list(self.publication_root.iterdir()))

    def test_stale_candidate_and_artifact_receipt_fail_closed(self) -> None:
        (self.package_root / "include/public.hpp").write_bytes(b"changed\n")
        stale_package = self.assemble()
        self.assertFalse(stale_package.succeeded)
        self.assertEqual("distribution.assembly.package-evidence-stale", stale_package.diagnostics[0].code)
        self.assertEqual([], list(self.publication_root.iterdir()))

        (self.package_root / "include/public.hpp").write_bytes(b"#pragma once\n")
        artifact_path = (
            self.artifact_receipt.artifact_generation_path
            / "packages"
            / PACKAGE_ID
            / "lib/distribution-test.lib"
        )
        artifact_path.write_bytes(b"corrupt")
        stale_artifact = self.assemble()
        self.assertFalse(stale_artifact.succeeded)
        self.assertEqual(
            "distribution.assembly.artifact-receipt-invalid",
            stale_artifact.diagnostics[0].code,
        )
        self.assertEqual([], list(self.publication_root.iterdir()))

    def test_forged_candidate_snapshot_cannot_relabel_author_bytes(self) -> None:
        forged_manifest = copy.deepcopy(self.candidate.manifest)
        forged_manifest["displayName"] = "Forged Snapshot"
        forged_candidate = replace(self.candidate, manifest=forged_manifest)
        request = self.request()
        forged_package = replace(
            request.bundled_packages[0],
            candidate=forged_candidate,
        )

        result = self.assemble(
            replace(request, bundled_packages=(forged_package,))
        )

        self.assertFalse(result.succeeded)
        self.assertEqual(
            "distribution.assembly.package-candidate-mismatch",
            result.diagnostics[0].code,
        )
        self.assertEqual([], list(self.publication_root.iterdir()))

    def test_rename_failure_cleans_owned_staging_and_exposes_no_receipt(self) -> None:
        with mock.patch.object(
            assembly.os,
            "rename",
            side_effect=OSError("injected rename failure"),
        ):
            result = self.assemble()

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.receipt)
        self.assertEqual("distribution.assembly.rename-failed", result.diagnostics[0].code)
        self.assertEqual(
            [],
            list((self.publication_root / ".asharia-distribution-staging").iterdir()),
        )
        self.assertEqual([], list((self.publication_root / "generations").iterdir()))

    def test_source_and_staging_drift_are_detected_before_publication(self) -> None:
        original_copy = assembly._copy_regular_file
        changed = False

        def copy_then_change(source: Path, destination: Path):
            nonlocal changed
            result = original_copy(source, destination)
            if not changed and source.name == "asharia-editor.exe":
                source.write_bytes(b"changed-after-copy")
                changed = True
            return result

        with mock.patch.object(
            assembly,
            "_copy_regular_file",
            side_effect=copy_then_change,
        ):
            source_drift = self.assemble()

        self.assertFalse(source_drift.succeeded)
        self.assertEqual(
            "distribution.assembly.source-drift",
            source_drift.diagnostics[0].code,
        )
        self.assertEqual([], list((self.publication_root / "generations").iterdir()))

        (self.editor_root / "bin/asharia-editor.exe").write_bytes(b"editor-binary")
        original_write = assembly._write_exact_file

        def write_then_change(path: Path, contents: bytes) -> None:
            original_write(path, contents)
            if path.name == contracts.HOST_PROFILE_NAME:
                path.write_bytes(b"{}\n")

        with mock.patch.object(
            assembly,
            "_write_exact_file",
            side_effect=write_then_change,
        ):
            staging_drift = self.assemble()

        self.assertFalse(staging_drift.succeeded)
        self.assertIsNone(staging_drift.receipt)
        self.assertEqual([], list((self.publication_root / "generations").iterdir()))

    def test_public_artifact_receipt_verifier_rejects_malformed_nested_values(self) -> None:
        malformed_manifest = replace(
            self.artifact_receipt.manifests[0],
            modules=(None,),
        )
        malformed_receipt = replace(
            self.artifact_receipt,
            manifests=(malformed_manifest,),
        )

        diagnostics = publication.verify_package_artifact_publication_receipt(
            malformed_receipt,
            self.validators,
        )

        self.assertEqual(1, len(diagnostics))
        self.assertEqual(
            "artifact.publication.receipt-invalid",
            diagnostics[0].code,
        )

    def test_existing_corrupt_generation_is_never_overwritten(self) -> None:
        first = self.assemble()
        self.assertTrue(first.succeeded)
        editor = first.receipt.engine_generation_path / "bin/asharia-editor.exe"
        editor.write_bytes(b"locally-corrupt")

        second = self.assemble()

        self.assertFalse(second.succeeded)
        self.assertIsNone(second.receipt)
        self.assertEqual("distribution.assembly.existing-corrupt", second.diagnostics[0].code)
        self.assertEqual(b"locally-corrupt", editor.read_bytes())
        self.assertEqual(
            [],
            list((self.publication_root / ".asharia-distribution-staging").iterdir()),
        )

    def test_editor_extra_file_and_source_publication_overlap_are_rejected(self) -> None:
        (self.editor_root / "unowned.txt").write_text("extra", encoding="utf-8")
        extra = self.assemble()
        self.assertFalse(extra.succeeded)
        self.assertEqual("distribution.assembly.editor-layout-mismatch", extra.diagnostics[0].code)

        (self.editor_root / "unowned.txt").unlink()
        overlap = assembly.assemble_engine_distribution(
            self.request(),
            self.editor_root,
            self.validators,
        )
        self.assertFalse(overlap.succeeded)
        self.assertEqual("distribution.assembly.root-overlap", overlap.diagnostics[0].code)


if __name__ == "__main__":
    unittest.main()
