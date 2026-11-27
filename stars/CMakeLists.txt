find_package(ZLIB REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui)
add_library(stars_scope STATIC
    atpa.cpp atpa.h fusion.cpp fusion.h msaw.cpp msaw.h
    stars.cpp stars.h tracks.cpp datablock.cpp datablock.h
    nexrad.cpp nexrad.h itws.cpp itws.h colors.h
    config.cpp config.h
    commands.cpp cmdops.cpp cmdslew.cpp cmdsupe.cpp cmdtools.cpp cmdsetup.cpp restrictions.cpp crda.cpp
    prefs.cpp prefs.h
    dcb.cpp dcb.h
    videomap.cpp
    ../aviation/cifp.cpp
    ../aviation/finals.cpp
    tools.cpp
    lists.cpp
    font.cpp font.h
    cursors.cpp cursors.h
)
target_compile_features(stars_scope PUBLIC cxx_std_20)
set_target_properties(stars_scope PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(stars_scope PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(stars_scope PRIVATE Qt6::Gui ZLIB::ZLIB PUBLIC stars_net stars_renderer stars_resources stars_radar)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test Gui)
    qt_add_executable(stars_fusion_tests tests/fusion.cpp)
    set_target_properties(stars_fusion_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(stars_fusion_tests PRIVATE stars_scope Qt6::Test)
    add_test(NAME stars_fusion COMMAND stars_fusion_tests)

    qt_add_executable(stars_dcb_tests tests/dcb.cpp)
    set_target_properties(stars_dcb_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(stars_dcb_tests PRIVATE stars_scope Qt6::Test Qt6::Gui)
    target_compile_definitions(stars_dcb_tests PRIVATE STARS_DCB_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference")
    add_test(NAME stars_dcb COMMAND stars_dcb_tests)

    qt_add_executable(stars_display_tests tests/display.cpp)
    set_target_properties(stars_display_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(stars_display_tests PRIVATE stars_scope Qt6::Test Qt6::Gui)
    add_test(NAME stars_display COMMAND stars_display_tests)

    qt_add_executable(stars_radar_tests tests/radar.cpp)
    set_target_properties(stars_radar_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(stars_radar_tests PRIVATE stars_scope Qt6::Test stars_zstd)
    target_compile_definitions(stars_radar_tests PRIVATE STARS_MAP_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/maps.json")
    add_test(NAME stars_radar COMMAND stars_radar_tests)
    set_tests_properties(stars_radar PROPERTIES ENVIRONMENT "STARS_RESOURCES=${PROJECT_SOURCE_DIR}/resources")
endif()
