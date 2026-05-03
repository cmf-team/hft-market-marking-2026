###########################################################
# Global settings

# Custom modules, if any
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/Modules)

option(BUILD_TESTS "Build tests" ON)

# Choose build type.  For Visual Studio this is mostly informational because
# the real configuration is selected by: cmake --build ... --config Debug/Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(ProcessorCount)
ProcessorCount(PROCESSOR_COUNT)

site_name(BUILD_NODE)

###########################################################
# Print info

set(DISABLE_PRINT_MESSAGE ${DISABLE_PRINT})
unset(DISABLE_PRINT CACHE)

macro(print_message)
    if(NOT DISABLE_PRINT_MESSAGE)
        message("${ARGV}")
    endif()
endmacro()

print_message("----------------------------------------")
print_message("Options:            BUILD_TESTS=${BUILD_TESTS}")
print_message("Build type:         ${CMAKE_BUILD_TYPE}")
print_message("Build host:         ${BUILD_NODE}")
print_message("Processor count:    ${PROCESSOR_COUNT}")
print_message("Host OS:            ${CMAKE_HOST_SYSTEM}")
print_message("Target OS:          ${CMAKE_SYSTEM}")
print_message("Compiler:           ${CMAKE_CXX_COMPILER}")
print_message("Compiler id:        ${CMAKE_CXX_COMPILER_ID}, frontend: ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}, version: ${CMAKE_CXX_COMPILER_VERSION}, launcher: ${CMAKE_CXX_COMPILER_LAUNCHER}")

###########################################################
# Compile settings

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static libs only" FORCE)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Debug/release flags and warnings.
# The original project was GCC-first and used flags like -O0/-gdwarf-4/-Wall/-Wextra.
# MSVC does not understand those flags, so keep separate compiler branches.
if(MSVC)
    add_compile_options(
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/DNDEBUG>
        /W4
        /WX
        /EHsc
        /permissive-
    )
else()
    add_compile_options(
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-DNDEBUG>
        -Werror
        -Wall
        -Wextra
    )
endif()

# Include dir
include_directories(src)
