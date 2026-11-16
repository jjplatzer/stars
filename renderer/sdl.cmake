# Use a packaged SDL3 where available; otherwise build a pinned official release.
find_package(SDL3 3.2 CONFIG QUIET)
if(NOT SDL3_FOUND)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    # The renderer uses SDL GPU exclusively; avoid OpenGL/GLES SDK dependencies.
    set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://libsdl.org/release/SDL3-3.2.20.tar.gz
        URL_HASH SHA256=467600ae090dd28616fa37369faf4e3143198ff1da37729b552137e47f751a67
    )
    FetchContent_MakeAvailable(SDL3)
endif()
