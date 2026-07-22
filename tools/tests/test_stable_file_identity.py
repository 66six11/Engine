"""Tests for stat normalization shared by release/publication boundaries."""

from __future__ import annotations

import os
import shutil
import stat
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import stable_file_identity


class StableFileIdentityTests(unittest.TestCase):
    def test_file_kind_ignores_synthetic_permission_bits(self) -> None:
        regular_read_only = SimpleNamespace(st_mode=stat.S_IFREG | 0o444)
        regular_executable = SimpleNamespace(st_mode=stat.S_IFREG | 0o777)

        self.assertEqual(
            stable_file_identity.file_kind(regular_read_only),
            stable_file_identity.file_kind(regular_executable),
        )

    def test_windows_uses_birth_time_across_path_and_handle_observations(self) -> None:
        path_status = SimpleNamespace(st_ctime_ns=10, st_birthtime_ns=7)
        handle_status = SimpleNamespace(st_ctime_ns=20, st_birthtime_ns=7)

        with mock.patch.object(stable_file_identity.os, "name", "nt"):
            self.assertEqual(
                stable_file_identity.changed_ns(path_status),
                stable_file_identity.changed_ns(handle_status),
            )

    def test_posix_preserves_metadata_change_time(self) -> None:
        status = SimpleNamespace(st_ctime_ns=11, st_birthtime_ns=7)

        with mock.patch.object(stable_file_identity.os, "name", "posix"):
            self.assertEqual(11, stable_file_identity.changed_ns(status))

    @unittest.skipUnless(os.name == "nt", "Windows path/handle stat regression")
    def test_windows_real_path_and_handle_observations_agree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "host.exe"
            path.write_bytes(b"host")
            path_status = path.lstat()
            with path.open("rb") as stream:
                handle_status = os.fstat(stream.fileno())

            self.assertEqual(
                stable_file_identity.file_kind(path_status),
                stable_file_identity.file_kind(handle_status),
            )
            self.assertEqual(
                stable_file_identity.changed_ns(path_status),
                stable_file_identity.changed_ns(handle_status),
            )

    @unittest.skipUnless(os.name == "nt", "Windows extended-path regression")
    def test_windows_extended_path_supports_deep_io_and_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            normal_root = Path(temporary_directory) / "deep"
            extended_root = stable_file_identity.extended_path(normal_root)
            deep_file = extended_root.joinpath(
                "a" * 100,
                "b" * 100,
                "c" * 100,
                "payload.bin",
            )
            deep_file.parent.mkdir(parents=True)
            deep_file.write_bytes(b"payload")

            self.assertEqual(b"payload", deep_file.read_bytes())
            shutil.rmtree(extended_root)
            self.assertFalse(extended_root.exists())

    @unittest.skipUnless(os.name == "nt", "Windows extended-path regression")
    def test_windows_extended_path_normalizes_dot_segments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            marker = root / "marker"
            marker.mkdir()

            actual = stable_file_identity.extended_path(
                marker / ".." / "distribution-publication"
            )
            expected = stable_file_identity.extended_path(
                root / "distribution-publication"
            )

            self.assertEqual(expected, actual)
            self.assertNotIn("..", actual.parts)

    @unittest.skipUnless(os.name == "nt", "Windows extended-path regression")
    def test_windows_standard_path_removes_device_prefixes(self) -> None:
        self.assertEqual(
            Path(r"C:\distribution\generation"),
            stable_file_identity.standard_path(
                Path(r"\\?\C:\distribution\generation")
            ),
        )
        for prefix in ("UNC", "unc", "UnC"):
            with self.subTest(prefix=prefix):
                self.assertEqual(
                    Path(r"\\server\share\generation"),
                    stable_file_identity.standard_path(
                        Path(rf"\\?\{prefix}\server\share\generation")
                    ),
                )
