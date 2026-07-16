"""Dual-compiler synthetic final Host build and verification integration."""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import check_package_contracts as contracts
from tools import host_binding_generation_verifier
from tools import host_binding_publication
from tools import host_build_adapter
from tools import host_executable_template as host_template
from tools import host_registration_verification
from tools import host_template_publication
from tools import static_composition_root
from tools.tests import host_template_test_support as support


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    os.environ.get("ASHARIA_RUN_HOST_TEMPLATE_INTEGRATION_TESTS") == "1",
    "set ASHARIA_RUN_HOST_TEMPLATE_INTEGRATION_TESTS=1 to build the generated Host",
)
class GeneratedHostExecutableIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()

    @staticmethod
    def write_fixture_source(source_root: Path) -> None:
        (source_root / "include/asharia/synthetic").mkdir(parents=True)
        (source_root / "include/asharia/synthetic/runtime_provider.hpp").write_text(
            """#pragma once
#include "asharia/host_runtime/static_factory_provider.hpp"
namespace asharia::synthetic {
void provideRuntimeFactories(
    asharia::host_runtime::StaticFactoryRegistrar& registrar) noexcept;
}
""",
            encoding="utf-8-sig",
            newline="\n",
        )
        (source_root / "provider.cpp").write_text(
            """#include <array>
#include <cstdlib>
#include <string_view>

#include "asharia/synthetic/runtime_provider.hpp"

namespace {

struct SyntheticRuntimeServiceContract final {
  static constexpr std::string_view kind{
      "com.asharia.contribution.synthetic-service"};
  static constexpr asharia::host_runtime::StaticContributionCardinalityV1 cardinality{
      asharia::host_runtime::StaticContributionCardinalityV1::Single};
};

[[nodiscard]] SyntheticRuntimeServiceContract* abortRuntimeServiceAccessor(
    asharia::host_runtime::FactoryInstanceViewV1) noexcept {
  std::abort();
}

constexpr std::array<asharia::host_runtime::StaticContributionBindingV2, 1>
    kRuntimeServiceContributions{
    asharia::host_runtime::bindStaticContributionV2<
        SyntheticRuntimeServiceContract,
        &abortRuntimeServiceAccessor>(
        "com.asharia.contribution.synthetic-runtime"),
};

// Registration-only verification must never invoke payload accessors or lifecycle callbacks.
asharia::host_runtime::FactoryCreateResultV1 createRuntimeService(
    asharia::host_runtime::FactoryCreateContextV1&) noexcept {
  std::abort();
}

asharia::host_runtime::FactoryCallbackResultV1 activateRuntimeService(
    asharia::host_runtime::FactoryActivateContextV1&,
    asharia::host_runtime::FactoryInstanceViewV1) noexcept {
  std::abort();
}

asharia::host_runtime::FactoryCallbackResultV1 quiesceRuntimeService(
    asharia::host_runtime::FactoryQuiesceContextV1&,
    asharia::host_runtime::FactoryInstanceViewV1) noexcept {
  std::abort();
}

asharia::host_runtime::FactoryCallbackResultV1 deactivateRuntimeService(
    asharia::host_runtime::FactoryDeactivateContextV1&,
    asharia::host_runtime::FactoryInstanceViewV1) noexcept {
  std::abort();
}

void destroyRuntimeService(
    asharia::host_runtime::FactoryInstanceTokenV1) noexcept {
  std::abort();
}

constexpr asharia::host_runtime::StaticFactoryCallbacksV1 kRuntimeServiceCallbacks{
    .create = &createRuntimeService,
    .activate = &activateRuntimeService,
    .quiesce = &quiesceRuntimeService,
    .deactivate = &deactivateRuntimeService,
    .destroy = &destroyRuntimeService,
};

} // namespace

void asharia::synthetic::provideRuntimeFactories(
    asharia::host_runtime::StaticFactoryRegistrar& registrar) noexcept {
  registrar.registerFactory(
      "runtime-service", kRuntimeServiceCallbacks,
      kRuntimeServiceContributions);
}
""",
            encoding="utf-8-sig",
            newline="\n",
        )
        repository_root = REPOSITORY_ROOT.as_posix()
        (source_root / "CMakeLists.txt").write_text(
            f"""cmake_minimum_required(VERSION 3.28)
project(AshariaGeneratedHostFixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(ASHARIA_REPOSITORY_ROOT "{repository_root}")
list(APPEND CMAKE_MODULE_PATH "{repository_root}/cmake")
include(AshariaCompilerOptions)
include(AshariaGeneratedHost)
set(ASHARIA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("{repository_root}/engine/host-runtime" asharia-host-runtime)
add_library(asharia_synthetic_runtime STATIC provider.cpp)
target_include_directories(asharia_synthetic_runtime PUBLIC include)
target_link_libraries(asharia_synthetic_runtime
    PUBLIC asharia::host_runtime_contract
    PRIVATE asharia::host_runtime_provider_bridge)
asharia_configure_target(asharia_synthetic_runtime)
asharia_include_generated_host_template()
""",
            encoding="utf-8",
            newline="\n",
        )

    def test_exact_host_build_and_restricted_verification(self) -> None:
        cmake_program = shutil.which("cmake")
        ninja_program = shutil.which("ninja")
        toolchain_value = os.environ.get("ASHARIA_HOST_TEST_TOOLCHAIN_FILE")
        if cmake_program is None or ninja_program is None:
            self.skipTest("CMake and Ninja are required")
        if toolchain_value is None:
            self.skipTest("ASHARIA_HOST_TEST_TOOLCHAIN_FILE must name the Conan toolchain")
        toolchain_file = Path(toolchain_value)
        if not toolchain_file.is_file():
            self.skipTest(f"Conan toolchain does not exist: {toolchain_file}")

        expected_compiler_id = os.environ.get(
            "ASHARIA_EXPECT_CMAKE_CXX_COMPILER_ID"
        )
        expected_compiler_version = os.environ.get(
            "ASHARIA_EXPECT_CMAKE_CXX_COMPILER_VERSION"
        )
        self.assertIsNotNone(
            expected_compiler_id,
            "binding integration requires the expected CMake compiler ID",
        )
        self.assertIsNotNone(
            expected_compiler_version,
            "binding integration requires the expected CMake compiler version",
        )
        assert expected_compiler_id is not None
        assert expected_compiler_version is not None
        composition = support.composition_generation(
            self.validators,
            compiler_version=expected_compiler_version,
            compiler_id=expected_compiler_id,
        )
        generated_template = (
            host_template.generate_windows_development_host_template(
                composition.manifest,
                "asharia-generated-host",
                self.validators,
            )
        )
        self.assertTrue(generated_template.succeeded)
        assert generated_template.generation is not None

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            composition_publication = (
                static_composition_root.publish_static_composition_root(
                    composition,
                    root / "static-compositions",
                    self.validators,
                )
            )
            template_publication = (
                host_template_publication.publish_windows_development_host_template(
                    generated_template.generation,
                    root / "host-templates",
                    self.validators,
                )
            )
            self.assertTrue(composition_publication.succeeded)
            self.assertTrue(template_publication.succeeded)
            assert composition_publication.receipt is not None
            assert template_publication.receipt is not None

            source_root = root / "fixture"
            source_root.mkdir()
            self.write_fixture_source(source_root)
            environment = tuple(
                sorted(os.environ.items(), key=lambda item: item[0].casefold())
            )
            request = host_build_adapter.FinalHostBuildRequestV1(
                cmake_executable=Path(cmake_program),
                source_root=source_root,
                build_root=root / "build",
                host_template_root=template_publication.receipt.generation_path,
                static_composition_root=(
                    composition_publication.receipt.generation_path
                ),
                toolchain_file=toolchain_file,
                host_template_generation=generated_template.generation,
                static_composition_generation=composition,
                target_name="asharia-generated-host",
                configuration="Debug",
                generator_name="Ninja",
                generator_multi_config=False,
                parallel_jobs=min(os.cpu_count() or 1, 8),
                enable_clang_tidy=(
                    os.environ.get("ASHARIA_HOST_TEST_ENABLE_CLANG_TIDY") == "1"
                ),
                environment=environment,
            )
            built = host_build_adapter.run_final_host_build(
                request,
                self.validators,
            )
            self.assertTrue(
                built.succeeded,
                [item.render() for item in built.diagnostics],
            )
            assert built.target is not None
            self.assertEqual("EXECUTABLE", built.target.target_type)
            self.assertEqual("Debug", built.target.configuration)
            self.assertEqual(
                "asharia-host/bin/Debug/asharia-generated-host.exe",
                built.target.artifact_relative_path,
            )
            self.assertTrue(built.target.artifact_path.is_file())

            compiler_facts = list(
                (built.target.build_root / "CMakeFiles").glob(
                    "*/CMakeCXXCompiler.cmake"
                )
            )
            self.assertEqual(1, len(compiler_facts), compiler_facts)
            compiler_data = compiler_facts[0].read_text(
                encoding="utf-8",
                errors="replace",
            )
            self.assertIn(
                f'set(CMAKE_CXX_COMPILER_ID "{expected_compiler_id}")',
                compiler_data,
            )
            self.assertIn(
                f'set(CMAKE_CXX_COMPILER_VERSION "{expected_compiler_version}")',
                compiler_data,
            )

            verified = (
                host_registration_verification.run_host_registration_verification(
                    host_registration_verification.HostRegistrationVerificationRequestV1(
                        target=built.target,
                        expected_generation_id=composition.manifest.generation_id,
                        expected_host_activation_blueprint_sha256=(
                            composition.manifest.inputs.host_activation_blueprint_integrity.digest
                        ),
                        environment=environment,
                    ),
                    self.validators,
                )
            )
            self.assertTrue(
                verified.succeeded,
                [item.render() for item in verified.diagnostics],
            )
            assert verified.snapshot is not None
            self.assertEqual(1, len(verified.snapshot.registrations))
            self.assertEqual(
                "runtime-service",
                verified.snapshot.registrations[0].factory_id,
            )

            binding_publication = (
                host_binding_publication.collect_and_publish_host_executable_binding(
                    host_binding_publication.HostExecutableBindingPublicationRequestV1(
                        composition_generation=composition,
                        composition_root=(
                            composition_publication.receipt.generation_path
                        ),
                        template_generation=generated_template.generation,
                        template_root=template_publication.receipt.generation_path,
                        target=built.target,
                        registration_snapshot=verified.snapshot,
                        environment=environment,
                    ),
                    root / "host-bindings",
                    self.validators,
                )
            )
            self.assertTrue(
                binding_publication.succeeded,
                [item.render() for item in binding_publication.diagnostics],
            )
            assert binding_publication.receipt is not None
            receipt = binding_publication.receipt
            artifact_hash = hashlib.sha256()
            with built.target.artifact_path.open("rb") as stream:
                while chunk := stream.read(1024 * 1024):
                    artifact_hash.update(chunk)
            self.assertEqual(
                built.target.artifact_path.stat().st_size,
                receipt.receipt.artifact.size,
            )
            self.assertEqual(
                artifact_hash.hexdigest(),
                receipt.receipt.artifact.integrity.digest,
            )
            deep_verification = (
                host_binding_generation_verifier.verify_published_host_binding_generation(
                    receipt.generation_path,
                    composition,
                    generated_template.generation,
                    self.validators,
                )
            )
            self.assertTrue(
                deep_verification.succeeded,
                [item.render() for item in deep_verification.diagnostics],
            )

            invalid_mode = subprocess.run(
                [str(built.target.artifact_path)],
                cwd=built.target.build_root,
                env=dict(environment),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
                shell=False,
            )
            self.assertEqual(64, invalid_mode.returncode)
            self.assertEqual(b"", invalid_mode.stdout)
            self.assertEqual(
                ["host-verification.invalid-arguments"],
                invalid_mode.stderr.decode("utf-8").splitlines(),
            )


if __name__ == "__main__":
    unittest.main()
