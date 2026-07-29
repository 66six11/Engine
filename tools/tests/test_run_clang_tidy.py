"""Tests for the standalone clang-tidy selection tool."""

from __future__ import annotations

import json
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import run_clang_tidy


class RunClangTidyTests(unittest.TestCase):
    def test_database_keeps_only_unique_source_owned_translation_units(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "src" / "main.cpp"
            generated = root / "build" / "generated.cpp"
            resource = root / "apps" / "editor.rc"
            c_source = root / "src" / "compatibility.c"
            external = root.parent / "external.cpp"
            database_path = root / "compile_commands.json"
            database_path.write_text(
                json.dumps(
                    [
                        {
                            "directory": str(root),
                            "file": str(source),
                            "command": "clang-cl main.cpp",
                        },
                        {
                            "directory": str(root),
                            "file": str(source),
                            "command": "clang-cl main.cpp",
                        },
                        {
                            "directory": str(root),
                            "file": str(generated),
                            "command": "clang-cl generated.cpp",
                        },
                        {
                            "directory": str(root),
                            "file": str(resource),
                            "command": "rc editor.rc",
                        },
                        {
                            "directory": str(root),
                            "file": str(c_source),
                            "command": "clang-cl compatibility.c",
                        },
                        {
                            "directory": str(root.parent),
                            "file": str(external),
                            "command": "clang-cl external.cpp",
                        },
                    ]
                ),
                encoding="utf-8",
            )

            actual = run_clang_tidy.load_project_translation_units(
                database_path,
                root,
            )

            self.assertEqual((source.resolve(),), actual)

    def test_changed_mode_selects_exact_database_translation_unit(self) -> None:
        root = Path("repository").resolve()
        first = root / "src" / "first.cpp"
        second = root / "src" / "second.cpp"

        selection = run_clang_tidy.select_changed_translation_units(
            root,
            (first, second),
            ("src/second.cpp", "docs/readme.md"),
        )

        self.assertFalse(selection.full)
        self.assertEqual((second,), selection.translation_units)

    def test_changed_translation_unit_missing_from_database_fails(self) -> None:
        root = Path("repository").resolve()

        with self.assertRaisesRegex(
            run_clang_tidy.TidySelectionError,
            "missing from the compilation database",
        ):
            run_clang_tidy.select_changed_translation_units(
                root,
                (root / "src" / "tracked.cpp",),
                ("src/untracked.cpp",),
            )

    def test_changed_header_expands_selection_to_full_database(self) -> None:
        root = Path("repository").resolve()
        translation_units = (
            root / "src" / "first.cpp",
            root / "src" / "second.cpp",
        )

        selection = run_clang_tidy.select_changed_translation_units(
            root,
            translation_units,
            ("include/project/public.hpp",),
        )

        self.assertTrue(selection.full)
        self.assertEqual(translation_units, selection.translation_units)
        self.assertEqual(
            ("include/project/public.hpp",),
            selection.reasons,
        )

    def test_changed_cmake_input_expands_selection_to_full_database(self) -> None:
        root = Path("repository").resolve()
        translation_units = (root / "src" / "main.cpp",)

        selection = run_clang_tidy.select_changed_translation_units(
            root,
            translation_units,
            ("packages/example/CMakeLists.txt",),
        )

        self.assertTrue(selection.full)
        self.assertEqual(
            ("packages/example/CMakeLists.txt",),
            selection.reasons,
        )

    def test_non_native_change_is_a_successful_empty_selection(self) -> None:
        root = Path("repository").resolve()

        selection = run_clang_tidy.select_changed_translation_units(
            root,
            (root / "src" / "main.cpp",),
            ("docs/readme.md",),
        )

        self.assertFalse(selection.full)
        self.assertEqual((), selection.translation_units)

    def test_changed_pattern_matches_only_selected_files(self) -> None:
        root = Path("repository").resolve()
        selected = root / "src" / "selected.cpp"
        pattern = run_clang_tidy.selection_pattern(
            root,
            run_clang_tidy.TidySelection((selected,), full=False),
        )

        self.assertIsNotNone(re.search(pattern, str(selected)))
        self.assertIsNone(re.search(pattern, str(root / "src" / "other.cpp")))

    def test_full_pattern_matches_only_project_cxx_translation_units(
        self,
    ) -> None:
        root = Path("repository").resolve()
        pattern = run_clang_tidy.selection_pattern(
            root,
            run_clang_tidy.TidySelection(
                (root / "src" / "selected.cpp",),
                full=True,
            ),
        )

        self.assertIsNotNone(re.search(pattern, str(root / "src" / "main.cpp")))
        self.assertIsNotNone(re.search(pattern, str(root / "src" / "MAIN.CPP")))
        self.assertIsNone(re.search(pattern, str(root / "apps" / "editor.rc")))
        self.assertIsNone(re.search(pattern, str(root / "src" / "legacy.c")))
        self.assertIsNone(
            re.search(pattern, str(root / "build" / "generated.cpp"))
        )

    def test_changed_files_include_untracked_with_explicit_base(self) -> None:
        root = Path("repository").resolve()
        diff_result = mock.Mock(
            returncode=0,
            stdout="src/tracked.cpp\0",
            stderr="",
        )
        untracked_result = mock.Mock(
            returncode=0,
            stdout="src/untracked.cpp\0",
            stderr="",
        )

        with mock.patch(
            "tools.run_clang_tidy.subprocess.run",
            side_effect=(diff_result, untracked_result),
        ) as run:
            actual = run_clang_tidy.collect_changed_files(
                root,
                base_ref="origin/main",
                staged=False,
                include_untracked=True,
            )

        self.assertEqual(
            ("src/tracked.cpp", "src/untracked.cpp"),
            actual,
        )
        self.assertEqual(2, run.call_count)
        self.assertIn("-z", run.call_args_list[0].args[0])
        self.assertEqual("--", run.call_args_list[0].args[0][-1])

    def test_runner_is_found_beside_clang_tidy_on_windows(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            tool_directory = Path(temporary_directory)
            clang_tidy = tool_directory / "clang-tidy.exe"
            runner = tool_directory / "run-clang-tidy"
            clang_tidy.write_bytes(b"tool")
            runner.write_text("# runner", encoding="utf-8")

            with mock.patch("tools.run_clang_tidy.shutil.which", return_value=None):
                actual = run_clang_tidy._find_runner(None, clang_tidy)

            self.assertEqual(runner.resolve(), actual)


if __name__ == "__main__":
    unittest.main()
