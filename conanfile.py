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
        self.requires("vulkan-headers/1.4.313.0")
        self.requires("vulkan-memory-allocator/3.3.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["VKE_BUILD_APPS"] = "ON"
        toolchain.cache_variables["VKE_BUILD_TESTS"] = "OFF"
        toolchain.generate()
        self._add_clion_toolchain_to_presets()

        deps = CMakeDeps(self)
        deps.generate()

    def _add_clion_toolchain_to_presets(self):
        presets_path = Path(self.generators_folder) / "CMakePresets.json"
        if not presets_path.exists():
            return

        presets = json.loads(presets_path.read_text(encoding="utf-8"))
        for preset in presets.get("configurePresets", []):
            vendor = preset.setdefault("vendor", {})
            clion = vendor.setdefault("jetbrains.com/clion", {})
            clion["toolchain"] = "Visual Studio"

        presets_path.write_text(json.dumps(presets, indent=4) + "\n", encoding="utf-8")
