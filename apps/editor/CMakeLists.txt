cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(AshariaEngineEditor VERSION 0.1.0 LANGUAGES CXX)
endif()

if(NOT DEFINED ASHARIA_REPOSITORY_ROOT)
  get_filename_component(ASHARIA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${ASHARIA_REPOSITORY_ROOT}/cmake/AshariaPackage.cmake")
asharia_package_init()
asharia_require_package_target(asharia::archive packages/archive)
asharia_require_package_target(asharia::asset_core packages/asset-core)
asharia_require_package_target(asharia::asset_core_io packages/asset-core)
asharia_require_package_target(asharia::asset_pipeline packages/asset-pipeline)
asharia_require_package_target(asharia::project_core_io packages/project-core)
asharia_require_package_target(asharia::renderer_basic_vulkan packages/renderer-basic)
asharia_require_package_target(asharia::rhi_vulkan packages/rhi-vulkan)
asharia_require_package_target(asharia::scene_core packages/scene-core)
asharia_require_package_target(asharia::shader_slang packages/shader-slang)
asharia_require_package_target(asharia::window_glfw packages/window-glfw)

find_package(glm CONFIG REQUIRED)
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

set(ASHARIA_EDITOR_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(ASHARIA_EDITOR_IMGUI_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui_srgb_color.slang"
)
set(ASHARIA_EDITOR_IMGUI_FRAGMENT_SPV
    "${ASHARIA_EDITOR_SHADER_OUTPUT_DIR}/imgui_srgb_color.frag.spv"
)
set(ASHARIA_EDITOR_IMGUI_FRAGMENT_METADATA
    "${ASHARIA_EDITOR_SHADER_OUTPUT_DIR}/imgui_srgb_color.frag.metadata.json"
)

asharia_add_slang_shader(asharia-editor-imgui-frag
    SOURCE "${ASHARIA_EDITOR_IMGUI_SHADER_SOURCE}"
    ENTRY imguiFragmentMain
    STAGE fragment
    OUTPUT "${ASHARIA_EDITOR_IMGUI_FRAGMENT_SPV}"
    METADATA_OUTPUT "${ASHARIA_EDITOR_IMGUI_FRAGMENT_METADATA}"
)

add_custom_target(asharia-editor-imgui-shaders
    DEPENDS
        asharia-editor-imgui-frag
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
    src/editor_app_config.cpp
    src/editor_app_registration.cpp
    src/editor_app_run_completion.cpp
    src/editor_app_services.cpp
    src/editor_asset_catalog.cpp
    src/editor_asset_catalog_report.cpp
    src/editor_asset_catalog_smoke.cpp
    src/editor_asset_import_settings_command.cpp
    src/editor_asset_icon.cpp
    src/editor_command.cpp
    src/editor_command_smoke.cpp
    src/editor_dock_layout.cpp
    src/editor_event.cpp
    src/editor_extension.cpp
    src/editor_frame_debugger.cpp
    src/editor_frame_debugger_replay.cpp
    src/editor_frame_debugger_smoke.cpp
    src/editor_frame_debug_preview.cpp
    src/editor_i18n.cpp
    src/editor_inspector_model.cpp
    src/editor_inspector_smoke.cpp
    src/editor_inspected_world.cpp
    src/editor_input_smoke.cpp
    src/editor_input_router.cpp
    src/editor_loop_host.cpp
    src/editor_panel.cpp
    src/editor_render_runtime.cpp
    src/editor_registration_smoke.cpp
    src/editor_settings.cpp
    src/editor_settings_contribution.cpp
    src/editor_selection.cpp
    src/editor_selection_smoke.cpp
    src/editor_shell_host.cpp
    src/editor_shortcut_router.cpp
    src/editor_shortcut_smoke.cpp
    src/editor_smoke.cpp
    src/editor_smoke_validation.cpp
    src/editor_startup_smoke.cpp
    src/editor_tool.cpp
    src/editor_tool_manager.cpp
    src/editor_ui.cpp
    src/editor_ui_widgets.cpp
    src/editor_viewport.cpp
    src/editor_viewport_coordinator.cpp
    src/editor_viewport_overlay_provider.cpp
    src/editor_viewport_smoke.cpp
    src/editor_vulkan_host.cpp
    src/editor_workspace.cpp
    src/imgui_editor_shell.cpp
    src/imgui_frame_renderer.cpp
    src/imgui_runtime.cpp
    src/imgui_texture_registry.cpp
    src/main.cpp
    src/panels/asset_browser_panel.cpp
    src/panels/editor_settings_panel.cpp
    src/panels/frame_debugger_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/log_panel.cpp
    src/panels/render_graph_panel.cpp
    src/panels/render_graph_snapshot_details.cpp
    src/panels/render_graph_snapshot_format.cpp
    src/panels/render_graph_snapshot_view.cpp
    src/panels/scene_view_panel.cpp
    src/panels/scene_tree_panel.cpp
    src/panels/ui_style_preview_panel.cpp
    ${ASHARIA_IMGUI_BACKEND_SOURCES}
    ${ASHARIA_EDITOR_RESOURCE_SOURCES}
)

add_executable(asharia::editor ALIAS asharia-editor)

add_dependencies(asharia-editor asharia-renderer-basic-shaders asharia-editor-imgui-shaders)

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
        asharia::archive
        asharia::asset_core
        asharia::asset_core_io
        asharia::asset_pipeline
        asharia::core
        asharia::project_core_io
        asharia::renderer_basic_vulkan
        asharia::rhi_vulkan
        asharia::scene_core
        asharia::window_glfw
        glm::glm
        imgui::imgui
)

target_compile_definitions(asharia-editor
    PRIVATE
        ASHARIA_EDITOR_I18N_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n"
        ASHARIA_EDITOR_IMGUI_FRAGMENT_SHADER_SPV="${ASHARIA_EDITOR_IMGUI_FRAGMENT_SPV}"
)

asharia_configure_target(asharia-editor)
