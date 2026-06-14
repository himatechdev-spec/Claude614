cmake_minimum_required(VERSION 3.16)
project(cook VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -DDEBUG")

file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(cook ${SOURCES})

target_include_directories(cook PRIVATE include)
target_compile_definitions(cook PRIVATE _GNU_SOURCE)
target_link_libraries(cook PRIVATE pthread)

# Generate compile_commands.json so clangd (IDE) gets the correct flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

install(TARGETS cook DESTINATION /usr/local/bin)
install(FILES systemd/cook.service DESTINATION /etc/systemd/system)
