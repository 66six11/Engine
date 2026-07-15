"""Pure final Host build-request validation and argument tests."""

from __future__ import annotations

import tempfile
import unittest
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
from typing import Iterator

from tools import check_package_contracts as contracts
from tools import host_build_request
from tools import host_executable_template as host_template
from tools import host_template_publication
from tools import static_composition_root as composition
from tools.tests import host_template_test_support as support


@contextmanager
def build_request_fixture() -> Iterator[
    tuple[
        host_build_request.FinalHostBuildRequestV1,
        contracts.ContractValidators,
    ]
]:
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        source_root = root / "source"
        host_root = root / "host-template"
        composition_root = root / "static-composition"
        source_root.mkdir()
        cmake_executable = root / "cmake.exe"
        toolchain_file = root / "toolchain.cmake"
        cmake_executable.write_bytes(b"cmake")
        toolchain_file.write_bytes(b"toolchain")

        validators = contracts.load_contract_validators()
        composition_generation = support.composition_generation(validators)
        template_result = host_template.generate_windows_development_host_template(
            composition_generation.manifest,
            "asharia-synthetic-host",
            validators,
        )
        assert template_result.generation is not None
        template_generation = template_result.generation
        composition_publication = composition.publish_static_composition_root(
            composition_generation,
            composition_root,
            validators,
        )
        template_publication = (
            host_template_publication.publish_windows_development_host_template(
                template_generation,
                host_root,
                validators,
            )
        )
        assert composition_publication.receipt is not None
        assert template_publication.receipt is not None
        request = host_build_request.FinalHostBuildRequestV1(
            cmake_executable=cmake_executable,
            source_root=source_root,
            build_root=root / "build",
            host_template_root=template_publication.receipt.generation_path,
            static_composition_root=composition_publication.receipt.generation_path,
            toolchain_file=toolchain_file,
            host_template_generation=template_generation,
            static_composition_generation=composition_generation,
            target_name=template_generation.manifest.target_name,
            configuration=composition_generation.manifest.configuration,
            generator_name=composition_generation.manifest.generator_name,
            generator_multi_config=(
                composition_generation.manifest.generator_multi_config
            ),
            parallel_jobs=8,
            enable_clang_tidy=False,
            environment=(("PATH", "tools"), ("SYSTEMROOT", "windows")),
        )
        yield request, validators


def validate(
    request: object,
    validators: contracts.ContractValidators,
) -> tuple[
    host_build_request.ValidatedFinalHostBuildRequestV1 | None,
    list[contracts.Diagnostic],
]:
    return host_build_request.validate_final_host_build_request(request, validators)


