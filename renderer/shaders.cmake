# qsb is a build-time compiler only. The renderer consumes native MSL/SPIR-V
# directly through SDL GPU and has no Qt runtime dependency.
find_package(Qt6 6.5 REQUIRED COMPONENTS ShaderTools)
set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${shader_dir}")
foreach(stage vert frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scope.${stage}")
    set(bundle "${shader_dir}/scope.${stage}.qsb")
    add_custom_command(OUTPUT "${bundle}"
        COMMAND Qt6::qsb --msl 12 -o "${bundle}" "${source}"
        DEPENDS "${source}" VERBATIM)
    foreach(format msl spirv)
        if(format STREQUAL "msl")
            set(version 12)
        else()
            set(version 100)
        endif()
        set(binary "${shader_dir}/${stage}.${format}")
        set(header "${binary}.h")
        add_custom_command(OUTPUT "${header}"
            COMMAND Qt6::qsb -x "${format},${version}" -o "${binary}" "${bundle}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${binary}" "-DOUTPUT=${header}"
                    "-DNAME=${stage}_${format}" -P "${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake"
            DEPENDS "${bundle}" "${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake" VERBATIM)
        target_sources(stars_renderer PRIVATE "${header}")
    endforeach()
endforeach()
target_include_directories(stars_renderer PRIVATE "${shader_dir}")
