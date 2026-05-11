cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(VkEngineEditor VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED VKE_REPOSITORY_ROOT)
  get_filename_component(VKE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${VKE_REPOSITORY_ROOT}/cmake/VkePackage.cmake")
vke_package_init()
vke_require_package_target(vke::renderer_basic_vulkan packages/renderer-basic)
vke_require_package_target(vke::rhi_vulkan packages/rhi-vulkan)
vke_require_package_target(vke::window_glfw packages/window-glfw)

find_package(imgui CONFIG REQUIRED)

string(TOUPPER "${CMAKE_BUILD_TYPE}" VKE_IMGUI_CONFIG_SUFFIX)
set(VKE_IMGUI_PACKAGE_FOLDER_VAR "imgui_PACKAGE_FOLDER_${VKE_IMGUI_CONFIG_SUFFIX}")
set(VKE_IMGUI_PACKAGE_FOLDER "${${VKE_IMGUI_PACKAGE_FOLDER_VAR}}")

if(NOT VKE_IMGUI_PACKAGE_FOLDER)
    message(FATAL_ERROR "Could not locate the Conan imgui package folder for ${CMAKE_BUILD_TYPE}.")
endif()

set(VKE_IMGUI_BACKENDS_DIR "${VKE_IMGUI_PACKAGE_FOLDER}/res/bindings")
set(VKE_IMGUI_BACKEND_SOURCES
    "${VKE_IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp"
    "${VKE_IMGUI_BACKENDS_DIR}/imgui_impl_vulkan.cpp"
)

set(VKE_EDITOR_RESOURCE_SOURCES)

if(WIN32)
    enable_language(RC)
    list(APPEND VKE_EDITOR_RESOURCE_SOURCES
        resources/vke-editor.rc
    )
    set_source_files_properties(resources/vke-editor.rc
        PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/vke-editor.ico"
    )
endif()

add_executable(vke-editor
    src/main.cpp
    ${VKE_IMGUI_BACKEND_SOURCES}
    ${VKE_EDITOR_RESOURCE_SOURCES}
)

add_executable(vke::editor ALIAS vke-editor)

set_source_files_properties(${VKE_IMGUI_BACKEND_SOURCES}
    PROPERTIES
        SKIP_LINTING ON
)

target_include_directories(vke-editor
    SYSTEM
    PRIVATE
        "${VKE_IMGUI_BACKENDS_DIR}"
)

target_link_libraries(vke-editor
    PRIVATE
        vke::core
        vke::renderer_basic_vulkan
        vke::rhi_vulkan
        vke::window_glfw
        imgui::imgui
)

vke_configure_target(vke-editor)