class FinalHostBuildRequestTests(unittest.TestCase):
    def test_valid_request_normalizes_paths_environment_and_publications(self) -> None:
        with build_request_fixture() as (request, validators):
            validated, diagnostics = validate(request, validators)

            self.assertEqual([], diagnostics)
            assert validated is not None
            self.assertTrue(validated.cmake_executable.is_absolute())
            self.assertTrue(validated.source_root.is_absolute())
            self.assertEqual(request.build_root.absolute(), validated.build_root)
            self.assertEqual(
                {"PATH": "tools", "SYSTEMROOT": "windows"},
                validated.environment,
            )

    def test_single_config_arguments_are_explicit_and_shell_free(self) -> None:
        with build_request_fixture() as (request, validators):
            validated, diagnostics = validate(request, validators)
            self.assertEqual([], diagnostics)
            assert validated is not None

            configure = host_build_request.configure_final_host_arguments(
                request, validated, validated.build_root
            )
            build = host_build_request.build_final_host_arguments(
                request, validated, validated.build_root
            )

            self.assertEqual(str(validated.cmake_executable), configure[0])
            self.assertIn("-DCMAKE_BUILD_TYPE:STRING=Debug", configure)
            self.assertIn("-DASHARIA_BUILD_APPS:BOOL=OFF", configure)
            self.assertIn("-DASHARIA_BUILD_TESTS:BOOL=OFF", configure)
            self.assertEqual(
                (
                    str(validated.cmake_executable),
                    "--build",
                    str(validated.build_root),
                    "--target",
                    request.target_name,
                    "--parallel",
                    "8",
                ),
                build,
            )
            self.assertNotIn("--config", build)

    def test_multi_config_arguments_select_config_only_during_build(self) -> None:
        with build_request_fixture() as (request, validators):
            validated, diagnostics = validate(request, validators)
            self.assertEqual([], diagnostics)
            assert validated is not None
            multi_config = replace(request, generator_multi_config=True)

            configure = host_build_request.configure_final_host_arguments(
                multi_config, validated, validated.build_root
            )
            build = host_build_request.build_final_host_arguments(
                multi_config, validated, validated.build_root
            )

            self.assertFalse(
                any(value.startswith("-DCMAKE_BUILD_TYPE") for value in configure)
            )
            self.assertEqual(
                ("--config", "Debug"),
                build[build.index("--config") : build.index("--config") + 2],
            )

    def test_environment_parallelism_and_path_failures_are_stable(self) -> None:
        with build_request_fixture() as (request, validators):
            invalid = replace(
                request,
                toolchain_file=request.source_root / "missing.cmake",
                parallel_jobs=0,
                configure_timeout_seconds=float("nan"),
                environment=(("PATH", "first"), ("path", "second")),
            )

            first = validate(invalid, validators)
            second = validate(invalid, validators)

            self.assertIsNone(first[0])
            self.assertEqual(first[1], second[1])
            self.assertEqual(
                {
                    "host-build.environment-duplicate",
                    "host-build.parallelism-invalid",
                    "host-build.path-invalid",
                    "host-build.timeout-invalid",
                },
                {value.code for value in first[1]},
            )

    def test_every_host_template_publication_file_is_reverified(self) -> None:
        with build_request_fixture() as (request, validators):
            expected = host_template.expected_host_template_publication_files(
                request.host_template_generation
            )
            for relative in expected:
                with self.subTest(relative=relative):
                    published = request.host_template_root / relative
                    original = published.read_bytes()
                    published.write_bytes(original + b" ")
                    try:
                        validated, diagnostics = validate(request, validators)
                    finally:
                        published.write_bytes(original)

                    self.assertIsNone(validated)
                    self.assertEqual(
                        ["host-build.template-publication-stale"],
                        [value.code for value in diagnostics],
                    )

    def test_static_composition_publication_is_closed_and_exact(self) -> None:
        with build_request_fixture() as (request, validators):
            expected = composition.expected_static_composition_publication_files(
                request.static_composition_generation
            )
            for relative in expected:
                with self.subTest(relative=relative):
                    published = request.static_composition_root / relative
                    original = published.read_bytes()
                    published.write_bytes(original + b" ")
                    try:
                        validated, diagnostics = validate(request, validators)
                    finally:
                        published.write_bytes(original)

                    self.assertIsNone(validated)
                    self.assertEqual(
                        ["host-build.composition-publication-stale"],
                        [value.code for value in diagnostics],
                    )

            unexpected = request.static_composition_root / "unexpected.txt"
            unexpected.write_text("unexpected", encoding="utf-8")
            try:
                validated, diagnostics = validate(request, validators)
            finally:
                unexpected.unlink()

            self.assertIsNone(validated)
            self.assertEqual(
                ["host-build.composition-publication-stale"],
                [value.code for value in diagnostics],
            )

    def test_two_individually_valid_generations_cannot_be_crossed(self) -> None:
        with build_request_fixture() as (request, validators):
            alternate = support.composition_generation(validators, "19.1.6")
            publication = composition.publish_static_composition_root(
                alternate,
                request.static_composition_root.parent,
                validators,
            )
            assert publication.receipt is not None

            validated, diagnostics = validate(
                replace(
                    request,
                    static_composition_generation=alternate,
                    static_composition_root=publication.receipt.generation_path,
                ),
                validators,
            )

            self.assertIsNone(validated)
            self.assertEqual(
                ["host-build.composition-stale"],
                [value.code for value in diagnostics],
            )

    def test_legacy_renderer_fails_before_cmake(self) -> None:
        with build_request_fixture() as (request, validators):
            legacy_template = replace(
                request.host_template_generation,
                manifest=replace(
                    request.host_template_generation.manifest,
                    renderer_revision=1,
                ),
            )

            validated, diagnostics = validate(
                replace(request, host_template_generation=legacy_template),
                validators,
            )

            self.assertIsNone(validated)
            self.assertIn(
                "host-template.schema",
                {value.code for value in diagnostics},
            )

if __name__ == "__main__":
    unittest.main()
