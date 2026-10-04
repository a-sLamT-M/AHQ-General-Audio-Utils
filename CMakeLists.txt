cmake_minimum_required(VERSION 3.25)
project(ahq_general_audio VERSION 0.1.0 LANGUAGES CXX)

add_library(ahq_audio_core STATIC
    src/gainProcessor.cpp
    src/linearSmoother.cpp
    src/quickFilter.cpp
    src/transientShaper.cpp
    src/saturator.cpp
    src/orbit.cpp
    src/loudnessMeter.cpp
    src/truePeakMeter.cpp
    src/grainDelay.cpp
    src/spaceLab.cpp
    src/chaos.cpp
    src/stereoMeter.cpp
    src/loudnessRange.cpp
    src/trueLimiter.cpp)
add_library(ahq::audio_core ALIAS ahq_audio_core)
target_include_directories(ahq_audio_core PUBLIC include)
target_compile_features(ahq_audio_core PUBLIC cxx_std_20)
set_target_properties(ahq_audio_core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name IN ITEMS gainProcessor_test quickFilter_test transientShaper_test
            saturator_test orbit_test loudnessMeter_test truePeakMeter_test grainDelay_test
            spaceLab_test chaos_test stereoMeter_test loudnessRange_test trueLimiter_test)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE ahq_audio_core)
        set_target_properties(${test_name} PROPERTIES CXX_EXTENSIONS OFF)
        add_test(NAME ${test_name} COMMAND ${test_name})
        if(MSVC)
            target_compile_options(${test_name} PRIVATE /W4)
        else()
            target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
endif()

if(MSVC)
    target_compile_options(ahq_audio_core PRIVATE /W4)
else()
    target_compile_options(ahq_audio_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(AHQ_BUILD_PLUGINS "Build JUCE plugin formats and standalone applications" OFF)
if(AHQ_BUILD_PLUGINS)
    enable_language(C)
    include(FetchContent)
    FetchContent_Declare(JUCE
        URL https://codeload.github.com/juce-framework/JUCE/tar.gz/be29c81492b6151c8ea8d14c840e1311963b3a83
        URL_HASH SHA256=f846e74503e5b959b1de514c772b05ab1cb05478b7513b19d6294cb2b3d2906a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugins)
    if(BUILD_TESTING)
        function(ahq_add_plugin_test test_name plugin_target plugin_directory)
            add_executable(${test_name} tests/${test_name}.cpp)
            target_include_directories(${test_name} PRIVATE ${plugin_directory}
                $<TARGET_PROPERTY:${plugin_target},INCLUDE_DIRECTORIES>)
            target_compile_definitions(${test_name} PRIVATE
                $<TARGET_PROPERTY:${plugin_target},COMPILE_DEFINITIONS>)
            target_link_libraries(${test_name} PRIVATE ${plugin_target} ahq::audio_core)
            set_target_properties(${test_name} PROPERTIES CXX_EXTENSIONS OFF)
            add_test(NAME ${test_name} COMMAND ${test_name})
        endfunction()
        ahq_add_plugin_test(quickFilterPlugin_test ahq_quick_filter plugins/quick-filter)
        ahq_add_plugin_test(transientPlugin_test ahq_transient plugins/transient)
        ahq_add_plugin_test(saturatorPlugin_test ahq_saturator plugins/saturator)
        ahq_add_plugin_test(orbitPlugin_test ahq_orbit plugins/orbit)
        ahq_add_plugin_test(ghostDelayPlugin_test ahq_ghost_delay plugins/ghost-delay)
        ahq_add_plugin_test(spaceLabPlugin_test ahq_space_lab plugins/space-lab)
        ahq_add_plugin_test(masterScopePlugin_test ahq_master_scope plugins/master-scope)
        ahq_add_plugin_test(chaosPlugin_test ahq_chaos plugins/chaos)
        ahq_add_plugin_test(trueLimiterPlugin_test ahq_true_limiter plugins/true-limiter)
    endif()
endif()
