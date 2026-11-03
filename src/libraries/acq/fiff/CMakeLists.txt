# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

# mne_acq_fiff: the FIFF and BIDS bridge of mne_acq (FiffInfo conversion, AcqRecorder). It is the only part
# of the acquisition library that depends on Qt; mne_acq itself stays standard C++.
cmake_minimum_required(VERSION 3.14)
project(mne_acq_fiff
  LANGUAGES CXX
)

set(CMAKE_AUTOMOC ON)

set(QT_REQUIRED_COMPONENTS Core)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})

set(SOURCES
  acq_fiff.cpp
  acq_recorder.cpp
  detail/acq_direct_file.cpp
)

set(PRIVATE_HEADERS
  detail/acq_direct_file.h
)

set(HEADERS
  acq_fiff_global.h
  acq_fiff.h
  acq_recorder.h
)

add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${PRIVATE_HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..> $<INSTALL_INTERFACE:include>)

set(QT_REQUIRED_COMPONENT_LIBS ${QT_REQUIRED_COMPONENTS})
list(TRANSFORM QT_REQUIRED_COMPONENT_LIBS PREPEND "Qt${QT_VERSION_MAJOR}::")

# FiffInfo appears in acq_fiff.h, so its clients need mne_fiff too.
target_link_libraries(${PROJECT_NAME} PUBLIC mne_acq mne_fiff)
target_link_libraries(${PROJECT_NAME} PRIVATE
  ${QT_REQUIRED_COMPONENT_LIBS}
  mne_bids
  Eigen3::Eigen)

target_compile_definitions(${PROJECT_NAME} PRIVATE MNE_ACQ_FIFF_LIBRARY)
# Physical values are raw * scale + offset exactly as decode() computes them: no FMA contraction.
if(MSVC)
    set_source_files_properties(acq_recorder.cpp PROPERTIES COMPILE_OPTIONS "/fp:precise;/fp:contract-")
else()
    set_source_files_properties(acq_recorder.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(${PROJECT_NAME} PRIVATE STATICBUILD)
endif()

install(TARGETS ${PROJECT_NAME} ${MNE_CPP_EXPORT_SET}
    RUNTIME DESTINATION bin COMPONENT runtime
    LIBRARY DESTINATION lib COMPONENT runtime
    ARCHIVE DESTINATION lib COMPONENT sdk
)

foreach(_header ${HEADERS})
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_header}"
        DESTINATION include/acq/fiff
        COMPONENT sdk
    )
endforeach()
