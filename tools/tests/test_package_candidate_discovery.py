"""Explicit-source Package Candidate Discovery v1 tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts
from tools import package_candidate_discovery
from tools import package_candidates
from tools import package_resolver
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
ENGINE_API_VERSION = "0.1.0"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


class PackageCandidateDiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )
        cls.feature_set_template = json.loads(
            (FIXTURE_ROOT / "valid-feature-set.json").read_text(encoding="utf-8")
        )

    def installable(self, identity: str, version: str) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["version"] = version
        manifest["displayName"] = identity
        manifest["dependencies"] = []
        return manifest

    def feature_set(self, identity: str, version: str) -> dict[str, object]:
        manifest = copy.deepcopy(self.feature_set_template)
        manifest["id"] = identity
        manifest["version"] = version
        manifest["displayName"] = identity
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

    def load(
        self,
        locations: list[package_candidate_discovery.CandidateLocation],
    ) -> package_candidate_discovery.CandidateDiscoveryResult:
        return package_candidate_discovery.load_package_candidates(
            locations,
            self.validators,
        )

    def test_shared_candidate_contract_keeps_resolver_import_compatible(self) -> None:
        self.assertIs(package_candidates.PackageCandidate, package_resolver.PackageCandidate)

    def test_three_explicit_source_kinds_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            distribution_root = temporary_root / "distribution"
            bundled_root = self.write_package(
                distribution_root / "packages/rendering",
                self.installable("com.asharia.system.rendering", "1.0.0"),
            )
            project_root = temporary_root / "project"
            embedded_root = self.write_package(
                project_root / "Packages/audio",
                self.installable("com.asharia.system.audio", "2.0.0"),
            )
            local_root = self.write_package(
                temporary_root / "workspace/physics",
                self.feature_set("com.asharia.features.physics", "3.0.0"),
            )
            locations: list[package_candidate_discovery.CandidateLocation] = [
                package_candidate_discovery.ProjectEmbeddedCandidateLocation(
                    project_root=project_root,
                    relative_path="Packages/audio",
                ),
                package_candidate_discovery.LocalCandidateLocation(
                    source_id="com.asharia.source.physics-workspace",
                    payload_root=local_root,
                ),
                package_candidate_discovery.EngineDistributedCandidateLocation(
                    distribution_root=distribution_root,
                    relative_path="packages/rendering",
                ),
            ]
            locations_before = copy.deepcopy(locations)

            first = self.load(locations)
            second = self.load(list(reversed(locations)))

            self.assertTrue(
                first.succeeded,
                [diagnostic.render() for diagnostic in first.diagnostics],
            )
            self.assertEqual(first.candidates, second.candidates)
            self.assertEqual(locations_before, locations)
            self.assertEqual(
                [
                    "engine-distribution:packages/rendering",
                    "local:com.asharia.source.physics-workspace",
                    "project-embedded:Packages/audio",
                ],
                [candidate.origin for candidate in first.candidates],
            )
            self.assertEqual(
                [
                    {"kind": "engine-distribution"},
                    {
                        "kind": "local",
                        "sourceId": "com.asharia.source.physics-workspace",
                    },
                    {"kind": "project-embedded", "relativePath": "Packages/audio"},
                ],
                [candidate.source for candidate in first.candidates],
            )
            self.assertEqual(
                [bundled_root.resolve(), local_root.resolve(), embedded_root.resolve()],
                [candidate.payload_location for candidate in first.candidates],
            )
            for candidate in first.candidates:
                root = candidate.payload_location
                self.assertEqual(
                    check_package_contracts.compute_manifest_file_integrity(
                        root / check_package_contracts.PACKAGE_MANIFEST_NAME
                    ),
                    candidate.manifest_integrity,
                )
                self.assertEqual(
                    check_package_contracts.compute_package_tree_integrity(root),
                    candidate.payload_integrity,
                )

    def test_empty_input_is_a_successful_empty_snapshot(self) -> None:
        result = self.load([])

        self.assertTrue(result.succeeded)
        self.assertEqual((), result.candidates)
        self.assertEqual((), result.diagnostics)

    def test_invalid_source_fails_atomically_and_is_permutation_stable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            valid_root = self.write_package(
                temporary_root / "valid",
                self.installable("com.asharia.system.valid", "1.0.0"),
            )
            locations: list[package_candidate_discovery.CandidateLocation] = [
                package_candidate_discovery.LocalCandidateLocation(
                    source_id="com.asharia.source.valid",
                    payload_root=valid_root,
                ),
                package_candidate_discovery.LocalCandidateLocation(
                    source_id="com.asharia.source.missing",
                    payload_root=temporary_root / "missing",
                ),
            ]

            first = self.load(locations)
            second = self.load(list(reversed(locations)))

            self.assertFalse(first.succeeded)
            self.assertEqual((), first.candidates)
            self.assertEqual(
                [diagnostic.render() for diagnostic in first.diagnostics],
                [diagnostic.render() for diagnostic in second.diagnostics],
            )
            self.assertEqual("discovery.source.unavailable", first.diagnostics[0].code)
            self.assertNotIn(str(temporary_root), first.diagnostics[0].render())

    def test_invalid_relative_path_is_rejected_by_existing_source_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            result = self.load(
                [
                    package_candidate_discovery.ProjectEmbeddedCandidateLocation(
                        project_root=project_root,
                        relative_path="../outside",
                    )
                ]
            )

        self.assertFalse(result.succeeded)
        self.assertEqual(["lock.source.invalid-relative-path"], [
            diagnostic.code for diagnostic in result.diagnostics
        ])
        self.assertNotIn(str(project_root), result.diagnostics[0].render())

    def test_manifest_and_descriptor_failures_are_atomic_and_classified(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            missing_manifest_root = temporary_root / "missing-manifest"
            missing_manifest_root.mkdir()
            invalid_json_root = temporary_root / "invalid-json"
            invalid_json_root.mkdir()
            (invalid_json_root / check_package_contracts.PACKAGE_MANIFEST_NAME).write_bytes(
                b"{not-json}\n"
            )
            unsupported_root = temporary_root / "unsupported"
            unsupported_root.mkdir()
            (unsupported_root / check_package_contracts.PACKAGE_MANIFEST_NAME).write_text(
                json.dumps(
                    {
                        "schema": "com.asharia.project-packages",
                        "schemaVersion": 2,
                        "engine": package_test_support.engine_requirement(),
                        "directPackages": [],
                        "directFeatureSets": [],
                        "packageOptions": [],
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            not_directory = temporary_root / "not-directory"
            not_directory.write_bytes(b"file")
            locations = [
                package_candidate_discovery.LocalCandidateLocation(
                    "com.asharia.source.missing-manifest",
                    missing_manifest_root,
                ),
                package_candidate_discovery.LocalCandidateLocation(
                    "com.asharia.source.invalid-json",
                    invalid_json_root,
                ),
                package_candidate_discovery.LocalCandidateLocation(
                    "com.asharia.source.unsupported",
                    unsupported_root,
                ),
                package_candidate_discovery.LocalCandidateLocation(
                    "com.asharia.source.not-directory",
                    not_directory,
                ),
                package_candidate_discovery.LocalCandidateLocation(
                    "com.asharia.source.invalid-root-type",
                    "adapter/path",  # type: ignore[arg-type]
                ),
            ]

            result = self.load(locations)

        self.assertFalse(result.succeeded)
        self.assertEqual((), result.candidates)
        self.assertEqual(
            {
                "contract.manifest.json",
                "discovery.input.invalid",
                "discovery.manifest.missing",
                "discovery.manifest.unsupported-kind",
                "discovery.source.not-directory",
            },
            {diagnostic.code for diagnostic in result.diagnostics},
        )
        self.assertNotIn(str(temporary_root), "\n".join(
            diagnostic.render() for diagnostic in result.diagnostics
        ))

    def test_duplicate_source_key_and_physical_alias_are_distinct(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            first_root = self.write_package(
                temporary_root / "first",
                self.installable("com.asharia.system.first", "1.0.0"),
            )
            second_root = self.write_package(
                temporary_root / "second",
                self.installable("com.asharia.system.second", "1.0.0"),
            )
            duplicate = self.load(
                [
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.duplicate",
                        first_root,
                    ),
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.duplicate",
                        second_root,
                    ),
                ]
            )
            alias = self.load(
                [
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.alpha",
                        first_root,
                    ),
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.beta",
                        first_root,
                    ),
                ]
            )

        self.assertEqual("discovery.source.duplicate", duplicate.diagnostics[0].code)
        self.assertEqual("discovery.source.alias", alias.diagnostics[0].code)
        self.assertEqual((), duplicate.candidates)
        self.assertEqual((), alias.candidates)

    def test_payload_case_fold_collision_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = self.write_package(
                Path(temporary_directory) / "package",
                self.installable("com.asharia.system.case-collision", "1.0.0"),
            )
            (root / "payload-ß.bin").write_bytes(b"eszett")
            (root / "payload-ss.bin").write_bytes(b"double-s")

            result = self.load(
                [
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.case-collision",
                        root,
                    )
                ]
            )

        self.assertFalse(result.succeeded)
        self.assertEqual("discovery.payload.case-collision", result.diagnostics[0].code)
        self.assertIn("payload-", result.diagnostics[0].location)

    def test_payload_link_and_source_link_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            payload_root = self.write_package(
                Path(temporary_directory) / "payload",
                self.installable("com.asharia.system.link", "1.0.0"),
            )
            payload_link = payload_root / "linked.bin"
            payload_link.write_bytes(b"link-target-placeholder")
            original_is_symlink = Path.is_symlink

            def mark_payload_link(path: Path) -> bool:
                return path == payload_link or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", new=mark_payload_link):
                payload_result = self.load(
                    [
                        package_candidate_discovery.LocalCandidateLocation(
                            "com.asharia.source.payload-link",
                            payload_root,
                        )
                    ]
                )
            with mock.patch.object(
                package_candidate_discovery,
                "_is_link",
                side_effect=lambda path: path == payload_root,
            ):
                source_result = self.load(
                    [
                        package_candidate_discovery.LocalCandidateLocation(
                            "com.asharia.source.root-link",
                            payload_root,
                        )
                    ]
                )

        self.assertEqual("discovery.payload.link", payload_result.diagnostics[0].code)
        self.assertEqual("discovery.source.link", source_result.diagnostics[0].code)

    def test_link_detector_includes_junctions(self) -> None:
        path = mock.Mock()
        path.is_symlink.return_value = False
        path.is_junction.return_value = True

        self.assertTrue(package_candidate_discovery._is_link(path))

    def test_manifest_change_during_hashing_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = self.write_package(
                Path(temporary_directory) / "package",
                self.installable("com.asharia.system.changing", "1.0.0"),
            )
            manifest_path = root / check_package_contracts.PACKAGE_MANIFEST_NAME
            original_compute = check_package_contracts.compute_package_tree_integrity

            def compute_then_change(payload_root: Path) -> dict[str, str]:
                integrity = original_compute(payload_root)
                manifest_path.write_bytes(manifest_path.read_bytes() + b" ")
                return integrity

            with mock.patch.object(
                package_candidate_discovery.contracts,
                "compute_package_tree_integrity",
                side_effect=compute_then_change,
            ):
                result = self.load(
                    [
                        package_candidate_discovery.LocalCandidateLocation(
                            "com.asharia.source.changing",
                            root,
                        )
                    ]
                )

        self.assertFalse(result.succeeded)
        self.assertEqual("discovery.source.changed", result.diagnostics[0].code)

    def test_discovered_candidate_hands_off_to_resolver_and_integrity_validator(self) -> None:
        identity = "com.asharia.system.discovery-handoff"
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = self.write_package(
                Path(temporary_directory) / "package",
                self.installable(identity, "1.0.0"),
            )
            discovery = self.load(
                [
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.discovery-handoff",
                        root,
                    )
                ]
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
                package_test_support.make_engine_distribution(discovery.candidates),
                discovery.candidates,
                self.validators,
            )
            integrity_diagnostics = check_package_contracts.validate_locked_candidate_integrity(
                resolution.lock,
                {identity: resolution.selected_candidates[0].payload_location},
            )

        self.assertTrue(discovery.succeeded)
        self.assertTrue(resolution.succeeded)
        self.assertEqual([], integrity_diagnostics)
        self.assertEqual(
            {"kind": "local", "sourceId": "com.asharia.source.discovery-handoff"},
            resolution.lock["nodes"][0]["source"],
        )

    def test_multiple_sources_remain_candidates_for_resolver_ambiguity(self) -> None:
        identity = "com.asharia.system.ambiguous-discovery"
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            manifest = self.installable(identity, "1.0.0")
            first_root = self.write_package(temporary_root / "first", manifest)
            second_root = self.write_package(temporary_root / "second", manifest)
            discovery = self.load(
                [
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.first",
                        first_root,
                    ),
                    package_candidate_discovery.LocalCandidateLocation(
                        "com.asharia.source.second",
                        second_root,
                    ),
                ]
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
                package_test_support.make_engine_distribution(discovery.candidates),
                discovery.candidates,
                self.validators,
            )

        self.assertTrue(discovery.succeeded)
        self.assertEqual(2, len(discovery.candidates))
        self.assertFalse(resolution.succeeded)
        self.assertEqual("resolver.candidate.ambiguous", resolution.diagnostics[0].code)


if __name__ == "__main__":
    unittest.main()
