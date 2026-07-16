"""Focused deterministic Host executable template tests."""

from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tools import check_package_contracts as contracts
from tools import host_executable_template as host_template
from tools import host_template_publication
from tools import static_composition_root as composition
from tools.tests import host_template_test_support as support


class HostExecutableTemplateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.composition = support.composition_generation(cls.validators)

    def generate(
        self, target_name: str = "asharia-generated-host"
    ) -> host_template.HostTemplateGenerationResult:
        return host_template.generate_windows_development_host_template(
            self.composition.manifest,
            target_name,
            self.validators,
        )

    def test_generation_is_deterministic_and_schema_valid(self) -> None:
        first = self.generate()
        second = self.generate()

        self.assertTrue(first.succeeded)
        self.assertEqual(first.generation, second.generation)
        assert first.generation is not None
        manifest = first.generation.manifest
        self.assertEqual(2, manifest.renderer_revision)
        self.assertEqual(
            self.composition.manifest.generation_id,
            manifest.static_composition_generation_id,
        )
        self.assertEqual("asharia-generated-host", manifest.target_name)
        self.assertEqual(
            [],
            contracts.validate_manifest_data(
                host_template.windows_development_host_template_manifest_to_data(
                    manifest
                ),
                host_template.HOST_TEMPLATE_NAME,
                self.validators,
            ),
        )

    def test_generated_files_keep_target_main_and_attachment_narrow(self) -> None:
        result = self.generate()
        assert result.generation is not None
        files = {value.path: value.content for value in result.generation.files}
        cmake = files[host_template.HOST_TEMPLATE_CMAKE_PATH].decode("utf-8")
        main = files[host_template.HOST_TEMPLATE_MAIN_PATH]

        self.assertFalse(cmake.encode("utf-8").startswith(b"\xef\xbb\xbf"))
        self.assertTrue(main.startswith(b"\xef\xbb\xbf"))
        main_text = main.removeprefix(b"\xef\xbb\xbf").decode("utf-8")
        self.assertIn("add_executable(asharia-generated-host", cmake)
        self.assertEqual(1, cmake.count("asharia_attach_static_composition("))
        self.assertIn("WIN32_EXECUTABLE FALSE", cmake)
        self.assertIn("asharia-host/bin/$<CONFIG>", cmake)
        self.assertIn("--asharia-verify-static-registration", main_text)
        self.assertEqual(
            1,
            main_text.count(
                "asharia::generated::recordStaticFactoryProviders(*recorder);"
            ),
        )
        self.assertIn("auto table = std::move(*recorder).finish();", main_text)
        self.assertIn("table->registrationSnapshot()", main_text)
        self.assertIn("static_factory_callback_table.hpp", main_text)
        for eligibility_api in (
            "activation_eligibility.hpp",
            "admitted_static_factory_recording.hpp",
            "admitPreRegistration",
            "recordAdmittedStaticFactoryProviders",
            "admitStaticFactoryActivation",
        ):
            self.assertNotIn(eligibility_api, main_text)
        self.assertNotIn("activate", main_text.casefold())
        self.assertNotIn("factory instance", main_text.casefold())

    def test_invalid_target_is_rejected_without_generation(self) -> None:
        result = self.generate("asharia::invalid")

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.generation)
        self.assertEqual(
            ["host-template.target-invalid"],
            [item.code for item in result.diagnostics],
        )

    def test_current_template_rejects_legacy_composition_contract(self) -> None:
        legacy = replace(
            self.composition.manifest,
            generation_id="sha256-" + "0" * 64,
            renderer_revision=2,
            provider_api="asharia-static-factory-provider-v1",
            integrity=composition.IntegrityRecord("sha256", "0" * 64),
        )
        legacy = replace(
            legacy,
            generation_id=composition._generation_id(legacy),
        )
        integrity = composition.compute_static_composition_root_manifest_integrity(
            legacy
        )
        legacy = replace(
            legacy,
            integrity=composition.IntegrityRecord(**integrity),
        )

        result = host_template.generate_windows_development_host_template(
            legacy,
            "asharia-generated-host",
            self.validators,
        )

        self.assertFalse(result.succeeded)
        self.assertIsNone(result.generation)
        self.assertEqual(
            {"static-composition.schema"},
            {item.code for item in result.diagnostics},
        )

    def test_target_name_changes_template_generation(self) -> None:
        first = self.generate("asharia-host-a")
        second = self.generate("asharia-host-b")

        assert first.generation is not None
        assert second.generation is not None
        self.assertNotEqual(
            first.generation.manifest.generation_id,
            second.generation.manifest.generation_id,
        )

    def test_generation_identity_integrity_and_layout_are_recomputed(self) -> None:
        result = self.generate()
        assert result.generation is not None
        generation = result.generation
        first_evidence = generation.manifest.files[0]
        malformed_layout = replace(
            first_evidence,
            role="registration-verification-main",
            media_type="text/x-c++src",
        )
        mutations = {
            "generation": replace(
                generation.manifest,
                generation_id="sha256-" + "f" * 64,
            ),
            "integrity": replace(
                generation.manifest,
                integrity=composition.IntegrityRecord("sha256", "e" * 64),
            ),
            "layout": replace(
                generation.manifest,
                files=(malformed_layout, *generation.manifest.files[1:]),
            ),
        }
        expected = {
            "generation": "host-template.generation-id-mismatch",
            "integrity": "host-template.integrity-mismatch",
            "layout": "host-template.files-not-normalized",
        }
        for name, manifest in mutations.items():
            with self.subTest(name=name):
                diagnostics = (
                    host_template.validate_windows_development_host_template_generation(
                        replace(generation, manifest=manifest),
                        self.validators,
                    )
                )
                self.assertIn(expected[name], {value.code for value in diagnostics})

    def test_self_consistent_file_evidence_cannot_replace_renderer_output(self) -> None:
        result = self.generate()
        assert result.generation is not None
        generation = result.generation
        generated = generation.files[0]
        modified = replace(generated, content=generated.content + b"# injected\n")
        integrity = contracts.compute_bytes_integrity(modified.content)
        evidence = replace(
            generation.manifest.files[0],
            size=len(modified.content),
            integrity=composition.IntegrityRecord(
                integrity["algorithm"], integrity["digest"]
            ),
        )
        manifest = replace(
            generation.manifest,
            files=(evidence, *generation.manifest.files[1:]),
        )
        manifest_integrity = (
            host_template.compute_windows_development_host_template_manifest_integrity(
                manifest
            )
        )
        manifest = replace(
            manifest,
            integrity=composition.IntegrityRecord(
                manifest_integrity["algorithm"], manifest_integrity["digest"]
            ),
        )

        diagnostics = (
            host_template.validate_windows_development_host_template_generation(
                replace(
                    generation,
                    manifest=manifest,
                    files=(modified, *generation.files[1:]),
                ),
                self.validators,
            )
        )

        self.assertIn(
            "host-template.rendered-output-mismatch",
            {value.code for value in diagnostics},
        )

    def test_publication_is_atomic_reusable_and_tamper_sensitive(self) -> None:
        result = self.generate()
        assert result.generation is not None
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "templates"
            first = host_template_publication.publish_windows_development_host_template(
                result.generation,
                root,
                self.validators,
            )
            second = host_template_publication.publish_windows_development_host_template(
                result.generation,
                root,
                self.validators,
            )

            self.assertTrue(first.succeeded)
            self.assertTrue(second.succeeded)
            assert first.receipt is not None
            assert second.receipt is not None
            self.assertFalse(first.receipt.reused)
            self.assertTrue(second.receipt.reused)
            self.assertEqual(first.receipt.generation_path, second.receipt.generation_path)

            cmake = first.receipt.generation_path / host_template.HOST_TEMPLATE_CMAKE_PATH
            cmake.write_bytes(cmake.read_bytes() + b"# tampered\n")
            tampered = (
                host_template_publication.publish_windows_development_host_template(
                    result.generation,
                    root,
                    self.validators,
                )
            )
            self.assertFalse(tampered.succeeded)
            self.assertEqual(
                ["host-template.publication-failed"],
                [item.code for item in tampered.diagnostics],
            )

    def test_closed_schema_rejects_unknown_template_fields(self) -> None:
        result = self.generate()
        assert result.generation is not None
        data = host_template.windows_development_host_template_manifest_to_data(
            result.generation.manifest
        )
        data["unexpected"] = True

        diagnostics = contracts.validate_manifest_data(
            data,
            host_template.HOST_TEMPLATE_NAME,
            self.validators,
        )

        self.assertEqual(["host-template.schema"], [item.code for item in diagnostics])


if __name__ == "__main__":
    unittest.main()
