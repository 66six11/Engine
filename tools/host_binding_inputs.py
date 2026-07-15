"""Shared cross-generation checks for Host executable binding evidence."""

from __future__ import annotations

from typing import Any

from tools import check_package_contracts as contracts
from tools import host_cmake_toolchain
from tools import host_executable_binding as binding
from tools import host_executable_template as host_template
from tools import static_composition_root as composition


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code,
        binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
        pointer,
        message,
    )


def _integrity_key(value: Any) -> tuple[str, str]:
    return value.algorithm, value.digest


def composition_template_diagnostics(
    composition_generation: composition.StaticCompositionRootGeneration,
    template_generation: host_template.WindowsDevelopmentHostTemplateGenerationV1,
) -> list[contracts.Diagnostic]:
    composition_manifest = composition_generation.manifest
    template_manifest = template_generation.manifest
    diagnostics: list[contracts.Diagnostic] = []
    if not (
        template_manifest.static_composition_generation_id
        == composition_manifest.generation_id
        and template_manifest.static_composition_manifest_integrity
        == composition_manifest.integrity
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.input.composition-mismatch",
                "/inputs/staticComposition",
                "Host Template does not bind the supplied static-composition generation",
            )
        )
    if (
        template_manifest.engine_generation_id,
        template_manifest.host_kind,
        template_manifest.target_platform,
        template_manifest.configuration,
    ) != (
        composition_manifest.engine_generation_id,
        composition_manifest.host_kind,
        composition_manifest.target_platform,
        composition_manifest.configuration,
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.input.host-mismatch",
                "/host",
                "Host Template and static composition disagree on Host/build identity",
            )
        )
    return diagnostics


def configured_target_diagnostics(
    composition_generation: composition.StaticCompositionRootGeneration,
    template_generation: host_template.WindowsDevelopmentHostTemplateGenerationV1,
    configured: host_cmake_toolchain.HostCMakeConfiguredTargetEvidence,
) -> list[contracts.Diagnostic]:
    composition_manifest = composition_generation.manifest
    template_manifest = template_generation.manifest
    target = configured.target
    diagnostics: list[contracts.Diagnostic] = []

    def require(
        condition: bool,
        code: str,
        pointer: str,
        message: str,
    ) -> None:
        if not condition:
            diagnostics.append(_diagnostic(code, pointer, message))

    require(
        target.configuration == composition_manifest.configuration,
        "host-binding.cmake.configuration-mismatch",
        "/build/configuration",
        "latest CMake target configuration differs from the composition",
    )
    require(
        target.target_name == template_manifest.target_name
        and target.target_type == "EXECUTABLE",
        "host-binding.cmake.target-mismatch",
        "/target",
        "latest CMake target differs from the generated Host Template",
    )
    require(
        configured.generator.name == composition_manifest.generator_name
        and configured.generator.multi_config
        == composition_manifest.generator_multi_config,
        "host-binding.cmake.generator-mismatch",
        "/build/generator",
        "latest CMake generator differs from the composition build evidence",
    )
    require(
        configured.configured_compiler.language == "CXX"
        and configured.configured_compiler.compiler_id
        == composition_manifest.compiler_id
        and configured.configured_compiler.compiler_version
        == composition_manifest.compiler_version,
        "host-binding.cmake.compiler-mismatch",
        "/build/configuredCompiler",
        "configured CXX compiler differs from the composition build evidence",
    )
    require(
        configured.reply_index_path == target.reply_index_path
        and configured.build_root == target.build_root,
        "host-binding.cmake.reply-mismatch",
        "/target/fileApi",
        "target and configured compiler must come from the same File API index",
    )
    return diagnostics


def receipt_input_diagnostics(
    receipt: binding.HostExecutableBindingReceiptV1,
    composition_generation: composition.StaticCompositionRootGeneration,
    template_generation: host_template.WindowsDevelopmentHostTemplateGenerationV1,
) -> list[contracts.Diagnostic]:
    composition_manifest = composition_generation.manifest
    template_manifest = template_generation.manifest
    diagnostics = composition_template_diagnostics(
        composition_generation,
        template_generation,
    )
    receipt_build = (
        receipt.host.engine_generation_id,
        receipt.host.host_kind,
        receipt.host.target_platform,
        receipt.build.configuration,
        receipt.build.generator.name,
        receipt.build.generator.multi_config,
        receipt.build.configured_compiler.compiler_id,
        receipt.build.configured_compiler.compiler_version,
    )
    composition_build = (
        composition_manifest.engine_generation_id,
        composition_manifest.host_kind,
        composition_manifest.target_platform,
        composition_manifest.configuration,
        composition_manifest.generator_name,
        composition_manifest.generator_multi_config,
        composition_manifest.compiler_id,
        composition_manifest.compiler_version,
    )
    checks = (
        (
            (
                receipt.inputs.static_composition.generation_id,
                _integrity_key(receipt.inputs.static_composition.manifest_integrity),
            )
            == (
                composition_manifest.generation_id,
                _integrity_key(composition_manifest.integrity),
            ),
            "host-binding.input.composition-mismatch",
            "/inputs/staticComposition",
        ),
        (
            (
                receipt.inputs.host_template.generation_id,
                _integrity_key(receipt.inputs.host_template.manifest_integrity),
            )
            == (
                template_manifest.generation_id,
                _integrity_key(template_manifest.integrity),
            ),
            "host-binding.input.template-mismatch",
            "/inputs/hostTemplate",
        ),
        (
            _integrity_key(receipt.inputs.host_activation_blueprint_integrity)
            == _integrity_key(
                composition_manifest.inputs.host_activation_blueprint_integrity
            ),
            "host-binding.input.blueprint-mismatch",
            "/inputs/hostActivationBlueprintIntegrity",
        ),
        (
            receipt_build == composition_build,
            "host-binding.input.build-mismatch",
            "/build",
        ),
        (
            receipt.target.name == template_manifest.target_name
            and receipt.target.target_type == "EXECUTABLE",
            "host-binding.input.target-mismatch",
            "/target",
        ),
    )
    diagnostics.extend(
        _diagnostic(
            code,
            pointer,
            "published receipt disagrees with its input generation",
        )
        for condition, code, pointer in checks
        if not condition
    )
    return diagnostics


__all__ = [
    "composition_template_diagnostics",
    "configured_target_diagnostics",
    "receipt_input_diagnostics",
]
