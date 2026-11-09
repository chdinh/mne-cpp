# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

# mne_acq_devices: vendor amplifiers as acquisition schemes, each with a simulator. Standard C++ only.
cmake_minimum_required(VERSION 3.21)
project(mne_acq_devices
  LANGUAGES CXX
)

set(SOURCES
  brainamp/brainamp_backend.cpp
  brainamp/brainamp_simulator.cpp
)

set(PRIVATE_HEADERS
  brainamp/brainamp_transport.h
)

set(HEADERS
  acq_devices_global.h
  acq_devices.h
)

# Brain Products' BrainAmpIoCtl.h is not redistributable
set(MNE_BRAINAMP_SDK_DIR "${CMAKE_SOURCE_DIR}/src/applications/mne_scan/plugins/brainamp" CACHE PATH
    "Directory with BrainAmpIoCtl.h; the BrainAmp driver interface is built on Windows when it is there")
set(_brainamp_hardware OFF)
if(WIN32 AND EXISTS "${MNE_BRAINAMP_SDK_DIR}/BrainAmpIoCtl.h")
  set(_brainamp_hardware ON)
  list(APPEND SOURCES brainamp/brainamp_driver_win32.cpp)
endif()

add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${PRIVATE_HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..> $<INSTALL_INTERFACE:include>)
target_link_libraries(${PROJECT_NAME} PUBLIC mne_acq)

if(_brainamp_hardware)
  target_include_directories(${PROJECT_NAME} PRIVATE "${MNE_BRAINAMP_SDK_DIR}")
  target_compile_definitions(${PROJECT_NAME} PUBLIC MNE_ACQ_BRAINAMP_HARDWARE)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE MNE_ACQ_DEVICES_LIBRARY)
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
        DESTINATION include/acq/devices
        COMPONENT sdk
    )
endforeach()
