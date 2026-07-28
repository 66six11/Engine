"""Read-only CMake File API codemodel normalization tests."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import cmake_file_api


class CMakeFileApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.toolchain = cmake_file_api.CMakeToolchainEvidence(
            compiler_id="MSVC",
            compiler_version="19.44",
            target_system="Windows",
            target_architecture="x86_64",
        )

    def write_reply(
        self,
        root: Path,
        targets: list[dict[str, object]] | None = None,
    ) -> Path:
        root.mkdir(parents=True)
        target_specs = targets or [
            {
                "name": "asharia-app",
                "type": "EXECUTABLE",
                "dependencies": ["asharia-lib"],
                "artifacts": ["apps/asharia-app.exe"],
            },
            {
                "name": "asharia-lib",
                "type": "STATIC_LIBRARY",
                "dependencies": [],
                "artifacts": ["packages/asharia-lib.lib"],
            },
        ]
        ids = {spec["name"]: f"opaque::{index}" for index, spec in enumerate(target_specs)}
        summaries: list[dict[str, object]] = []
        for index, spec in enumerate(target_specs):
            filename = f"target-{index}.json"
            target_id = ids[spec["name"]]
            summaries.append(
                {
                    "id": target_id,
                    "name": spec["name"],
                    "jsonFile": filename,
                }
            )
            target = {
                "id": target_id,
                "name": spec["name"],
                "type": spec["type"],
                "dependencies": [
                    {"id": ids[name]} for name in spec["dependencies"]
                ],
                "artifacts": [{"path": path} for path in spec["artifacts"]],
            }
            (root / filename).write_text(
                json.dumps(target, indent=2) + "\n",
                encoding="utf-8",
            )
        codemodel_filename = "codemodel-v2.json"
        (root / codemodel_filename).write_text(
            json.dumps(
                {
                    "kind": "codemodel",
                    "version": {"major": 2, "minor": 6},
                    "paths": {
                        "source": "D:/machine/source",
                        "build": "D:/machine/build",
                    },
                    "configurations": [
                        {"name": "Debug", "targets": summaries}
                    ],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        index = {
            "cmake": {
                "generator": {"name": "Ninja", "multiConfig": False},
                "paths": {"cmake": "D:/machine/cmake.exe"},
                "version": {"major": 3, "minor": 28, "patch": 0},
            },
            "reply": {
                "client-asharia-planning": {
                    "codemodel-v2": {
                        "kind": "codemodel",
                        "version": {"major": 2, "minor": 6},
                        "jsonFile": codemodel_filename,
                    }
                }
            },
        }
        index_path = root / "index-explicit.json"
        index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
        return index_path

    def read(self, index_path: Path) -> cmake_file_api.CMakeCodemodelSnapshotResult:
        return cmake_file_api.read_cmake_codemodel_snapshot(
            index_path,
            "Debug",
            self.toolchain,
            self.validators,
        )

    def test_reader_normalizes_names_dependencies_and_relative_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            index = self.write_reply(Path(temporary_directory) / "reply")

            result = self.read(index)

            self.assertTrue(
                result.succeeded,
                [item.render() for item in result.diagnostics],
            )
            assert result.snapshot is not None
            self.assertEqual(
                ["asharia-app", "asharia-lib"],
                [target.name for target in result.snapshot.targets],
            )
            self.assertEqual(
                ("asharia-lib",),
                result.snapshot.targets[0].dependencies,
            )
            self.assertEqual(
                ("apps/asharia-app.exe",),
                result.snapshot.targets[0].artifacts,
            )
            rendered = cmake_file_api.render_cmake_codemodel_snapshot(
                result.snapshot
            )
            self.assertNotIn("opaque::", rendered)
            self.assertNotIn(str(Path(temporary_directory)), rendered)
            self.assertNotIn("index-explicit", rendered)
            self.assertEqual(
                [],
                cmake_file_api.validate_cmake_codemodel_snapshot(
                    result.snapshot,
                    self.validators,
                ),
            )

    def test_reply_and_dependency_order_do_not_change_snapshot_bytes(self) -> None:
        first_targets = [
            {
                "name": "root",
                "type": "EXECUTABLE",
                "dependencies": ["z-dependency", "a-dependency"],
                "artifacts": ["z.exe", "a.pdb"],
            },
            {
                "name": "z-dependency",
                "type": "STATIC_LIBRARY",
                "dependencies": [],
                "artifacts": [],
            },
            {
                "name": "a-dependency",
                "type": "STATIC_LIBRARY",
                "dependencies": [],
                "artifacts": [],
            },
        ]
        second_targets = list(reversed(first_targets))
        second_targets[-1] = dict(second_targets[-1])
        second_targets[-1]["dependencies"] = list(
            reversed(second_targets[-1]["dependencies"])
        )
        second_targets[-1]["artifacts"] = list(
            reversed(second_targets[-1]["artifacts"])
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            first = self.read(self.write_reply(root / "first", first_targets))
            second = self.read(self.write_reply(root / "second", second_targets))

            self.assertTrue(first.succeeded)
            self.assertTrue(second.succeeded)
            assert first.snapshot is not None
            assert second.snapshot is not None
            self.assertEqual(
                cmake_file_api.render_cmake_codemodel_snapshot(first.snapshot),
                cmake_file_api.render_cmake_codemodel_snapshot(second.snapshot),
            )
            self.assertEqual(
                cmake_file_api.compute_cmake_codemodel_snapshot_integrity(
                    first.snapshot
                ),
                cmake_file_api.compute_cmake_codemodel_snapshot_integrity(
                    second.snapshot
                ),
            )

    def test_reader_rejects_dangling_dependency_and_absolute_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "reply"
            index = self.write_reply(root)
            target = json.loads((root / "target-0.json").read_text(encoding="utf-8"))
            target["dependencies"] = [{"id": "opaque::missing"}]
            target["artifacts"] = [{"path": "C:/machine/app.exe"}]
            (root / "target-0.json").write_text(
                json.dumps(target, indent=2) + "\n",
                encoding="utf-8",
            )

            result = self.read(index)

            self.assertFalse(result.succeeded)
            self.assertIsNone(result.snapshot)
            self.assertEqual(
                {
                    "build.codemodel.artifact-path-invalid",
                    "build.codemodel.dangling-dependency",
                },
                {item.code for item in result.diagnostics},
            )

    def test_reader_follows_only_safe_explicit_reply_references(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "reply"
            index = self.write_reply(root)
            data = json.loads(index.read_text(encoding="utf-8"))
            data["reply"]["client-asharia-planning"]["codemodel-v2"][
                "jsonFile"
            ] = "../other.json"
            index.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

            result = self.read(index)

            self.assertFalse(result.succeeded)
            self.assertEqual(
                ["build.codemodel.reply-reference-invalid"],
                [item.code for item in result.diagnostics],
            )

    def test_reader_fails_atomically_when_index_changes_during_read(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            index = self.write_reply(Path(temporary_directory) / "reply")
            original_read_bytes = Path.read_bytes
            calls = 0

            def unstable_read_bytes(path: Path) -> bytes:
                nonlocal calls
                content = original_read_bytes(path)
                if path == index:
                    calls += 1
                    if calls > 1:
                        return content + b" "
                return content

            with mock.patch.object(Path, "read_bytes", unstable_read_bytes):
                result = self.read(index)

            self.assertFalse(result.succeeded)
            self.assertIsNone(result.snapshot)
            self.assertEqual(
                ["build.codemodel.reply-changed"],
                [item.code for item in result.diagnostics],
            )


if __name__ == "__main__":
    unittest.main()
