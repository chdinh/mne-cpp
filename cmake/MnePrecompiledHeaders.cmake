# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

#[=======================================================================[.rst:
MnePrecompiledHeaders
---------------------

``mne_precompile_headers(<directory>)`` gives every library target defined in
``<directory>`` or below a precompiled header of the standard library, plus
Eigen and Qt Core for targets that link them. A Qt-free or Eigen-free library
gets neither, so precompiling cannot hide a dependency it does not have.

Controlled by ``MNE_PRECOMPILED_HEADERS``; one CI job builds without them so a
missing ``#include`` still fails somewhere.
#]=======================================================================]

option(MNE_PRECOMPILED_HEADERS "Precompile the standard library, Eigen and Qt Core headers of each library" ON)

set(_MNE_PCH_STD
    <algorithm> <array> <cmath> <cstddef> <cstdint> <cstring> <expected> <filesystem> <format> <functional>
    <map> <memory> <optional> <span> <string> <string_view> <utility> <vector>)
set(_MNE_PCH_EIGEN <Eigen/Core> <Eigen/Dense> <Eigen/SparseCore>)
set(_MNE_PCH_QT <QByteArray> <QDebug> <QList> <QObject> <QSharedPointer> <QString> <QStringList>)

function(_mne_pch_targets directory out)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        _mne_pch_targets("${child}" child_targets)
        list(APPEND targets ${child_targets})
    endforeach()
    set(${out} ${targets} PARENT_SCOPE)
endfunction()

function(mne_precompile_headers directory)
    if(NOT MNE_PRECOMPILED_HEADERS)
        return()
    endif()
    _mne_pch_targets("${directory}" targets)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(NOT type MATCHES "^(SHARED|STATIC|MODULE)_LIBRARY$")
            continue()
        endif()
        get_target_property(links ${target} LINK_LIBRARIES)
        set(headers ${_MNE_PCH_STD})
        if(links MATCHES "Eigen3::Eigen")
            list(APPEND headers ${_MNE_PCH_EIGEN})
        endif()
        if(links MATCHES "Qt[56]::Core")
            list(APPEND headers ${_MNE_PCH_QT})
        endif()
        target_precompile_headers(${target} PRIVATE ${headers})
    endforeach()
endfunction()
