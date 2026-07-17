"""Cross-generation compatibility tests for Host binding inputs."""

from __future__ import annotations

import unittest
from dataclasses import replace

from tools import check_package_contracts as contracts
from tools import host_binding_inputs
from tools import host_executable_template
from tools.tests import host_template_test_support as support


class HostBindingInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def setUp(self) -> None:
        self.composition = support.composition_generation(self.validators)
        generated = (
            host_executable_template.generate_windows_development_host_template(
                self.composition.manifest,
                "asharia-generated-host",
                self.validators,
            )
        )
        assert generated.generation is not None
        self.template = generated.generation

    def diagnostic_codes(self, composition, template) -> list[str]:
        return [
            item.code
            for item in host_binding_inputs.composition_template_diagnostics(
                composition,
                template,
            )
        ]

    def test_current_renderer_pair_is_supported(self) -> None:
        self.assertEqual([], self.diagnostic_codes(self.composition, self.template))

    def test_any_legacy_renderer_or_provider_fails_closed(self) -> None:
        legacy_composition = replace(
            self.composition,
            manifest=replace(
                self.composition.manifest,
                renderer_revision=5,
                provider_api="asharia-static-factory-provider-v4",
            ),
        )
        legacy_template = replace(
            self.template,
            manifest=replace(self.template.manifest, renderer_revision=2),
        )

        for composition, template in (
            (legacy_composition, self.template),
            (self.composition, legacy_template),
            (legacy_composition, legacy_template),
        ):
            with self.subTest(
                composition_renderer=composition.manifest.renderer_revision,
                template_renderer=template.manifest.renderer_revision,
            ):
                self.assertEqual(
                    ["host-binding.input.renderer-incompatible"],
                    self.diagnostic_codes(composition, template),
                )


if __name__ == "__main__":
    unittest.main()
