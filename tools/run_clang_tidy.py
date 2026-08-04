"""Run clang-tidy separately from compilation by using a CMake database."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


_SOURCE_EXTENSIONS = frozenset({".cc", ".cpp", ".cxx"})


class TidySelectionError(RuntimeError):
    """A changed-file selection cannot be analyzed safely."""


@dataclass(frozen=True)
class TidySelection:
    """Translation units selected for one clang-tidy invocation."""

    translation_units: tuple[Path, ...]
    full: bool


def _resolved_path(path: Path) -> Path:
    return path.resolve(strict=False)


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def load_project_translation_units(
    database_path: Path,
    source_root: Path,
) -> tuple[Path, ...]:
    """Load unique, source-owned translation units from compile_commands.json."""

    try:
        raw_database = json.loads(database_path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError as error:
        raise TidySelectionError(
            f"compilation database does not exist: {database_path}"
        ) from error
    except (OSError, json.JSONDecodeError) as error:
        raise TidySelectionError(
            f"cannot read compilation database {database_path}: {error}"
        ) from error

    if not isinstance(raw_database, list):
        raise TidySelectionError(
            f"compilation database must contain a JSON array: {database_path}"
        )

    resolved_source_root = _resolved_path(source_root)
    translation_units: set[Path] = set()
    for index, entry in enumerate(raw_database):
        if not isinstance(entry, dict):
            raise TidySelectionError(
                f"compilation database entry {index} must be an object"
            )
        directory = entry.get("directory")
        file_name = entry.get("file")
        if not isinstance(directory, str) or not isinstance(file_name, str):
            raise TidySelectionError(
                f"compilation database entry {index} needs string "
                "'directory' and 'file' fields"
            )

        file_path = Path(file_name)
        if not file_path.is_absolute():
            file_path = Path(directory) / file_path
        resolved_file = _resolved_path(file_path)
        if not _is_within(resolved_file, resolved_source_root):
            continue

        relative_file = resolved_file.relative_to(resolved_source_root)
        if relative_file.parts and relative_file.parts[0].casefold() == "build":
            continue
        if resolved_file.suffix.casefold() not in _SOURCE_EXTENSIONS:
            continue
        translation_units.add(resolved_file)

    if not translation_units:
        raise TidySelectionError(
            "compilation database contains no source-owned translation units"
        )
    return tuple(sorted(translation_units, key=lambda path: str(path).casefold()))


def select_changed_translation_units(
    source_root: Path,
    available_translation_units: Sequence[Path],
    changed_files: Iterable[str],
) -> TidySelection:
    """Select only changed source files present in the compilation database."""

    resolved_source_root = _resolved_path(source_root)
    available = {
        _resolved_path(path): path for path in available_translation_units
    }
    relative_changes = tuple(
        sorted(
            {
                Path(path.replace("\\", "/"))
                for path in changed_files
                if path and not Path(path).is_absolute()
            },
            key=lambda path: path.as_posix().casefold(),
        )
    )

    selected: list[Path] = []
    missing: list[str] = []
    for relative_path in relative_changes:
        if relative_path.suffix.casefold() not in _SOURCE_EXTENSIONS:
            continue
        absolute_path = _resolved_path(resolved_source_root / relative_path)
        database_path = available.get(absolute_path)
        if database_path is None:
            missing.append(relative_path.as_posix())
        else:
            selected.append(database_path)

    if missing:
        rendered = ", ".join(missing)
        raise TidySelectionError(
            "changed translation units are missing from the compilation "
            f"database: {rendered}"
        )

    return TidySelection(
        tuple(sorted(set(selected), key=lambda path: str(path).casefold())),
        full=False,
    )


def collect_changed_files(
    source_root: Path,
    *,
    base_ref: str,
    staged: bool,
    include_untracked: bool,
) -> tuple[str, ...]:
    """Collect repository-relative changed files using the established Git rules."""

    git_arguments = [
        "-c",
        "core.quotePath=false",
        "diff",
        "--name-only",
        "--diff-filter=ACMRT",
        "-z",
    ]
    if staged:
        git_arguments.append("--cached")
    elif base_ref == "HEAD":
        git_arguments.append("HEAD")
    elif ".." in base_ref:
        git_arguments.append(base_ref)
    else:
        git_arguments.append(f"{base_ref}...HEAD")
    git_arguments.append("--")

    result = subprocess.run(
        ["git", *git_arguments],
        cwd=source_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        raise TidySelectionError(
            f"git {' '.join(git_arguments)} failed: {result.stderr.strip()}"
        )

    changed_files = {
        path.replace("\\", "/")
        for path in result.stdout.split("\0")
        if path
    }
    if include_untracked and not staged:
        untracked = subprocess.run(
            [
                "git",
                "-c",
                "core.quotePath=false",
                "ls-files",
                "--others",
                "--exclude-standard",
                "-z",
                "--",
            ],
            cwd=source_root,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if untracked.returncode != 0:
            raise TidySelectionError(
                "git ls-files failed: " + untracked.stderr.strip()
            )
        changed_files.update(
            path.replace("\\", "/")
            for path in untracked.stdout.split("\0")
            if path
        )
    return tuple(sorted(changed_files, key=str.casefold))


def selection_pattern(
    source_root: Path,
    selection: TidySelection,
) -> str:
    """Build the source regex passed to LLVM's run-clang-tidy."""

    if selection.full:
        root_prefix = re.escape(str(_resolved_path(source_root)) + os.sep)
        extensions = "|".join(
            re.escape(extension.removeprefix("."))
            for extension in sorted(_SOURCE_EXTENSIONS)
        )
        return rf"(?i)^{root_prefix}(?!build(?:\\|/|$)).*\.(?:{extensions})$"

    alternatives = "|".join(
        re.escape(str(_resolved_path(path)))
        for path in selection.translation_units
    )
    return rf"(?i)^(?:{alternatives})$"


