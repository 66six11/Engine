"""Shared verified composition fixture for Host Template tests."""

from __future__ import annotations

from dataclasses import replace

from tools import check_package_contracts as contracts
from tools import host_activation_blueprint as activation
from tools import source_build_plan
from tools import static_composition_root
from tools import static_factory_provider_bindings as provider_bindings
from tools.cmake_file_api import CMakeGeneratorEvidence, CMakeToolchainEvidence


def digest(character: str) -> str:
    return character * 64


def build_plan(
    compiler_version: str = "19.1.5",
    compiler_id: str = "Clang",
) -> source_build_plan.SourceBuildPlan:
    target = source_build_plan.BuildTargetReference(
        "asharia_synthetic_runtime", "STATIC_LIBRARY"
    )
    plan = source_build_plan.SourceBuildPlan(
        inputs=source_build_plan.SourceBuildInputs(
            host_composition_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("1")
            ),
            descriptor_set_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("2")
            ),
            topology_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("3")
            ),
            codemodel_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("4")
            ),
            configuration_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("5")
            ),
        ),
        host_kind="minimal",
        target_platform="com.asharia.platform.windows-x86-64",
        configuration="Debug",
        generator=CMakeGeneratorEvidence("Ninja", False),
        toolchain=CMakeToolchainEvidence(
            compiler_id, compiler_version, "Windows", "x86_64"
        ),
        packages=(),
        build_roots=(target,),
        target_closure=(
            source_build_plan.TargetClosureEvidence(
                target.name, target.target_type, ()
            ),
        ),
        build_options=(),
        integrity=source_build_plan.IntegrityRecord("sha256", digest("0")),
    )
    integrity = source_build_plan.compute_source_build_plan_integrity(plan)
    return replace(
        plan,
        integrity=source_build_plan.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def blueprint() -> activation.HostActivationBlueprint:
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
    value = activation.HostActivationBlueprint(
        inputs=activation.HostActivationBlueprintInputs(
            effective_session_integrity=activation.IntegrityRecord(
                "sha256", digest("6")
            ),
            host_composition_integrity=activation.IntegrityRecord(
                "sha256", digest("1")
            ),
            factory_declaration_set_integrity=activation.IntegrityRecord(
                "sha256", digest("7")
            ),
        ),
        engine_generation_id="sha256-" + digest("a"),
        host_kind="minimal",
        target_platform="com.asharia.platform.windows-x86-64",
        lifecycle_model=activation.LIFECYCLE_MODEL,
        scope_templates=(
            activation.ScopeActivationTemplate("process", None, (factory,)),
        ),
        integrity=activation.IntegrityRecord("sha256", digest("0")),
    )
    integrity = activation.compute_host_activation_blueprint_integrity(value)
    return replace(
        value,
        integrity=activation.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def binding_plan(
    plan: source_build_plan.SourceBuildPlan,
    host_blueprint: activation.HostActivationBlueprint,
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
    value = provider_bindings.StaticFactoryProviderBindingPlan(
        inputs=provider_bindings.StaticFactoryProviderBindingInputs(
            effective_session_integrity=provider_bindings.IntegrityRecord(
                "sha256", digest("6")
            ),
            source_build_plan_integrity=provider_bindings.IntegrityRecord(
                plan.integrity.algorithm, plan.integrity.digest
            ),
            host_activation_blueprint_integrity=provider_bindings.IntegrityRecord(
                host_blueprint.integrity.algorithm, host_blueprint.integrity.digest
            ),
            binding_declaration_set_integrity=provider_bindings.IntegrityRecord(
                "sha256", digest("8")
            ),
        ),
        engine_generation_id=host_blueprint.engine_generation_id,
        host_kind=host_blueprint.host_kind,
        target_platform=host_blueprint.target_platform,
        providers=(provider,),
        integrity=provider_bindings.IntegrityRecord("sha256", digest("0")),
    )
    integrity = (
        provider_bindings.compute_static_factory_provider_binding_plan_integrity(
            value
        )
    )
    return replace(
        value,
        integrity=provider_bindings.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def composition_generation(
    validators: contracts.ContractValidators,
    compiler_version: str = "19.1.5",
    compiler_id: str = "Clang",
) -> static_composition_root.StaticCompositionRootGeneration:
    plan = build_plan(compiler_version, compiler_id)
    host_blueprint = blueprint()
    result = static_composition_root.generate_static_composition_root(
        plan,
        host_blueprint,
        binding_plan(plan, host_blueprint),
        validators,
    )
    if not result.succeeded or result.generation is None:
        raise AssertionError([item.render() for item in result.diagnostics])
    return result.generation
