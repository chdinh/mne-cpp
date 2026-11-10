# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

# The connector interface and the command vocabulary of the command port, shared by mne_rt_server and its plugins
# so that all of them see one IConnector type.
cmake_minimum_required(VERSION 3.14)
project(rtserver_connector LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

set(QT_REQUIRED_COMPONENTS Core)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})

add_library(${PROJECT_NAME}
    command.cpp
    command.h
    command_manager.cpp
    command_manager.h
    command_parser.cpp
    command_parser.h
    command_pattern.h
    iconnector.cpp
    iconnector.h
    observer_pattern.h
    raw_command.cpp
    raw_command.h
    rtserver_connector_global.h)

# Next to the other shared libraries, where the server, its plugins and the tests find it at run time
if(WIN32)
    set(_connector_dir ${BINARY_OUTPUT_DIRECTORY}/bin)
else()
    set(_connector_dir ${BINARY_OUTPUT_DIRECTORY}/lib)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY $<1:${_connector_dir}>
    RUNTIME_OUTPUT_DIRECTORY $<1:${_connector_dir}>
    ARCHIVE_OUTPUT_DIRECTORY $<1:${_connector_dir}>)

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)

target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    mne_fiff
    Eigen3::Eigen)
target_link_libraries(${PROJECT_NAME} PRIVATE mne_base)

target_compile_definitions(${PROJECT_NAME} PRIVATE RTSERVER_CONNECTOR_LIBRARY)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC STATICBUILD)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin COMPONENT applications
    LIBRARY DESTINATION lib COMPONENT applications
    ARCHIVE DESTINATION lib COMPONENT sdk)
