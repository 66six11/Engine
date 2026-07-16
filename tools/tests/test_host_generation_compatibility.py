"""Focused policy tests for Host generation compatibility."""

from __future__ import annotations

import unittest

from tools import host_generation_compatibility as compatibility


class HostGenerationCompatibilityTests(unittest.TestCase):
    def test_only_current_generation_tuple_is_accepted(self) -> None:
        current = (2, 5, "asharia-static-factory-provider-v4")

        self.assertEqual(current, compatibility.CURRENT_HOST_GENERATION_PAIR)
        self.assertEqual(current, compatibility.generation_pair(*current))
        self.assertTrue(compatibility.is_current_host_generation_pair(*current))

    def test_legacy_and_future_generation_variants_fail_closed(self) -> None:
        incompatible = (
            (1, 5, "asharia-static-factory-provider-v4"),
            (3, 5, "asharia-static-factory-provider-v4"),
            (2, 4, "asharia-static-factory-provider-v4"),
            (2, 6, "asharia-static-factory-provider-v4"),
            (2, 5, "asharia-static-factory-provider-v3"),
            (2, 5, "asharia-static-factory-provider-v5"),
        )

        for generation in incompatible:
            with self.subTest(generation=generation):
                self.assertFalse(
                    compatibility.is_current_host_generation_pair(*generation)
                )


if __name__ == "__main__":
    unittest.main()
