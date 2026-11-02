# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

# Builds mne_acq and every src/testframes/test_acq_* test with one sanitizer set, without the rest of
# MNE-CPP. Usage: cmake -S scripts/ci/sanitizers -B build-tsan -DMNE_SANITIZE=thread
cmake_minimum_required(VERSION 3.21)
project(mne_acq_sanitizers LANGUAGES CXX)

set(MNE_SANITIZE "address,undefined" CACHE STRING "Value passed to -fsanitize=")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(BUILD_SHARED_LIBS OFF)

add_compile_options(-fsanitize=${MNE_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
add_link_options(-fsanitize=${MNE_SANITIZE})

find_package(Qt6 REQUIRED COMPONENTS Core Test)

get_filename_component(MNE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_subdirectory("${MNE_ROOT}/src/libraries/acq" acq)

enable_testing()
file(GLOB _acq_tests LIST_DIRECTORIES true "${MNE_ROOT}/src/testframes/test_acq_*")
foreach(_dir ${_acq_tests})
    get_filename_component(_name "${_dir}" NAME)
    add_executable(${_name} "${_dir}/${_name}.cpp")
    target_link_libraries(${_name} PRIVATE Qt6::Core Qt6::Test mne_acq)
    if(_name STREQUAL "test_acq_dlpack")
        # An instrumented probe cannot be loaded into an uninstrumented Python, so the NumPy round trip is skipped.
        target_sources(${_name} PRIVATE "${_dir}/acq_dlpack_probe.cpp")
        target_include_directories(${_name} PRIVATE "${MNE_ROOT}/src/external/dlpack/include")
        target_compile_definitions(${_name} PRIVATE ACQ_DLPACK_PROBE_PATH="" ACQ_LIBRARY_DIR="")
    endif()
    add_test(NAME ${_name} COMMAND ${_name})
endforeach()
