# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

cmake_minimum_required(VERSION 3.14)
project(mne_acq_tool LANGUAGES CXX)

set(QT_REQUIRED_COMPONENTS Core Network)
if(TARGET mne_disp)
    list(APPEND QT_REQUIRED_COMPONENTS Widgets)
endif()
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})

# The target is not called mne_acq: that name is the library.
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(${PROJECT_NAME} MANUAL_FINALIZATION main.cpp)
else()
    add_executable(${PROJECT_NAME} main.cpp)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network mne_acq mne_acq_fiff mne_acq_lsl mne_lsl)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME mne_acq
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE FALSE
)

# view (live traces) needs the display library; the other commands stay headless.
if(TARGET mne_disp)
    target_sources(${PROJECT_NAME} PRIVATE acq_view.cpp acq_view.h)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets mne_disp mne_fiff Eigen3::Eigen)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MNE_ACQ_VIEW)
endif()

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STATICBUILD)
endif()

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION . COMPONENT applications
    RUNTIME DESTINATION bin COMPONENT applications
    LIBRARY DESTINATION lib COMPONENT applications
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${PROJECT_NAME})
endif()
