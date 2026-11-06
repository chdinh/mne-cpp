# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>

include_guard(GLOBAL)

# Qt's headers disable MSVC's narrowing warning C4244, so code without Qt is the first to hit it on Windows.
# Libraries built without Qt call this so that Clang reports the same conversions on every developer build.
function(mne_warn_on_narrowing target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE -Wshorten-64-to-32 -Wimplicit-float-conversion
                                             -Wno-implicit-int-float-conversion -Wfloat-conversion)
  endif()
endfunction()
