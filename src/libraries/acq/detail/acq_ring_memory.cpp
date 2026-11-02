//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_ring_memory.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Ring memory from the operating system: VirtualAlloc on Windows, mmap on POSIX.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_ring_memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define ACQ_RING_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif __has_include(<sys/mman.h>) && !defined(__EMSCRIPTEN__)
#define ACQ_RING_MMAP
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB::detail;

namespace
{

std::size_t roundUp(std::size_t value, std::size_t multiple) noexcept
{
    return (value + multiple - 1) / multiple * multiple;
}

#if defined(ACQ_RING_WINDOWS)
std::byte* virtualAlloc(void* address, std::size_t bytes, int numaNode, int& boundNode) noexcept
{
    constexpr DWORD kType = MEM_RESERVE | MEM_COMMIT;
    ULONG highest = 0;
    if (numaNode >= 0 && GetNumaHighestNodeNumber(&highest) != 0 && static_cast<ULONG>(numaNode) <= highest) {
        void* p = VirtualAllocExNuma(GetCurrentProcess(), address, bytes, kType, PAGE_READWRITE,
                                     static_cast<DWORD>(numaNode));
        if (p != nullptr) {
            boundNode = numaNode;
            return static_cast<std::byte*>(p);
        }
    }
    return static_cast<std::byte*>(VirtualAlloc(address, bytes, kType, PAGE_READWRITE));
}
#endif

#if defined(ACQ_RING_MMAP) && defined(__linux__) && defined(SYS_mbind)
bool bindToNode(void* address, std::size_t bytes, int node) noexcept
{
    constexpr int kMaxNodes = 1024;
    constexpr int kBits = static_cast<int>(8 * sizeof(unsigned long));
    if (node < 0 || node >= kMaxNodes) {
        return false;
    }
    unsigned long mask[kMaxNodes / kBits] = {};
    mask[node / kBits] = 1UL << (node % kBits);
    constexpr long kMpolBind = 2;
    return syscall(SYS_mbind, address, bytes, kMpolBind, mask, static_cast<unsigned long>(kMaxNodes) + 1, 0UL) == 0;
}
#endif

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

RingMemory ACQLIB::detail::allocateRingMemory(std::size_t bytes, std::size_t alignment, bool hugePages,
                                              int numaNode) noexcept
{
    RingMemory out;
    if (hugePages) {
        alignment = std::max(alignment, kHugePageBytes);
    }
#if defined(ACQ_RING_WINDOWS)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    out.bytes = roundUp(bytes, hugePages ? kHugePageBytes : static_cast<std::size_t>(info.dwPageSize));
    if (alignment <= info.dwAllocationGranularity) {
        out.data = virtualAlloc(nullptr, out.bytes, numaNode, out.numaNode);
        return out;
    }
    // Reserve a larger range to find an aligned address, release it and map there; another thread may take the
    // address in between, so retry.
    for (int attempt = 0; attempt < 16 && out.data == nullptr; ++attempt) {
        void* probe = VirtualAlloc(nullptr, out.bytes + alignment, MEM_RESERVE, PAGE_NOACCESS);
        if (probe == nullptr) {
            break;
        }
        const auto aligned = roundUp(reinterpret_cast<std::uintptr_t>(probe), alignment);
        VirtualFree(probe, 0, MEM_RELEASE);
        out.numaNode = -1;
        out.data = virtualAlloc(reinterpret_cast<void*>(aligned), out.bytes, numaNode, out.numaNode);
    }
    return out;
#elif defined(ACQ_RING_MMAP)
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    alignment = std::max(alignment, page);
    out.bytes = roundUp(bytes, hugePages ? kHugePageBytes : page);
    const std::size_t total = out.bytes + alignment - page;
    void* mapped = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        return out;
    }
    auto* first = static_cast<std::byte*>(mapped);
    out.data = first + (roundUp(reinterpret_cast<std::uintptr_t>(first), alignment) - reinterpret_cast<std::uintptr_t>(first));
    const auto lead = static_cast<std::size_t>(out.data - first);
    if (lead > 0) {
        munmap(first, lead);
    }
    if (total - lead > out.bytes) {
        munmap(out.data + out.bytes, total - lead - out.bytes);
    }
#if defined(MADV_HUGEPAGE)
    out.hugePages = hugePages && madvise(out.data, out.bytes, MADV_HUGEPAGE) == 0;
#endif
#if defined(__linux__) && defined(SYS_mbind)
    if (bindToNode(out.data, out.bytes, numaNode)) {
        out.numaNode = numaNode;
    }
#endif
    static_cast<void>(numaNode);
    return out;
#else
    static_cast<void>(numaNode);
    out.bytes = roundUp(bytes, alignment);
    out.data = static_cast<std::byte*>(std::aligned_alloc(alignment, out.bytes));
    if (out.data != nullptr) {
        std::memset(out.data, 0, out.bytes);
    }
    return out;
#endif
}

//=============================================================================================================

void ACQLIB::detail::lockRingMemory(RingMemory& memory) noexcept
{
    if (memory.data == nullptr || memory.locked) {
        return;
    }
#if defined(ACQ_RING_WINDOWS)
    memory.locked = VirtualLock(memory.data, memory.bytes) != 0;
#elif defined(ACQ_RING_MMAP)
    memory.locked = mlock(memory.data, memory.bytes) == 0;
#endif
}

//=============================================================================================================

void ACQLIB::detail::releaseRingMemory(RingMemory& memory) noexcept
{
    if (memory.data == nullptr) {
        return;
    }
#if defined(ACQ_RING_WINDOWS)
    if (memory.locked) {
        VirtualUnlock(memory.data, memory.bytes);
    }
    VirtualFree(memory.data, 0, MEM_RELEASE);
#elif defined(ACQ_RING_MMAP)
    if (memory.locked) {
        munlock(memory.data, memory.bytes);
    }
    munmap(memory.data, memory.bytes);
#else
    std::free(memory.data);
#endif
    memory = RingMemory();
}
