import json
from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class VkEngineConan(ConanFile):
    name = "vkengine"
    version = "0.1.0"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("glfw/3.4")
        self.requires("glm/1.0.1")
        self.requires("imgui/1.92.7-docking")
        self.requires("vulkan-headers/1.4.313.0")
        self.requires("vulkan-memory-allocator/3.3.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = "ConanPresets.json"
        toolchain.cache_variables["VKE_BUILD_APPS"] = "ON"
        toolchain.cache_variables["VKE_BUILD_TESTS"] = "OFF"
        toolchain.cache_variables["VKE_ENABLE_CLANG_TIDY"] = "OFF"
        toolchain.cache_variables["CMAKE_CXX_STANDARD"] = "23"
        toolchain.cache_variables["CMAKE_CXX_STANDARD_REQUIRED"] = "ON"
        toolchain.cache_variables["CMAKE_CXX_EXTENSIONS"] = "OFF"
        toolchain.generate()
        self._remove_legacy_user_presets()

        deps = CMakeDeps(self)
        deps.generate()

    def _remove_legacy_user_presets(self):
        user_presets_path = Path(self.source_folder) / "CMakeUserPresets.json"
        if not user_presets_path.exists():
            return

        try:
            user_presets = json.loads(user_presets_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return

        include_entries = user_presets.get("include", [])
        is_legacy_conan_user_presets = (
            user_presets.get("vendor", {}).get("conan") == {}
            and "build/generators/CMakePresets.json" in include_entries
        )

        if is_legacy_conan_user_presets:
            user_presets_path.unlink()
