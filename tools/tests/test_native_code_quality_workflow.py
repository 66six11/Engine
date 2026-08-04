"""Policy tests for the native GitHub Actions workflow."""

from __future__ import annotations

import unittest
from pathlib import Path


class NativeCodeQualityWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        repository_root = Path(__file__).resolve().parents[2]
        cls.workflow = (
            repository_root / ".github" / "workflows" / "native-code-quality.yml"
        ).read_text(encoding="utf-8")
        cls.bootstrap = (
            repository_root / "scripts" / "bootstrap-conan.ps1"
        ).read_text(encoding="utf-8")

    def test_ci_builds_and_tests_only_with_msvc(self) -> None:
        self.assertIn("Build and test with MSVC", self.workflow)
        self.assertNotIn("Build and test with ClangCL", self.workflow)
        self.assertNotIn("cmake --build --preset clangcl", self.workflow)
        self.assertIn("-Profiles windows-msvc-debug", self.workflow)

    def test_conan_bootstrap_accepts_a_profile_subset(self) -> None:
        self.assertIn("[string[]] $Profiles", self.bootstrap)
        self.assertIn("foreach ($profile in $Profiles)", self.bootstrap)

    def test_ci_tidy_uses_changed_mode_and_msvc_database(self) -> None:
        self.assertIn("tidy_base_ref", self.workflow)
        self.assertIn(
            "--build-dir build\\cmake\\msvc-debug-tests --changed "
            '--base-ref "%TIDY_BASE_REF%"',
            self.workflow,
        )


if __name__ == "__main__":
    unittest.main()
