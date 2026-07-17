"""Version-pair policy shared by Host generation consumers."""

from __future__ import annotations


CURRENT_HOST_GENERATION_PAIR = (
    3,
    6,
    "asharia-static-factory-provider-v4",
)


def generation_pair(
    template_renderer_revision: int,
    composition_renderer_revision: int,
    provider_api: str,
) -> tuple[int, int, str]:
    return (
        template_renderer_revision,
        composition_renderer_revision,
        provider_api,
    )


def is_current_host_generation_pair(
    template_renderer_revision: int,
    composition_renderer_revision: int,
    provider_api: str,
) -> bool:
    """Allow only the active executable build path."""

    return (
        generation_pair(
            template_renderer_revision,
            composition_renderer_revision,
            provider_api,
        )
        == CURRENT_HOST_GENERATION_PAIR
    )


__all__ = [
    "CURRENT_HOST_GENERATION_PAIR",
    "generation_pair",
    "is_current_host_generation_pair",
]
