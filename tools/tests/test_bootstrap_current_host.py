"""Focused current published Host image admission tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import bootstrap_current_host
from tools import bootstrap_session as bootstrap
from tools import check_package_contracts as contracts
from tools import effective_session
from tools.tests import bootstrap_host_test_support as support


class BootstrapCurrentHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.fixture = support.make_fixture(
            Path(self.temporary_directory.name), self.validators
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def observe(
        self,
        *,
        composition: object | None = None,
        verified_binding: object | None = None,
    ) -> bootstrap.CurrentImageObservationV1:
        return bootstrap_current_host.observe_current_host_image(
            self.fixture.inspection,
            (
                self.fixture.static_composition
                if composition is None
                else composition
            ),
            (
                self.fixture.verified_binding
                if verified_binding is None
                else verified_binding
            ),
            self.validators,
        )

    def test_current_image_uses_identity_and_lightweight_file_observation(self) -> None:
        with mock.patch.object(
            Path,
            "open",
            side_effect=AssertionError("normal open must not hash artifact bytes"),
        ):
            observed = self.observe()

        self.assertEqual(
            bootstrap.CurrentImageDispositionV1.CURRENT,
            observed.disposition,
        )
        self.assertEqual(
            self.fixture.plan.session_fingerprint,
            observed.current_session_integrity,
        )
        self.assertIsInstance(
            observed.payload,
            bootstrap_current_host.CurrentHostPayloadV1,
        )

    def test_missing_stale_and_invalid_images_are_distinct(self) -> None:
        missing_generation = bootstrap_current_host.observe_current_host_image(
            self.fixture.inspection,
            None,
            None,
            self.validators,
        )
        stale_plan = replace(
            self.fixture.plan,
            session_fingerprint=effective_session.SessionIntegrity(
                "sha256", "f" * 64
            ),
        )
        stale_inspection = replace(
            self.fixture.inspection,
            effective_session=effective_session.EffectiveSessionResult(
                effective_session.EffectiveSessionState.READY,
                stale_plan,
                (),
            ),
        )
        stale = bootstrap_current_host.observe_current_host_image(
            stale_inspection,
            self.fixture.static_composition,
            self.fixture.verified_binding,
            self.validators,
        )
        artifact_path = (
            self.fixture.verified_binding.generation_path
            / self.fixture.verified_binding.receipt.artifact.path
        )
        artifact_path.write_bytes(b"wrong-size")
        invalid = self.observe()

        self.assertEqual(
            bootstrap.CurrentImageDispositionV1.MISSING,
            missing_generation.disposition,
        )
        self.assertEqual(
            bootstrap.CurrentImageDispositionV1.STALE,
            stale.disposition,
        )
        self.assertEqual(
            bootstrap.CurrentImageDispositionV1.INVALID,
            invalid.disposition,
        )
        self.assertEqual(
            ["bootstrap.image.artifact-invalid"],
            [item.code for item in invalid.diagnostics],
        )

    def test_stale_identity_short_circuits_missing_artifact_observation(self) -> None:
        mismatched_plan = replace(
            self.fixture.plan,
            session_fingerprint=effective_session.SessionIntegrity(
                "sha256", "f" * 64
            ),
        )
        inspection = replace(
            self.fixture.inspection,
            effective_session=effective_session.EffectiveSessionResult(
                effective_session.EffectiveSessionState.READY,
                mismatched_plan,
                (),
            ),
        )
        artifact_path = (
            self.fixture.verified_binding.generation_path
            / self.fixture.verified_binding.receipt.artifact.path
        )
        artifact_path.unlink()

        observed = bootstrap_current_host.observe_current_host_image(
            inspection,
            self.fixture.static_composition,
            self.fixture.verified_binding,
            self.validators,
        )

        self.assertEqual(
            bootstrap.CurrentImageDispositionV1.STALE,
            observed.disposition,
        )
        self.assertEqual(
            ["bootstrap.image.identity-stale"],
            [item.code for item in observed.diagnostics],
        )

    def test_invalid_validator_handoff_returns_typed_image_failure(self) -> None:
        malformed = replace(
            self.validators,
            static_composition_root=None,  # type: ignore[arg-type]
        )
        for validators in (None, malformed):
            with self.subTest(validators=validators), mock.patch.object(
                Path,
                "lstat",
            ) as observe_file:
                observed = bootstrap_current_host.observe_current_host_image(
                    self.fixture.inspection,
                    self.fixture.static_composition,
                    self.fixture.verified_binding,
                    validators,  # type: ignore[arg-type]
                )

            self.assertEqual(
                bootstrap.CurrentImageDispositionV1.INVALID,
                observed.disposition,
            )
            self.assertEqual(
                ["bootstrap.image.validators-invalid"],
                [item.code for item in observed.diagnostics],
            )
            observe_file.assert_not_called()


if __name__ == "__main__":
    unittest.main()
