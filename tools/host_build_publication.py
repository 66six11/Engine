"""Verify complete generated inputs before final Host configure."""

from __future__ import annotations

from pathlib import Path

from tools import check_package_contracts as contracts
from tools import generated_publication_tree
from tools import host_executable_template as host_template
from tools import static_composition_root as composition


def _diagnostic(code: str, pointer: str, label: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code,
        "final-host-build",
        pointer,
        f"published {label} tree does not match the complete generation",
    )


def validate_final_host_publications(
    host_template_root: Path,
    host_template_generation: host_template.WindowsDevelopmentHostTemplateGenerationV1,
    static_composition_root: Path,
    static_composition_generation: composition.StaticCompositionRootGeneration,
) -> list[contracts.Diagnostic]:
    """Require both publication roots to be closed exact-byte trees."""

    checks = (
        (
            host_template_root,
            host_template.expected_host_template_publication_files(
                host_template_generation
            ),
            "host-build.template-publication-stale",
            "/hostTemplateRoot",
            "Host Template",
        ),
        (
            static_composition_root,
            composition.expected_static_composition_publication_files(
                static_composition_generation
            ),
            "host-build.composition-publication-stale",
            "/staticCompositionRoot",
            "static-composition",
        ),
    )
    diagnostics: list[contracts.Diagnostic] = []
    for root, expected, code, pointer, label in checks:
        try:
            generated_publication_tree.verify_exact_publication_tree(root, expected)
        except OSError:
            diagnostics.append(_diagnostic(code, pointer, label))
    return diagnostics
