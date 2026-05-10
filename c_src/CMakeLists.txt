# Copyright (c) 2026 Benoit Chesneau. Licensed under the MIT License.
# See the LICENSE file at the project root.

cmake_minimum_required(VERSION 3.20)
project(iommap_nif C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# Erlang ERTS include dir
# -----------------------------------------------------------------------------
if(NOT DEFINED ENV{ERTS_INCLUDE_DIR})
    execute_process(
        COMMAND erl -noshell
            -eval "io:format(\"~s\", [filename:join([code:root_dir(), \"erts-\" ++ erlang:system_info(version), \"include\"])])"
            -s init stop
        OUTPUT_VARIABLE ERTS_INCLUDE_DIR
    )
else()
    set(ERTS_INCLUDE_DIR $ENV{ERTS_INCLUDE_DIR})
endif()

# -----------------------------------------------------------------------------
# iommap_nif shared library
# -----------------------------------------------------------------------------
add_library(iommap_nif SHARED
    iommap_nif.c
    iommap_resource.c
    iommap_ops.c
    iommap_atoms.c
    iommap_platform.c
)

target_compile_options(iommap_nif PRIVATE
    -O2 -g -Wall -Wextra -Werror -fPIC
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(iommap_nif PRIVATE _GNU_SOURCE)
endif()

target_include_directories(iommap_nif PRIVATE
    ${ERTS_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

target_link_libraries(iommap_nif PRIVATE
    Threads::Threads
)

set_target_properties(iommap_nif PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "iommap_nif"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../priv"
)

if(APPLE)
    target_link_options(iommap_nif PRIVATE
        -undefined dynamic_lookup
        -flat_namespace
    )
endif()
