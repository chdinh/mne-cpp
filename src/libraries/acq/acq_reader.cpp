//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_reader.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqReader definitions: zero-copy acquire/release with exact loss accounting.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_reader.h"

#include "acq_stream.h"
#include "detail/acq_stream_core.h"

#include <algorithm>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace ACQLIB::detail;

namespace
{
using Clock = std::chrono::steady_clock;

constexpr std::chrono::nanoseconds kMaxTimeout = std::chrono::hours(24 * 365);

struct RecordWindow
{
    std::uint64_t first = 0;
    std::uint64_t end = 0;
};

// Records of @p ring with a sample index in [from, to), starting at @p cursor; counts overwritten records.
template<typename T, typename SampleOf>
RecordWindow selectRecords(const RecordRing<T>& ring, std::uint64_t cursor, std::uint64_t from, std::uint64_t to,
                           std::atomic<std::uint64_t>& lostCounter, SampleOf sampleOf) noexcept
{
    const std::uint64_t oldest = ring.oldest();
    if (cursor < oldest) {
        lostCounter.fetch_add(oldest - cursor, std::memory_order_relaxed);
        cursor = oldest;
    }
    const std::uint64_t published = ring.published.load(std::memory_order_acquire);
    while (cursor < published && sampleOf(ring.records[cursor & ring.mask]) < from) {
        ++cursor;
    }
    RecordWindow window{cursor, cursor};
    while (window.end < published && sampleOf(ring.records[window.end & ring.mask]) < to) {
        ++window.end;
    }
    return window;
}

template<typename T>
AcqSpanPair<const T> recordSpans(const std::vector<T>& records, std::uint64_t mask, RecordWindow window) noexcept
{
    const std::uint64_t n = window.end - window.first;
    if (n == 0) {
        return {};
    }
    const std::uint64_t index = window.first & mask;
    const std::uint64_t head = std::min<std::uint64_t>(n, mask + 1 - index);
    return {std::span<const T>(records.data() + index, static_cast<std::size_t>(head)),
            std::span<const T>(records.data(), static_cast<std::size_t>(n - head))};
}

AcqSpanPair<const std::byte> payloadSpans(const RecordRing<AcqSegmentHeader>& ring, RecordWindow window) noexcept
{
    const std::uint64_t n = window.end - window.first;
    if (n == 0 || ring.payloadBytes == 0) {
        return {};
    }
    const std::uint64_t index = window.first & ring.mask;
    const std::uint64_t head = std::min<std::uint64_t>(n, ring.capacity() - index);
    const std::size_t pb = ring.payloadBytes;
    return {std::span<const std::byte>(ring.payloads.data() + index * pb, static_cast<std::size_t>(head) * pb),
            std::span<const std::byte>(ring.payloads.data(), static_cast<std::size_t>(n - head) * pb)};
}

template<typename T>
bool recordsOverwritten(const RecordRing<T>& ring, RecordWindow window) noexcept
{
    if (window.end == window.first) {
        return false;
    }
    const std::uint64_t r = ring.reserved.load(std::memory_order_relaxed);
    return r > ring.capacity() && r - ring.capacity() > window.first;
}

/**
 * Gating reader: decides for every output commit visible up to @p written whether it is played. A commit
 * whose scheduled start the device has already passed (played as fill) is dropped whole and counted late.
 */
void validateCommits(AcqStreamCore& c, ReaderSlot& slot, std::uint64_t pos, std::uint64_t written) noexcept
{
    const std::uint64_t published = c.outputCommits.published.load(std::memory_order_acquire);
    std::uint64_t i = slot.commitCursor;
    for (; i < published; ++i) {
        const OutputCommit& commit = c.outputCommits.records[i & c.outputCommits.mask];
        if (commit.end > written) {
            break;
        }
        if (commit.start < pos) {
            slot.dropEnd = std::max(slot.dropEnd, commit.end);
            c.lateCommits.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (i != slot.commitCursor) {
        slot.commitCursor = i;
        c.commitsValidated.store(i, std::memory_order_seq_cst);
        c.notifyWriter();
    }
}

void detach(AcqStreamCore& core, std::uint32_t index) noexcept
{
    ReaderSlot& slot = core.slots[index];
    slot.holding = false;
    if (slot.gating) {
        core.gatingSlot.store(-1, std::memory_order_release);
        core.deviceGone.store(true, std::memory_order_seq_cst);
        core.notifyWriter();
    }
    slot.inUse.store(false, std::memory_order_release);
}
} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

AcqReader::AcqReader() noexcept = default;

AcqReader::AcqReader(std::shared_ptr<AcqStreamCore> core, std::uint32_t slot) noexcept
: m_core(std::move(core))
, m_slot(slot)
{
}

AcqReader::~AcqReader()
{
    if (m_core) {
        detach(*m_core, m_slot);
    }
}

AcqReader::AcqReader(AcqReader&& other) noexcept
: m_core(std::move(other.m_core))
, m_slot(other.m_slot)
{
}

AcqReader& AcqReader::operator=(AcqReader&& other) noexcept
{
    if (this != &other) {
        if (m_core) {
            detach(*m_core, m_slot);
        }
        m_core = std::move(other.m_core);
        m_slot = other.m_slot;
    }
    return *this;
}

bool AcqReader::isValid() const noexcept
{
    return m_core != nullptr;
}

AcqReader::ReadMode AcqReader::mode() const noexcept
{
    return m_core ? m_core->slots[m_slot].mode : ReadMode::Lossless;
}

std::size_t AcqReader::channelCount() const noexcept
{
    return m_core ? m_core->channels : 0;
}

AcqStream AcqReader::stream() const noexcept
{
    return AcqStream(m_core);
}

AcqExpected<AcqBlockView> AcqReader::acquire(std::size_t minSamples, std::chrono::nanoseconds timeout,
                                             std::size_t maxSamples) noexcept
{
    if (!m_core) {
        return AcqErrorCode::InvalidState;
    }
    AcqStreamCore& c = *m_core;
    ReaderSlot& slot = c.slots[m_slot];
    if (slot.holding) {
        return AcqErrorCode::InvalidState;
    }
    if (minSamples > c.capacity) {
        return AcqErrorCode::InvalidParameter;
    }
    const std::uint64_t maxCount = maxSamples == 0 ? c.capacity : std::min<std::uint64_t>(maxSamples, c.capacity);
    const bool latest = slot.mode == ReadMode::Latest;
    const AcqWait wait = slot.mode == ReadMode::LowLatency ? AcqWait::Spin : slot.options.wait;
    const auto deadline = Clock::now() + std::clamp(timeout, std::chrono::nanoseconds(0), kMaxTimeout);

    std::uint64_t pos = 0;
    std::uint64_t written = 0;
    std::uint64_t oldest = 0;
    while (true) {
        pos = slot.position.load(std::memory_order_relaxed);
        written = c.committed.load(std::memory_order_acquire);
        const std::uint64_t r = c.reserved.load(std::memory_order_acquire);
        oldest = r > c.capacity ? r - c.capacity : 0;
        if (!latest && pos < oldest) {
            const std::uint64_t lostNow = oldest - pos;
            slot.pendingLost += lostNow;
            slot.lost.fetch_add(lostNow, std::memory_order_relaxed);
            pos = oldest;
            slot.position.store(pos, std::memory_order_release);
        }
        std::uint64_t available = written > pos ? written - pos : 0;
        const bool finished = c.finished.load(std::memory_order_acquire);
        if (finished) {
            written = c.committed.load(std::memory_order_acquire);
            available = written > pos ? written - pos : 0;
        }
        if (slot.gating) {
            validateCommits(c, slot, pos, written);
            if (pos < slot.dropEnd) {
                // Nothing playable until the device has played fill past the dropped commit.
                if (minSamples != 0) {
                    return AcqErrorCode::Timeout;
                }
                written = pos;
                break;
            }
        }
        if (minSamples == 0 || available >= minSamples || (finished && available > 0)) {
            break;
        }
        if (finished) {
            return AcqErrorCode::EndOfStream;
        }
        if (Clock::now() >= deadline) {
            return AcqErrorCode::Timeout;
        }
        const std::uint64_t from = pos;
        auto ready = [&c, from, minSamples] {
            const std::uint64_t w = c.committed.load(std::memory_order_seq_cst);
            return (w > from && w - from >= minSamples) || c.finished.load(std::memory_order_seq_cst);
        };
        waitFor(ready, wait, slot.options.spinFor, deadline, slot.waiting, slot.wake, &c.waiters);
    }

    const std::uint64_t available = written > pos ? written - pos : 0;
    std::uint64_t start = pos;
    std::uint64_t count = std::min(available, maxCount);
    std::uint64_t skipped = 0;
    if (latest) {
        count = maxSamples != 0 ? std::min<std::uint64_t>(available, maxCount)
                                : (minSamples != 0 ? std::min<std::uint64_t>(available, minSamples) : available);
        count = std::min(count, written > oldest ? written - oldest : 0);
        start = written - count;
        skipped = start - pos;
        slot.position.store(start, std::memory_order_release);
    }

    const std::size_t versionIndex = c.versionAt(start);
    if (versionIndex + 1 < c.versionCount.load(std::memory_order_acquire)) {
        const std::uint64_t nextFrom = c.versions[versionIndex + 1]->validFrom;
        if (nextFrom < start + count) {
            count = nextFrom - start;
        }
    }

    const RecordWindow events = selectRecords(c.events, slot.eventCursor, start, start + count, slot.eventsLost,
                                              [](const AcqEvent& e) { return e.sample; });
    const RecordWindow segments = selectRecords(c.segments, slot.segmentCursor, start, start + count,
                                                slot.segmentsLost,
                                                [](const AcqSegmentHeader& h) { return h.firstSample; });

    AcqBlockView view;
    ViewAccess::setSamples(view, c, start, count, *c.versions[versionIndex]);
    ViewAccess::setRead(view, slot.pendingLost, skipped, recordSpans(c.events.records, c.events.mask, events),
                        recordSpans(c.segments.records, c.segments.mask, segments), payloadSpans(c.segments, segments),
                        c.segments.payloadBytes);
    slot.ticket = (static_cast<std::uint64_t>(m_slot) << 48U) | (++slot.generation & 0xFFFFFFFFFFFFULL);
    ViewAccess::setTicket(view, slot.ticket);
    slot.holding = true;
    slot.viewFirst = start;
    slot.viewCount = count;
    slot.eventFirst = events.first;
    slot.eventEnd = events.end;
    slot.segmentFirst = segments.first;
    slot.segmentEnd = segments.end;
    slot.pendingLost = 0;
    slot.views.fetch_add(1, std::memory_order_relaxed);
    if (skipped > 0) {
        slot.skipped.fetch_add(skipped, std::memory_order_relaxed);
    }
    return view;
}

AcqError AcqReader::release(AcqBlockView& view) noexcept
{
    if (!m_core) {
        return AcqErrorCode::InvalidState;
    }
    AcqStreamCore& c = *m_core;
    ReaderSlot& slot = c.slots[m_slot];
    if (!slot.holding || ViewAccess::ticket(view) != slot.ticket) {
        return AcqErrorCode::InvalidState;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t r = c.reserved.load(std::memory_order_relaxed);
    const bool overwritten = (r > c.capacity && r - c.capacity > slot.viewFirst) ||
        recordsOverwritten(c.events, {slot.eventFirst, slot.eventEnd}) ||
        recordsOverwritten(c.segments, {slot.segmentFirst, slot.segmentEnd});
    if (overwritten) {
        slot.lost.fetch_add(slot.viewCount, std::memory_order_relaxed);
        slot.overruns.fetch_add(1, std::memory_order_relaxed);
    } else {
        slot.delivered.fetch_add(slot.viewCount, std::memory_order_relaxed);
    }
    slot.eventCursor = slot.eventEnd;
    slot.segmentCursor = slot.segmentEnd;
    const std::uint64_t end = slot.viewFirst + slot.viewCount;
    slot.holding = false;
    view = AcqBlockView();
    if (slot.gating) {
        slot.position.store(end, std::memory_order_release);
        c.gatePosition.store(end, std::memory_order_seq_cst);
        c.notifyWriter();
    } else {
        slot.position.store(end, std::memory_order_release);
    }
    return overwritten ? AcqError(AcqErrorCode::Overrun) : AcqError();
}

AcqError AcqReader::consumeIdle(std::size_t samples) noexcept
{
    if (!m_core) {
        return AcqErrorCode::InvalidState;
    }
    AcqStreamCore& c = *m_core;
    ReaderSlot& slot = c.slots[m_slot];
    if (!slot.gating) {
        return AcqErrorCode::WrongDirection;
    }
    if (slot.holding) {
        return AcqErrorCode::InvalidState;
    }
    const std::uint64_t end = slot.position.load(std::memory_order_relaxed) + samples;
    slot.idle.fetch_add(samples, std::memory_order_relaxed);
    c.idleTotal.fetch_add(samples, std::memory_order_acq_rel);
    slot.position.store(end, std::memory_order_release);
    c.gatePosition.store(end, std::memory_order_seq_cst);
    c.notifyWriter();
    return {};
}

std::size_t AcqReader::available() const noexcept
{
    if (!m_core) {
        return 0;
    }
    const std::uint64_t written = m_core->committed.load(std::memory_order_acquire);
    const std::uint64_t pos = m_core->slots[m_slot].position.load(std::memory_order_acquire);
    return static_cast<std::size_t>(written > pos ? std::min(written - pos, m_core->capacity) : 0);
}

AcqReaderStats AcqReader::stats() const noexcept
{
    AcqReaderStats s;
    if (!m_core) {
        return s;
    }
    const ReaderSlot& slot = m_core->slots[m_slot];
    s.position = slot.position.load(std::memory_order_acquire);
    s.delivered = slot.delivered.load(std::memory_order_relaxed);
    s.lost = slot.lost.load(std::memory_order_relaxed);
    s.skipped = slot.skipped.load(std::memory_order_relaxed);
    s.idle = slot.idle.load(std::memory_order_relaxed);
    s.overruns = slot.overruns.load(std::memory_order_relaxed);
    s.views = slot.views.load(std::memory_order_relaxed);
    s.eventsLost = slot.eventsLost.load(std::memory_order_relaxed);
    s.segmentsLost = slot.segmentsLost.load(std::memory_order_relaxed);
    return s;
}
