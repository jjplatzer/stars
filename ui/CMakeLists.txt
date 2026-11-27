find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Network Concurrent)

add_library(stars_ui OBJECT
    adaptation.cpp adaptation.h
    palette.cpp palette.h
    playback.cpp playback.h
    weather.cpp weather.h itws.cpp itws.h
    metar.cpp metar.h
    pane.cpp
    pane.h
    window.cpp
    window.h
    catalog.cpp
    catalog.h
    store.cpp
    store.h
    select.cpp
    select.h
    option.h
    scrollbar.cpp
    scrollbar.h
    alertaudio.cpp alertaudio.h
    theme.cpp
    theme.h
)
include(../cmake/recording.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${recording_config}")
target_compile_definitions(stars_ui PRIVATE STARS_RETENTION_DAYS=${retention_days} STARS_BUFFER_MINUTES=${buffer_minutes})
# Read the ITWS site allowlist from the same configuration as the recorder.
file(READ "${recording_config}" recording_yaml)
string(REGEX MATCH "\n  itws:[ \t]*\n    facilities:[ \t]*\n(      - [A-Z0-9]+[ \t]*\n)+" itws_block "${recording_yaml}\n")
if(NOT itws_block)
    message(FATAL_ERROR "Missing swim.itws.facilities in config.yaml")
endif()
string(REGEX MATCHALL "- [A-Z0-9]+" itws_entries "${itws_block}")
list(TRANSFORM itws_entries REPLACE "^- " "")
list(JOIN itws_entries "," itws_sites)
target_compile_definitions(stars_ui PRIVATE STARS_ITWS_SITES="${itws_sites}")
set_target_properties(stars_ui PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
target_compile_features(stars_ui PUBLIC cxx_std_20)
target_include_directories(stars_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(stars_ui PUBLIC Qt6::Concurrent Qt6::Network Qt6::Widgets Qt6::Svg stars_scope stars_net)
if(APPLE)
    target_sources(stars_ui PRIVATE native.mm)
    set_source_files_properties(native.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(stars_ui PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    target_link_libraries(stars_ui PUBLIC "-framework Cocoa")
endif()

qt_add_executable(stars MACOSX_BUNDLE main.cpp)
set_target_properties(stars PROPERTIES
    CXX_EXTENSIONS OFF
    MACOSX_BUNDLE_BUNDLE_NAME "STARS"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.jjplatzer.stars"
)
target_link_libraries(stars PRIVATE stars_ui)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/stars-$<CONFIG>.path"
    CONTENT "$<TARGET_FILE:stars>\n" NEWLINE_STYLE UNIX)
# Profile-page styles and application UI fonts; STARS resources live in util/.
qt_add_resources(stars_ui ui_resources
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/audio/msaw.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/profiles.qss"
        "${PROJECT_SOURCE_DIR}/fonts/Inter-Regular.ttf.zst"
        "${PROJECT_SOURCE_DIR}/fonts/Inter-Black.ttf.zst"
        "${PROJECT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf.zst"
        "${PROJECT_SOURCE_DIR}/fonts/material-symbols.ttf.zst"

)

# Large map bundles remain external and travel with the executable. Do not
# depend on the working directory or on a sibling REDS checkout at runtime.
if(APPLE)
    set(stars_resource_dir "$<TARGET_BUNDLE_CONTENT_DIR:stars>/Resources/resources")
else()
    set(stars_resource_dir "$<TARGET_FILE_DIR:stars>/resources")
endif()
add_custom_target(stars_maps
    COMMAND ${CMAKE_COMMAND} -E make_directory "${stars_resource_dir}/videomaps"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/resources/videomaps" "${stars_resource_dir}/videomaps"
)
add_dependencies(stars stars_maps)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(stars_ui_tests tests/profiles.cpp)
    set_target_properties(stars_ui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(stars_ui_tests PRIVATE stars_ui Qt6::Test stars_zstd)
    if(APPLE)
        target_sources(stars_ui_tests PRIVATE tests/native.mm)
        set_target_properties(stars_ui_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
        target_link_libraries(stars_ui_tests PRIVATE "-framework Cocoa")
    endif()
    qt_add_executable(stars_weather_tests tests/weather.cpp)
    set_target_properties(stars_weather_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(stars_weather_tests PRIVATE stars_ui Qt6::Test stars_zstd)
    add_test(NAME stars_weather COMMAND stars_weather_tests)
    # Widget layout tests use deterministic offscreen focus and hover. AppKit/SDL
    # keyboard and cursor checks need the native event loop when requested.
    add_test(NAME stars_ui COMMAND ${CMAKE_COMMAND} -E env
        --unset=STARS_TEST_PANE --unset=STARS_TEST_N90_TARGETS --unset=STARS_TEST_NCT_WEATHER
        $<TARGET_FILE:stars_ui_tests> -platform offscreen)
    add_test(NAME stars_ui_native COMMAND stars_ui_tests cursorTransitions itwsMenu displayControls
        targetRecording weatherRecording openRadarPane)
    set_tests_properties(stars_ui stars_ui_native PROPERTIES ENVIRONMENT "STARS_RESOURCES=${PROJECT_SOURCE_DIR}/resources")
endif()
