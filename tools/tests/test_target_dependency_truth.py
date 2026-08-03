from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_target_dependency_truth as truth


class TargetDependencyTruthTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name).resolve()
        self.reply = self.root / "build/.cmake/api/v1/reply"
        self.reply.mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_manifest(
        self,
        dependencies: dict[str, list[str]],
        *,
        test_targets: list[str] | None = None,
    ) -> None:
        test_targets = test_targets or []
        targets = [name for name in dependencies if name not in test_targets]
        package_root = self.root / "packages/example"
        package_root.mkdir(parents=True)
        manifest = {
            "schemaVersion": 1,
            "packageKind": "source-boundary",
            "sourceRole": "module-group",
            "ownerDomain": "foundation",
            "plannedOwnershipRoot": "com.asharia.system.test",
            "selectable": False,
            "catalogVisible": False,
            "name": "com.asharia.test",
            "version": "0.1.0",
            "displayName": "Test",
            "description": "fixture",
            "dependencies": [],
            "targets": targets,
            "testTargets": test_targets,
            "targetRoles": {
                name: "test" if name in test_targets else "runtime"
                for name in dependencies
            },
            "targetDependencies": dependencies,
        }
        (package_root / truth.MANIFEST_NAME).write_text(
            json.dumps(manifest, indent=2) + "\n",
            encoding="utf-8",
        )

    def write_reply(
        self,
        specifications: list[dict[str, object]],
        *,
        minor: int = 9,
        configuration: str = "Debug",
        source_root: Path | None = None,
    ) -> Path:
        ids = {
            specification["name"]: f"opaque::{index}"
            for index, specification in enumerate(specifications)
        }
        targets: list[dict[str, object]] = []
        abstract_targets: list[dict[str, object]] = []
        for index, specification in enumerate(specifications):
            name = str(specification["name"])
            target_id = ids[name]
            filename = f"target-{index}.json"
            abstract = bool(specification.get("abstract", False))
            summary = {
                "id": target_id,
                "name": name,
                "directoryIndex": 0,
                "projectIndex": 0,
                "jsonFile": filename,
            }
            (abstract_targets if abstract else targets).append(summary)
            target: dict[str, object] = {
                "codemodelVersion": {"major": 2, "minor": minor},
                "id": target_id,
                "name": name,
                "type": specification.get("type", "STATIC_LIBRARY"),
                "paths": {"source": "packages/example", "build": "packages/example"},
                "abstract": abstract,
            }
            if specification.get("imported"):
                target["imported"] = True
            if specification.get("generator_provided"):
                target["isGeneratorProvided"] = True
            for field in truth.DIRECT_RELATION_FIELDS:
                if field not in specification:
                    continue
                relations: list[dict[str, str]] = []
                for dependency in specification[field]:
                    if isinstance(dependency, str) and dependency.startswith("fragment:"):
                        relations.append({"fragment": dependency.removeprefix("fragment:")})
                    else:
                        relations.append({"id": ids[str(dependency)]})
                target[field] = relations
            (self.reply / filename).write_text(
                json.dumps(target, indent=2) + "\n",
                encoding="utf-8",
            )
        codemodel_filename = "codemodel-v2.json"
        codemodel = {
            "kind": "codemodel",
            "version": {"major": 2, "minor": minor},
            "paths": {
                "source": str(source_root or self.root),
                "build": str(self.root / "build"),
            },
            "configurations": [
                {
                    "name": configuration,
                    "directories": [],
                    "projects": [],
                    "targets": targets,
                    "abstractTargets": abstract_targets,
                }
            ],
        }
        (self.reply / codemodel_filename).write_text(
            json.dumps(codemodel, indent=2) + "\n",
            encoding="utf-8",
        )
        index = {
            "cmake": {
                "version": {"major": 4, "minor": 2, "patch": 0, "string": "4.2.0"}
            },
            "objects": [
                {
                    "kind": "codemodel",
                    "version": {"major": 2, "minor": minor},
                    "jsonFile": codemodel_filename,
                }
            ],
        }
        index_path = self.reply / "index-test.json"
        index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
        return index_path

    def audit(self, index: Path) -> truth.AuditResult:
        return truth.inspect_target_dependency_truth(self.root, index, "Debug")

    def test_all_six_direct_relation_families_are_reconciled(self) -> None:
        dependencies = [
            "link-dependency",
            "interface-link-dependency",
            "compile-dependency",
            "interface-compile-dependency",
            "object-dependency",
            "order-dependency",
        ]
        self.write_manifest({"owner": dependencies})
        specifications: list[dict[str, object]] = [
            {
                "name": "owner",
                "type": "EXECUTABLE",
                "linkLibraries": ["link-dependency"],
                "interfaceLinkLibraries": ["interface-link-dependency"],
                "compileDependencies": ["compile-dependency"],
                "interfaceCompileDependencies": ["interface-compile-dependency"],
                "objectDependencies": ["object-dependency"],
                "orderDependencies": ["order-dependency"],
            },
            {"name": "link-dependency", "type": "STATIC_LIBRARY"},
            {
                "name": "interface-link-dependency",
                "type": "INTERFACE_LIBRARY",
                "abstract": True,
            },
            {"name": "compile-dependency", "type": "SHARED_LIBRARY"},
            {
                "name": "interface-compile-dependency",
                "type": "INTERFACE_LIBRARY",
                "abstract": True,
            },
            {"name": "object-dependency", "type": "OBJECT_LIBRARY"},
            {"name": "order-dependency", "type": "UTILITY"},
        ]

        result = self.audit(self.write_reply(specifications))

        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        self.assertEqual(1, result.summary.compared_target_count)
        self.assertEqual(6, result.summary.actual_edge_count)

    def test_imported_generated_and_fragment_relations_are_filtered(self) -> None:
        self.write_manifest({"owner": []})
        index = self.write_reply(
            [
                {
                    "name": "owner",
                    "linkLibraries": ["external", "fragment:system.lib"],
                    "orderDependencies": ["generated"],
                },
                {
                    "name": "external",
                    "type": "INTERFACE_LIBRARY",
                    "abstract": True,
                    "imported": True,
                },
                {"name": "generated", "type": "UTILITY", "generator_provided": True},
            ]
        )

        result = self.audit(index)

        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        self.assertEqual(1, result.summary.filtered_imported_relations)
        self.assertEqual(1, result.summary.filtered_generator_relations)
        self.assertEqual(1, result.summary.non_target_fragments)

    def test_missing_test_target_fails_without_a_weakening_option(self) -> None:
        self.write_manifest(
            {"owner": [], "owner-tests": ["owner"]},
            test_targets=["owner-tests"],
        )
        result = self.audit(self.write_reply([{"name": "owner"}]))

        self.assertFalse(result.succeeded)
        self.assertEqual(1, result.summary.test_target_count)
        self.assertEqual(1, result.summary.missing_target_count)
        self.assertEqual(
            ["truth.manifest-target.missing"],
            [item.code for item in result.diagnostics],
        )
        self.assertIn("manifest test target 'owner-tests'", result.diagnostics[0].message)

    def test_codemodel_before_2_9_fails_closed(self) -> None:
        self.write_manifest({"owner": []})

        result = self.audit(self.write_reply([{"name": "owner"}], minor=8))

        self.assertFalse(result.succeeded)
        self.assertEqual(
            ["truth.cmake.codemodel-version-unsupported"],
            [item.code for item in result.diagnostics],
        )

    def test_manifest_only_and_cmake_only_edges_are_reported_stably(self) -> None:
        self.write_manifest({"owner": ["manifest-dependency"]})
        result = self.audit(
            self.write_reply(
                [
                    {"name": "owner", "linkLibraries": ["cmake-dependency"]},
                    {"name": "manifest-dependency"},
                    {"name": "cmake-dependency"},
                ]
            )
        )

        self.assertFalse(result.succeeded)
        self.assertEqual(
            ["truth.dependency.cmake-only", "truth.dependency.manifest-only"],
            [item.code for item in result.diagnostics],
        )
        self.assertEqual(
            tuple(sorted(result.diagnostics)),
            result.diagnostics,
        )

    def test_dangling_relation_id_fails_closed(self) -> None:
        self.write_manifest({"owner": []})
        index = self.write_reply([{"name": "owner"}])
        target_path = self.reply / "target-0.json"
        target = json.loads(target_path.read_text(encoding="utf-8"))
        target["orderDependencies"] = [{"id": "opaque::missing"}]
        target_path.write_text(json.dumps(target, indent=2) + "\n", encoding="utf-8")

        result = self.audit(index)

        self.assertFalse(result.succeeded)
        self.assertEqual(
            ["truth.cmake.relation-dangling"],
            [item.code for item in result.diagnostics],
        )
        self.assertEqual(0, result.summary.compared_target_count)

    def test_reply_for_another_source_root_is_rejected(self) -> None:
        self.write_manifest({"owner": []})
        index = self.write_reply(
            [{"name": "owner"}],
            source_root=self.root / "another-repository",
        )

        result = self.audit(index)

        self.assertFalse(result.succeeded)
        self.assertEqual(
            ["truth.cmake.source-root-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_wrong_codemodel_kind_and_duplicate_index_object_fail_closed(self) -> None:
        self.write_manifest({"owner": []})
        index = self.write_reply([{"name": "owner"}])
        index_data = json.loads(index.read_text(encoding="utf-8"))
        index_data["objects"].append(dict(index_data["objects"][0]))
        index.write_text(json.dumps(index_data, indent=2) + "\n", encoding="utf-8")

        duplicate_result = self.audit(index)

        self.assertIn(
            "truth.cmake.codemodel-ambiguous",
            {item.code for item in duplicate_result.diagnostics},
        )

        index_data["objects"] = index_data["objects"][:1]
        index.write_text(json.dumps(index_data, indent=2) + "\n", encoding="utf-8")
        codemodel_path = self.reply / index_data["objects"][0]["jsonFile"]
        codemodel = json.loads(codemodel_path.read_text(encoding="utf-8"))
        codemodel["kind"] = "not-codemodel"
        codemodel_path.write_text(
            json.dumps(codemodel, indent=2) + "\n", encoding="utf-8"
        )

        wrong_kind_result = self.audit(index)

        self.assertIn(
            "truth.cmake.codemodel-kind-mismatch",
            {item.code for item in wrong_kind_result.diagnostics},
        )

    def test_cli_prints_deterministic_coverage_on_success_and_failure(self) -> None:
        self.write_manifest({"owner": []})
        index = self.write_reply([{"name": "owner"}])
        stdout = io.StringIO()
        stderr = io.StringIO()
        arguments = [
            "--root",
            str(self.root),
            "--reply-index",
            str(index),
            "--configuration",
            "Debug",
        ]
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            exit_code = truth.main(arguments)
        self.assertEqual(0, exit_code)
        self.assertIn("Target dependency truth OK", stdout.getvalue())
        self.assertIn("compared-targets=1", stdout.getvalue())
        self.assertEqual("", stderr.getvalue())

        target_path = self.reply / "target-0.json"
        target = json.loads(target_path.read_text(encoding="utf-8"))
        target["orderDependencies"] = [{"id": "missing"}]
        target_path.write_text(json.dumps(target, indent=2) + "\n", encoding="utf-8")
        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            exit_code = truth.main(arguments)
        self.assertEqual(1, exit_code)
        self.assertEqual("", stdout.getvalue())
        self.assertIn("truth.cmake.relation-dangling", stderr.getvalue())
        self.assertIn("Coverage:", stderr.getvalue())

    def test_prepare_query_writes_latest_codemodel_major_2_request(self) -> None:
        build_directory = self.root / "build" / "configured-build"

        query_path = truth.prepare_file_api_query(self.root, build_directory)

        self.assertEqual(
            build_directory
            / ".cmake/api/v1/query/client-asharia-target-truth/query.json",
            query_path,
        )
        self.assertEqual(truth.FILE_API_QUERY_BYTES, query_path.read_bytes())
        self.assertEqual(
            {"requests": [{"kind": "codemodel", "version": 2}]},
            json.loads(query_path.read_text(encoding="utf-8")),
        )
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            exit_code = truth.main(
                [
                    "--root",
                    str(self.root),
                    "--prepare-query",
                    str(build_directory),
                ]
            )
        self.assertEqual(0, exit_code)
        self.assertIn(str(query_path), stdout.getvalue())

    def test_prepare_query_creates_missing_build_directory_chain(self) -> None:
        build_directory = self.root / "build" / "cmake" / "clean-configure"

        query_path = truth.prepare_file_api_query(self.root, build_directory)

        self.assertTrue(build_directory.is_dir())
        self.assertEqual(truth.FILE_API_QUERY_BYTES, query_path.read_bytes())

    def test_prepare_query_rejects_build_directory_outside_root(self) -> None:
        outside = self.root.parent / f"{self.root.name}-outside"
        with self.assertRaises(truth.QueryPreparationError):
            truth.prepare_file_api_query(self.root, outside)
        self.assertFalse(outside.exists())

    def test_prepare_query_wraps_path_inspection_errors(self) -> None:
        build_directory = self.root / "build" / "configured-build"
        original_lstat = truth.os.lstat

        def fail_for_build(path: object) -> os.stat_result:
            if Path(path) == build_directory:
                raise PermissionError("fixture denied")
            return original_lstat(path)

        with mock.patch.object(truth.os, "lstat", side_effect=fail_for_build):
            with self.assertRaisesRegex(truth.QueryPreparationError, "fixture denied"):
                truth.prepare_file_api_query(self.root, build_directory)

    def test_prepare_query_is_idempotent_without_replacing_matching_bytes(self) -> None:
        build_directory = self.root / "build" / "configured-build"
        build_directory.mkdir(parents=True)
        first = truth.prepare_file_api_query(self.root, build_directory)

        with mock.patch.object(truth.os, "replace") as replace:
            second = truth.prepare_file_api_query(self.root, build_directory)

        self.assertEqual(first, second)
        self.assertEqual(truth.FILE_API_QUERY_BYTES, second.read_bytes())
        replace.assert_not_called()

    def test_prepare_query_rejects_non_directory_build_path(self) -> None:
        regular_file = self.root / "build" / "not-a-build-directory"
        regular_file.parent.mkdir(exist_ok=True)
        regular_file.write_text("fixture\n", encoding="utf-8")
        with self.assertRaises(truth.QueryPreparationError):
            truth.prepare_file_api_query(self.root, regular_file)

    def test_prepare_query_rejects_symlink_build_path_when_supported(self) -> None:
        build_root = self.root / "build"
        build_root.mkdir(exist_ok=True)
        real_directory = build_root / "real-build-directory"
        linked_directory = build_root / "linked-build-directory"
        real_directory.mkdir()
        try:
            os.symlink(real_directory, linked_directory, target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"directory symlinks are unavailable: {exc}")
        with self.assertRaises(truth.QueryPreparationError):
            truth.prepare_file_api_query(self.root, linked_directory)

    def test_audit_mode_requires_both_reply_index_and_configuration(self) -> None:
        invalid_arguments = (
            ["--root", str(self.root)],
            ["--root", str(self.root), "--reply-index", "index.json"],
            ["--root", str(self.root), "--configuration", "Debug"],
            [
                "--root",
                str(self.root),
                "--prepare-query",
                str(self.root),
                "--configuration",
                "Debug",
            ],
        )
        for arguments in invalid_arguments:
            with self.subTest(arguments=arguments):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
                    truth.main(arguments)
                self.assertEqual(2, raised.exception.code)
                self.assertIn("error:", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
