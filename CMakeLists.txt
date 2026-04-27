cmake_minimum_required(VERSION 3.22)

project(ezstemz VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use ccache as a compiler launcher when available. Hugely speeds up
# rebuilds of JUCE / demucs.cpp object files when toggling build types or
# nuking the build directory.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
endif()

# CMake 3.31 deprecates pre-3.5 minimums. demucs.cpp's CMakeLists.txt declares
# 3.0; opt in to a modern policy version when we add it as a subdirectory.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------

include(FetchContent)

if(DEFINED JUCE_DIR)
    message(STATUS "Using local JUCE at ${JUCE_DIR}")
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE-build)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# demucs.cpp (local source separation)
# ---------------------------------------------------------------------------

# Default to the sibling checkout next to this repo.
set(EZSTEMZ_DEMUCS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../demucs.cpp"
    CACHE PATH "Path to a demucs.cpp source tree (must contain CMakeLists.txt)")

if(NOT EXISTS "${EZSTEMZ_DEMUCS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Cannot find demucs.cpp at '${EZSTEMZ_DEMUCS_DIR}'. "
        "Clone https://github.com/sevagh/demucs.cpp (with --recurse-submodules) "
        "and either place it next to ezstemz/ or pass -DEZSTEMZ_DEMUCS_DIR=/path/to/demucs.cpp")
endif()

# We only need demucs.cpp.lib; EXCLUDE_FROM_ALL keeps its CLI / test executables
# out of our default build.
add_subdirectory(${EZSTEMZ_DEMUCS_DIR} demucs-cpp-build EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Standalone GUI app
# ---------------------------------------------------------------------------

juce_add_gui_app(ezstemz
    PRODUCT_NAME            "EZStemz"
    COMPANY_NAME            "ezstemz"
    BUNDLE_ID               "com.ezstemz.app"
    ICON_BIG                ""
    NEEDS_CURL              FALSE
    NEEDS_WEB_BROWSER       FALSE
)

juce_generate_juce_header(ezstemz)

target_sources(ezstemz PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/SongProcessorComponent.cpp
    Source/SongProcessorComponent.h
    Source/MultitrackPlayer.cpp
    Source/MultitrackPlayer.h
    Source/TrackStripComponent.cpp
    Source/TrackStripComponent.h
    Source/TransportBar.cpp
    Source/TransportBar.h
    Source/LocalSeparator.cpp
    Source/LocalSeparator.h
    Source/AppConfig.cpp
    Source/AppConfig.h
    Source/Project.h
    Source/ProjectsLibrary.cpp
    Source/ProjectsLibrary.h
    Source/ProjectsListComponent.cpp
    Source/ProjectsListComponent.h
    Source/SeparationService.cpp
    Source/SeparationService.h
)

target_compile_definitions(ezstemz PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ezstemz,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ezstemz,JUCE_VERSION>"
)

target_link_libraries(ezstemz PRIVATE
    demucs.cpp.lib
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Bundled demucs model
# ---------------------------------------------------------------------------
#
# The app loads its model from a fixed location relative to the executable
# (Contents/Resources/ on macOS, alongside the binary on Windows / Linux),
# so the user never has to configure a path. The filename is fixed; if you
# point EZSTEMZ_MODEL_FILE at a different model, rename the destination by
# also setting EZSTEMZ_MODEL_NAME.

set(EZSTEMZ_MODEL_FILE "$ENV{HOME}/projects/ggml-demucs/ggml-model-htdemucs-6s-f16.bin"
    CACHE FILEPATH "Demucs .bin model file to bundle inside the app")
set(EZSTEMZ_MODEL_NAME "ggml-model-htdemucs-6s-f16.bin"
    CACHE STRING "Filename used for the bundled model (must match AppConfig::kBundledModelName)")

if(NOT EXISTS "${EZSTEMZ_MODEL_FILE}")
    message(FATAL_ERROR
        "Cannot find demucs model at '${EZSTEMZ_MODEL_FILE}'. "
        "Pass -DEZSTEMZ_MODEL_FILE=/path/to/ggml-model-htdemucs-6s-f16.bin (and "
        "optionally -DEZSTEMZ_MODEL_NAME=<filename>) to point at a downloaded "
        "demucs.cpp model.")
endif()

target_compile_definitions(ezstemz PRIVATE
    EZSTEMZ_BUNDLED_MODEL_NAME="${EZSTEMZ_MODEL_NAME}"
)

if(APPLE)
    add_custom_command(TARGET ezstemz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:ezstemz>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EZSTEMZ_MODEL_FILE}"
            "$<TARGET_BUNDLE_CONTENT_DIR:ezstemz>/Resources/${EZSTEMZ_MODEL_NAME}"
        COMMENT "Bundling demucs model into EZStemz.app/Contents/Resources"
        VERBATIM)
else()
    add_custom_command(TARGET ezstemz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EZSTEMZ_MODEL_FILE}"
            "$<TARGET_FILE_DIR:ezstemz>/${EZSTEMZ_MODEL_NAME}"
        COMMENT "Copying demucs model next to EZStemz binary"
        VERBATIM)
endif()
