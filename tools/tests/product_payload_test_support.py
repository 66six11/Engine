"""Shared language-neutral product-payload fixture loader for tests."""

from __future__ import annotations

import json
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any


_FIXTURE_PATH = (
    Path(__file__).parent
    / "fixtures"
    / "product-boundaries"
    / "python-product-payload-v1.json"
)


@dataclass(frozen=True)
class PythonProductPayloadCases:
    forbidden: tuple[str, ...]
    allowed: tuple[str, ...]


def _string_tuple(value: Any, field: str) -> tuple[str, ...]:
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(item, str) or not item for item in value)
    ):
        raise ValueError(f"fixture field '{field}' must be a non-empty string array")
    values = tuple(value)
    if len(set(values)) != len(values):
        raise ValueError(f"fixture field '{field}' must not contain duplicates")
    return values


@lru_cache(maxsize=1)
def load_python_product_payload_cases() -> PythonProductPayloadCases:
    """Load and strictly validate the shared C#/Python policy fixture."""

    data = json.loads(_FIXTURE_PATH.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or set(data) != {
        "schemaVersion",
        "forbidden",
        "allowed",
    }:
        raise ValueError("Python product payload fixture has an invalid closed shape")
    if data["schemaVersion"] != 1:
        raise ValueError("Python product payload fixture must use schemaVersion 1")
    forbidden = _string_tuple(data["forbidden"], "forbidden")
    allowed = _string_tuple(data["allowed"], "allowed")
    if set(forbidden) & set(allowed):
        raise ValueError("forbidden and allowed fixture paths must be disjoint")
    return PythonProductPayloadCases(forbidden=forbidden, allowed=allowed)
