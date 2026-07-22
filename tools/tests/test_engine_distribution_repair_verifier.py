"""Installed Engine Distribution Repair Verifier v1 tests."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import engine_distribution_repair_verifier as repair
from tools import package_artifact_publication as publication
from tools import stable_file_identity
from tools.tests import test_engine_distribution_assembly as assembly_tests


class InstalledDistributionRepairVerifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assembly_tests.EngineDistributionAssemblyTests.setUpClass()
        cls.validators = assembly_tests.EngineDistributionAssemblyTests.validators

    def setUp(self) -> None:
        self.fixture = assembly_tests.EngineDistributionAssemblyTests(
            methodName="runTest"
        )
        self.fixture.setUp()
        assembled = self.fixture.assemble()
        self.assertTrue(
            assembled.succeeded,
            [diagnostic.render() for diagnostic in assembled.diagnostics],
        )
        self.receipt = assembled.receipt
        assert self.receipt is not None
        self.generation_root = self.receipt.engine_generation_path
        self.generation_io_root = stable_file_identity.extended_path(
            self.generation_root
        )
        self.generation_id = self.receipt.engine_generation_id

    def tearDown(self) -> None:
        self.fixture.tearDown()

    def request(
        self,
        *,
        root: Path | None = None,
        generation_id: str | None = None,
    ) -> repair.InstalledDistributionVerificationRequest:
        return repair.InstalledDistributionVerificationRequest(
            generation_root=root or self.generation_root,
            expected_engine_generation_id=generation_id or self.generation_id,
        )

    def verify(
        self,
        request: object | None = None,
    ) -> repair.InstalledDistributionVerificationResult:
        return repair.verify_installed_engine_distribution(
            request if request is not None else self.request(),
            self.validators,
        )

    @staticmethod
    def finding_codes(
        result: repair.InstalledDistributionVerificationResult,
    ) -> list[str]:
        assert result.report is not None
        return [finding.code for finding in result.report.findings]

    @staticmethod
    def tree_snapshot(root: Path) -> tuple[tuple[str, str, int], ...]:
        root = stable_file_identity.extended_path(root)
        values: list[tuple[str, str, int]] = []
        for path in sorted(root.rglob("*"), key=lambda value: value.as_posix()):
            relative = path.relative_to(root).as_posix()
            status = path.lstat()
            if path.is_file() and not path.is_symlink():
                digest = hashlib.sha256()
                with path.open("rb") as source:
                    while chunk := source.read(1024 * 1024):
                        digest.update(chunk)
                values.append((relative, digest.hexdigest(), status.st_size))
            else:
                values.append((relative, "<directory-or-link>", status.st_size))
        return tuple(values)

    def test_valid_generation_is_healthy_read_only_and_preserves_input(self) -> None:
        request = self.request()
        request_before = copy.deepcopy(request)
        tree_before = self.tree_snapshot(self.generation_root)

        result = self.verify(request)

        self.assertTrue(result.succeeded)
        self.assertEqual((), result.diagnostics)
        self.assertIs(result.report.state, repair.DistributionHealthState.HEALTHY)
        self.assertEqual((), result.report.findings)
        self.assertEqual(request_before, request)
        self.assertEqual(tree_before, self.tree_snapshot(self.generation_root))
        verified = result.report.verified_distribution
        self.assertIsNotNone(verified)
        self.assertEqual(self.generation_id, verified.engine_generation_id)
        self.assertEqual(self.generation_root, verified.generation_root)
        self.assertEqual(
            contracts.compute_bytes_integrity(verified.manifest_bytes),
            verified.manifest_integrity,
        )
        self.assertNotIn(str(self.fixture.root), str(result.report.findings))

    def test_invalid_request_stops_before_filesystem_access(self) -> None:
        invalid = repair.InstalledDistributionVerificationRequest(
            generation_root=self.generation_root,
            expected_engine_generation_id="not-a-generation",
        )
        with mock.patch.object(Path, "absolute") as absolute:
            result = self.verify(invalid)

        absolute.assert_not_called()
        self.assertIsNone(result.report)
        self.assertEqual(
            ["distribution.repair.request-generation-id-invalid"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_missing_root_is_repair_required_not_request_failure(self) -> None:
        result = self.verify(
            self.request(root=self.fixture.root / self.generation_id)
        )

        self.assertFalse(result.succeeded)
        self.assertEqual((), result.diagnostics)
        self.assertIs(
            result.report.state,
            repair.DistributionHealthState.REPAIR_REQUIRED,
        )
        self.assertIn("distribution.repair.root-missing", self.finding_codes(result))
        self.assertIsNone(result.report.verified_distribution)

    def test_external_expected_id_is_not_inferred_from_disk(self) -> None:
        other_id = "sha256-" + ("0" * 64)
        result = self.verify(self.request(generation_id=other_id))

        self.assertIn(
            "distribution.repair.root-generation-id-mismatch",
            self.finding_codes(result),
        )
        self.assertIsNone(result.report.observed_engine_generation_id)

    def test_manifest_noncanonical_stops_before_downstream_inventory(self) -> None:
        manifest_path = (
            self.generation_root / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
        )
        with manifest_path.open("r", encoding="utf-8") as source:
            manifest = json.load(source)
        with manifest_path.open("w", encoding="utf-8", newline="\n") as destination:
            json.dump(manifest, destination, ensure_ascii=False, separators=(",", ":"))

        with mock.patch.object(repair, "_scan_regular_tree") as scan:
            result = self.verify()

        scan.assert_not_called()
        self.assertEqual(
            ["distribution.repair.manifest-noncanonical"],
            self.finding_codes(result),
        )
        self.assertEqual(self.generation_id, result.report.observed_engine_generation_id)

    def test_manifest_invalid_json_stops_before_downstream_inventory(self) -> None:
        manifest_path = (
            self.generation_root / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
        )
        with manifest_path.open("wb") as destination:
            destination.write(b"{not-json}\n")

        with mock.patch.object(repair, "_scan_regular_tree") as scan:
            result = self.verify()

        scan.assert_not_called()
        self.assertEqual(
            ["distribution.repair.manifest-json"],
            self.finding_codes(result),
        )

    def test_manifest_bom_is_an_encoding_failure(self) -> None:
        manifest_path = (
            self.generation_root / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
        )
        with manifest_path.open("rb") as source:
            contents = source.read()
        with manifest_path.open("wb") as destination:
            destination.write(b"\xef\xbb\xbf" + contents)

        result = self.verify()

        self.assertEqual(
            ["distribution.repair.manifest-encoding"],
            self.finding_codes(result),
        )

    def test_editor_profile_and_package_corruption_are_independent_findings(self) -> None:
        (self.generation_root / "bin/asharia-editor.exe").write_bytes(b"bad-editor")
        (self.generation_root / "profiles/editor/asharia.host-profile.json").write_bytes(
            b"{}\n"
        )
        package_manifest = (
            self.generation_io_root
            / "packages/systems"
            / self.fixture.candidate.identity
            / contracts.PACKAGE_MANIFEST_NAME
        )
        with package_manifest.open("ab") as destination:
            destination.write(b" ")

        result = self.verify()

        codes = self.finding_codes(result)
        self.assertIn("distribution.repair.editor-integrity", codes)
        self.assertIn("distribution.repair.profile-integrity", codes)
        self.assertIn("distribution.repair.profile-contract", codes)
        self.assertIn("distribution.repair.package-manifest-integrity", codes)
        self.assertIn("distribution.repair.package-payload-integrity", codes)
        self.assertEqual(
            list(result.report.findings),
            sorted(result.report.findings, key=repair._diagnostic_sort_key),
        )
        self.assertIsNone(result.report.verified_distribution)

    def test_excluded_package_content_is_rejected_even_though_tree_hash_ignores_it(self) -> None:
        excluded = (
            self.generation_io_root
            / "packages/systems"
            / self.fixture.candidate.identity
            / "build"
        )
        excluded.mkdir()
        (excluded / "unowned.obj").write_bytes(b"ignored-by-package-tree-domain")

        result = self.verify()

        self.assertIn(
            "distribution.repair.package-excluded-content",
            self.finding_codes(result),
        )

    def test_artifact_byte_corruption_is_detected_without_publication_receipt(self) -> None:
        artifact_root = (
            self.generation_io_root
            / "artifacts"
            / self.fixture.artifact_receipt.artifact_generation_id
        )
        artifact_file = (
            artifact_root
            / "packages"
            / self.fixture.candidate.identity
            / "lib/distribution-test.lib"
        )
        artifact_file.write_bytes(b"corrupt")

        disk_result = publication.verify_published_package_artifact_generation(
            artifact_root,
            self.fixture.artifact_receipt.artifact_generation_id,
            self.validators,
        )
        distribution_result = self.verify()

        self.assertFalse(disk_result.succeeded)
        self.assertIsNone(disk_result.verified_generation)
        self.assertIn(
            "distribution.repair.artifact-invalid",
            self.finding_codes(distribution_result),
        )

    def test_artifact_generation_extra_file_fails_disk_closed_layout(self) -> None:
        generation_id = self.fixture.artifact_receipt.artifact_generation_id
        artifact_root = self.generation_io_root / "artifacts" / generation_id
        (artifact_root / "undeclared.txt").write_bytes(b"extra")

        result = publication.verify_published_package_artifact_generation(
            artifact_root,
            generation_id,
            self.validators,
        )

        self.assertFalse(result.succeeded)
        self.assertIn(
            "artifact.publication.layout-mismatch",
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_artifact_manifest_noncanonical_bytes_fail_disk_bootstrap(self) -> None:
        generation_id = self.fixture.artifact_receipt.artifact_generation_id
        artifact_root = self.generation_io_root / "artifacts" / generation_id
        manifest_path = (
            artifact_root
            / "packages"
            / self.fixture.candidate.identity
            / contracts.PACKAGE_ARTIFACT_MANIFEST_NAME
        )
        with manifest_path.open("r", encoding="utf-8") as source:
            data = json.load(source)
        with manifest_path.open("w", encoding="utf-8", newline="\n") as destination:
            json.dump(data, destination, ensure_ascii=False, separators=(",", ":"))

        result = publication.verify_published_package_artifact_generation(
            artifact_root,
            generation_id,
            self.validators,
        )

        self.assertFalse(result.succeeded)
        self.assertIn(
            "artifact.generation.manifest-noncanonical",
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_disk_artifact_loader_reconstructs_canonical_generation(self) -> None:
        generation_id = self.fixture.artifact_receipt.artifact_generation_id
        artifact_root = self.generation_io_root / "artifacts" / generation_id

        result = publication.verify_published_package_artifact_generation(
            artifact_root,
            generation_id,
            self.validators,
        )

        self.assertTrue(result.succeeded, [value.render() for value in result.diagnostics])
        verified = result.verified_generation
        self.assertIsNotNone(verified)
        self.assertEqual(generation_id, verified.artifact_generation_id)
        self.assertEqual(
            (self.fixture.candidate.identity,),
            tuple(manifest.package_id for manifest in verified.manifests),
        )
        self.assertIsNot(verified.manifests, self.fixture.artifact_receipt.manifests)

    def test_extra_file_and_empty_directory_fail_closed(self) -> None:
        (self.generation_root / "undeclared.txt").write_bytes(b"extra")
        (self.generation_root / "empty-directory").mkdir()

        result = self.verify()

        codes = self.finding_codes(result)
        self.assertIn("distribution.repair.layout-extra-file", codes)
        self.assertIn("distribution.repair.layout-directory-mismatch", codes)

    def test_verification_does_not_use_path_read_bytes_for_payloads(self) -> None:
        with mock.patch.object(
            Path,
            "read_bytes",
            side_effect=AssertionError("payload Path.read_bytes() is forbidden"),
        ), mock.patch.object(repair, "_HASH_CHUNK_SIZE", 3):
            result = self.verify()

        self.assertTrue(result.succeeded, [value.render() for value in result.report.findings])

    def test_failure_path_is_read_only(self) -> None:
        editor = self.generation_root / "bin/asharia-editor.exe"
        editor.write_bytes(b"corrupt-editor")
        before = self.tree_snapshot(self.generation_root)

        result = self.verify()

        self.assertFalse(result.succeeded)
        self.assertEqual(before, self.tree_snapshot(self.generation_root))

    def test_root_reparse_decision_fails_closed_without_os_link_privileges(self) -> None:
        with mock.patch.object(repair, "_is_link_or_reparse", return_value=True):
            result = self.verify()

        self.assertIn("distribution.repair.root-link", self.finding_codes(result))

    def test_generation_root_link_is_rejected_when_supported(self) -> None:
        link = self.fixture.root / "generation-link"
        try:
            os.symlink(self.generation_root, link, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"directory symlink is unavailable: {error}")

        result = self.verify(self.request(root=link))

        self.assertIn("distribution.repair.root-link", self.finding_codes(result))


if __name__ == "__main__":
    unittest.main()
