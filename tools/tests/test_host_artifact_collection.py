"""Focused exact-byte Host artifact collection tests."""

from __future__ import annotations

import hashlib
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import host_artifact_collection as collection
from tools import host_cmake_target
from tools import static_composition_root


class HostArtifactCollectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.build_root = self.root / "build"
        self.staging_root = self.root / "staging"
        self.build_root.mkdir()
        self.staging_root.mkdir()
        self.source = self.build_root / "bin/Debug/host.exe"
        self.source.parent.mkdir(parents=True)
        self.content = (
            b"a" * collection.HOST_ARTIFACT_COPY_CHUNK_SIZE
            + b"b" * collection.HOST_ARTIFACT_COPY_CHUNK_SIZE
            + b"tail"
        )
        self.source.write_bytes(self.content)
        self.target = host_cmake_target.HostCMakeTargetEvidence(
            build_root=self.build_root.resolve(),
            reply_index_path=self.build_root / "reply/index.json",
            configuration="Debug",
            target_name="asharia-generated-host",
            target_type="EXECUTABLE",
            name_on_disk="host.exe",
            artifact_relative_path="bin/Debug/host.exe",
            artifact_path=self.source.resolve(),
            codemodel_major=2,
            codemodel_minor=6,
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def collect(self) -> collection.HostArtifactCollectionResult:
        return collection.collect_host_artifact(self.target, self.staging_root)

    def test_streams_exact_bytes_into_owned_staging(self) -> None:
        with mock.patch.object(
            Path,
            "read_bytes",
            side_effect=AssertionError("collector must stream large artifacts"),
        ):
            result = self.collect()

        self.assertTrue(result.succeeded, result.diagnostics)
        assert result.artifact is not None
        self.assertEqual("host/host.exe", result.artifact.publication_path)
        self.assertEqual(len(self.content), result.artifact.size)
        self.assertEqual(
            hashlib.sha256(self.content).hexdigest(),
            result.artifact.integrity.digest,
        )
        with result.artifact.staged_path.open("rb") as staged:
            self.assertEqual(self.content, staged.read())
        self.assertEqual(
            (),
            collection.verify_collected_host_artifact(
                result.artifact, verify_source=True
            ),
        )

    def test_staged_tamper_fails_closed(self) -> None:
        result = self.collect()
        assert result.artifact is not None
        result.artifact.staged_path.write_bytes(b"tampered")

        diagnostics = collection.verify_collected_host_artifact(
            result.artifact, verify_source=False
        )

        self.assertEqual(
            ["host-binding.artifact.staged-hash-mismatch"],
            [item.code for item in diagnostics],
        )

    def test_source_drift_after_collection_fails_closed(self) -> None:
        result = self.collect()
        assert result.artifact is not None
        self.source.write_bytes(self.content + b"changed")

        diagnostics = collection.verify_collected_host_artifact(
            result.artifact, verify_source=True
        )

        self.assertEqual(
            ["host-binding.artifact.source-drift"],
            [item.code for item in diagnostics],
        )

    def test_mutable_build_and_owned_staging_roots_must_not_overlap(self) -> None:
        nested_staging = self.build_root / "staging"
        nested_staging.mkdir()

        result = collection.collect_host_artifact(self.target, nested_staging)

        self.assertEqual(
            ["host-binding.artifact.root-overlap"],
            [item.code for item in result.diagnostics],
        )

    def test_target_name_must_identify_primary_artifact(self) -> None:
        result = collection.collect_host_artifact(
            replace(self.target, name_on_disk="other.exe"),
            self.staging_root,
        )

        self.assertEqual(
            ["host-binding.artifact.name-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_relative_binding_must_identify_the_same_source_file(self) -> None:
        other = self.build_root / "other/host.exe"
        other.parent.mkdir()
        other.write_bytes(self.content)

        result = collection.collect_host_artifact(
            replace(self.target, artifact_path=other.resolve()),
            self.staging_root,
        )

        self.assertEqual(
            ["host-binding.artifact.binding-mismatch"],
            [item.code for item in result.diagnostics],
        )

    def test_file_api_binding_rejects_symlink_component(self) -> None:
        real = self.build_root / "real.exe"
        real.write_bytes(self.content)
        self.source.unlink()
        try:
            self.source.symlink_to(real)
        except OSError as error:
            self.skipTest(f"symlink creation unavailable: {error}")

        result = collection.collect_host_artifact(
            replace(self.target, artifact_path=real.resolve()),
            self.staging_root,
        )

        self.assertEqual(
            ["host-binding.artifact.path-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_non_regular_artifact_is_rejected(self) -> None:
        self.source.unlink()
        self.source.mkdir()

        result = self.collect()

        self.assertEqual(
            ["host-binding.artifact.file-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_observation_rejects_untyped_path(self) -> None:
        result = collection.observe_host_artifact(str(self.source))

        self.assertEqual(
            ["host-binding.artifact.path-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_failed_independent_rehash_removes_owned_partial_tree(self) -> None:
        observed = collection.observe_host_artifact(self.source)
        assert observed.observation is not None
        wrong = collection.HostArtifactObservation(
            observed.observation.size,
            static_composition_root.IntegrityRecord("sha256", "0" * 64),
            observed.observation.fingerprint,
        )
        with mock.patch.object(
            collection,
            "_stream_observation",
            return_value=wrong,
        ):
            result = self.collect()

        self.assertEqual(
            ["host-binding.artifact.staged-hash-mismatch"],
            [item.code for item in result.diagnostics],
        )
        self.assertFalse((self.staging_root / "host").exists())


if __name__ == "__main__":
    unittest.main()
