# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

cmake_minimum_required(VERSION 3.14)
project(mne_com LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

set(QT_REQUIRED_COMPONENTS Core)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS ${QT_REQUIRED_COMPONENTS})

set(SOURCES
    com_global.cpp
    rt_command/command.cpp
    rt_command/command_manager.cpp
    rt_command/command_parser.cpp
    rt_command/raw_command.cpp
)

set(HEADERS
    com_global.h
    rt_command/command.h
    rt_command/command_pattern.h
    rt_command/command_manager.h
    rt_command/command_parser.h
    rt_command/observer_pattern.h
    rt_command/raw_command.h
)

set(FILE_TO_UPDATE com_global.cpp)

set(SOURCE_PATHS ${SOURCES})
list(TRANSFORM SOURCE_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set_source_files_properties(${FILE_TO_UPDATE} PROPERTIES OBJECT_DEPENDS "${SOURCE_PATHS}")

add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..> $<INSTALL_INTERFACE:include>)

set(QT_REQUIRED_COMPONENT_LIBS ${QT_REQUIRED_COMPONENTS})
list(TRANSFORM QT_REQUIRED_COMPONENT_LIBS PREPEND "Qt${QT_VERSION_MAJOR}::")

# The command headers expose QObject, QString and QJsonObject
target_link_libraries(${PROJECT_NAME} PUBLIC ${QT_REQUIRED_COMPONENT_LIBS})
target_link_libraries(${PROJECT_NAME} PRIVATE mne_base)

target_compile_definitions(${PROJECT_NAME} PRIVATE MNE_COM_LIBRARY MNE_GIT_HASH_SHORT="${MNE_GIT_HASH_SHORT}" MNE_GIT_HASH_LONG="${MNE_GIT_HASH_LONG}")

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STATICBUILD)
endif()

install(TARGETS ${PROJECT_NAME} ${MNE_CPP_EXPORT_SET}
    RUNTIME DESTINATION bin COMPONENT runtime
    LIBRARY DESTINATION lib COMPONENT runtime
    ARCHIVE DESTINATION lib COMPONENT sdk
)

# Install public headers for SDK
foreach(_header ${HEADERS})
    get_filename_component(_dir "${_header}" DIRECTORY)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_header}"
        DESTINATION include/com/${_dir}
        COMPONENT sdk
    )
endforeach()

