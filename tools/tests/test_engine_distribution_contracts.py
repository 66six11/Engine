"""Engine Distribution Manifest v1 schema, semantics, and writer tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools import check_package_contracts


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class EngineDistributionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()

    def load(self) -> dict[str, object]:
        return json.loads(
            (FIXTURE_ROOT / "valid-engine-distribution.json").read_text(encoding="utf-8")
        )

    def refresh_generation_id(self, manifest: dict[str, object]) -> None:
        manifest["engineGenerationId"] = (
            check_package_contracts.compute_engine_generation_id(manifest)
        )

    def validate(
        self, manifest: dict[str, object]
    ) -> list[check_package_contracts.Diagnostic]:
        return check_package_contracts.validate_manifest_data(
            manifest,
            check_package_contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
            self.validators,
        )

    def codes(self, manifest: dict[str, object]) -> set[str]:
        return {diagnostic.code for diagnostic in self.validate(manifest)}

    def test_valid_distribution_is_accepted_by_filename_and_discriminator(self) -> None:
        manifest = self.load()

        self.assertEqual([], self.validate(manifest))
        self.assertEqual(
            [],
            check_package_contracts.validate_manifest_data(
                manifest,
                "renamed.json",
                self.validators,
            ),
        )

    def test_closed_schema_rejects_unknown_fields_and_malformed_generation_ids(self) -> None:
        unknown = self.load()
        unknown["generatedAt"] = "2026-07-14T00:00:00Z"
        self.assertEqual(
            {"distribution.manifest.schema"},
            self.codes(unknown),
        )

        malformed = self.load()
        malformed["engineGenerationId"] = "sha256:ABC"
        self.assertEqual(
            {"distribution.manifest.schema"},
            self.codes(malformed),
        )

        illegal_digest = self.load()
        illegal_digest["editorImage"]["files"][0]["integrity"]["digest"] = "ABC"
        self.assertEqual(
            {"distribution.manifest.schema"},
            self.codes(illegal_digest),
        )

        incomplete = self.load()
        del incomplete["distribution"]["engineApiVersion"]
        self.assertEqual(
            {"distribution.manifest.schema"},
            self.codes(incomplete),
        )

        absolute_path = self.load()
        absolute_path["hostProfiles"][0]["path"] = "C:/profiles/editor.json"
        self.assertEqual(
            {"distribution.manifest.schema"},
            self.codes(absolute_path),
        )

        copied_artifact_files = self.load()
        copied_artifact_files["packageArtifacts"][0]["files"] = []
        self.assertEqual(
            {"distribution.manifest.schema"},
            self.codes(copied_artifact_files),
        )

    def test_generation_id_is_content_derived_and_stale_ids_fail_closed(self) -> None:
        manifest = self.load()
        original_id = manifest["engineGenerationId"]
        manifest["context"]["configuration"] = "Release"
        manifest["packageArtifacts"][0]["context"]["configuration"] = "Release"

        changed_id = check_package_contracts.compute_engine_generation_id(manifest)

        self.assertNotEqual(original_id, changed_id)
        self.assertEqual(
            {"distribution.generation-id.mismatch"},
            self.codes(manifest),
        )
        manifest["engineGenerationId"] = changed_id
        self.assertEqual([], self.validate(manifest))
        payload = check_package_contracts.render_engine_generation_payload(manifest)
        self.assertNotIn("engineGenerationId", payload)
        self.assertNotIn(str(original_id), payload)

    def test_canonical_writer_is_permutation_independent_and_round_trips(self) -> None:
        manifest = self.load()
        original = copy.deepcopy(manifest)
        reordered = copy.deepcopy(manifest)
        reordered["editorImage"]["files"].reverse()
        reordered["bundledPackages"].reverse()
        reordered["hostProfiles"].reverse()

        first = check_package_contracts.render_normalized_engine_distribution_manifest(
            manifest
        )
        second = check_package_contracts.render_normalized_engine_distribution_manifest(
            reordered
        )

        self.assertEqual(first, second)
        self.assertEqual(
            (FIXTURE_ROOT / "valid-engine-distribution.json").read_text(encoding="utf-8"),
            first,
        )
        self.assertEqual(original, manifest)
        self.assertEqual(
            first,
            check_package_contracts.render_normalized_engine_distribution_manifest(
                json.loads(first)
            ),
        )
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\r", first)

    def test_writer_uses_utf8_without_bom_and_lf(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "asharia.engine-distribution.json"
            check_package_contracts.write_normalized_engine_distribution_manifest(
                path, self.load()
            )
            contents = path.read_bytes()

        self.assertFalse(contents.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\r", contents)
        self.assertTrue(contents.endswith(b"\n"))

    def test_editor_entry_point_must_reference_an_executable_file(self) -> None:
        missing = self.load()
        missing["editorImage"]["entryPoint"] = "bin/missing.exe"
        self.refresh_generation_id(missing)
        self.assertEqual(
            {"distribution.editor.entry-point-missing"},
            self.codes(missing),
        )

        wrong_role = self.load()
        wrong_role["editorImage"]["files"][0]["role"] = "resource"
        self.refresh_generation_id(wrong_role)
        self.assertEqual(
            {"distribution.editor.entry-point-role"},
            self.codes(wrong_role),
        )

    def test_duplicate_package_identity_and_multiple_versions_are_rejected(self) -> None:
        duplicate = self.load()
        duplicate["bundledPackages"].append(
            copy.deepcopy(duplicate["bundledPackages"][1])
        )
        duplicate["bundledPackages"][-1]["root"] = "packages/systems/rendering-copy"
        self.refresh_generation_id(duplicate)
        self.assertIn("distribution.package.duplicate-id", self.codes(duplicate))

        multiple = self.load()
        multiple["bundledPackages"].append(
            copy.deepcopy(multiple["bundledPackages"][1])
        )
        multiple["bundledPackages"][-1]["version"] = "0.2.0"
        multiple["bundledPackages"][-1]["root"] = "packages/systems/rendering-next"
        self.refresh_generation_id(multiple)
        self.assertIn("distribution.package.multiple-versions", self.codes(multiple))

    def test_package_artifact_references_exact_bundled_package_and_context(self) -> None:
        mutations = {
            "missing": (
                lambda manifest: manifest["packageArtifacts"][0]["package"].update(
                    {"id": "com.asharia.system.missing"}
                ),
                "distribution.artifact.package-missing",
            ),
            "version": (
                lambda manifest: manifest["packageArtifacts"][0]["package"].update(
                    {"version": "0.2.0"}
                ),
                "distribution.artifact.package-version",
            ),
            "feature-set": (
                lambda manifest: manifest["packageArtifacts"][0]["package"].update(
                    {"id": "com.asharia.features.standard3d"}
                ),
                "distribution.artifact.package-kind",
            ),
            "platform": (
                lambda manifest: manifest["packageArtifacts"][0]["context"].update(
                    {"targetPlatform": "com.asharia.platform.linux"}
                ),
                "distribution.artifact.target-platform",
            ),
            "configuration": (
                lambda manifest: manifest["packageArtifacts"][0]["context"].update(
                    {"configuration": "Release"}
                ),
                "distribution.artifact.configuration",
            ),
        }
        for name, (mutate, expected_code) in mutations.items():
            with self.subTest(name=name):
                manifest = self.load()
                mutate(manifest)
                self.refresh_generation_id(manifest)
                self.assertIn(expected_code, self.codes(manifest))

        duplicate = self.load()
        duplicate["packageArtifacts"].append(
            copy.deepcopy(duplicate["packageArtifacts"][0])
        )
        duplicate["packageArtifacts"][-1]["manifestPath"] = (
            "artifacts/duplicate/asharia.package.artifacts.json"
        )
        self.refresh_generation_id(duplicate)
        self.assertIn(
            "distribution.artifact.duplicate-context",
            self.codes(duplicate),
        )

    def test_host_profiles_are_unique_and_match_distribution_platform(self) -> None:
        duplicate = self.load()
        duplicate["hostProfiles"].append(copy.deepcopy(duplicate["hostProfiles"][0]))
        duplicate["hostProfiles"][-1]["path"] = "profiles/editor-copy/profile.json"
        self.refresh_generation_id(duplicate)
        self.assertIn("distribution.host-profile.duplicate", self.codes(duplicate))

        platform = self.load()
        platform["hostProfiles"][0]["targetPlatform"] = "com.asharia.platform.linux"
        self.refresh_generation_id(platform)
        self.assertIn(
            "distribution.host-profile.target-platform",
            self.codes(platform),
        )

    def test_distribution_paths_are_portable_and_collision_free(self) -> None:
        mutations = {
            "reserved": (
                lambda manifest: manifest["hostProfiles"][0].update(
                    {"path": "profiles/CON/profile.json"}
                ),
                "distribution.path.invalid",
            ),
            "case-fold": (
                lambda manifest: manifest["hostProfiles"][0].update(
                    {"path": "BIN/ASHARIA-EDITOR.EXE"}
                ),
                "distribution.path.collision",
            ),
            "ancestor": (
                lambda manifest: manifest["hostProfiles"][0].update(
                    {"path": "bin/asharia-editor.exe/profile.json"}
                ),
                "distribution.path.ancestor-collision",
            ),
            "duplicate": (
                lambda manifest: manifest["hostProfiles"][0].update(
                    {"path": "resources/editor-shell.json"}
                ),
                "distribution.path.duplicate",
            ),
            "nested-package-root": (
                lambda manifest: manifest["bundledPackages"][1].update(
                    {"root": "packages/features/standard3d/rendering"}
                ),
                "distribution.path.ancestor-collision",
            ),
        }
        for name, (mutate, expected_code) in mutations.items():
            with self.subTest(name=name):
                manifest = self.load()
                mutate(manifest)
                self.refresh_generation_id(manifest)
                self.assertIn(expected_code, self.codes(manifest))

    def test_discovery_finds_distribution_manifest_once(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            path = root / "distribution" / "asharia.engine-distribution.json"
            path.parent.mkdir()
            path.write_text(json.dumps(self.load()), encoding="utf-8")

            discovered = check_package_contracts.discover_contract_manifests(root)

        self.assertEqual([path], discovered)


if __name__ == "__main__":
    unittest.main()
