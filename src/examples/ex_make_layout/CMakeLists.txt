# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022-2026 MNE-CPP Authors
#   Gabriel Motta <gabrielbenmotta@gmail.com>
#   Juan GPC <jgarciaprieto@mgh.harvard.edu>
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

cmake_minimum_required(VERSION 3.14)
project(ex_make_layout LANGUAGES CXX)

# fiff_global.h still takes its export macros from Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
  mne_channels
  mne_fiff
  mne_base
  Eigen3::Eigen
  Qt${QT_VERSION_MAJOR}::Core
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STATICBUILD)
endif()
