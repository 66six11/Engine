"""Deterministic generated static composition-root tests."""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tools import check_package_contracts as contracts
from tools import host_activation_blueprint as activation
from tools import source_build_plan
from tools import static_composition_root as composition_root
from tools import static_factory_provider_bindings as provider_bindings
from tools.cmake_file_api import CMakeGeneratorEvidence, CMakeToolchainEvidence


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


def _digest(character: str) -> str:
    return character * 64


def _build_plan() -> source_build_plan.SourceBuildPlan:
    target = source_build_plan.BuildTargetReference(
        "asharia_synthetic_runtime", "STATIC_LIBRARY"
    )
    plan = source_build_plan.SourceBuildPlan(
        inputs=source_build_plan.SourceBuildInputs(
            host_composition_integrity=source_build_plan.IntegrityRecord(
                "sha256", _digest("1")
            ),
            descriptor_set_integrity=source_build_plan.IntegrityRecord(
                "sha256", _digest("2")
            ),
            topology_integrity=source_build_plan.IntegrityRecord(
                "sha256", _digest("3")
            ),
            codemodel_integrity=source_build_plan.IntegrityRecord(
                "sha256", _digest("4")
            ),
            configuration_integrity=source_build_plan.IntegrityRecord(
                "sha256", _digest("5")
            ),
        ),
        host_kind="minimal",
        target_platform="com.asharia.platform.windows-x86-64",
        configuration="Debug",
        generator=CMakeGeneratorEvidence("Ninja", False),
        toolchain=CMakeToolchainEvidence("Clang", "19.1.5", "Windows", "x86_64"),
        packages=(),
        build_roots=(target,),
        target_closure=(
            source_build_plan.TargetClosureEvidence(
                target.name, target.target_type, ()
            ),
        ),
        build_options=(),
        integrity=source_build_plan.IntegrityRecord("sha256", _digest("0")),
    )
    integrity = source_build_plan.compute_source_build_plan_integrity(plan)
    return replace(
        plan,
        integrity=source_build_plan.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def _blueprint() -> activation.HostActivationBlueprint:
    factory = activation.FactoryActivation(
        reference=activation.ExactFactoryReference(
            "com.asharia.synthetic",
            "1.0.0",
            "implementation",
            "runtime-service",
        ),
        requirements=(),
        contributions=(),
    )
    blueprint = activation.HostActivationBlueprint(
        inputs=activation.HostActivationBlueprintInputs(
            effective_session_integrity=activation.IntegrityRecord(
                "sha256", _digest("6")
            ),
            host_composition_integrity=activation.IntegrityRecord(
                "sha256", _digest("1")
            ),
            factory_declaration_set_integrity=activation.IntegrityRecord(
                "sha256", _digest("7")
            ),
        ),
        engine_generation_id="sha256-" + _digest("a"),
        host_kind="minimal",
        target_platform="com.asharia.platform.windows-x86-64",
        lifecycle_model=activation.LIFECYCLE_MODEL,
        scope_templates=(
            activation.ScopeActivationTemplate("process", None, (factory,)),
        ),
        integrity=activation.IntegrityRecord("sha256", _digest("0")),
    )
    integrity = activation.compute_host_activation_blueprint_integrity(blueprint)
    return replace(
        blueprint,
        integrity=activation.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def _binding_plan(
    build_plan: source_build_plan.SourceBuildPlan,
    blueprint: activation.HostActivationBlueprint,
) -> provider_bindings.StaticFactoryProviderBindingPlan:
    provider = provider_bindings.StaticFactoryProvider(
        package_id="com.asharia.synthetic",
        package_version="1.0.0",
        module_id="implementation",
        target=provider_bindings.ProviderTarget(
            "asharia_synthetic_runtime", "STATIC_LIBRARY"
        ),
        entry_point=provider_bindings.ProviderEntryPoint(
            "asharia/synthetic/runtime_provider.hpp",
            "asharia::synthetic::provideRuntimeFactories",
        ),
        factory_ids=("runtime-service",),
    )
    plan = provider_bindings.StaticFactoryProviderBindingPlan(
        inputs=provider_bindings.StaticFactoryProviderBindingInputs(
            effective_session_integrity=provider_bindings.IntegrityRecord(
                "sha256", _digest("6")
            ),
            source_build_plan_integrity=provider_bindings.IntegrityRecord(
                build_plan.integrity.algorithm, build_plan.integrity.digest
            ),
            host_activation_blueprint_integrity=provider_bindings.IntegrityRecord(
                blueprint.integrity.algorithm, blueprint.integrity.digest
            ),
            binding_declaration_set_integrity=provider_bindings.IntegrityRecord(
                "sha256", _digest("8")
            ),
        ),
        engine_generation_id=blueprint.engine_generation_id,
        host_kind=blueprint.host_kind,
        target_platform=blueprint.target_platform,
        providers=(provider,),
        integrity=provider_bindings.IntegrityRecord("sha256", _digest("0")),
    )
    integrity = (
        provider_bindings.compute_static_factory_provider_binding_plan_integrity(
            plan
        )
    )
    return replace(
        plan,
        integrity=provider_bindings.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


class StaticCompositionRootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    def generate(self) -> composition_root.StaticCompositionRootGeneration:
        build_plan = _build_plan()
        blueprint = _blueprint()
        result = composition_root.generate_static_composition_root(
            build_plan,
            blueprint,
            _binding_plan(build_plan, blueprint),
            self.validators,
        )
        self.assertTrue(result.succeeded, [item.render() for item in result.diagnostics])
        assert result.generation is not None
        return result.generation

    def test_generation_is_deterministic_and_records_exact_bytes(self) -> None:
        first = self.generate()
        second = self.generate()

        self.assertEqual(first, second)
        self.assertEqual(
            composition_root.render_static_composition_root_manifest(first.manifest),
            composition_root.render_static_composition_root_manifest(second.manifest),
        )
        self.assertRegex(first.manifest.generation_id, r"^sha256-[0-9a-f]{64}$")
        self.assertEqual(
            composition_root.STATIC_COMPOSITION_RENDERER_REVISION,
            first.manifest.renderer_revision,
        )
        revised_manifest = replace(
            first.manifest,
            renderer_revision=first.manifest.renderer_revision + 1,
        )
        self.assertNotEqual(
            first.manifest.generation_id,
            composition_root._generation_id(revised_manifest),
        )
        self.assertEqual(
            [],
            composition_root.validate_static_composition_root_generation(
                first, self.validators
            ),
        )
        files = {value.path: value.content for value in first.files}
        self.assertTrue(
            files[composition_root.STATIC_COMPOSITION_HEADER_PATH].startswith(
                b"\xef\xbb\xbf"
            )
        )
        self.assertTrue(
            files[composition_root.STATIC_COMPOSITION_SOURCE_PATH].startswith(
                b"\xef\xbb\xbf"
            )
        )
        source = files[composition_root.STATIC_COMPOSITION_SOURCE_PATH].decode(
            "utf-8-sig"
        )
        self.assertIn("static_assert(std::is_same_v<", source)
        self.assertIn("provideRuntimeFactories(registrar);", source)
        self.assertEqual(1, source.count("provideRuntimeFactories(registrar);"))
        for forbidden in (
            "GetProcAddress",
            "dlsym",
            "__DATE__",
            "__TIME__",
            '#include "src/',
            "unordered_map",
        ):
            self.assertNotIn(forbidden, source)
        cmake = files[composition_root.STATIC_COMPOSITION_CMAKE_PATH].decode("utf-8")
        self.assertIn("target_sources", cmake)
        self.assertIn("target_link_libraries", cmake)
        self.assertNotIn("add_executable", cmake)
        self.assertNotIn("add_custom_command", cmake)
        self.assertNotIn("add_subdirectory", cmake)
        self.assertNotIn("find_package", cmake)

    def test_generation_rejects_stale_cross_plan_fingerprint(self) -> None:
        build_plan = _build_plan()
        blueprint = _blueprint()
        binding = _binding_plan(build_plan, blueprint)
        stale_inputs = replace(
            binding.inputs,
            source_build_plan_integrity=provider_bindings.IntegrityRecord(
                "sha256", _digest("f")
            ),
        )
        stale = replace(
            binding,
            inputs=stale_inputs,
            integrity=provider_bindings.IntegrityRecord("sha256", _digest("0")),
        )
        integrity = (
            provider_bindings.compute_static_factory_provider_binding_plan_integrity(
                stale
            )
        )
        stale = replace(
            stale,
            integrity=provider_bindings.IntegrityRecord(
                integrity["algorithm"], integrity["digest"]
            ),
        )

        result = composition_root.generate_static_composition_root(
            build_plan, blueprint, stale, self.validators
        )

        self.assertFalse(result.succeeded)
        self.assertIn(
            "static-composition.source-build-plan-mismatch",
            {item.code for item in result.diagnostics},
        )

    def test_generation_rejects_mismatched_effective_session(self) -> None:
        build_plan = _build_plan()
        blueprint = _blueprint()
        binding = _binding_plan(build_plan, blueprint)
        stale = replace(
            binding,
            inputs=replace(
                binding.inputs,
                effective_session_integrity=provider_bindings.IntegrityRecord(
                    "sha256", _digest("f")
                ),
            ),
            integrity=provider_bindings.IntegrityRecord("sha256", _digest("0")),
        )
        integrity = (
            provider_bindings.compute_static_factory_provider_binding_plan_integrity(
                stale
            )
        )
        stale = replace(
            stale,
            integrity=provider_bindings.IntegrityRecord(
                integrity["algorithm"], integrity["digest"]
            ),
        )

        result = composition_root.generate_static_composition_root(
            build_plan, blueprint, stale, self.validators
        )

        self.assertFalse(result.succeeded)
        self.assertIn(
            "static-composition.effective-session-mismatch",
            {item.code for item in result.diagnostics},
        )

    def test_provider_change_invalidates_only_generated_handoff_bytes(self) -> None:
        original = self.generate()
        build_plan = _build_plan()
        blueprint = _blueprint()
        binding = _binding_plan(build_plan, blueprint)
        changed_provider = replace(
            binding.providers[0],
            entry_point=provider_bindings.ProviderEntryPoint(
                "asharia/synthetic/runtime_provider.hpp",
                "asharia::synthetic::provideAlternateFactories",
            ),
        )
        changed_binding = replace(
            binding,
            providers=(changed_provider,),
            integrity=provider_bindings.IntegrityRecord("sha256", _digest("0")),
        )
        integrity = (
            provider_bindings.compute_static_factory_provider_binding_plan_integrity(
                changed_binding
            )
        )
        changed_binding = replace(
            changed_binding,
            integrity=provider_bindings.IntegrityRecord(
                integrity["algorithm"], integrity["digest"]
            ),
        )
        changed_result = composition_root.generate_static_composition_root(
            build_plan, blueprint, changed_binding, self.validators
        )
        self.assertTrue(
            changed_result.succeeded,
            [item.render() for item in changed_result.diagnostics],
        )
        assert changed_result.generation is not None
        changed = changed_result.generation
        original_files = {value.path: value.content for value in original.files}
        changed_files = {value.path: value.content for value in changed.files}

        self.assertNotEqual(original.manifest.generation_id, changed.manifest.generation_id)
        self.assertEqual(
            original_files[composition_root.STATIC_COMPOSITION_HEADER_PATH],
            changed_files[composition_root.STATIC_COMPOSITION_HEADER_PATH],
        )
        self.assertNotEqual(
            original_files[composition_root.STATIC_COMPOSITION_SOURCE_PATH],
            changed_files[composition_root.STATIC_COMPOSITION_SOURCE_PATH],
        )

    def test_manifest_rejects_tampering(self) -> None:
        generation = self.generate()
        data = composition_root.static_composition_root_manifest_to_data(
            generation.manifest
        )
        data["configuration"]["name"] = "Release"

        diagnostics = composition_root.validate_static_composition_root_manifest_data(
            data, self.validators
        )

        self.assertTrue(
            {
                "static-composition.generation-id-mismatch",
                "static-composition.integrity-mismatch",
            }.issubset({item.code for item in diagnostics})
        )

    def test_publication_is_atomic_reusable_and_fail_closed(self) -> None:
        generation = self.generate()
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "static-composition"
            first = composition_root.publish_static_composition_root(
                generation, root, self.validators
            )
            self.assertTrue(first.succeeded)
            assert first.receipt is not None
            source = (
                first.receipt.generation_path
                / composition_root.STATIC_COMPOSITION_SOURCE_PATH
            )
            stable_modified_time = source.stat().st_mtime_ns
            second = composition_root.publish_static_composition_root(
                generation, root, self.validators
            )

            self.assertTrue(second.succeeded)
            assert second.receipt is not None
            self.assertFalse(first.receipt.reused)
            self.assertTrue(second.receipt.reused)
            self.assertEqual(first.receipt.generation_path, second.receipt.generation_path)

            self.assertEqual(stable_modified_time, source.stat().st_mtime_ns)
            source.write_bytes(source.read_bytes() + b"tampered")
            conflict = composition_root.publish_static_composition_root(
                generation, root, self.validators
            )
            self.assertFalse(conflict.succeeded)
            self.assertEqual(
                {"static-composition.publication-failed"},
                {item.code for item in conflict.diagnostics},
            )
            self.assertFalse(any(path.name.startswith(".asharia-") for path in root.iterdir()))

    def test_invalid_output_path_is_rejected_before_publication(self) -> None:
        generation = self.generate()
        invalid_evidence = replace(
            generation.manifest.files[0], path="../escape.cmake"
        )
        invalid_manifest = replace(
            generation.manifest,
            files=(invalid_evidence, *generation.manifest.files[1:]),
        )
        invalid_generation = replace(generation, manifest=invalid_manifest)
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "static-composition"
            result = composition_root.publish_static_composition_root(
                invalid_generation, root, self.validators
            )

            self.assertFalse(result.succeeded)
            self.assertIn(
                "static-composition.schema",
                {item.code for item in result.diagnostics},
            )
            self.assertFalse(root.exists())

    def test_publication_rejects_link_root_when_supported(self) -> None:
        generation = self.generate()
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            real_root = root / "real"
            real_root.mkdir()
            link_root = root / "linked"
            try:
                link_root.symlink_to(real_root, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"directory symlinks are unavailable: {error}")

            result = composition_root.publish_static_composition_root(
                generation, link_root, self.validators
            )

            self.assertFalse(result.succeeded)
            self.assertEqual(
                {"static-composition.publication-failed"},
                {item.code for item in result.diagnostics},
            )
            self.assertEqual([], list(real_root.iterdir()))

    def test_windows_reparse_attribute_is_treated_as_a_link(self) -> None:
        class FakeStatus:
            st_mode = stat.S_IFDIR
            st_file_attributes = getattr(
                stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400
            )

        self.assertTrue(composition_root._is_link_or_reparse(FakeStatus()))

    @unittest.skipUnless(
        os.environ.get("ASHARIA_RUN_CMAKE_INTEGRATION_TESTS") == "1",
        "set ASHARIA_RUN_CMAKE_INTEGRATION_TESTS=1 to compile the generated root",
    )
    def test_generated_fragment_configures_compiles_and_links(self) -> None:
        if shutil.which("cmake") is None or shutil.which("ninja") is None:
            self.skipTest("CMake and Ninja are required")
        generation = self.generate()
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            published = composition_root.publish_static_composition_root(
                generation, root / "generated", self.validators
            )
            self.assertTrue(published.succeeded)
            assert published.receipt is not None
            source_root = root / "fixture"
            source_root.mkdir()
            (source_root / "include/asharia/synthetic").mkdir(parents=True)
            (source_root / "include/asharia/synthetic/runtime_provider.hpp").write_text(
                """#pragma once
#include \"asharia/host_runtime/static_factory_provider.hpp\"
namespace asharia::synthetic {
void provideRuntimeFactories(
    asharia::host_runtime::StaticFactoryRegistrar& registrar) noexcept;
}
""",
                encoding="utf-8-sig",
                newline="\n",
            )
            (source_root / "provider.cpp").write_text(
                """#include \"asharia/synthetic/runtime_provider.hpp\"
void asharia::synthetic::provideRuntimeFactories(
    asharia::host_runtime::StaticFactoryRegistrar& registrar) noexcept {
  (void)registrar;
}
""",
                encoding="utf-8-sig",
                newline="\n",
            )
            (source_root / "main.cpp").write_text(
                "int main() { return 0; }\n",
                encoding="utf-8-sig",
                newline="\n",
            )
            generation_path = published.receipt.generation_path.as_posix()
            contract_include = (
                REPOSITORY_ROOT / "engine/host-runtime/include"
            ).as_posix()
            cmake = f"""cmake_minimum_required(VERSION 3.28)
project(AshariaStaticCompositionFixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
add_library(asharia-host-runtime-contract INTERFACE)
add_library(asharia::host_runtime_contract ALIAS asharia-host-runtime-contract)
target_include_directories(asharia-host-runtime-contract INTERFACE "{contract_include}")
add_library(asharia_synthetic_runtime STATIC provider.cpp)
target_include_directories(asharia_synthetic_runtime PUBLIC include)
target_link_libraries(asharia_synthetic_runtime PUBLIC asharia::host_runtime_contract)
add_executable(host main.cpp)
include("{generation_path}/asharia-static-composition.cmake")
asharia_attach_static_composition(host)
"""
            (source_root / "CMakeLists.txt").write_text(
                cmake, encoding="utf-8", newline="\n"
            )
            build_root = root / "build"
            configure = subprocess.run(
                ["cmake", "-S", str(source_root), "-B", str(build_root), "-G", "Ninja"],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            self.assertEqual(0, configure.returncode, configure.stdout + configure.stderr)
            expected_compiler = os.environ.get(
                "ASHARIA_EXPECT_CMAKE_CXX_COMPILER"
            )
            if expected_compiler is not None:
                cache = (build_root / "CMakeCache.txt").read_text(
                    encoding="utf-8", errors="replace"
                )
                self.assertIn(expected_compiler.casefold(), cache.casefold())
            build = subprocess.run(
                ["cmake", "--build", str(build_root)],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            self.assertEqual(0, build.returncode, build.stdout + build.stderr)

            provider_header = (
                source_root / "include/asharia/synthetic/runtime_provider.hpp"
            )
            provider_source = source_root / "provider.cpp"
            provider_header.write_text(
                provider_header.read_text(encoding="utf-8-sig").replace(
                    ") noexcept;", ");"
                ),
                encoding="utf-8-sig",
                newline="\n",
            )
            provider_source.write_text(
                provider_source.read_text(encoding="utf-8-sig").replace(
                    ") noexcept {", ") {"
                ),
                encoding="utf-8-sig",
                newline="\n",
            )
            mismatched_root = root / "build-mismatched-signature"
            configure = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source_root),
                    "-B",
                    str(mismatched_root),
                    "-G",
                    "Ninja",
                ],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            self.assertEqual(0, configure.returncode, configure.stdout + configure.stderr)
            mismatched = subprocess.run(
                ["cmake", "--build", str(mismatched_root)],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            mismatch_output = mismatched.stdout + mismatched.stderr
            self.assertNotEqual(0, mismatched.returncode, mismatch_output)
            self.assertTrue(
                "static_assert" in mismatch_output
                or "StaticFactoryProviderV1" in mismatch_output
                or "static_composition_root.cpp" in mismatch_output,
                mismatch_output,
            )

            duplicate_cmake = cmake + "asharia_attach_static_composition(host)\n"
            (source_root / "CMakeLists.txt").write_text(
                duplicate_cmake, encoding="utf-8", newline="\n"
            )
            duplicate = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source_root),
                    "-B",
                    str(root / "build-duplicate"),
                    "-G",
                    "Ninja",
                ],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            duplicate_output = duplicate.stdout + duplicate.stderr
            self.assertNotEqual(0, duplicate.returncode, duplicate_output)
            self.assertIn("already attached", duplicate_output)

            wrong_type_cmake = cmake.replace(
                "add_library(asharia_synthetic_runtime STATIC provider.cpp)",
                "add_library(asharia_synthetic_runtime SHARED provider.cpp)",
            )
            (source_root / "CMakeLists.txt").write_text(
                wrong_type_cmake, encoding="utf-8", newline="\n"
            )
            wrong_type = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source_root),
                    "-B",
                    str(root / "build-wrong-type"),
                    "-G",
                    "Ninja",
                ],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            wrong_type_output = wrong_type.stdout + wrong_type.stderr
            self.assertNotEqual(0, wrong_type.returncode, wrong_type_output)
            self.assertIn("must be a STATIC_LIBRARY", wrong_type_output)

            missing_provider_cmake = cmake
            for line in (
                "add_library(asharia_synthetic_runtime STATIC provider.cpp)\n",
                "target_include_directories(asharia_synthetic_runtime PUBLIC include)\n",
                "target_link_libraries(asharia_synthetic_runtime PUBLIC asharia::host_runtime_contract)\n",
            ):
                missing_provider_cmake = missing_provider_cmake.replace(line, "")
            (source_root / "CMakeLists.txt").write_text(
                missing_provider_cmake, encoding="utf-8", newline="\n"
            )
            missing_provider = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source_root),
                    "-B",
                    str(root / "build-missing-provider"),
                    "-G",
                    "Ninja",
                ],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            missing_provider_output = (
                missing_provider.stdout + missing_provider.stderr
            )
            self.assertNotEqual(
                0, missing_provider.returncode, missing_provider_output
            )
            self.assertIn("Static provider target", missing_provider_output)
            self.assertIn("does not exist", missing_provider_output)

            missing_host_cmake = cmake.replace(
                "add_executable(host main.cpp)", ""
            )
            (source_root / "CMakeLists.txt").write_text(
                missing_host_cmake, encoding="utf-8", newline="\n"
            )
            missing_host = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source_root),
                    "-B",
                    str(root / "build-missing-host"),
                    "-G",
                    "Ninja",
                ],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
            missing_host_output = missing_host.stdout + missing_host.stderr
            self.assertNotEqual(0, missing_host.returncode, missing_host_output)
            self.assertIn("does not exist", missing_host_output)


if __name__ == "__main__":
    unittest.main()
