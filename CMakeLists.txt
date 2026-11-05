cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME AA_STM)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Renode's IWDG timing runs closer to host wall-clock than a real LSI
# oscillator, and this simulation already runs slower than real-time when
# being precise about TIM3 stepper timing (see the quantum note in
# run_stm32.resc) - so the watchdog can trip spuriously under Renode even
# though the firmware never actually hung. This option exists ONLY to
# build a separate binary for the Renode/CI harness; real hardware always
# builds through the Release/Debug presets, which never define this, so
# there is no way to accidentally flash a board without the watchdog.
option(RENODE_BUILD "Simulation-only build: skip IWDG init/refresh (Renode's timing can trip it spuriously). NEVER use for a build you intend to flash." OFF)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link options setup
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -u _printf_float
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/relay.c
    Core/Src/hatch.c
    Core/Src/axis.c
    Core/Src/alignment.c
    Core/Src/system.c
    Core/Src/commands.c
)

# Build traceability: aa_build_info.h is generated at configure time from
# git, into the build tree - a fresh clone builds with no extra tooling.
# (Re-run the CMake configure step to refresh it after new commits.)
find_package(Git QUIET)
set(AA_BUILD_HASH "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --abbrev=8
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE AA_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(AA_GIT_DESCRIBE)
        set(AA_BUILD_HASH "${AA_GIT_DESCRIBE}")
    endif()
endif()
file(WRITE ${CMAKE_BINARY_DIR}/generated/aa_build_info.h
    "#ifndef AA_BUILD_INFO_H\n#define AA_BUILD_INFO_H\n#define AA_BUILD_HASH \"${AA_BUILD_HASH}\"\n#endif\n")

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    $<$<BOOL:${RENODE_BUILD}>:RENODE_BUILD>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)