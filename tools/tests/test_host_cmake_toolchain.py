"""Focused same-index Host CMake configured-toolchain tests."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import host_cmake_toolchain


class HostCMakeToolchainTests(unittest.TestCase):
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
        toolchains: list[dict[str, object]] | None = None,
        generator: dict[str, object] | None = None,
        toolchains_minor: int = 0,
    ) -> dict[str, Path]:
        reply_root = self.build_root / ".cmake/api/v1/reply"
        reply_root.mkdir(parents=True, exist_ok=True)
        target_name = "asharia-generated-host"
        target_id = f"{target_name}::{suffix}"
        name_on_disk = f"asharia-host-{suffix}.exe"
        target_name_json = f"target-{suffix}.json"
        codemodel_name_json = f"codemodel-{suffix}.json"
        toolchains_name_json = f"toolchains-{suffix}.json"
        target_path = reply_root / target_name_json
        target_path.write_text(
            json.dumps(
                {
                    "name": target_name,
                    "id": target_id,
                    "type": "EXECUTABLE",
                    "nameOnDisk": name_on_disk,
                    "artifacts": [{"path": f"out/{name_on_disk}"}],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        codemodel_path = reply_root / codemodel_name_json
        codemodel_path.write_text(
            json.dumps(
                {
                    "kind": "codemodel",
                    "version": {"major": 2, "minor": 6},
                    "paths": {
                        "source": (self.root / "source").as_posix(),
                        "build": self.build_root.resolve().as_posix(),
                    },
                    "configurations": [
                        {
                            "name": "Debug",
                            "targets": [
                                {
                                    "name": target_name,
                                    "id": target_id,
                                    "jsonFile": target_name_json,
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
        if toolchains is None:
            toolchains = [
                {
                    "language": "CXX",
                    "compiler": {
                        "path": "C:/private/toolchain/cl.exe",
                        "id": "MSVC",
                        "version": "19.40.33811.0",
                        "target": "x86_64-pc-windows-msvc",
                    },
                }
            ]
        toolchains_path = reply_root / toolchains_name_json
        toolchains_path.write_text(
            json.dumps(
                {
                    "kind": "toolchains",
                    "version": {"major": 1, "minor": toolchains_minor},
                    "toolchains": toolchains,
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
                        "generator": generator
                        or {"name": "Ninja", "multiConfig": False},
                        "version": {"major": 3, "minor": 28, "patch": 0},
                    },
                    "reply": {
                        "client-asharia-host-build-v1": {
                            "query.json": {
                                "requests": [
                                    {
                                        "kind": "codemodel",
                                        "version": {"major": 2, "minor": 6},
                                    },
                                    {
                                        "kind": "toolchains",
                                        "version": {"major": 1, "minor": 0},
                                    },
                                ],
                                "responses": [
                                    {
                                        "kind": "codemodel",
                                        "version": {"major": 2, "minor": 6},
                                        "jsonFile": codemodel_name_json,
                                    },
                                    {
                                        "kind": "toolchains",
                                        "version": {
                                            "major": 1,
                                            "minor": toolchains_minor,
                                        },
                                        "jsonFile": toolchains_name_json,
                                    },
                                ],
                            }
                        }
                    },
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        return {
            "index": index_path,
            "codemodel": codemodel_path,
            "target": target_path,
            "toolchains": toolchains_path,
        }

    @staticmethod
    def codes(
        result: host_cmake_toolchain.HostCMakeConfiguredTargetResult,
    ) -> list[str]:
        return [diagnostic.code for diagnostic in result.diagnostics]

    def read(self) -> host_cmake_toolchain.HostCMakeConfiguredTargetResult:
        return host_cmake_toolchain.read_host_cmake_configured_target(
            self.build_root,
            "Debug",
            "asharia-generated-host",
            require_artifact=False,
        )

    def test_reads_generator_target_and_cxx_from_one_index(self) -> None:
        paths = self.write_reply("001")

        result = self.read()

        self.assertTrue(result.succeeded, self.codes(result))
        assert result.evidence is not None
        evidence = result.evidence
        self.assertEqual(paths["index"], evidence.reply_index_path)
        self.assertEqual(evidence.reply_index_path, evidence.target.reply_index_path)
        self.assertEqual(
            host_cmake_toolchain.HostCMakeGeneratorEvidence("Ninja", False),
            evidence.generator,
        )
        self.assertEqual(
            host_cmake_toolchain.ConfiguredCxxCompilerEvidence(
                "CXX",
                "MSVC",
                "19.40.33811.0",
                "x86_64-pc-windows-msvc",
            ),
            evidence.configured_compiler,
        )
        self.assertNotIn("C:/private/toolchain/cl.exe", repr(evidence))
        self.assertFalse(hasattr(evidence.configured_compiler, "path"))

    def test_compiler_target_is_optional_and_newer_minor_is_accepted(self) -> None:
        self.write_reply(
            "001",
            toolchains=[
                {
                    "language": "CXX",
                    "compiler": {"id": "Clang", "version": "18.1.8"},
                }
            ],
            toolchains_minor=1,
        )

        result = self.read()

        self.assertTrue(result.succeeded, self.codes(result))
        assert result.evidence is not None
        self.assertIsNone(result.evidence.configured_compiler.target)
        self.assertEqual(1, result.evidence.toolchains_minor)

    def test_requires_exactly_one_cxx_toolchain(self) -> None:
        cases = (
            [{"language": "C", "compiler": {"id": "MSVC", "version": "1"}}],
            [
                {"language": "CXX", "compiler": {"id": "MSVC", "version": "1"}},
                {"language": "CXX", "compiler": {"id": "Clang", "version": "2"}},
            ],
        )
        for index, entries in enumerate(cases):
            with self.subTest(index=index):
                case_root = self.root / f"case-{index}"
                case_root.mkdir()
                previous_root = self.build_root
                self.build_root = case_root
                try:
                    self.write_reply("001", toolchains=entries)
                    result = self.read()
                finally:
                    self.build_root = previous_root
                self.assertEqual(
                    ["host-build.cmake-cxx-toolchain-mismatch"],
                    self.codes(result),
                )

    def test_compiler_id_version_and_target_must_be_stable_strings(self) -> None:
        cases = (
            {"id": "", "version": "19.40"},
            {"id": "MSVC", "version": "C:/version"},
            {"id": "Clang", "version": "18.1", "target": "bad/target"},
        )
        for index, compiler in enumerate(cases):
            with self.subTest(index=index):
                case_root = self.root / f"invalid-{index}"
                case_root.mkdir()
                previous_root = self.build_root
                self.build_root = case_root
                try:
                    self.write_reply(
                        "001",
                        toolchains=[{"language": "CXX", "compiler": compiler}],
                    )
                    result = self.read()
                finally:
                    self.build_root = previous_root
                self.assertEqual(
                    ["host-build.cmake-cxx-compiler-invalid"],
                    self.codes(result),
                )

    def test_missing_and_wrong_stateful_responses_fail_closed(self) -> None:
        cases = (
            ("missing", "host-build.cmake-query-response-missing"),
            ("wrong", "host-build.cmake-toolchains-version-unsupported"),
            ("query", "host-build.cmake-query-mismatch"),
        )
        for index, (mutation, expected_code) in enumerate(cases):
            with self.subTest(mutation=mutation):
                case_root = self.root / f"response-{index}"
                case_root.mkdir()
                previous_root = self.build_root
                self.build_root = case_root
                try:
                    paths = self.write_reply("001")
                    data = json.loads(paths["index"].read_text(encoding="utf-8"))
                    stateful = data["reply"]["client-asharia-host-build-v1"][
                        "query.json"
                    ]
                    if mutation == "missing":
                        stateful["responses"].pop()
                    elif mutation == "wrong":
                        stateful["responses"][1]["kind"] = "cache"
                    else:
                        stateful["requests"][1]["kind"] = "cache"
                    paths["index"].write_text(
                        json.dumps(data, indent=2) + "\n",
                        encoding="utf-8",
                    )
                    result = self.read()
                finally:
                    self.build_root = previous_root
                self.assertEqual([expected_code], self.codes(result))

    def test_generator_name_and_multi_config_are_validated(self) -> None:
        cases = (
            {"name": "", "multiConfig": False},
            {"name": "Ninja", "multiConfig": "false"},
        )
        for index, generator in enumerate(cases):
            with self.subTest(index=index):
                case_root = self.root / f"generator-{index}"
                case_root.mkdir()
                previous_root = self.build_root
                self.build_root = case_root
                try:
                    self.write_reply("001", generator=generator)
                    result = self.read()
                finally:
                    self.build_root = previous_root
                self.assertEqual(
                    ["host-build.cmake-generator-invalid"],
                    self.codes(result),
                )

    def test_latest_index_keeps_target_and_compiler_evidence_together(self) -> None:
        self.write_reply("001")
        latest = self.write_reply(
            "002",
            toolchains=[
                {
                    "language": "CXX",
                    "compiler": {"id": "Clang", "version": "18.1.8"},
                }
            ],
        )

        result = self.read()

        self.assertTrue(result.succeeded, self.codes(result))
        assert result.evidence is not None
        self.assertEqual(latest["index"], result.evidence.reply_index_path)
        self.assertEqual(
            "out/asharia-host-002.exe",
            result.evidence.target.artifact_relative_path,
        )
        self.assertEqual("Clang", result.evidence.configured_compiler.compiler_id)

    def test_referenced_toolchains_drift_retries_the_whole_bundle(self) -> None:
        paths = self.write_reply("001")
        original_read_bytes = Path.read_bytes
        reads = 0

        def drifting_read(path: Path) -> bytes:
            nonlocal reads
            content = original_read_bytes(path)
            if path == paths["toolchains"]:
                reads += 1
                if reads == 2:
                    return content + b" "
            return content

        with mock.patch.object(Path, "read_bytes", drifting_read):
            result = self.read()

        self.assertTrue(result.succeeded, self.codes(result))
        self.assertGreaterEqual(reads, 4)


if __name__ == "__main__":
    unittest.main()
