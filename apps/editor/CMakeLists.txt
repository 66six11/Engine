cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineEditor VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::renderer_basic_vulkan packages/renderer-basic)
asharia_require_package_target(asharia::rhi_vulkan packages/rhi-vulkan)
asharia_require_package_target(asharia::window_glfw packages/window-glfw)

find_package(imgui CONFIG REQUIRED)

string(TOUPPER "${CMAKE_BUILD_TYPE}" ASHARIA_IMGUI_CONFIG_SUFFIX)
set(ASHARIA_IMGUI_PACKAGE_FOLDER_VAR "imgui_PACKAGE_FOLDER_${ASHARIA_IMGUI_CONFIG_SUFFIX}")
set(ASHARIA_IMGUI_PACKAGE_FOLDER "${${ASHARIA_IMGUI_PACKAGE_FOLDER_VAR}}")

if(NOT ASHARIA_IMGUI_PACKAGE_FOLDER)
    message(FATAL_ERROR "Could not locate the Conan imgui package folder for ${CMAKE_BUILD_TYPE}.")
endif()

set(ASHARIA_IMGUI_BACKENDS_DIR "${ASHARIA_IMGUI_PACKAGE_FOLDER}/res/bindings")
set(ASHARIA_IMGUI_BACKEND_SOURCES
    "${ASHARIA_IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp"
    "${ASHARIA_IMGUI_BACKENDS_DIR}/imgui_impl_vulkan.cpp"
)

set(ASHARIA_EDITOR_RESOURCE_SOURCES)

if(WIN32)
    enable_language(RC)
    list(APPEND ASHARIA_EDITOR_RESOURCE_SOURCES
        resources/asharia-editor.rc
    )
    set_source_files_properties(resources/asharia-editor.rc
        PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/asharia-editor.ico"
    )
endif()

add_executable(asharia-editor
    src/editor_action.cpp
    src/editor_app.cpp
    src/editor_context.cpp
    src/editor_event.cpp
    src/editor_input_router.cpp
    src/editor_panel.cpp
    src/editor_shortcut_router.cpp
    src/editor_viewport.cpp
    src/editor_viewport_coordinator.cpp
    src/imgui_editor_shell.cpp
    src/imgui_runtime.cpp
    src/imgui_texture_registry.cpp
    src/main.cpp
    src/panels/log_panel.cpp
    src/panels/scene_view_panel.cpp
    ${ASHARIA_IMGUI_BACKEND_SOURCES}
    ${ASHARIA_EDITOR_RESOURCE_SOURCES}
)

add_executable(asharia::editor ALIAS asharia-editor)

add_dependencies(asharia-editor asharia-renderer-basic-shaders)

set_source_files_properties(${ASHARIA_IMGUI_BACKEND_SOURCES}
    PROPERTIES
        SKIP_LINTING ON
)

target_include_directories(asharia-editor
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_include_directories(asharia-editor
    SYSTEM
    PRIVATE
        "${ASHARIA_IMGUI_BACKENDS_DIR}"
)

target_link_libraries(asharia-editor
    PRIVATE
        asharia::core
        asharia::renderer_basic_vulkan
        asharia::rhi_vulkan
        asharia::window_glfw
        imgui::imgui
)

asharia_configure_target(asharia-editor)
