"""Focused final Host CMake target binding tests."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import host_cmake_target


class HostCMakeTargetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.build_root = self.root / "build"
        self.build_root.mkdir()

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_reply(
        self,
        suffix: str,
        *,
        configuration: str = "Debug",
        target_name: str = "asharia-generated-host",
        target_type: str = "EXECUTABLE",
        name_on_disk: str = "asharia-host.exe",
        artifacts: list[str] | None = None,
        cmake_version: tuple[int, int, int] = (3, 28, 0),
        codemodel_build: str | None = None,
    ) -> tuple[Path, Path, Path]:
        reply_root = self.build_root / ".cmake/api/v1/reply"
        reply_root.mkdir(parents=True, exist_ok=True)
        target_id = f"{target_name}::{suffix}"
        target_filename = f"target-{suffix}.json"
        codemodel_filename = f"codemodel-{suffix}.json"
        toolchains_filename = f"toolchains-{suffix}.json"
        artifact_values = artifacts or [f"out/{name_on_disk}"]
        target_path = reply_root / target_filename
        target_path.write_text(
            json.dumps(
                {
                    "name": target_name,
                    "id": target_id,
                    "type": target_type,
                    "nameOnDisk": name_on_disk,
                    "artifacts": [{"path": value} for value in artifact_values],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        codemodel_path = reply_root / codemodel_filename
        codemodel_path.write_text(
            json.dumps(
                {
                    "kind": "codemodel",
                    "version": {"major": 2, "minor": 6},
                    "paths": {
                        "source": (self.root / "source").as_posix(),
                        "build": codemodel_build or self.build_root.resolve().as_posix(),
                    },
                    "configurations": [
                        {
                            "name": configuration,
                            "targets": [
                                {
                                    "name": target_name,
                                    "id": target_id,
                                    "jsonFile": target_filename,
                                }
                            ],
                        }
                    ],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        (reply_root / toolchains_filename).write_text(
            json.dumps(
                {
                    "kind": "toolchains",
                    "version": {"major": 1, "minor": 0},
                    "toolchains": [
                        {
                            "language": "CXX",
                            "compiler": {
                                "path": "C:/toolchain/cl.exe",
                                "id": "MSVC",
                                "version": "19.40.33811.0",
                                "target": "x86_64-pc-windows-msvc",
                            },
                        }
                    ],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        index_path = reply_root / f"index-{suffix}.json"
        index_path.write_text(
            json.dumps(
                {
                    "cmake": {
                        "generator": {
                            "name": "Ninja",
                            "multiConfig": False,
                        },
                        "version": {
                            "major": cmake_version[0],
                            "minor": cmake_version[1],
                            "patch": cmake_version[2],
                        }
                    },
                    "reply": {
                        host_cmake_target.HOST_BUILD_FILE_API_CLIENT: {
                            "query.json": {
                                "requests": [
                                    {
                                        "kind": "codemodel",
                                        "version": {"major": 2, "minor": 6},
                                    },
                                    {
                                        "kind": "toolchains",
                                        "version": {"major": 1, "minor": 0},
                                    }
                                ],
                                "responses": [
                                    {
                                        "kind": "codemodel",
                                        "version": {"major": 2, "minor": 6},
                                        "jsonFile": codemodel_filename,
                                    },
                                    {
                                        "kind": "toolchains",
                                        "version": {"major": 1, "minor": 0},
                                        "jsonFile": toolchains_filename,
                                    }
                                ],
                            }
                        }
                    }
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        return index_path, codemodel_path, target_path

    @staticmethod
    def codes(result: host_cmake_target.HostCMakeTargetResult) -> list[str]:
        return [diagnostic.code for diagnostic in result.diagnostics]

    def read(
        self,
        *,
        configuration: str = "Debug",
        target_name: str = "asharia-generated-host",
        require_artifact: bool = False,
    ) -> host_cmake_target.HostCMakeTargetResult:
        return host_cmake_target.read_host_cmake_target(
            self.build_root,
            configuration,
            target_name,
            require_artifact=require_artifact,
        )

    def create_junction(self, link: Path, target: Path) -> None:
        completed = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(target)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            shell=False,
        )
        if completed.returncode != 0:
            self.skipTest("Windows junction creation is unavailable")

    def test_writes_exact_stateful_host_query(self) -> None:
        result = host_cmake_target.write_host_cmake_file_api_query(self.build_root)

        self.assertTrue(result.succeeded)
        assert result.evidence is not None
        self.assertEqual(
            self.build_root.resolve(),
            result.evidence.build_root,
        )
        self.assertEqual(2, result.evidence.codemodel_major)
        self.assertEqual(6, result.evidence.codemodel_minor)
        self.assertEqual(1, result.evidence.toolchains_major)
        self.assertEqual(0, result.evidence.toolchains_minor)
        self.assertEqual(
            {
                "requests": [
                    {
                        "kind": "codemodel",
                        "version": {"major": 2, "minor": 6},
                    },
                    {
                        "kind": "toolchains",
                        "version": {"major": 1, "minor": 0},
                    }
                ]
            },
            json.loads(result.evidence.query_path.read_text(encoding="utf-8")),
        )
        self.assertFalse(result.evidence.query_path.read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertEqual([], list(result.evidence.query_path.parent.glob(".query-*")))

    def test_uses_lexicographically_latest_reply_index(self) -> None:
        self.write_reply("001", target_name="stale-host")
        latest, _, _ = self.write_reply("002")

        result = self.read()

        self.assertTrue(result.succeeded, self.codes(result))
        assert result.evidence is not None
        self.assertEqual(latest, result.evidence.reply_index_path)
        self.assertEqual("out/asharia-host.exe", result.evidence.artifact_relative_path)
        self.assertEqual("asharia-host.exe", result.evidence.name_on_disk)

    def test_wrong_configuration_target_and_type_fail_closed(self) -> None:
        cases = (
            (
                {"configuration": "Release"},
                {},
                "host-build.cmake-configuration-mismatch",
            ),
            (
                {"target_name": "other-host"},
                {},
                "host-build.cmake-target-mismatch",
            ),
            (
                {"target_type": "STATIC_LIBRARY"},
                {},
                "host-build.cmake-target-type-mismatch",
            ),
        )
        for index, (reply_options, read_options, expected_code) in enumerate(cases):
            with self.subTest(expected_code=expected_code):
                case_root = self.root / f"case-{index}"
                case_root.mkdir()
                previous_root = self.build_root
                self.build_root = case_root
                try:
                    self.write_reply("001", **reply_options)
                    result = self.read(**read_options)
                finally:
                    self.build_root = previous_root
                self.assertFalse(result.succeeded)
                self.assertEqual([expected_code], self.codes(result))

    def test_cmake_version_and_codemodel_build_root_are_exact(self) -> None:
        cases = (
            (
                {"cmake_version": (3, 27, 9)},
                "host-build.cmake-version-unsupported",
            ),
            (
                {"codemodel_build": "relative/build"},
                "host-build.cmake-build-root-mismatch",
            ),
            (
                {"codemodel_build": (self.root / "other-build").as_posix()},
                "host-build.cmake-build-root-mismatch",
            ),
        )
        (self.root / "other-build").mkdir()
        for index, (reply_options, expected_code) in enumerate(cases):
            with self.subTest(expected_code=expected_code):
                case_root = self.root / f"root-case-{index}"
                case_root.mkdir()
                previous_root = self.build_root
                self.build_root = case_root
                try:
                    self.write_reply("001", **reply_options)
                    result = self.read()
                finally:
                    self.build_root = previous_root
                self.assertEqual([expected_code], self.codes(result))

    def test_primary_artifact_must_be_unique_and_inside_build_root(self) -> None:
        self.write_reply(
            "001",
            artifacts=["first/asharia-host.exe", "second/asharia-host.exe"],
        )
        ambiguous = self.read()
        self.assertEqual(
            ["host-build.cmake-primary-artifact-mismatch"],
            self.codes(ambiguous),
        )

        outside_root = self.root / "outside"
        outside_root.mkdir()
        outside_artifact = outside_root / "asharia-host.exe"
        outside_artifact.write_bytes(b"host")
        self.write_reply("002", artifacts=[outside_artifact.as_posix()])
        outside = self.read(require_artifact=True)
        self.assertEqual(
            ["host-build.cmake-artifact-path-invalid"],
            self.codes(outside),
        )

    def test_artifact_requirement_is_deferred_until_after_build(self) -> None:
        self.write_reply("001")

        configured = self.read(require_artifact=False)
        missing = self.read(require_artifact=True)

        self.assertTrue(configured.succeeded, self.codes(configured))
        self.assertEqual(
            ["host-build.cmake-artifact-missing"],
            self.codes(missing),
        )

        artifact = self.build_root / "out/asharia-host.exe"
        artifact.parent.mkdir()
        artifact.write_bytes(b"host")
        built = self.read(require_artifact=True)
        self.assertTrue(built.succeeded, self.codes(built))
        assert built.evidence is not None
        self.assertEqual(artifact.resolve(), built.evidence.artifact_path)

    def test_non_regular_artifact_is_rejected(self) -> None:
        self.write_reply("001")
        artifact = self.build_root / "out/asharia-host.exe"
        artifact.mkdir(parents=True)

        result = self.read(require_artifact=True)

        self.assertEqual(
            ["host-build.cmake-artifact-not-regular"],
            self.codes(result),
        )

    @unittest.skipUnless(os.name == "nt", "junction regression is Windows-only")
    def test_artifact_path_rejects_intermediate_junction(self) -> None:
        self.write_reply("001", artifacts=["out/asharia-host.exe"])
        real_output = self.build_root / "real-output"
        real_output.mkdir()
        (real_output / "asharia-host.exe").write_bytes(b"host")
        self.create_junction(self.build_root / "out", real_output)

        result = self.read(require_artifact=True)

        self.assertEqual(
            ["host-build.cmake-artifact-not-regular"],
            self.codes(result),
        )

    @unittest.skipUnless(os.name == "nt", "junction regression is Windows-only")
    def test_reply_root_rejects_intermediate_junction(self) -> None:
        self.write_reply("001")
        reply_root = self.build_root / ".cmake/api/v1/reply"
        real_reply_root = reply_root.with_name("real-reply")
        reply_root.rename(real_reply_root)
        self.create_junction(reply_root, real_reply_root)

        result = self.read()

        self.assertEqual(["host-build.cmake-index-missing"], self.codes(result))

    def test_index_drift_and_disappearing_reference_are_retried(self) -> None:
        index_path, codemodel_path, _ = self.write_reply("001")
        original_read_bytes = Path.read_bytes
        index_reads = 0
        codemodel_reads = 0

        def transient_read(path: Path) -> bytes:
            nonlocal index_reads, codemodel_reads
            if path == index_path:
                index_reads += 1
                content = original_read_bytes(path)
                if index_reads == 2:
                    return content + b" "
                return content
            if path == codemodel_path:
                codemodel_reads += 1
                if codemodel_reads == 1:
                    raise FileNotFoundError
            return original_read_bytes(path)

        with mock.patch.object(Path, "read_bytes", transient_read):
            result = self.read()

        self.assertTrue(result.succeeded, self.codes(result))
        self.assertGreaterEqual(index_reads, 3)
        self.assertGreaterEqual(codemodel_reads, 2)

    def test_persistent_index_drift_fails_after_bounded_retries(self) -> None:
        index_path, _, _ = self.write_reply("001")
        original_read_bytes = Path.read_bytes
        index_reads = 0

        def unstable_read(path: Path) -> bytes:
            nonlocal index_reads
            content = original_read_bytes(path)
            if path == index_path:
                index_reads += 1
                if index_reads % 2 == 0:
                    return content + b" "
            return content

        with mock.patch.object(Path, "read_bytes", unstable_read):
            result = self.read()

        self.assertEqual(
            ["host-build.cmake-reply-unstable"],
            self.codes(result),
        )
        self.assertEqual(
            host_cmake_target.HOST_BUILD_FILE_API_READ_ATTEMPTS * 2,
            index_reads,
        )

    def test_missing_referenced_file_eventually_fails_closed(self) -> None:
        _, codemodel_path, _ = self.write_reply("001")
        codemodel_path.unlink()

        result = self.read()

        self.assertEqual(
            ["host-build.cmake-reply-unstable"],
            self.codes(result),
        )


if __name__ == "__main__":
    unittest.main()
