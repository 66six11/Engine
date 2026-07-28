"""Focused deterministic Host executable template tests."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_executable_template as host_template
from tools import host_template_renderer
from tools import host_template_publication
from tools import package_resolver
from tools import static_composition_root as composition
from tools.tests import host_template_test_support as support
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


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
        self.assertEqual(3, manifest.renderer_revision)
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

    def test_composition_identity_can_follow_a_real_effective_session(self) -> None:
        profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-minimal.json").read_text(
                encoding="utf-8"
            )
        )
        snapshot = package_test_support.make_host_profile_snapshot(
            profile,
            path="profiles/minimal/asharia.host-profile.json",
        )
        distribution = package_test_support.make_engine_distribution(
            host_profile_snapshots=[snapshot]
        )
        project = {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [],
            "directFeatureSets": [],
            "packageOptions": [],
        }
        resolved = package_resolver.resolve_package_graph(
            project, distribution, (), self.validators
        )
        self.assertTrue(resolved.succeeded)
        assert resolved.lock is not None
        session = effective_session.plan_effective_session(
            distribution,
            project,
            resolved.lock,
            (),
            snapshot,
            self.validators,
        )
        self.assertTrue(session.succeeded)
        assert session.plan is not None

        generated = support.composition_generation(
            self.validators, session=session.plan
        )
        manifest = generated.manifest

        self.assertEqual(
            session.plan.session_fingerprint.algorithm,
            manifest.inputs.effective_session_integrity.algorithm,
        )
        self.assertEqual(
            session.plan.session_fingerprint.digest,
            manifest.inputs.effective_session_integrity.digest,
        )
        self.assertEqual(
            session.plan.verified_graph.engine_generation_id,
            manifest.engine_generation_id,
        )
        self.assertEqual(session.plan.host_kind, manifest.host_kind)
        self.assertEqual(session.plan.target_platform, manifest.target_platform)

    def test_renderer_revision_three_keeps_exact_output_bytes(self) -> None:
        outputs = {
            host_template.HOST_TEMPLATE_CMAKE_PATH: (
                host_template_renderer.render_windows_development_cmake(
                    "asharia-generated-host",
                    "sha256-" + "b" * 64,
                    "sha256-" + "a" * 64,
                )
            ),
            host_template.HOST_TEMPLATE_INTERNAL_HEADER_PATH: (
                host_template_renderer.render_internal_header()
            ),
            host_template.HOST_TEMPLATE_MAIN_PATH: host_template_renderer.render_main(),
            host_template.HOST_TEMPLATE_PROCESS_APPLICATION_PATH: (
                host_template_renderer.render_process_application_host()
            ),
            host_template.HOST_TEMPLATE_REGISTRATION_PATH: (
                host_template_renderer.render_registration_verification()
            ),
        }
        expected = {
            host_template.HOST_TEMPLATE_CMAKE_PATH: (
                1685,
                "f41cdb4c59add9803c948561aefdc426e08aa195c9c7e0c9c1f312d559317631",
            ),
            host_template.HOST_TEMPLATE_INTERNAL_HEADER_PATH: (
                227,
                "2b7009c9de51a93367f65db7db1ddb265e04260a1ae3ff6234a2e4295911c69d",
            ),
            host_template.HOST_TEMPLATE_MAIN_PATH: (
                1133,
                "10fca8c88c9dcff56179673fecd9bd512f97d0ba7fe634279f03c54bfbeaf5a8",
            ),
            host_template.HOST_TEMPLATE_PROCESS_APPLICATION_PATH: (
                4756,
                "07bced1fd352fb4e6221a654617292066f7785846f428c37a170fa8fb612911d",
            ),
            host_template.HOST_TEMPLATE_REGISTRATION_PATH: (
                1678,
                "7264488d33402e08d043f654af782952e285c3e9428c77feb41bcb9c28dcaf97",
            ),
        }

        self.assertEqual(3, host_template.HOST_TEMPLATE_RENDERER_REVISION)
        for path, content in outputs.items():
            with self.subTest(path=path):
                self.assertEqual(expected[path][0], len(content))
                self.assertEqual(expected[path][1], hashlib.sha256(content).hexdigest())

    def test_generated_files_separate_modes_and_keep_attachment_narrow(self) -> None:
        result = self.generate()
        assert result.generation is not None
        files = {value.path: value.content for value in result.generation.files}
        cmake = files[host_template.HOST_TEMPLATE_CMAKE_PATH].decode("utf-8")
        main = files[host_template.HOST_TEMPLATE_MAIN_PATH].decode("utf-8-sig")
        restricted = files[host_template.HOST_TEMPLATE_REGISTRATION_PATH].decode(
            "utf-8-sig"
        )
        normal = files[host_template.HOST_TEMPLATE_PROCESS_APPLICATION_PATH].decode(
            "utf-8-sig"
        )

        self.assertFalse(
            files[host_template.HOST_TEMPLATE_CMAKE_PATH].startswith(b"\xef\xbb\xbf")
        )
        for path, content in files.items():
            if path != host_template.HOST_TEMPLATE_CMAKE_PATH:
                self.assertTrue(content.startswith(b"\xef\xbb\xbf"), path)
        self.assertIn("add_executable(asharia-generated-host", cmake)
        self.assertIn("src/process_application_host.cpp", cmake)
        self.assertIn("src/registration_verification.cpp", cmake)
        self.assertEqual(1, cmake.count("asharia_attach_static_composition("))
        self.assertIn("WIN32_EXECUTABLE FALSE", cmake)
        self.assertIn("asharia-host/bin/$<CONFIG>", cmake)

        self.assertIn("--asharia-verify-static-registration", main)
        self.assertIn("runRegistrationVerification()", main)
        self.assertIn("runProcessApplicationHost(argc, argv)", main)
        self.assertEqual(
            1,
            restricted.count(
                "asharia::generated::recordStaticFactoryProviders(*recorder);"
            ),
        )
        self.assertIn("auto table = std::move(*recorder).finish();", restricted)
        self.assertIn("table->registrationSnapshot()", restricted)
        for forbidden in (
            "admitCurrentImagePreRegistration",
            "prepareProcessScopeExecutorV2",
            "ProcessApplicationV1",
            "tryBorrow",
        ):
            self.assertNotIn(forbidden, restricted)

        for expected_call in (
            "admitCurrentImagePreRegistration()",
            "recordAdmittedStaticFactoryProviders(",
            "admitStaticFactoryActivation(",
            "prepareProcessScopeExecutorV2(",
            "executor->start()",
            "single<asharia::host_runtime::ProcessApplicationV1>()",
            "applicationHandle->tryBorrow()",
            "application->get().run(",
            "stopAfterFailure(*executor",
            "executor->stop()",
        ):
            self.assertIn(expected_call, normal)

        self.assertIn("catch (...)", normal)
        self.assertIn(
            "The helper returns only after the synchronous contribution borrow is",
            normal,
        )
        self.assertIn(
            "const int reportedExitCode = fail(code, result.diagnosticMessage, exitCode);",
            normal,
        )
        self.assertIn(
            "return stopAfterFailure(*executor, reportedExitCode);",
            normal,
        )
        self.assertNotIn(
            "stopAfterFailure(*executor, code, result.diagnosticMessage",
            normal,
        )

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
            renderer_revision=5,
            provider_api="asharia-static-factory-provider-v4",
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
            role="host-main",
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
