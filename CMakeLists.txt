cmake_minimum_required(VERSION 3.15)
project(tinta VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags for performance
if(MSVC)
    add_compile_options(/O2 /W4)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# MD4C markdown parser
include(FetchContent)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG release-0.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(md4c)

# Source files
set(SOURCES
    src/main_d2d.cpp
    src/markdown.cpp
)

set(HEADERS
    include/markdown.h
    include/types.h
)

# Main executable
add_executable(tinta WIN32 ${SOURCES} ${HEADERS})

target_include_directories(tinta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(tinta PRIVATE
    md4c
    d2d1
    dwrite
    shell32
)

target_compile_definitions(tinta PRIVATE UNICODE _UNICODE)

# Install
install(TARGETS tinta RUNTIME DESTINATION bin)
