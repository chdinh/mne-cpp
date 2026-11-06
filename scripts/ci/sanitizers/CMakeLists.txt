# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

# Builds mne_acq, its FIFF bridge mne_acq_fiff (with the FIFF and BIDS libraries it needs) and every
# src/testframes/test_acq_* test with one sanitizer set, without the rest of MNE-CPP. Eigen is taken from
# src/external/eigen. Usage: cmake -S scripts/ci/sanitizers -B build-tsan -DMNE_SANITIZE=thread
cmake_minimum_required(VERSION 3.21)
project(mne_acq_sanitizers LANGUAGES CXX)

set(MNE_SANITIZE "address,undefined" CACHE STRING "Value passed to -fsanitize=")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(BUILD_SHARED_LIBS OFF)

add_compile_options(-fsanitize=${MNE_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
add_link_options(-fsanitize=${MNE_SANITIZE})

find_package(Qt6 REQUIRED COMPONENTS Core Test)

get_filename_component(MNE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
find_package(Eigen3 REQUIRED NO_MODULE PATHS "${MNE_ROOT}/src/external/eigen")
add_subdirectory("${MNE_ROOT}/src/libraries/acq" acq)
foreach(_lib math utils fiff bids acq/fiff lsl acq/lsl)
    add_subdirectory("${MNE_ROOT}/src/libraries/${_lib}" "lib_${_lib}")
endforeach()

enable_testing()
file(GLOB _acq_tests LIST_DIRECTORIES true "${MNE_ROOT}/src/testframes/test_acq_*")
foreach(_dir ${_acq_tests})
    get_filename_component(_name "${_dir}" NAME)
    add_executable(${_name} "${_dir}/${_name}.cpp")
    if(_name STREQUAL "test_acq_recorder" OR _name STREQUAL "test_acq_fiff_replay")
        # mne_acq_fiff brings mne_acq and mne_fiff; listing them again duplicates the static archives.
        target_link_libraries(${_name} PRIVATE Qt6::Core Qt6::Test mne_acq_fiff Eigen3::Eigen)
        target_compile_definitions(${_name} PRIVATE ACQ_RECORDER_CHECK_SCRIPT="${_dir}/check_recording.py")
    elseif(_name STREQUAL "test_acq_lsl")
        target_link_libraries(${_name} PRIVATE Qt6::Core Qt6::Test mne_acq_lsl mne_lsl)
    else()
        target_link_libraries(${_name} PRIVATE Qt6::Core Qt6::Test mne_acq)
    endif()
    if(_name STREQUAL "test_acq_dlpack")
        # An instrumented probe cannot be loaded into an uninstrumented Python, so the NumPy round trip is skipped.
        target_sources(${_name} PRIVATE "${_dir}/acq_dlpack_probe.cpp")
        target_include_directories(${_name} PRIVATE "${MNE_ROOT}/src/external/dlpack/include")
        target_compile_definitions(${_name} PRIVATE ACQ_DLPACK_PROBE_PATH="" ACQ_LIBRARY_DIR="")
    endif()
    add_test(NAME ${_name} COMMAND ${_name})
endforeach()
