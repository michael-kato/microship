cmake_minimum_required(VERSION 3.16)
project(nanovore LANGUAGES CXX)

option(NANOVORE_NATIVE_DIAGNOSTIC "Build the native headless Vulkan comparison" OFF)
if(NOT WIN32 AND NOT NANOVORE_NATIVE_DIAGNOSTIC)
    message(FATAL_ERROR "Use the Windows toolchain, or explicitly enable NANOVORE_NATIVE_DIAGNOSTIC for a headless Linux comparison.")
endif()
find_package(Vulkan REQUIRED)
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc is required; install the Vulkan SDK shader compiler")
endif()

# Shipping builds set this OFF: no wireframe, buffer views or debug hotkeys.
option(DEVELOPMENT "Wireframe view, F2 buffer views and F1/F3/F5/F6/F8-F10 debug hotkeys" ON)
set(dev_shader_flags)
if(DEVELOPMENT)
    set(dev_shader_flags -DDEVELOPMENT=1)
endif()
set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/ray_trace.comp.spv")
set(bloom_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/bloom.comp.spv")
set(volume_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/volume.comp.spv")
set(shadow_grid_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/shadow_grid.comp.spv")
add_custom_command(OUTPUT "${shadow_grid_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O -DNANOVORE_SHADOW_GRID_PASS=1
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/ray_trace.comp" -o "${shadow_grid_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/ray_trace.comp assets/shaders/render_uniform.glsl VERBATIM)
set(cascade_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/radiance_cascade.comp.spv")
add_custom_command(OUTPUT "${cascade_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O -DNANOVORE_CASCADE_PASS=1
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/ray_trace.comp" -o "${cascade_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/ray_trace.comp assets/shaders/render_uniform.glsl VERBATIM)
set(debug_outputs)
set(debug_variants guide_surface guide_transparent guide_opaque)
if(DEVELOPMENT)
    list(APPEND debug_variants surface volume)
endif()
foreach(debug_variant IN LISTS debug_variants)
    set(debug_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/debug_${debug_variant}.comp.spv")
    set(debug_flags -DNANOVORE_DEBUG_BUFFERS=1 ${dev_shader_flags})
    if(debug_variant MATCHES "^guide_")
        list(APPEND debug_flags -DNANOVORE_NO_QUERY_COUNTERS=1)
    endif()
    if(debug_variant MATCHES "volume$")
        list(APPEND debug_flags -DNANOVORE_VOLUME_PASS=1)
    endif()
    if(debug_variant STREQUAL "guide_transparent")
        list(APPEND debug_flags -DNANOVORE_TRANSPARENT_PASS=1)
    elseif(debug_variant STREQUAL "guide_opaque")
        list(APPEND debug_flags -DNANOVORE_OPAQUE_PASS=1)
    endif()
    add_custom_command(OUTPUT "${debug_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
        COMMAND ${GLSLC} --target-env=vulkan1.2 -O ${debug_flags}
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/ray_trace.comp" -o "${debug_output}"
        DEPENDS assets/shaders/render_bindings.glsl assets/shaders/ray_trace.comp assets/shaders/render_uniform.glsl
            assets/shaders/debug_buffers.glsl VERBATIM)
    list(APPEND debug_outputs "${debug_output}")
endforeach()
foreach(volume_variant IN ITEMS coarse filter)
    set(volume_variant_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/volume_${volume_variant}.comp.spv")
    string(TOUPPER "${volume_variant}" volume_variant_define)
    add_custom_command(OUTPUT "${volume_variant_output}"
        COMMAND ${GLSLC} --target-env=vulkan1.2 -O -DNANOVORE_VOLUME_PASS=1
            -DNANOVORE_DEBUG_BUFFERS=1 -DNANOVORE_NO_QUERY_COUNTERS=1 -DNANOVORE_VOLUME_${volume_variant_define}=1
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/ray_trace.comp" -o "${volume_variant_output}"
        DEPENDS assets/shaders/render_bindings.glsl assets/shaders/ray_trace.comp assets/shaders/render_uniform.glsl
            assets/shaders/debug_buffers.glsl VERBATIM)
    list(APPEND debug_outputs "${volume_variant_output}")
endforeach()
set(backdrop_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/backdrop.comp.spv")
add_custom_command(OUTPUT "${backdrop_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/backdrop.comp" -o "${backdrop_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/backdrop.comp assets/shaders/render_uniform.glsl VERBATIM)
set(fog_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/fog.comp.spv")
add_custom_command(OUTPUT "${fog_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/fog.comp" -o "${fog_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/fog.comp assets/shaders/render_uniform.glsl VERBATIM)
set(depth_of_field_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/depth_of_field.comp.spv")
add_custom_command(OUTPUT "${depth_of_field_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/depth_of_field.comp" -o "${depth_of_field_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/render_uniform.glsl assets/shaders/debug_buffers.glsl assets/shaders/depth_of_field.comp VERBATIM)
set(effects_output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/effects.comp.spv")
add_custom_command(OUTPUT "${effects_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/effects.comp" -o "${effects_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/effects.comp assets/shaders/render_uniform.glsl assets/shaders/debug_buffers.glsl VERBATIM)
set(debug_overlay "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/debug.comp.spv")
add_custom_command(OUTPUT "${debug_overlay}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/debug.comp" -o "${debug_overlay}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/hud_layout.glsl assets/shaders/debug.comp assets/shaders/debug_buffers.glsl
        assets/shaders/display_transform.glsl VERBATIM)
add_custom_command(OUTPUT "${volume_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O -DNANOVORE_VOLUME_PASS=1
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/ray_trace.comp" -o "${volume_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/ray_trace.comp assets/shaders/render_uniform.glsl VERBATIM)
add_custom_command(OUTPUT "${bloom_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/bloom.comp" -o "${bloom_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/bloom.comp assets/shaders/debug_buffers.glsl assets/shaders/display_transform.glsl VERBATIM)
add_custom_command(OUTPUT "${shader_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O ${dev_shader_flags}
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/ray_trace.comp" -o "${shader_output}"
    DEPENDS assets/shaders/render_bindings.glsl assets/shaders/ray_trace.comp assets/shaders/render_uniform.glsl
    VERBATIM)
file(GLOB model_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/*.fmesh")
set(json_license "${CMAKE_CURRENT_BINARY_DIR}/licenses/json.txt")
add_custom_command(OUTPUT "${json_license}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/LICENSE" "${json_license}"
    DEPENDS third_party/json/LICENSE VERBATIM)
set(asset_outputs)
# Do not leave a second renderer or obsolete reconstruction kernels in the bundle.
foreach(old_shader IN ITEMS path_trace.comp.spv denoise.comp.spv debug_guide_volume.comp.spv medium_blur.comp.spv)
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/${old_shader}")
endforeach()
file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/licenses/nile.txt")
# Remove only the six inputs replaced by the JSON migration from an older bundle.
foreach(legacy_asset IN ITEMS startup.game gameplay/biology.content
    levels/biology-virus.world levels/biology-immune.world
    rendering/biology.render rendering/reference.render
    models/cell_spine.fmesh models/cell_lobe.fmesh
    levels/biology-body.json models/body_spore.fmesh models/fungus_fighter.fmesh models/fungus_bomber.fmesh
    gameplay/trial-units.json gameplay/trial-buildings.json)
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/assets/${legacy_asset}")
endforeach()
# Remove old configuration names from an existing active build bundle.
foreach(old_settings IN ITEMS
    audio/sound-playback.json
    gameplay/gameplay-content.json
    presentation/scene-appearance.json
    audio/biology.json
    audio/synthesis.json
    gameplay/biology.json
    gameplay/bodies.json
    gameplay/buildings.json
    gameplay/groups.json
    gameplay/rules.json
    gameplay/units.json
    gameplay/faction-trials.json
    presentation/biology.json
    presentation/combat.json
    rendering/biology.json
    rendering/biology-quality.json
    rendering/reference.json
    models/source/biology_materials.json
    models/source/biology_profiles.json
    models/source/background_profiles.json
    models/source/anatomy_backgrounds.json
    models/source/colony_forms.json
    schemas/biology-materials.schema.json
    levels/biology-virus.json
    levels/biology-immune.json
    levels/biology-fungus.json
    levels/biology-swarm.json
    levels/biology-immune-trial.json
)
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/assets/${old_settings}")
endforeach()
file(GLOB_RECURSE authored_files CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.json" "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.wav")
list(FILTER authored_files EXCLUDE REGEX "^models/source/")
foreach(scene_asset IN LISTS authored_files)
    set(asset_output "${CMAKE_CURRENT_BINARY_DIR}/assets/${scene_asset}")
    get_filename_component(asset_folder "${asset_output}" DIRECTORY)
    add_custom_command(OUTPUT "${asset_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${asset_folder}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/${scene_asset}" "${asset_output}"
        DEPENDS "assets/${scene_asset}" VERBATIM)
    list(APPEND asset_outputs "${asset_output}")
endforeach()
set(model_outputs)
foreach(model IN LISTS model_files)
    get_filename_component(model_name "${model}" NAME)
    set(model_output "${CMAKE_CURRENT_BINARY_DIR}/assets/models/${model_name}")
    add_custom_command(OUTPUT "${model_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${model}" "${model_output}"
        DEPENDS "${model}" VERBATIM)
    list(APPEND model_outputs "${model_output}")
endforeach()
add_custom_target(nanovore_assets DEPENDS "${depth_of_field_output}" "${shadow_grid_output}" "${cascade_output}" "${fog_output}" "${effects_output}" "${backdrop_output}" "${json_license}" "${shader_output}" "${bloom_output}" "${volume_output}" "${debug_overlay}" ${debug_outputs} ${model_outputs} ${asset_outputs})

if(WIN32)
    set(platform_sources source/window.cpp source/start_menu.cpp source/pause_menu.cpp)
else()
    set(platform_sources source/native_headless.cpp)
endif()
add_executable(nanovore
    ${platform_sources}
    source/content.cpp source/asset_json.cpp
    source/effects.cpp source/communications.cpp source/audio.cpp
    source/main.cpp
    source/camera.cpp
    source/world.cpp source/map_layout.cpp
    source/render_config.cpp
    source/presentation.cpp source/cell_deformation.cpp source/render_scene.cpp
    source/geometry.cpp source/geometry_variation.cpp
    source/simulation.cpp
    source/flight.cpp source/fog.cpp
    source/orders.cpp
    source/renderer.cpp source/profiler.cpp
    source/game.cpp source/player_settings.cpp
)
add_dependencies(nanovore nanovore_assets)
target_link_libraries(nanovore PRIVATE Vulkan::Vulkan)
option(NANOVORE_RENDERDOC_CAPTURE "Enable explicit native headless RenderDoc capture" OFF)
if(NANOVORE_RENDERDOC_CAPTURE)
    if(WIN32 OR NOT NANOVORE_NATIVE_DIAGNOSTIC)
        message(FATAL_ERROR "RenderDoc capture uses the native headless diagnostic target.")
    endif()
    find_path(NANOVORE_RENDERDOC_INCLUDE_DIR renderdoc_app.h)
    if(NOT NANOVORE_RENDERDOC_INCLUDE_DIR)
        message(FATAL_ERROR "Set NANOVORE_RENDERDOC_INCLUDE_DIR to the RenderDoc SDK include folder.")
    endif()
    target_sources(nanovore PRIVATE source/renderdoc_capture.cpp)
    target_include_directories(nanovore SYSTEM PRIVATE "${NANOVORE_RENDERDOC_INCLUDE_DIR}")
    target_compile_definitions(nanovore PRIVATE NANOVORE_RENDERDOC_CAPTURE=1)
    target_link_libraries(nanovore PRIVATE ${CMAKE_DL_LIBS})
endif()
if(DEVELOPMENT)
    target_compile_definitions(nanovore PRIVATE DEVELOPMENT=1)
endif()
if(WIN32)
    target_link_libraries(nanovore PRIVATE user32 gdi32 winmm)
    target_compile_definitions(nanovore PRIVATE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(nanovore PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>"
)

if(MSVC)
    target_compile_options(nanovore PRIVATE /W4 /permissive-)
    # The distributable should not require a separately installed C++ runtime.
    set_property(TARGET nanovore PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(nanovore PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME smoke COMMAND nanovore --smoke)
set_tests_properties(smoke PROPERTIES TIMEOUT 5)

if(NOT WIN32)
    # Existing window-input suites use the Windows application through Proton.
    return()
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME world_files COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/world_files.py $<TARGET_FILE:nanovore>)
set_tests_properties(world_files PROPERTIES TIMEOUT 10)
add_test(NAME polish COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/polish.py $<TARGET_FILE:nanovore>)
set_tests_properties(polish PROPERTIES TIMEOUT 30)
add_test(NAME spikes COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/spikes.py $<TARGET_FILE:nanovore>)
set_tests_properties(spikes PROPERTIES TIMEOUT 45)
if(CMAKE_CROSSCOMPILING)
    # Each Windows process crosses Proton startup; keep the same real scenarios.
    set_tests_properties(world_files PROPERTIES TIMEOUT 60)
endif()

add_test(NAME data_assets COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/data_assets.py $<TARGET_FILE:nanovore>)
set_tests_properties(data_assets PROPERTIES TIMEOUT 45)
add_test(NAME navigation COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/navigation.py $<TARGET_FILE:nanovore>)
set_tests_properties(navigation PROPERTIES TIMEOUT 60)

add_test(NAME production_ratio COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/production_ratio.py $<TARGET_FILE:nanovore>)
set_tests_properties(production_ratio PROPERTIES TIMEOUT 45)


foreach(group IN ITEMS cell_send faction_opening communications faction_motion virus_shapes selection colony_forms cell_animation small_factories factory_curves three_factions factory_emission factory_repair fungal_defence fungal_sources)
    add_test(NAME ${group} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/${group}.py $<TARGET_FILE:nanovore>)
    set_tests_properties(${group} PROPERTIES TIMEOUT 60)
endforeach()

option(NANOVORE_GPU_TESTS "Run real hardware Vulkan E2E scenarios (explicit opt-in)" OFF)
if(NANOVORE_GPU_TESTS)
    add_test(NAME map_render COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/map_render.py $<TARGET_FILE:nanovore>
        --output "${CMAKE_CURRENT_BINARY_DIR}/map_render" --run)
    set_tests_properties(map_render PROPERTIES TIMEOUT 35 LABELS gpu RUN_SERIAL TRUE)
endif()

add_test(NAME match_flow COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/match_flow.py $<TARGET_FILE:nanovore>)
set_tests_properties(match_flow PROPERTIES TIMEOUT 45)

add_test(NAME cell_medium COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/cell_medium.py $<TARGET_FILE:nanovore>)
set_tests_properties(cell_medium PROPERTIES TIMEOUT 90)
add_test(NAME payloads COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/payloads.py $<TARGET_FILE:nanovore>)
set_tests_properties(payloads PROPERTIES TIMEOUT 30)
add_test(NAME subsurface COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/subsurface.py $<TARGET_FILE:nanovore>)
set_tests_properties(subsurface PROPERTIES TIMEOUT 30)
add_test(NAME backgrounds COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/backgrounds.py $<TARGET_FILE:nanovore>)
set_tests_properties(backgrounds PROPERTIES TIMEOUT 45)
add_test(NAME trails COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/trails.py $<TARGET_FILE:nanovore>)
set_tests_properties(trails PROPERTIES TIMEOUT 30)
add_test(NAME fog_memory COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/fog_memory.py $<TARGET_FILE:nanovore>)
set_tests_properties(fog_memory PROPERTIES TIMEOUT 45)

add_test(NAME immune_combat COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/immune_combat.py $<TARGET_FILE:nanovore>)
set_tests_properties(immune_combat PROPERTIES TIMEOUT 30)

add_test(NAME target_lines COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/target_lines.py $<TARGET_FILE:nanovore>)
set_tests_properties(target_lines PROPERTIES TIMEOUT 30)

add_test(NAME keyboard_camera COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/keyboard_camera.py $<TARGET_FILE:nanovore>)
set_tests_properties(keyboard_camera PROPERTIES TIMEOUT 30)

add_test(NAME screen_selection COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/screen_selection.py $<TARGET_FILE:nanovore>)
set_tests_properties(screen_selection PROPERTIES TIMEOUT 30)

# Explicit E2E input driver; it is not part of the distributed game.
add_executable(nanovore_input_probe EXCLUDE_FROM_ALL tests/e2e/win32_input.cpp)
if(MINGW)
    target_link_options(nanovore_input_probe PRIVATE -static)
endif()
