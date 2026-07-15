"""Expected/observed static Host registration cross-check tests."""

from __future__ import annotations

import unittest
from dataclasses import replace

from tools import check_package_contracts as contracts
from tools import host_registration_cross_verifier as cross_verifier
from tools import host_registration_snapshot as snapshot_model
from tools import static_composition_root as composition_root
from tools import static_factory_provider_bindings as provider_bindings
from tools.tests import host_template_test_support as support


class HostRegistrationCrossVerifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.generation = support.composition_generation(self.validators)

    def expected_registrations(
        self,
        manifest: composition_root.StaticCompositionRootManifest | None = None,
    ) -> tuple[snapshot_model.StaticFactoryRegistration, ...]:
        selected = self.generation.manifest if manifest is None else manifest
        return tuple(
            snapshot_model.StaticFactoryRegistration(
                package_id=provider.package_id,
                package_version=provider.package_version,
                module_id=provider.module_id,
                factory_id=factory_id,
                provider_entry_point=provider.entry_point.function,
            )
            for provider in selected.providers
            for factory_id in provider.factory_ids
        )

    def snapshot(
        self,
        registrations: tuple[
            snapshot_model.StaticFactoryRegistration, ...
        ] | None = None,
        *,
        manifest: composition_root.StaticCompositionRootManifest | None = None,
        generation_id: str | None = None,
        blueprint_sha256: str | None = None,
    ) -> snapshot_model.HostRegistrationSnapshot:
        selected = self.generation.manifest if manifest is None else manifest
        return snapshot_model.HostRegistrationSnapshot(
            generation_id=(
                selected.generation_id
                if generation_id is None
                else generation_id
            ),
            host_activation_blueprint_sha256=(
                selected.inputs.host_activation_blueprint_integrity.digest
                if blueprint_sha256 is None
                else blueprint_sha256
            ),
            registrations=(
                self.expected_registrations(selected)
                if registrations is None
                else registrations
            ),
        )

    def verify(
        self,
        composition: object,
        snapshot: object,
    ) -> cross_verifier.HostRegistrationCrossVerificationResult:
        return cross_verifier.verify_host_registration_cross_binding(
            composition,
            snapshot,
            self.validators,
        )

    def manifest_with_providers(
        self,
        providers: tuple[provider_bindings.StaticFactoryProvider, ...],
    ) -> composition_root.StaticCompositionRootManifest:
        value = replace(
            self.generation.manifest,
            generation_id="sha256-" + "0" * 64,
            providers=providers,
            integrity=composition_root.IntegrityRecord("sha256", "0" * 64),
        )
        value = replace(
            value,
            generation_id=composition_root._generation_id(value),
        )
        integrity = (
            composition_root.compute_static_composition_root_manifest_integrity(
                value
            )
        )
        return replace(
            value,
            integrity=composition_root.IntegrityRecord(
                integrity["algorithm"], integrity["digest"]
            ),
        )

    def second_provider(
        self,
        *,
        same_owner: bool = False,
    ) -> provider_bindings.StaticFactoryProvider:
        original = self.generation.manifest.providers[0]
        return replace(
            original,
            package_id=(
                original.package_id
                if same_owner
                else "com.asharia.synthetic.other"
            ),
            target=provider_bindings.ProviderTarget(
                "zz_synthetic_runtime", "STATIC_LIBRARY"
            ),
            entry_point=provider_bindings.ProviderEntryPoint(
                "asharia/synthetic/other_provider.hpp",
                "asharia::synthetic::provideOtherFactories",
            ),
        )

    def assert_atomic_failure(
        self,
        result: cross_verifier.HostRegistrationCrossVerificationResult,
        *codes: str,
    ) -> None:
        self.assertFalse(result.succeeded)
        self.assertIsNone(result.verified)
        self.assertEqual(set(codes), {value.code for value in result.diagnostics})

    def test_manifest_and_complete_generation_verify_the_same_canonical_set(
        self,
    ) -> None:
        observed = self.snapshot()

        for value in (self.generation.manifest, self.generation):
            with self.subTest(type=type(value).__name__):
                result = self.verify(value, observed)

                self.assertTrue(
                    result.succeeded,
                    [item.render() for item in result.diagnostics],
                )
                assert result.verified is not None
                self.assertEqual(
                    self.generation.manifest.generation_id,
                    result.verified.generation_id,
                )
                self.assertEqual(
                    self.expected_registrations(),
                    result.verified.registrations,
                )
                self.assertFalse(hasattr(result.verified, "activation_order"))

    def test_generation_and_blueprint_are_reverified_exactly(self) -> None:
        observed = self.snapshot(
            generation_id="sha256-" + "a" * 64,
            blueprint_sha256="b" * 64,
        )

        result = self.verify(self.generation, observed)

        self.assert_atomic_failure(
            result,
            "host-binding.registration.generation-mismatch",
            "host-binding.registration.blueprint-mismatch",
        )

    def test_duplicate_expected_identity_is_rejected_without_pairing(self) -> None:
        duplicate = self.second_provider(same_owner=True)
        manifest = self.manifest_with_providers(
            (self.generation.manifest.providers[0], duplicate)
        )

        observed_once = (self.expected_registrations(manifest)[0],)
        result = self.verify(
            manifest,
            self.snapshot(observed_once, manifest=manifest),
        )

        self.assert_atomic_failure(
            result,
            "host-binding.registration.expected-duplicate",
        )

    def test_duplicate_observed_identity_is_rejected_without_pairing(self) -> None:
        expected = self.expected_registrations()[0]
        duplicate = replace(
            expected,
            provider_entry_point="asharia::synthetic::provideOtherFactories",
        )

        result = self.verify(
            self.generation,
            self.snapshot((expected, duplicate)),
        )

        self.assert_atomic_failure(
            result,
            "host-binding.registration.observed-duplicate",
        )

    def test_missing_extra_and_provider_mismatch_are_distinct(self) -> None:
        expected = self.expected_registrations()[0]
        extra = replace(
            expected,
            package_id="com.asharia.synthetic.extra",
        )
        cases = {
            "missing": (
                (),
                "host-binding.registration.missing",
            ),
            "extra": (
                (expected, extra),
                "host-binding.registration.extra",
            ),
            "provider": (
                (
                    replace(
                        expected,
                        provider_entry_point=(
                            "asharia::synthetic::provideOtherFactories"
                        ),
                    ),
                ),
                "host-binding.registration.provider-mismatch",
            ),
        }
        for name, (values, code) in cases.items():
            with self.subTest(name=name):
                result = self.verify(self.generation, self.snapshot(values))

                self.assert_atomic_failure(result, code)

    def test_factory_id_is_not_treated_as_a_global_identity(self) -> None:
        expected = self.expected_registrations()[0]
        other_owner = replace(
            expected,
            package_id="com.asharia.synthetic.other",
        )

        result = self.verify(
            self.generation,
            self.snapshot((other_owner,)),
        )

        self.assert_atomic_failure(
            result,
            "host-binding.registration.missing",
            "host-binding.registration.extra",
        )
        self.assertNotIn(
            "host-binding.registration.provider-mismatch",
            {value.code for value in result.diagnostics},
        )

    def test_same_factory_id_under_distinct_owners_is_valid(self) -> None:
        manifest = self.manifest_with_providers(
            (
                self.generation.manifest.providers[0],
                self.second_provider(),
            )
        )

        observed = tuple(reversed(self.expected_registrations(manifest)))
        result = self.verify(
            manifest,
            self.snapshot(observed, manifest=manifest),
        )

        self.assertTrue(
            result.succeeded,
            [value.render() for value in result.diagnostics],
        )
        assert result.verified is not None
        self.assertEqual(
            self.expected_registrations(manifest),
            result.verified.registrations,
        )

    def test_diagnostics_are_input_order_independent(self) -> None:
        expected = self.expected_registrations()[0]
        extras = (
            replace(expected, package_id="com.asharia.zeta"),
            replace(expected, package_id="com.asharia.alpha"),
        )

        first = self.verify(self.generation, self.snapshot(extras))
        second = self.verify(
            self.generation,
            self.snapshot(tuple(reversed(extras))),
        )

        self.assertIsNone(first.verified)
        self.assertEqual(first.diagnostics, second.diagnostics)
        self.assertEqual(
            sorted(first.diagnostics, key=lambda value: (
                value.manifest_path,
                value.pointer,
                value.code,
                value.message,
            )),
            list(first.diagnostics),
        )

    def test_invalid_inputs_and_tampered_generation_never_return_partial_value(
        self,
    ) -> None:
        invalid = self.verify(object(), object())
        self.assert_atomic_failure(
            invalid,
            "host-binding.registration.composition-invalid",
            "host-binding.registration.snapshot-invalid",
        )

        changed_file = replace(
            self.generation.files[0],
            content=self.generation.files[0].content + b"tampered",
        )
        tampered = replace(
            self.generation,
            files=(changed_file, *self.generation.files[1:]),
        )
        result = self.verify(tampered, self.snapshot())

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.verified)
        self.assertIn(
            "static-composition.output-integrity-mismatch",
            {value.code for value in result.diagnostics},
        )


if __name__ == "__main__":
    unittest.main()
