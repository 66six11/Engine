"""Project Package Sources v1 schema, semantics, discovery, and writer tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

from tools import check_package_contracts as contracts


class ProjectPackageSourcesContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    @staticmethod
    def manifest() -> dict[str, Any]:
        return {
            "schema": "com.asharia.project-package-sources",
            "schemaVersion": 1,
            "sources": [
                {
                    "kind": "project-embedded",
                    "relativePath": "Packages/com.asharia.project.gameplay",
                },
                {
                    "kind": "local",
                    "sourceId": "com.example.workspace.rendering",
                },
            ],
        }

    def validate(
        self,
        manifest: dict[str, Any],
        manifest_path: str = contracts.PROJECT_PACKAGE_SOURCES_NAME,
    ) -> list[contracts.Diagnostic]:
        return contracts.validate_manifest_data(
            manifest,
            manifest_path,
            self.validators,
        )

    def codes(self, manifest: dict[str, Any]) -> list[str]:
        return [diagnostic.code for diagnostic in self.validate(manifest)]

    def test_valid_sources_are_accepted_by_filename_and_discriminator(self) -> None:
        manifest = self.manifest()

        self.assertEqual([], self.validate(manifest))
        self.assertEqual([], self.validate(manifest, "renamed.json"))
        self.assertEqual([], self.validate({**manifest, "sources": []}))

    def test_closed_union_rejects_nonportable_or_evidence_fields(self) -> None:
        mutations = {
            "absolute-project-root": lambda source: source.update(
                {"relativePath": "C:/work/package"}
            ),
            "backslash-project-root": lambda source: source.update(
                {"relativePath": "Packages\\package"}
            ),
            "project-source-id": lambda source: source.update(
                {"sourceId": "com.example.workspace.package"}
            ),
            "local-relative-path": lambda source: source.update(
                {"relativePath": "Packages/package"}
            ),
            "absolute-local-root": lambda source: source.update(
                {"root": "C:/work/package"}
            ),
            "identity-evidence": lambda source: source.update(
                {"id": "com.example.package.gameplay"}
            ),
            "version-evidence": lambda source: source.update({"version": "1.0.0"}),
            "integrity-evidence": lambda source: source.update(
                {"integrity": {"algorithm": "sha256", "digest": "0" * 64}}
            ),
        }

        for name, mutate in mutations.items():
            with self.subTest(name=name):
                manifest = self.manifest()
                source_index = 1 if name.startswith(("local", "absolute-local")) else 0
                mutate(manifest["sources"][source_index])
                self.assertEqual(["project.sources.schema"], self.codes(manifest))

    def test_project_embedded_roots_must_be_portable_normalized_paths(self) -> None:
        invalid_paths = (
            "Packages/../package",
            "Packages//package",
            "Packages/CON/package",
            "Packages/package.",
            "Packages/package ",
            "Packages/package?",
            "Packages/package\u001f",
            "Packages/Cafe\u0301",
        )

        for relative_path in invalid_paths:
            with self.subTest(relative_path=relative_path):
                manifest = self.manifest()
                manifest["sources"][0]["relativePath"] = relative_path
                self.assertEqual(
                    ["project.sources.invalid-relative-path"],
                    self.codes(manifest),
                )

    def test_exact_and_unicode_casefold_duplicate_sources_are_rejected(self) -> None:
        duplicate_path = self.manifest()
        duplicate_path["sources"].insert(1, copy.deepcopy(duplicate_path["sources"][0]))
        self.assertEqual(["project.sources.duplicate"], self.codes(duplicate_path))

        path_collision = self.manifest()
        path_collision["sources"].insert(
            1,
            {
                "kind": "project-embedded",
                "relativePath": "packages/COM.ASHARIA.PROJECT.GAMEPLAY",
            },
        )
        self.assertEqual(
            ["project.sources.casefold-collision"],
            self.codes(path_collision),
        )
        collision_rendered = [item.render() for item in self.validate(path_collision)]
        path_collision["sources"].reverse()
        self.assertEqual(
            collision_rendered,
            [item.render() for item in self.validate(path_collision)],
        )

        duplicate_source_id = self.manifest()
        duplicate_source_id["sources"].append(
            copy.deepcopy(duplicate_source_id["sources"][1])
        )
        self.assertEqual(["project.sources.duplicate"], self.codes(duplicate_source_id))

    def test_ancestor_project_roots_are_rejected_permutation_independently(self) -> None:
        manifest = self.manifest()
        manifest["sources"].insert(
            1,
            {
                "kind": "project-embedded",
                "relativePath": "packages/com.asharia.project.gameplay/nested",
            },
        )

        diagnostics = self.validate(manifest)
        self.assertEqual(
            ["project.sources.overlapping-root"],
            [item.code for item in diagnostics],
        )
        self.assertEqual("/sources", diagnostics[0].pointer)
        rendered = [item.render() for item in diagnostics]

        manifest["sources"].reverse()
        self.assertEqual(rendered, [item.render() for item in self.validate(manifest)])

    def test_canonical_writer_is_permutation_independent_and_round_trips(self) -> None:
        manifest = self.manifest()
        original = copy.deepcopy(manifest)
        reordered = copy.deepcopy(manifest)
        reordered["sources"].reverse()

        first = contracts.render_normalized_project_package_sources(manifest)
        second = contracts.render_normalized_project_package_sources(reordered)

        self.assertEqual(first, second)
        self.assertEqual(original, manifest)
        self.assertEqual(
            first,
            contracts.render_normalized_project_package_sources(json.loads(first)),
        )
        self.assertLess(
            first.index('"kind": "local"'),
            first.index('"kind": "project-embedded"'),
        )
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\r", first)

    def test_writer_uses_utf8_without_bom_and_lf(self) -> None:
        manifest = self.manifest()
        manifest["sources"][0]["relativePath"] = "Packages/\u00e9clairage"

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / contracts.PROJECT_PACKAGE_SOURCES_NAME
            contracts.write_normalized_project_package_sources(path, manifest)
            contents = path.read_bytes()

        self.assertFalse(contents.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\r", contents)
        self.assertIn("\u00e9clairage".encode("utf-8"), contents)
        self.assertTrue(contents.endswith(b"\n"))

    def test_repository_discovery_finds_sources_contract_once(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            path = root / "Project" / contracts.PROJECT_PACKAGE_SOURCES_NAME
            path.parent.mkdir()
            path.write_text(json.dumps(self.manifest()), encoding="utf-8")

            discovered = contracts.discover_contract_manifests(root)

        self.assertEqual([path], discovered)


if __name__ == "__main__":
    unittest.main()
