cmake_minimum_required(VERSION 3.16)
project(GuitarAmp VERSION 1.0.0 LANGUAGES CXX C)

set(AMPLITRON_VERSION "0.0.0" CACHE STRING "Application version (set by CI)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(EMSCRIPTEN)
    add_compile_options(-O3 -ffast-math -pthread "-sUSE_SDL=2" -Wno-nontrivial-memaccess)
elseif(MSVC)
    add_compile_options(/W4 /fp:fast)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /arch:AVX)
    endif()
else()
    add_compile_options(-Wall -Wextra -ffast-math)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native -ftree-vectorize)
    endif()
endif()

# --- Dependencies ---
# Emscripten provides SDL2, OpenGL ES, and audio via built-in ports.
# Native builds find PortAudio, SDL2, and OpenGL from the system.
if(NOT EMSCRIPTEN)

# PortAudio
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PORTAUDIO portaudio-2.0)
endif()
if(NOT PORTAUDIO_FOUND)
    find_path(PORTAUDIO_INCLUDE_DIRS portaudio.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
              "C:/Program Files/portaudio/include"
              "${CMAKE_SOURCE_DIR}/external/portaudio/include")
    find_library(PORTAUDIO_LIBRARIES NAMES portaudio portaudio_x64
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
              "C:/Program Files/portaudio/lib"
              "${CMAKE_SOURCE_DIR}/external/portaudio/lib")
endif()

# SDL2
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
endif()
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
        set(SDL2_LIBRARIES SDL2::SDL2 SDL2::SDL2main)
    else()
        find_path(SDL2_INCLUDE_DIRS SDL.h
            PATH_SUFFIXES SDL2
            PATHS /usr/include /usr/local/include /opt/homebrew/include
                  "C:/Program Files/SDL2/include"
                  "${CMAKE_SOURCE_DIR}/external/SDL2/include")
        find_library(SDL2_LIBRARIES NAMES SDL2
            PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
                  "C:/Program Files/SDL2/lib"
                  "${CMAKE_SOURCE_DIR}/external/SDL2/lib")
    endif()
endif()

# OpenGL
find_package(OpenGL REQUIRED)

endif() # NOT EMSCRIPTEN

# --- Dear ImGui (bundled) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Suppress warnings on vendored imgui sources
if(NOT MSVC)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
endif()

# --- Application Sources (platform-agnostic) ---
set(APP_SOURCES
    src/main.cpp
    src/preset_manager.cpp
    src/audio/audio_engine.cpp
    src/audio/recorder.cpp
    src/audio/effects/distortion.cpp
    src/audio/effects/overdrive.cpp
    src/audio/effects/delay.cpp
    src/audio/effects/reverb.cpp
    src/audio/effects/chorus.cpp
    src/audio/effects/equalizer.cpp
    src/audio/effects/noise_gate.cpp
    src/audio/effects/compressor.cpp
    src/audio/effects/cabinet_sim.cpp
    src/audio/effects/amp_simulator.cpp
    src/audio/effects/tuner.cpp
    src/gui/gui_manager.cpp
    src/gui/spectrum_analyzer.cpp
    src/gui/pedal_board.cpp
    src/gui/pedal_widget.cpp
    src/gui/command_history.cpp
)

# --- Platform-specific sources (selected at configure time) ---
if(EMSCRIPTEN)
    set(BACKEND_SOURCES  src/audio/audio_backend_sdl.cpp)
    set(DIALOG_SOURCES   src/gui/file_dialog_web.cpp)
else()
    set(BACKEND_SOURCES  src/audio/audio_backend_portaudio.cpp)
    set(DIALOG_SOURCES   src/gui/file_dialog_native.cpp)
endif()

