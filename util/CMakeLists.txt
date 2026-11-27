find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

add_library(stars_resources STATIC resources.cpp resources.h)
target_compile_features(stars_resources PUBLIC cxx_std_20)
set_target_properties(stars_resources PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(stars_resources PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(stars_resources PUBLIC Qt6::Core PRIVATE stars_zstd)

# Shared data used by both the profile catalog and STARS. Large video-map
# bundles remain external; the application target packages them at build time.
file(GLOB configuration_resources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/config/*.json.zst")
file(GLOB mva_resources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/mva/*.geojson.zst")
qt_add_resources(stars_resources common_resources
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/cfp/cifp.zst"
        "${PROJECT_SOURCE_DIR}/resources/meta/atc.json.zst"
        "${PROJECT_SOURCE_DIR}/resources/bmp/a.bin.zst"
        "${PROJECT_SOURCE_DIR}/resources/bmp/b.bin.zst"
        "${PROJECT_SOURCE_DIR}/resources/bmp/oa.bin.zst"
        "${PROJECT_SOURCE_DIR}/resources/bmp/ob.bin.zst"
        "${PROJECT_SOURCE_DIR}/resources/geo/apt.json.zst"
        "${PROJECT_SOURCE_DIR}/resources/geo/tiles.json.zst"
        "${PROJECT_SOURCE_DIR}/resources/cur/dcb.bin.zst"
        "${PROJECT_SOURCE_DIR}/resources/cur/keyboard.bin.zst"
        ${configuration_resources}
        ${mva_resources}
)
