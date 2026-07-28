"""Project/local package source catalog tests for #302."""

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
from tools import engine_distribution_package_catalog as distribution_catalog
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import package_candidate_discovery as discovery
from tools import package_lock_verification
from tools import package_resolver
from tools import project_package_source_catalog as source_catalog
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


class ProjectPackageSourceCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
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
        root: Path,
        manifest: dict[str, object],
    ) -> Path:
        root.mkdir(parents=True)
        (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (root / "payload.bin").write_bytes(
            f"payload:{manifest['id']}".encode("utf-8")
        )
        return root

    def source_index(
        self,
        sources: list[dict[str, str]],
    ) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-package-sources",
            "schemaVersion": 1,
            "sources": sources,
        }

    def write_index(
        self,
        project_root: Path,
        sources: list[dict[str, str]],
    ) -> Path:
        project_root.mkdir(parents=True, exist_ok=True)
        path = project_root / contracts.PROJECT_PACKAGE_SOURCES_NAME
        path.write_text(
            json.dumps(
                self.source_index(sources),
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        return path

    def derive(
        self,
        project_root: Path,
        local_sources: object = (),
    ) -> source_catalog.ProjectPackageSourceCatalogResult:
        return source_catalog.derive_project_package_source_catalog(
            project_root.resolve(),
            local_sources,
            self.validators,
        )

    def assert_atomic_failure(
        self,
        result: source_catalog.ProjectPackageSourceCatalogResult,
    ) -> None:
        self.assertFalse(result.succeeded)
        self.assertIsNone(result.snapshot)
        self.assertTrue(result.diagnostics)

    def test_explicit_embedded_and_selected_local_sources_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            project_root = root / "project"
            embedded_root = self.write_package(
                project_root / "Packages/audio",
                self.installable("com.asharia.system.audio"),
            )
            local_root = self.write_package(
                root / "workspace/physics",
                self.installable("com.asharia.system.physics"),
            )
            extra_root = root / "workspace/extra-does-not-exist"
            embedded = {
                "kind": "project-embedded",
                "relativePath": "Packages/audio",
            }
            local = {
                "kind": "local",
                "sourceId": "com.asharia.source.physics-workspace",
            }
            self.write_index(project_root, [embedded, local])
            mappings = [
                discovery.LocalCandidateLocation(
                    "com.asharia.source.unselected",
                    extra_root,
                ),
                discovery.LocalCandidateLocation(
                    "com.asharia.source.physics-workspace",
                    local_root,
                ),
                object(),
            ]

            first = self.derive(project_root, mappings)
            self.write_index(project_root, [local, embedded])
            second = self.derive(project_root, list(reversed(mappings)))

            self.assertTrue(
                first.succeeded,
                [diagnostic.render() for diagnostic in first.diagnostics],
            )
            self.assertTrue(second.succeeded)
            assert first.snapshot is not None
            assert second.snapshot is not None
            self.assertEqual(first.snapshot, second.snapshot)
            self.assertEqual(
                [
                    "local:com.asharia.source.physics-workspace",
                    "project-embedded:Packages/audio",
                ],
                [entry.source_key for entry in first.snapshot.entries],
            )
            self.assertEqual(
                [local_root.resolve(), embedded_root.resolve()],
                [candidate.payload_location for candidate in first.snapshot.candidates],
            )
            self.assertEqual(
                [
                    {
                        "kind": "local",
                        "sourceId": "com.asharia.source.physics-workspace",
                    },
                    {
                        "kind": "project-embedded",
                        "relativePath": "Packages/audio",
                    },
                ],
                first.snapshot.source_index["sources"],
            )

            captured_index = first.snapshot.source_index
            captured_candidates = first.snapshot.candidates
            captured_index["sources"].clear()
            captured_candidates[0].manifest["id"] = "com.asharia.mutated"
            self.assertEqual(2, len(first.snapshot.source_index["sources"]))
            self.assertEqual(
                "com.asharia.system.physics",
                first.snapshot.candidates[0].identity,
            )
            self.assertNotEqual(
                first.snapshot,
                replace(first.snapshot, _source_index_bytes=b"{}\n"),
            )

    def test_empty_source_index_is_a_successful_empty_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            self.write_index(project_root, [])

            result = self.derive(project_root, object())

        self.assertTrue(result.succeeded)
        assert result.snapshot is not None
        self.assertEqual((), result.snapshot.entries)
        self.assertEqual((), result.snapshot.candidates)

    def test_only_selected_local_mappings_are_validated_or_read(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            project_root = root / "project"
            selected_root = self.write_package(
                root / "selected",
                self.installable("com.asharia.system.selected"),
            )
            selected_id = "com.asharia.source.selected"
            self.write_index(
                project_root,
                [{"kind": "local", "sourceId": selected_id}],
            )
            valid = discovery.LocalCandidateLocation(selected_id, selected_root)
            extra_duplicate = discovery.LocalCandidateLocation(
                "com.asharia.source.extra",
                root / "missing-extra",
            )

            result = self.derive(
                project_root,
                [extra_duplicate, extra_duplicate, object(), valid],
            )

        self.assertTrue(
            result.succeeded,
            [diagnostic.render() for diagnostic in result.diagnostics],
        )

    def test_missing_and_duplicate_selected_local_mappings_fail_atomically(self) -> None:
        source_id = "com.asharia.source.selected"
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            project_root = root / "project"
            local_root = self.write_package(
                root / "workspace",
                self.installable("com.asharia.system.local"),
            )
            self.write_index(
                project_root,
                [{"kind": "local", "sourceId": source_id}],
            )
            missing = self.derive(project_root)
            duplicate = self.derive(
                project_root,
                [
                    discovery.LocalCandidateLocation(source_id, local_root),
                    discovery.LocalCandidateLocation(source_id, local_root),
                ],
            )

        self.assert_atomic_failure(missing)
        self.assert_atomic_failure(duplicate)
        self.assertEqual(
            ["project-sources.local-mapping.missing"],
            [diagnostic.code for diagnostic in missing.diagnostics],
        )
        self.assertEqual(
            ["project-sources.local-mapping.duplicate"],
            [diagnostic.code for diagnostic in duplicate.diagnostics],
        )

    def test_selected_local_mapping_root_must_be_absolute(self) -> None:
        source_id = "com.asharia.source.selected"
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            self.write_index(
                project_root,
                [{"kind": "local", "sourceId": source_id}],
            )

            result = self.derive(
                project_root,
                [
                    discovery.LocalCandidateLocation(
                        source_id,
                        Path("relative-workspace"),
                    )
                ],
            )

        self.assert_atomic_failure(result)
        self.assertEqual(
            ["project-sources.local-mapping.invalid"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_invalid_index_contracts_fail_before_candidate_loading(self) -> None:
        mutations = {
            "absolute": [
                {"kind": "project-embedded", "relativePath": "C:/packages/audio"}
            ],
            "escape": [
                {"kind": "project-embedded", "relativePath": "../packages/audio"}
            ],
            "duplicate": [
                {"kind": "local", "sourceId": "com.asharia.source.same"},
                {"kind": "local", "sourceId": "com.asharia.source.same"},
            ],
            "casefold": [
                {"kind": "project-embedded", "relativePath": "Packages/Audio"},
                {"kind": "project-embedded", "relativePath": "packages/audio"},
            ],
        }
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            for label, sources in mutations.items():
                with self.subTest(label=label):
                    self.write_index(project_root, sources)
                    with mock.patch.object(
                        source_catalog.discovery,
                        "load_package_candidates",
                        side_effect=AssertionError("invalid index reached strict loader"),
                    ):
                        result = self.derive(project_root)
                    self.assert_atomic_failure(result)

    def test_index_must_be_a_utf8_object_in_a_nonlink_regular_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            index_path = project_root / contracts.PROJECT_PACKAGE_SOURCES_NAME

            index_path.write_bytes(b"\xef\xbb\xbf{}\n")
            bom = self.derive(project_root)

            index_path.write_text("[]\n", encoding="utf-8", newline="\n")
            non_object = self.derive(project_root)

            self.write_index(project_root, [])

            def index_is_link(path: Path) -> bool:
                return path.name == contracts.PROJECT_PACKAGE_SOURCES_NAME

            with mock.patch.object(
                source_catalog,
                "_is_link",
                side_effect=index_is_link,
            ):
                linked = self.derive(project_root)

        self.assert_atomic_failure(bom)
        self.assertEqual("contract.manifest.json", bom.diagnostics[0].code)
        self.assert_atomic_failure(non_object)
        self.assertEqual("contract.manifest.document", non_object.diagnostics[0].code)
        self.assert_atomic_failure(linked)
        self.assertEqual("project-sources.index.unavailable", linked.diagnostics[0].code)

    def test_physical_alias_and_unavailable_source_fail_without_path_leak(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            project_root = root / "project"
            package_root = self.write_package(
                project_root / "Packages/shared",
                self.installable("com.asharia.system.shared"),
            )
            self.write_index(
                project_root,
                [
                    {
                        "kind": "project-embedded",
                        "relativePath": "Packages/shared",
                    },
                    {
                        "kind": "local",
                        "sourceId": "com.asharia.source.shared",
                    },
                ],
            )
            aliased = self.derive(
                project_root,
                [
                    discovery.LocalCandidateLocation(
                        "com.asharia.source.shared",
                        package_root,
                    )
                ],
            )

            self.write_index(
                project_root,
                [
                    {
                        "kind": "local",
                        "sourceId": "com.asharia.source.missing",
                    }
                ],
            )
            unavailable = self.derive(
                project_root,
                [
                    discovery.LocalCandidateLocation(
                        "com.asharia.source.missing",
                        root / "absolute-secret" / "missing",
                    )
                ],
            )

        self.assert_atomic_failure(aliased)
        self.assertIn(
            "discovery.source.alias",
            {diagnostic.code for diagnostic in aliased.diagnostics},
        )
        self.assert_atomic_failure(unavailable)
        rendered = "\n".join(
            diagnostic.render() for diagnostic in unavailable.diagnostics
        )
        self.assertNotIn(str(root), rendered)
        self.assertNotIn("absolute-secret", rendered)

    def test_physically_nested_embedded_and_local_roots_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            outer_root = self.write_package(
                project_root / "Packages/outer",
                self.installable("com.asharia.system.outer"),
            )
            inner_root = self.write_package(
                outer_root / "vendor/inner",
                self.installable("com.asharia.system.inner"),
            )
            source_id = "com.asharia.source.nested-inner"
            self.write_index(
                project_root,
                [
                    {
                        "kind": "project-embedded",
                        "relativePath": "Packages/outer",
                    },
                    {"kind": "local", "sourceId": source_id},
                ],
            )

            result = self.derive(
                project_root,
                [discovery.LocalCandidateLocation(source_id, inner_root.resolve())],
            )

        self.assert_atomic_failure(result)
        self.assertEqual(
            ["project-sources.candidate.overlapping-root"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_index_mutation_during_candidate_collection_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            self.write_index(project_root, [])
            index_path = project_root / contracts.PROJECT_PACKAGE_SOURCES_NAME
            original_loader = discovery.load_package_candidates

            def mutate_after_load(
                locations: object,
                validators: contracts.ContractValidators,
            ) -> discovery.CandidateDiscoveryResult:
                result = original_loader(locations, validators)
                index_path.write_bytes(index_path.read_bytes() + b" ")
                return result

            with mock.patch.object(
                source_catalog.discovery,
                "load_package_candidates",
                side_effect=mutate_after_load,
            ):
                result = self.derive(project_root)

        self.assert_atomic_failure(result)
        self.assertEqual(
            ["project-sources.index.changed"],
            [diagnostic.code for diagnostic in result.diagnostics],
        )

    def test_root_replacement_after_strict_loading_is_rejected(self) -> None:
        for source_kind in ("project-embedded", "local"):
            with (
                self.subTest(source_kind=source_kind),
                tempfile.TemporaryDirectory() as temporary_directory,
            ):
                root = Path(temporary_directory)
                project_root = root / "project"
                if source_kind == "project-embedded":
                    package_root = project_root / "Packages/replaceable"
                    sources = [
                        {
                            "kind": "project-embedded",
                            "relativePath": "Packages/replaceable",
                        }
                    ]
                    mappings: list[object] = []
                else:
                    package_root = root / "workspace/replaceable"
                    source_id = "com.asharia.source.replaceable"
                    sources = [{"kind": "local", "sourceId": source_id}]
                    mappings = [
                        discovery.LocalCandidateLocation(
                            source_id,
                            package_root.resolve(),
                        )
                    ]
                self.write_package(
                    package_root,
                    self.installable("com.asharia.system.before-replacement"),
                )
                self.write_index(project_root, sources)
                moved_root = package_root.with_name(f"{package_root.name}-old")
                original_loader = discovery.load_package_candidates

                def replace_after_load(
                    locations: object,
                    validators: contracts.ContractValidators,
                ) -> discovery.CandidateDiscoveryResult:
                    loaded = original_loader(locations, validators)
                    package_root.rename(moved_root)
                    self.write_package(
                        package_root,
                        self.installable("com.asharia.system.after-replacement"),
                    )
                    return loaded

                with mock.patch.object(
                    source_catalog.discovery,
                    "load_package_candidates",
                    side_effect=replace_after_load,
                ):
                    result = self.derive(project_root, mappings)

            self.assert_atomic_failure(result)
            self.assertEqual(
                ["project-sources.candidate.root-changed"],
                [diagnostic.code for diagnostic in result.diagnostics],
            )

    def test_discovery_diagnostic_absolute_paths_are_redacted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            project_root = root / "project"
            local_root = self.write_package(
                root / "private-workspace",
                self.installable("com.asharia.system.private"),
            )
            source_id = "com.asharia.source.private"
            self.write_index(
                project_root,
                [{"kind": "local", "sourceId": source_id}],
            )
            injected = discovery.CandidateDiscoveryDiagnostic(
                code="discovery.source.changed",
                source_key=f"local:{local_root}",
                location=str(local_root / "secret.bin"),
                message=f"source changed at {local_root}",
            )
            with mock.patch.object(
                source_catalog.discovery,
                "load_package_candidates",
                return_value=discovery.CandidateDiscoveryResult(
                    candidates=(),
                    diagnostics=(injected,),
                ),
            ):
                result = self.derive(
                    project_root,
                    [discovery.LocalCandidateLocation(source_id, local_root)],
                )

        self.assert_atomic_failure(result)
        rendered = result.diagnostics[0].render()
        self.assertNotIn(str(root), rendered)
        self.assertNotIn("secret.bin", rendered)

    def test_candidate_cardinality_and_source_binding_are_defensive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project_root = Path(temporary_directory)
            package_root = self.write_package(
                project_root / "Packages/audio",
                self.installable("com.asharia.system.audio"),
            )
            self.write_index(
                project_root,
                [
                    {
                        "kind": "project-embedded",
                        "relativePath": "Packages/audio",
                    }
                ],
            )
            loaded = discovery.load_package_candidates(
                [
                    discovery.ProjectEmbeddedCandidateLocation(
                        project_root.resolve(),
                        "Packages/audio",
                    )
                ],
                self.validators,
            )
            self.assertTrue(loaded.succeeded)
            candidate = loaded.candidates[0]
            cases = {
                "missing": (
                    (),
                    "project-sources.candidate.missing",
                ),
                "duplicate": (
                    (candidate, candidate),
                    "project-sources.candidate.duplicate",
                ),
                "source": (
                    (
                        replace(
                            candidate,
                            source={
                                "kind": "project-embedded",
                                "relativePath": "Packages/other",
                            },
                        ),
                    ),
                    "project-sources.candidate.source-mismatch",
                ),
                "root": (
                    (replace(candidate, payload_location=package_root.parent),),
                    "project-sources.candidate.root-mismatch",
                ),
            }
            for label, (candidates, expected_code) in cases.items():
                with self.subTest(label=label), mock.patch.object(
                    source_catalog.discovery,
                    "load_package_candidates",
                    return_value=discovery.CandidateDiscoveryResult(
                        candidates=candidates,
                        diagnostics=(),
                    ),
                ):
                    result = self.derive(project_root)
                self.assert_atomic_failure(result)
                self.assertIn(
                    expected_code,
                    {diagnostic.code for diagnostic in result.diagnostics},
                )

    def test_resolver_keeps_ambiguity_and_distribution_shadow_fail_closed(self) -> None:
        identity = "com.asharia.system.shared"
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            project_root = root / "project"
            self.write_package(
                project_root / "Packages/shared",
                self.installable(identity),
            )
            local_root = self.write_package(
                root / "workspace/shared",
                self.installable(identity),
            )
            source_id = "com.asharia.source.shared-workspace"
            self.write_index(
                project_root,
                [
                    {
                        "kind": "project-embedded",
                        "relativePath": "Packages/shared",
                    },
                    {"kind": "local", "sourceId": source_id},
                ],
            )
            sources = self.derive(
                project_root,
                [discovery.LocalCandidateLocation(source_id, local_root.resolve())],
            )

        self.assertTrue(
            sources.succeeded,
            [diagnostic.render() for diagnostic in sources.diagnostics],
        )
        assert sources.snapshot is not None
        project_manifest = {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [{"id": identity, "version": exact("1.0.0")}],
            "directFeatureSets": [],
            "packageOptions": [],
        }
        project_candidates = sources.snapshot.candidates
        ambiguous = package_resolver.resolve_package_graph(
            project_manifest,
            package_test_support.make_engine_distribution(),
            project_candidates,
            self.validators,
        )

        distributed = replace(
            project_candidates[0],
            origin="engine-distribution:packages/shared",
            source={"kind": "engine-distribution"},
        )
        distribution_manifest = package_test_support.make_engine_distribution(
            [distributed]
        )
        shadowed = package_resolver.resolve_package_graph(
            project_manifest,
            distribution_manifest,
            (distributed, *project_candidates),
            self.validators,
        )

        self.assertFalse(ambiguous.succeeded)
        self.assertIn(
            "resolver.candidate.ambiguous",
            {diagnostic.code for diagnostic in ambiguous.diagnostics},
        )
        self.assertFalse(shadowed.succeeded)
        self.assertIn(
            "resolver.engine.distribution-shadowed",
            {diagnostic.code for diagnostic in shadowed.diagnostics},
        )

    def test_distribution_and_project_sources_resolve_and_reuse_one_lock(self) -> None:
        distributed_id = "com.asharia.system.distributed"
        project_id = "com.asharia.system.project-owned"
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            staging_root = root / "distribution-staging"
            distributed_manifest = self.installable(distributed_id)
            distributed_root = self.write_package(
                staging_root / "packages/distributed",
                distributed_manifest,
            )
            inventory = {
                "id": distributed_id,
                "version": "1.0.0",
                "packageKind": "installable-capability",
                "availability": "required",
                "root": "packages/distributed",
                "manifestIntegrity": contracts.compute_manifest_file_integrity(
                    distributed_root / contracts.PACKAGE_MANIFEST_NAME
                ),
                "payloadIntegrity": contracts.compute_package_tree_integrity(
                    distributed_root
                ),
            }
            distribution_manifest = package_test_support.make_engine_distribution()
            distribution_manifest["bundledPackages"] = [inventory]
            distribution_manifest["engineGenerationId"] = (
                contracts.compute_engine_generation_id(distribution_manifest)
            )
            distribution_bytes = (
                contracts.render_normalized_engine_distribution_manifest(
                    distribution_manifest
                ).encode("utf-8")
            )
            captured_distribution = json.loads(distribution_bytes.decode("utf-8"))
            generation_root = root / captured_distribution["engineGenerationId"]
            shutil.copytree(staging_root / "packages", generation_root / "packages")
            verified = distribution_verifier.VerifiedInstalledDistribution(
                engine_generation_id=captured_distribution["engineGenerationId"],
                generation_root=generation_root.resolve(),
                manifest=captured_distribution,
                manifest_bytes=distribution_bytes,
                manifest_integrity=contracts.compute_bytes_integrity(
                    distribution_bytes
                ),
            )
            distributed = distribution_catalog.derive_engine_distribution_package_catalog(
                verified,
                self.validators,
            )
            self.assertTrue(
                distributed.succeeded,
                [diagnostic.render() for diagnostic in distributed.diagnostics],
            )
            assert distributed.snapshot is not None

            project_root = root / "project"
            self.write_package(
                project_root / "Packages/project-owned",
                self.installable(
                    project_id,
                    dependencies=[{"id": distributed_id, "version": exact("1.0.0")}],
                ),
            )
            self.write_index(
                project_root,
                [
                    {
                        "kind": "project-embedded",
                        "relativePath": "Packages/project-owned",
                    }
                ],
            )
            project_sources = self.derive(project_root)
            self.assertTrue(
                project_sources.succeeded,
                [diagnostic.render() for diagnostic in project_sources.diagnostics],
            )
            assert project_sources.snapshot is not None

            project_manifest = {
                "schema": "com.asharia.project-packages",
                "schemaVersion": 2,
                "engine": package_test_support.engine_requirement(),
                "directPackages": [{"id": project_id, "version": exact("1.0.0")}],
                "directFeatureSets": [],
                "packageOptions": [],
            }
            all_candidates = (
                distributed.snapshot.candidates
                + project_sources.snapshot.candidates
            )
            resolution = package_resolver.resolve_package_graph(
                project_manifest,
                distributed.snapshot.distribution_manifest,
                all_candidates,
                self.validators,
            )
            self.assertTrue(
                resolution.succeeded,
                [diagnostic.render() for diagnostic in resolution.diagnostics],
            )
            verification = package_lock_verification.verify_locked_package_graph(
                project_manifest,
                distributed.snapshot.distribution_manifest,
                resolution.lock,
                all_candidates,
                self.validators,
            )

        self.assertTrue(
            verification.succeeded,
            [diagnostic.render() for diagnostic in verification.diagnostics],
        )
        self.assertEqual(
            [distributed_id, project_id],
            [node["id"] for node in resolution.lock["nodes"]],
        )
        self.assertEqual(resolution.lock, verification.lock)


if __name__ == "__main__":
    unittest.main()