def _find_program(explicit: str | None, names: Sequence[str]) -> Path:
    if explicit:
        path = _resolved_path(Path(explicit))
        if path.is_file():
            return path
        raise TidySelectionError(f"tool does not exist: {path}")

    for name in names:
        discovered = shutil.which(name)
        if discovered:
            return _resolved_path(Path(discovered))
    raise TidySelectionError(
        "required tool was not found on PATH: " + ", ".join(names)
    )


def _find_runner(explicit: str | None, clang_tidy_path: Path) -> Path:
    if explicit:
        return _find_program(explicit, ())

    for name in ("run-clang-tidy", "run-clang-tidy.py"):
        sibling = clang_tidy_path.parent / name
        if sibling.is_file():
            return _resolved_path(sibling)
    return _find_program(None, ("run-clang-tidy", "run-clang-tidy.py"))


def _run_tidy(
    *,
    build_directory: Path,
    source_root: Path,
    selection: TidySelection,
    jobs: int,
    clang_tidy: str | None,
    runner: str | None,
    config_file: Path,
) -> int:
    clang_tidy_path = _find_program(
        clang_tidy,
        ("clang-tidy", "clang-tidy.exe"),
    )
    runner_path = _find_runner(runner, clang_tidy_path)
    if not config_file.is_file():
        raise TidySelectionError(
            f"clang-tidy configuration does not exist: {config_file}"
        )

    runner_command = [str(runner_path)]
    if runner_path.suffix.casefold() in {"", ".py"}:
        runner_command.insert(0, sys.executable)
    command = [
        *runner_command,
        "-p",
        str(build_directory),
        "-j",
        str(jobs),
        "-clang-tidy-binary",
        str(clang_tidy_path),
        "-config-file",
        str(config_file),
        "-quiet",
        "-extra-arg-before=/EHsc",
        "-extra-arg=-fexceptions",
        "-extra-arg=-fcxx-exceptions",
        selection_pattern(source_root, selection),
    ]
    return subprocess.run(command, cwd=source_root, check=False).returncode


def _parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run clang-tidy independently from compilation using a CMake "
            "compile_commands.json database."
        )
    )
    parser.add_argument(
        "--build-dir",
        default="build/cmake/clangcl-debug",
        help="CMake build directory containing compile_commands.json.",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="Root containing the source files owned by this analysis.",
    )
    parser.add_argument(
        "--config-file",
        default=".clang-tidy",
        help="clang-tidy configuration file.",
    )
    parser.add_argument(
        "--changed",
        action="store_true",
        help="Analyze changed translation units instead of the full database.",
    )
    parser.add_argument(
        "--base-ref",
        default="HEAD",
        help="Git base ref or explicit diff range used with --changed.",
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="Use staged changes with --changed.",
    )
    parser.add_argument(
        "--include-untracked",
        action="store_true",
        help="Include untracked files in a non-staged changed selection.",
    )
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--clang-tidy")
    parser.add_argument("--runner")
    arguments = parser.parse_args(argv)
    if arguments.jobs < 1:
        parser.error("--jobs must be at least 1")
    if not arguments.changed and (
        arguments.staged or arguments.include_untracked
    ):
        parser.error("--staged and --include-untracked require --changed")
    if arguments.staged and arguments.include_untracked:
        parser.error("--staged cannot be combined with --include-untracked")
    return arguments


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_arguments(argv if argv is not None else sys.argv[1:])
    source_root = _resolved_path(Path(arguments.source_root))
    build_directory = _resolved_path(Path(arguments.build_dir))
    config_file = _resolved_path(Path(arguments.config_file))
    database_path = build_directory / "compile_commands.json"

    try:
        translation_units = load_project_translation_units(
            database_path,
            source_root,
        )
        if arguments.changed:
            changed_files = collect_changed_files(
                source_root,
                base_ref=arguments.base_ref,
                staged=arguments.staged,
                include_untracked=arguments.include_untracked,
            )
            selection = select_changed_translation_units(
                source_root,
                translation_units,
                changed_files,
            )
        else:
            selection = TidySelection(translation_units, full=True)

        if not selection.translation_units:
            print("clang-tidy: no changed project translation units.")
            return 0
        mode = "full" if selection.full else "changed"
        print(
            f"clang-tidy: {mode} selection contains "
            f"{len(selection.translation_units)} translation unit(s).",
            flush=True,
        )
        result = _run_tidy(
            build_directory=build_directory,
            source_root=source_root,
            selection=selection,
            jobs=arguments.jobs,
            clang_tidy=arguments.clang_tidy,
            runner=arguments.runner,
            config_file=config_file,
        )
        print(f"clang-tidy: completed with exit code {result}.")
        return result
    except TidySelectionError as error:
        print(f"clang-tidy: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
