# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

cmake_minimum_required(VERSION 3.14)
project(ex_acq_performance LANGUAGES CXX)

# Standard C++20 only: links mne_acq and nothing else.
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE mne_acq)

if(BUILD_TESTS)
    mne_add_test(${PROJECT_NAME} LABELS example integration TIMEOUT 300
        COMMAND ${PROJECT_NAME} --seconds 3 --row-seconds 0.5)
endif()