# =============================================================================
# Target configuration: Emscripten (web) vs Native (desktop)
# =============================================================================
if(EMSCRIPTEN)

    # --- Emscripten / WebAssembly build ---
    add_executable(guitar-amp ${APP_SOURCES} ${BACKEND_SOURCES} ${DIALOG_SOURCES} ${IMGUI_SOURCES})

    target_include_directories(guitar-amp PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )

    # OpenGL ES 3 for ImGui's OpenGL3 backend on WebGL 2
    target_compile_definitions(guitar-amp PRIVATE IMGUI_IMPL_OPENGL_ES3 AMPLITRON_VERSION="${AMPLITRON_VERSION}")

    # Emscripten linker flags for Web Audio + AudioWorklet + SharedArrayBuffer
    target_link_options(guitar-amp PRIVATE
        "-sWASM=1"
        "-sUSE_SDL=2"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sAUDIO_WORKLET=1"
        "-sWASM_WORKERS=1"
        "-pthread"
        "-sSHARED_MEMORY=1"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sSTACK_SIZE=1048576"
        "-sPTHREAD_POOL_SIZE=4"
        "-Wno-pthreads-mem-growth"
        "--shell-file" "${CMAKE_SOURCE_DIR}/web/shell.html"
        "--preload-file" "${CMAKE_SOURCE_DIR}/presets@/presets"
    )

    # Output name: index.html (+ index.js, index.wasm generated by emcc)
    set_target_properties(guitar-amp PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
    )

    # Copy coi-serviceworker.js next to the build output for SharedArrayBuffer support
    add_custom_command(TARGET guitar-amp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/coi-serviceworker.js"
            "$<TARGET_FILE_DIR:guitar-amp>/coi-serviceworker.js"
    )

else() # NOT EMSCRIPTEN

    # --- Native desktop build (unchanged) ---

    # Windows resource file for app icon
    if(WIN32 AND NOT MSVC)
        set(WIN_RC ${CMAKE_SOURCE_DIR}/assets/app.rc)
        set(CMAKE_RC_COMPILER_INIT windres)
        enable_language(RC)
    endif()

    add_executable(guitar-amp ${APP_SOURCES} ${BACKEND_SOURCES} ${DIALOG_SOURCES} ${IMGUI_SOURCES} $<$<PLATFORM_ID:Windows>:${WIN_RC}>)

    target_include_directories(guitar-amp PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${PORTAUDIO_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
    )

    target_compile_definitions(guitar-amp PRIVATE AMPLITRON_VERSION="${AMPLITRON_VERSION}")

    target_link_libraries(guitar-amp PRIVATE
        ${PORTAUDIO_LIBRARIES}
        ${SDL2_LIBRARIES}
        ${OPENGL_LIBRARIES}
    )

    if(WIN32)
        target_link_libraries(guitar-amp PRIVATE winmm ole32 setupapi comdlg32 shell32)
        if(NOT MSVC)
            target_link_options(guitar-amp PRIVATE -mwindows)
        else()
            set_target_properties(guitar-amp PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(guitar-amp PRIVATE dl pthread)
    endif()

    # --- Test Target ---
    set(TEST_SOURCES
        tests/test_main.cpp
        tests/test_common.cpp
        tests/test_effects.cpp
        tests/test_preset_manager.cpp
        tests/test_recorder.cpp
        tests/test_theme.cpp
        tests/test_command_history.cpp
    )

    # Effect + core sources needed by tests (no GUI, no main)
    set(CORE_SOURCES
        src/preset_manager.cpp
        src/audio/audio_engine.cpp
        src/audio/audio_backend_portaudio.cpp
        src/audio/recorder.cpp
        src/gui/command_history.cpp
        src/audio/effects/distortion.cpp
        src/audio/effects/overdrive.cpp
        src/audio/effects/delay.cpp
        src/audio/effects/reverb.cpp
        src/audio/effects/chorus.cpp
        src/audio/effects/equalizer.cpp
        src/audio/effects/noise_gate.cpp
        src/audio/effects/compressor.cpp
        src/audio/effects/cabinet_sim.cpp
        src/audio/effects/amp_simulator.cpp
        src/audio/effects/tuner.cpp
    )

    add_executable(amplitron-tests ${TEST_SOURCES} ${CORE_SOURCES})

    target_include_directories(amplitron-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${PORTAUDIO_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
    )

    target_link_libraries(amplitron-tests PRIVATE
        ${PORTAUDIO_LIBRARIES}
    )

    if(WIN32)
        target_link_libraries(amplitron-tests PRIVATE winmm ole32 setupapi)
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(amplitron-tests PRIVATE dl pthread)
    endif()

    # Copy presets into build directory alongside the executable
    add_custom_target(copy_presets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/presets"
            "$<TARGET_FILE_DIR:guitar-amp>/presets"
        COMMENT "Copying presets to build directory"
    )
    add_dependencies(copy_presets guitar-amp)

    # Install
    install(TARGETS guitar-amp RUNTIME DESTINATION bin)
    install(DIRECTORY presets DESTINATION bin)

endif() # EMSCRIPTEN
