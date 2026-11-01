//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     test_acq_stream.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Tests for the lock-free stream ring: AcqWriter, AcqStream, AcqReader and the views, in both directions.
 *
 * Samples carry their own index (value(s, c)), so every read checks position and content. A replaced global
 * operator new counts heap allocations to prove the data path allocates nothing after set-up.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/acq_stream.h>
#include <acq/detail/acq_diagnostics.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QtTest>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace std::chrono_literals;

//=============================================================================================================
// COUNTING ALLOCATOR
//=============================================================================================================

namespace
{
std::atomic<bool> g_countAllocations{false};
std::atomic<std::size_t> g_allocations{0};

void* countedAlloc(std::size_t size, std::size_t alignment)
{
    if (g_countAllocations.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    size = size == 0 ? 1 : size;
#if defined(_WIN32)
    void* p = _aligned_malloc(size, alignment);
#else
    void* p = nullptr;
    if (alignment <= alignof(std::max_align_t)) {
        p = std::malloc(size);
    } else {
        p = std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
    }
#endif
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void countedFree(void* p) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void* operator new(std::size_t size)
{
    return countedAlloc(size, alignof(std::max_align_t));
}
void* operator new[](std::size_t size)
{
    return countedAlloc(size, alignof(std::max_align_t));
}
void* operator new(std::size_t size, std::align_val_t al)
{
    return countedAlloc(size, static_cast<std::size_t>(al));
}
void* operator new[](std::size_t size, std::align_val_t al)
{
    return countedAlloc(size, static_cast<std::size_t>(al));
}
void operator delete(void* p) noexcept
{
    countedFree(p);
}
void operator delete[](void* p) noexcept
{
    countedFree(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    countedFree(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    countedFree(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    countedFree(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    countedFree(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    countedFree(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    countedFree(p);
}

//=============================================================================================================
// HELPERS
//=============================================================================================================

namespace
{
std::int32_t value(std::uint64_t sample, std::size_t channel)
{
    return static_cast<std::int32_t>((sample * 16 + channel) & 0x7FFFFFFFULL);
}

AcqStreamInfo makeInfo(std::size_t channels, AcqLayout layout = AcqLayout::Interleaved,
                       AcqDirection direction = AcqDirection::Input)
{
    AcqStreamInfo info;
    info.name = "test";
    info.direction = direction;
    info.format = AcqSampleFormat::Int32;
    info.layout = layout;
    info.sfreq = 1000.0;
    for (std::size_t c = 0; c < channels; ++c) {
        AcqChannelInfo ch;
        ch.name = "ch" + std::to_string(c);
        ch.kind = AcqChannelKind::Eeg;
        ch.unit = AcqUnit::Volt;
        info.channels.push_back(ch);
    }
    return info;
}

AcqStreamOptions options(std::size_t capacity, std::size_t maxReaders = 8)
{
    AcqStreamOptions o;
    o.capacitySamples = capacity;
    o.maxReaders = maxReaders;
    o.lockMemory = false;
    return o;
}

void fill(const AcqWriteView& view)
{
    std::uint64_t sample = view.firstSample();
    for (std::size_t p = 0; p < view.partCount(); ++p) {
        const AcqWritePart& part = view.part(p);
        for (std::size_t s = 0; s < part.samples; ++s, ++sample) {
            for (std::size_t c = 0; c < view.channelCount(); ++c) {
                const std::int32_t v = value(sample, c);
                std::memcpy(part.at(c, s), &v, sizeof(v));
            }
        }
    }
}

bool matches(const AcqBlockView& view)
{
    std::uint64_t sample = view.firstSample();
    for (std::size_t p = 0; p < view.partCount(); ++p) {
        const AcqBlockPart& part = view.part(p);
        for (std::size_t s = 0; s < part.samples; ++s, ++sample) {
            for (std::size_t c = 0; c < view.channelCount(); ++c) {
                std::int32_t v = 0;
                std::memcpy(&v, part.at(c, s), sizeof(v));
                if (v != value(sample, c)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool produce(AcqWriter& writer, std::size_t samples)
{
    auto view = writer.beginWrite(samples);
    if (!view || view->sampleCount() != samples) {
        return false;
    }
    fill(*view);
    writer.commit(samples);
    return true;
}
} // namespace

//=============================================================================================================

class TestAcqStream : public QObject
{
    Q_OBJECT

private slots:
    void testCreateValidation();
    void testLosslessRoundTrip_data();
    void testLosslessRoundTrip();
    void testWrapAroundTwoParts_data();
    void testWrapAroundTwoParts();
    void testExactLossUnderStall();
    void testOverrunWhileHeld();
    void testLatestSkips();
    void testStartPositions();
    void testTimeoutAndEndOfStream();
    void testFinishWakesBlockedReader();
    void testLowLatencySpinWake();
    void testReaderSlotsAndMisuse();
    void testManyConcurrentReaders();
    void testSlowReaderLossAccounting();
    void testNoAllocationsOrLocksPerBlock();
    void testAdoptExternalBuffer();
    void testEvents();
    void testSegmentedComplexStream();
    void testClockAndTimebasePublished();
    void testInfoVersioning();
    void testOutputWriterWaitsForDevice();
    void testOutputUnderrun();
    void testOutputScheduledWrite();
    void testScheduledWriteAfterLongIdle();
    void testOutputMonitorAndEvents();
    void testWriterHandles();
    void testInvalidHandles();
    void testExternalPlanarBuffer();
    void testDefaultCapacityLimitAndMemoryLock();
    void testHoldLastUnderrunFill();
    void testOutputDeviceGoneAndLimits();
    void testRecordOrdering();
    void testInfoVersionLimit();
    void testViewHelpers();
};

//=============================================================================================================

void TestAcqStream::testCreateValidation()
{
    auto rounded = AcqWriter::create(makeInfo(4), options(100));
    QVERIFY(rounded);
    QCOMPARE(rounded->stream().stats().capacitySamples, std::size_t(128));
    QCOMPARE(rounded->stream().direction(), AcqDirection::Input);
    QCOMPARE(makeInfo(4).frameBytes(), std::size_t(16));
    QCOMPARE(makeInfo(4).channelCount(), std::size_t(4));
    QVERIFY(rounded->stream().isValid());
    QVERIFY(!AcqStream().isValid());

    auto automatic = AcqWriter::create(makeInfo(4), options(0));
    QVERIFY(automatic);
    const std::size_t cap = automatic->stream().stats().capacitySamples;
    QVERIFY(cap >= 2000);
    QCOMPARE(cap & (cap - 1), std::size_t(0));

    QVERIFY(AcqWriter::create(makeInfo(4), options(64, 0)).error() == AcqErrorCode::InvalidParameter);
    AcqStreamInfo negative = makeInfo(4);
    negative.sfreq = -1.0;
    QVERIFY(AcqWriter::create(negative, options(64)).error() == AcqErrorCode::InvalidParameter);
    AcqStreamInfo badFormat = makeInfo(4);
    badFormat.format = static_cast<AcqSampleFormat>(99);
    QVERIFY(AcqWriter::create(badFormat, options(64)).error() == AcqErrorCode::UnsupportedFormat);

    auto eventsOnly = AcqWriter::create(makeInfo(0), options(64));
    QVERIFY(eventsOnly);
}

//=============================================================================================================

void TestAcqStream::testLosslessRoundTrip_data()
{
    QTest::addColumn<int>("layout");
    QTest::newRow("interleaved") << static_cast<int>(AcqLayout::Interleaved);
    QTest::newRow("planar") << static_cast<int>(AcqLayout::Planar);
}

void TestAcqStream::testLosslessRoundTrip()
{
    QFETCH(int, layout);
    auto writer = AcqWriter::create(makeInfo(5, static_cast<AcqLayout>(layout)), options(256));
    QVERIFY(writer);
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(reader);

    std::uint64_t next = 0;
    for (int block = 0; block < 20; ++block) {
        QVERIFY(produce(*writer, 37));
        auto view = reader->acquire(37, 0ns, 37);
        QVERIFY(view);
        QCOMPARE(view->firstSample(), next);
        QCOMPARE(view->sampleCount(), std::size_t(37));
        QCOMPARE(view->lost(), std::uint64_t(0));
        QVERIFY(matches(*view));
        QVERIFY(reader->release(*view).ok());
        next += 37;
    }
    const AcqReaderStats stats = reader->stats();
    QCOMPARE(stats.delivered, next);
    QCOMPARE(stats.position, next);
    QCOMPARE(stats.views, std::uint64_t(20));
    QCOMPARE(writer->stream().stats().committed, next);
    QCOMPARE(writer->stream().stats().commits, std::uint64_t(20));

    if (static_cast<AcqLayout>(layout) == AcqLayout::Planar) {
        QVERIFY(produce(*writer, 1));
        auto view = reader->acquire(1, 0ns);
        QVERIFY(view);
        QCOMPARE(view->part(0).channelStride % 64, std::ptrdiff_t(0));
        QCOMPARE(view->part(0).sampleStride, std::ptrdiff_t(4));
        QVERIFY(reader->release(*view).ok());
    }
}

//=============================================================================================================

void TestAcqStream::testWrapAroundTwoParts_data()
{
    testLosslessRoundTrip_data();
}

void TestAcqStream::testWrapAroundTwoParts()
{
    QFETCH(int, layout);
    AcqStreamOptions o = options(16);
    o.mirror = false;
    auto writer = AcqWriter::create(makeInfo(3, static_cast<AcqLayout>(layout)), o);
    QVERIFY(writer);
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(produce(*writer, 12));
    auto first = reader->acquire(12, 0ns);
    QVERIFY(first);
    QVERIFY(reader->release(*first).ok());

    auto wrapped = writer->beginWrite(8);
    QVERIFY(wrapped);
    QCOMPARE(wrapped->partCount(), std::size_t(2));
    fill(*wrapped);
    writer->commit(8);

    auto view = reader->acquire(8, 0ns);
    QVERIFY(view);
    QCOMPARE(view->firstSample(), std::uint64_t(12));
    QCOMPARE(view->partCount(), std::size_t(2));
    QCOMPARE(view->part(0).samples, std::size_t(4));
    QCOMPARE(view->part(1).samples, std::size_t(4));
    QVERIFY(matches(*view));
    QVERIFY(reader->release(*view).ok());
}

//=============================================================================================================

void TestAcqStream::testExactLossUnderStall()
{
    auto writer = AcqWriter::create(makeInfo(4), options(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    for (int i = 0; i < 20; ++i) {
        QVERIFY(produce(*writer, 10));
    }
    QCOMPARE(reader->available(), std::size_t(64));
    auto view = reader->acquire(0, 0ns);
    QVERIFY(view);
    QCOMPARE(view->lost(), std::uint64_t(136));
    QCOMPARE(view->firstSample(), std::uint64_t(136));
    QCOMPARE(view->sampleCount(), std::size_t(64));
    QVERIFY(matches(*view));
    QVERIFY(reader->release(*view).ok());
    QCOMPARE(reader->stats().lost, std::uint64_t(136));
    QCOMPARE(reader->stats().delivered + reader->stats().lost, std::uint64_t(200));
}

//=============================================================================================================

void TestAcqStream::testOverrunWhileHeld()
{
    auto writer = AcqWriter::create(makeInfo(4), options(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(produce(*writer, 64));
    auto view = reader->acquire(64, 0ns);
    QVERIFY(view);
    QVERIFY(produce(*writer, 10));
    QVERIFY(reader->release(*view) == AcqErrorCode::Overrun);
    QCOMPARE(reader->stats().overruns, std::uint64_t(1));
    QCOMPARE(reader->stats().lost, std::uint64_t(64));
    QCOMPARE(reader->stats().delivered, std::uint64_t(0));

    auto next = reader->acquire(0, 0ns);
    QVERIFY(next);
    QCOMPARE(next->firstSample(), std::uint64_t(64));
    QCOMPARE(next->lost(), std::uint64_t(0));
    QVERIFY(reader->release(*next).ok());
}

//=============================================================================================================

void TestAcqStream::testLatestSkips()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64));
    auto latest = writer->stream().reader(AcqReader::ReadMode::Latest);
    QVERIFY(produce(*writer, 50));
    auto view = latest->acquire(10, 0ns);
    QVERIFY(view);
    QCOMPARE(view->firstSample(), std::uint64_t(40));
    QCOMPARE(view->sampleCount(), std::size_t(10));
    QCOMPARE(view->skipped(), std::uint64_t(40));
    QCOMPARE(view->lost(), std::uint64_t(0));
    QVERIFY(matches(*view));
    QVERIFY(latest->release(*view).ok());

    for (int i = 0; i < 10; ++i) {
        QVERIFY(produce(*writer, 30));
    }
    auto again = latest->acquire(5, 0ns, 20);
    QVERIFY(again);
    QCOMPARE(again->firstSample(), std::uint64_t(330));
    QCOMPARE(again->sampleCount(), std::size_t(20));
    QCOMPARE(again->lost(), std::uint64_t(0));
    QVERIFY(latest->release(*again).ok());
    QCOMPARE(latest->stats().skipped, std::uint64_t(40 + 280));
    QCOMPARE(latest->stats().lost, std::uint64_t(0));
}

//=============================================================================================================

void TestAcqStream::testStartPositions()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64));
    QVERIFY(produce(*writer, 60));
    QVERIFY(produce(*writer, 40));
    AcqReaderOptions oldest;
    oldest.startAtOldest = true;
    auto fromOldest = writer->stream().reader(AcqReader::ReadMode::Lossless, oldest);
    auto fromNewest = writer->stream().reader(AcqReader::ReadMode::Lossless);

    auto view = fromOldest->acquire(0, 0ns);
    QVERIFY(view);
    QCOMPARE(view->firstSample(), std::uint64_t(36));
    QCOMPARE(view->lost(), std::uint64_t(0));
    QVERIFY(fromOldest->release(*view).ok());

    QVERIFY(fromNewest->acquire(1, 0ns).error() == AcqErrorCode::Timeout);
    QVERIFY(produce(*writer, 1));
    auto fresh = fromNewest->acquire(1, 0ns);
    QVERIFY(fresh);
    QCOMPARE(fresh->firstSample(), std::uint64_t(100));
    QVERIFY(fromNewest->release(*fresh).ok());
}

//=============================================================================================================

void TestAcqStream::testTimeoutAndEndOfStream()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    const auto start = std::chrono::steady_clock::now();
    QVERIFY(reader->acquire(1, 5ms).error() == AcqErrorCode::Timeout);
    QVERIFY(std::chrono::steady_clock::now() - start >= 5ms);
    QVERIFY(reader->acquire(65, 0ns).error() == AcqErrorCode::InvalidParameter);

    QVERIFY(produce(*writer, 3));
    writer->finish();
    writer->finish();
    auto rest = reader->acquire(10, 1s);
    QVERIFY(rest);
    QCOMPARE(rest->sampleCount(), std::size_t(3));
    QVERIFY(reader->release(*rest).ok());
    QVERIFY(reader->acquire(1, 1s).error() == AcqErrorCode::EndOfStream);
}

//=============================================================================================================

void TestAcqStream::testFinishWakesBlockedReader()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    AcqErrorCode result = AcqErrorCode::None;
    std::thread consumer([&] { result = reader->acquire(1, 10s).error().code(); });
    std::this_thread::sleep_for(20ms);
    const auto start = std::chrono::steady_clock::now();
    writer->finish();
    consumer.join();
    QCOMPARE(result, AcqErrorCode::EndOfStream);
    QVERIFY(std::chrono::steady_clock::now() - start < 2s);
}

//=============================================================================================================

void TestAcqStream::testLowLatencySpinWake()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::LowLatency);
    QCOMPARE(reader->mode(), AcqReader::ReadMode::LowLatency);
    std::atomic<bool> ready{false};
    std::uint64_t first = 99;
    std::thread consumer([&] {
        ready = true;
        auto view = reader->acquire(1, 10s);
        if (view) {
            first = view->firstSample();
            reader->release(*view);
        }
    });
    while (!ready) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(5ms);
    QVERIFY(produce(*writer, 1));
    consumer.join();
    QCOMPARE(first, std::uint64_t(0));
}

//=============================================================================================================

void TestAcqStream::testReaderSlotsAndMisuse()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64, 2));
    auto a = writer->stream().reader(AcqReader::ReadMode::Lossless);
    std::optional<AcqReader> b;
    {
        auto tmp = writer->stream().reader(AcqReader::ReadMode::Latest);
        QVERIFY(tmp);
        b.emplace(std::move(*tmp));
    }
    QVERIFY(writer->stream().reader(AcqReader::ReadMode::Lossless).error() == AcqErrorCode::TooManyReaders);
    QCOMPARE(writer->stream().stats().activeReaders, std::size_t(2));
    b.reset();
    QCOMPARE(writer->stream().stats().activeReaders, std::size_t(1));
    auto c = writer->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(c);
    QCOMPARE(c->channelCount(), std::size_t(2));
    QVERIFY(c->stream().isValid());

    AcqReaderOptions gating;
    gating.gating = true;
    QVERIFY(writer->stream().reader(AcqReader::ReadMode::Lossless, gating).error() == AcqErrorCode::WrongDirection);
    QVERIFY(c->consumeIdle(1).code() == AcqErrorCode::WrongDirection);

    QVERIFY(produce(*writer, 4));
    auto held = a->acquire(1, 0ns);
    QVERIFY(held);
    QVERIFY(a->acquire(1, 0ns).error() == AcqErrorCode::InvalidState);
    auto other = c->acquire(1, 0ns);
    QVERIFY(other);
    QVERIFY(a->release(*other).code() == AcqErrorCode::InvalidState);
    QVERIFY(a->release(*held).ok());
    QVERIFY(c->release(*other).ok());
    AcqBlockView empty;
    QVERIFY(a->release(empty).code() == AcqErrorCode::InvalidState);

    AcqReader moved = std::move(*a);
    QVERIFY(moved.isValid());
    QVERIFY(!a->isValid());
    QVERIFY(a->acquire(0, 0ns).error() == AcqErrorCode::InvalidState);
}

//=============================================================================================================

void TestAcqStream::testManyConcurrentReaders()
{
    constexpr std::size_t kTotal = 1U << 18;
    constexpr std::size_t kBlock = 64;
    constexpr int kReaders = 8;
    auto writer = AcqWriter::create(makeInfo(4), options(kTotal, kReaders));
    QVERIFY(writer);

    std::vector<AcqReader> readers;
    for (int i = 0; i < kReaders; ++i) {
        AcqReaderOptions o;
        o.wait = i % 3 == 0 ? AcqWait::Block : (i % 3 == 1 ? AcqWait::Spin : AcqWait::SpinThenBlock);
        auto r = writer->stream().reader(i == kReaders - 1 ? AcqReader::ReadMode::LowLatency
                                                           : AcqReader::ReadMode::Lossless,
                                         o);
        QVERIFY(r);
        readers.push_back(std::move(*r));
    }

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(readers.size());
    for (auto& reader : readers) {
        threads.emplace_back([&reader, &failures] {
            std::uint64_t next = 0;
            while (true) {
                auto view = reader.acquire(1, 10s);
                if (!view) {
                    if (view.error() != AcqErrorCode::EndOfStream) {
                        ++failures;
                    }
                    break;
                }
                if (view->firstSample() != next || view->lost() != 0 || !matches(*view)) {
                    ++failures;
                }
                next = view->firstSample() + view->sampleCount();
                if (!reader.release(*view).ok()) {
                    ++failures;
                }
            }
            if (next != kTotal) {
                ++failures;
            }
        });
    }
    std::thread producer([&] {
        for (std::size_t written = 0; written < kTotal; written += kBlock) {
            if (!produce(*writer, kBlock)) {
                ++failures;
            }
        }
        writer->finish();
    });
    producer.join();
    for (auto& t : threads) {
        t.join();
    }
    QCOMPARE(failures.load(), 0);
    for (const auto& reader : readers) {
        QCOMPARE(reader.stats().delivered, std::uint64_t(kTotal));
    }
}

//=============================================================================================================

void TestAcqStream::testSlowReaderLossAccounting()
{
    constexpr std::size_t kTotal = 1U << 16;
    auto writer = AcqWriter::create(makeInfo(2), options(256));
    auto slow = writer->stream().reader(AcqReader::ReadMode::Lossless);
    std::thread producer([&] {
        for (std::size_t written = 0; written < kTotal; written += 16) {
            produce(*writer, 16);
            if (written % 4096 == 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
        writer->finish();
    });
    std::uint64_t lostInViews = 0;
    std::uint64_t next = 0;
    while (true) {
        auto view = slow->acquire(1, 10s, 32);
        if (!view) {
            QVERIFY(view.error() == AcqErrorCode::EndOfStream);
            break;
        }
        QCOMPARE(view->firstSample(), next + view->lost());
        lostInViews += view->lost();
        next = view->firstSample() + view->sampleCount();
        std::this_thread::sleep_for(50us); // stall: the producer laps this reader
        slow->release(*view);
    }
    producer.join();
    const AcqReaderStats stats = slow->stats();
    QCOMPARE(stats.delivered + stats.lost, std::uint64_t(kTotal));
    QVERIFY(stats.lost >= lostInViews);
    QVERIFY(stats.lost > 0);
}

//=============================================================================================================

void TestAcqStream::testNoAllocationsOrLocksPerBlock()
{
    auto writer = AcqWriter::create(makeInfo(64), options(4096));
    auto lossless = writer->stream().reader(AcqReader::ReadMode::Lossless);
    auto latest = writer->stream().reader(AcqReader::ReadMode::Latest);
    QVERIFY(produce(*writer, 32));
    bool ok = true;

    const std::uint64_t locksBefore = detail::controlLockCount();
    g_allocations = 0;
    g_countAllocations = true;
    for (int i = 0; i < 10'000; ++i) {
        AcqEvent event;
        event.sample = writer->writePosition();
        event.code = i;
        writer->pushEvent(event);
        ok &= produce(*writer, 32);
        auto a = lossless->acquire(32, 0ns);
        ok &= a.hasValue() && lossless->release(*a).ok();
        auto b = latest->acquire(8, 0ns);
        ok &= b.hasValue() && latest->release(*b).ok();
    }
    g_countAllocations = false;
    QVERIFY(ok);
    QCOMPARE(g_allocations.load(), std::size_t(0));
    QCOMPARE(detail::controlLockCount(), locksBefore);
}

//=============================================================================================================

void TestAcqStream::testAdoptExternalBuffer()
{
    constexpr std::size_t kCapacity = 64;
    constexpr std::size_t kBytes = kCapacity * 4 * sizeof(std::int32_t);
    auto* raw = static_cast<std::byte*>(::operator new(kBytes, std::align_val_t(4096)));
    int released = 0;
    {
        AcqStreamOptions o = options(0);
        o.external.data = raw;
        o.external.bytes = kBytes;
        o.external.release = [&released, raw] {
            ++released;
            ::operator delete(raw, std::align_val_t(4096));
        };
        auto writer = AcqWriter::create(makeInfo(4), o);
        QVERIFY(writer);
        QVERIFY(writer->stream().stats().adoptedBuffer);
        QCOMPARE(writer->stream().stats().capacitySamples, kCapacity);
        auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
        QVERIFY(produce(*writer, 3));
        std::int32_t direct = 0;
        std::memcpy(&direct, raw + (2 * 4 + 1) * sizeof(std::int32_t), sizeof(direct));
        QCOMPARE(direct, value(2, 1));
        auto view = reader->acquire(3, 0ns);
        QVERIFY(view);
        QCOMPARE(view->part(0).data, static_cast<const std::byte*>(raw));
        QVERIFY(reader->release(*view).ok());
        QCOMPARE(released, 0);
    }
    QCOMPARE(released, 1);

    alignas(4096) static std::byte scratch[8192];
    AcqStreamOptions misaligned = options(0);
    misaligned.external.data = scratch + 64;
    misaligned.external.bytes = kBytes;
    QVERIFY(AcqWriter::create(makeInfo(4), misaligned).error() == AcqErrorCode::InvalidBuffer);
    AcqStreamOptions notPow2 = options(0);
    notPow2.external.data = scratch;
    notPow2.external.bytes = 48 * 4 * sizeof(std::int32_t);
    QVERIFY(AcqWriter::create(makeInfo(4), notPow2).error() == AcqErrorCode::InvalidBuffer);
}

//=============================================================================================================

void TestAcqStream::testEvents()
{
    AcqStreamOptions o = options(256);
    o.eventCapacity = 4;
    auto writer = AcqWriter::create(makeInfo(2), o);
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    for (const std::uint64_t s : {3ULL, 10ULL, 20ULL}) {
        AcqEvent e;
        e.sample = s;
        e.code = static_cast<std::int32_t>(s);
        QVERIFY(writer->pushEvent(e).ok());
    }
    QVERIFY(produce(*writer, 16));
    auto view = reader->acquire(16, 0ns);
    QVERIFY(view);
    QCOMPARE(view->events().size(), std::size_t(2));
    QCOMPARE(view->events()[0].code, 3);
    QCOMPARE(view->events()[1].code, 10);
    QVERIFY(reader->release(*view).ok());
    QVERIFY(produce(*writer, 16));
    auto second = reader->acquire(16, 0ns);
    QCOMPARE(second->events().size(), std::size_t(1));
    QCOMPARE(second->events()[0].sample, std::uint64_t(20));
    QVERIFY(reader->release(*second).ok());

    for (int i = 0; i < 10; ++i) {
        AcqEvent e;
        e.sample = 32 + static_cast<std::uint64_t>(i);
        e.code = 100 + i;
        writer->pushEvent(e);
    }
    QVERIFY(produce(*writer, 16));
    auto third = reader->acquire(16, 0ns);
    QCOMPARE(third->events().size(), std::size_t(4));
    QCOMPARE(third->events()[0].code, 106);
    QCOMPARE(third->events()[3].code, 109);
    QVERIFY(reader->release(*third).ok());
    QCOMPARE(reader->stats().eventsLost, std::uint64_t(6));
    QCOMPARE(writer->stream().stats().events, std::uint64_t(13));
}

//=============================================================================================================

void TestAcqStream::testSegmentedComplexStream()
{
    constexpr std::size_t kCoils = 32;
    constexpr std::uint32_t kReadout = 512;
    constexpr std::uint32_t kHeader = 340;
    AcqStreamInfo rx;
    rx.name = "rx";
    rx.format = AcqSampleFormat::ComplexInt16;
    rx.layout = AcqLayout::Planar;
    rx.sfreq = 1.0e6;
    for (std::size_t c = 0; c < kCoils; ++c) {
        AcqChannelInfo coil;
        coil.name = "coil" + std::to_string(c);
        coil.kind = AcqChannelKind::MrReceive;
        coil.unit = AcqUnit::Volt;
        coil.scale = 1.0 / 32768.0;
        rx.channels.push_back(coil);
    }
    rx.segmentation.mode = AcqSegmentation::Mode::Triggered;
    rx.segmentation.samplesPerSegment = kReadout;
    rx.segmentation.headerBytes = kHeader;
    rx.segmentation.headerSchema = "ismrmrd.AcquisitionHeader/1";

    auto writer = AcqWriter::create(rx, options(4096));
    QVERIFY(writer);
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    for (std::uint64_t readout = 0; readout < 3; ++readout) {
        AcqSegmentHeader header;
        header.firstSample = readout * kReadout;
        header.sampleCount = kReadout;
        header.counter = readout;
        std::vector<std::byte> payload(kHeader, static_cast<std::byte>(0xA0 + readout));
        QVERIFY(writer->pushSegment(header, payload).ok());
        auto view = writer->beginWrite(kReadout);
        QVERIFY(view);
        for (std::size_t c = 0; c < kCoils; ++c) {
            for (std::size_t s = 0; s < kReadout; ++s) {
                const AcqComplexInt16 iq{static_cast<std::int16_t>(s), static_cast<std::int16_t>(-static_cast<int>(c))};
                std::memcpy(view->part(0).at(c, s), &iq, sizeof(iq));
            }
        }
        writer->commit(kReadout);
    }

    auto view = reader->acquire(3 * kReadout, 0ns);
    QVERIFY(view);
    QCOMPARE(view->format(), AcqSampleFormat::ComplexInt16);
    QCOMPARE(view->segments().size(), std::size_t(3));
    for (std::size_t i = 0; i < 3; ++i) {
        QCOMPARE(view->segments()[i].counter, std::uint64_t(i));
        QCOMPARE(view->segments()[i].firstSample, std::uint64_t(i * kReadout));
        const auto payload = view->segmentPayload(i);
        QCOMPARE(payload.size(), std::size_t(kHeader));
        QCOMPARE(payload[kHeader - 1], static_cast<std::byte>(0xA0 + i));
    }
    AcqComplexInt16 iq{};
    std::memcpy(&iq, view->part(0).at(7, 1000 - kReadout), sizeof(iq));
    QCOMPARE(int(iq.re), int(1000 - kReadout));
    QCOMPARE(int(iq.im), -7);
    QVERIFY(reader->release(*view).ok());
}

//=============================================================================================================

void TestAcqStream::testClockAndTimebasePublished()
{
    auto writer = AcqWriter::create(makeInfo(2), options(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    AcqClockModel clock;
    clock.sfreq = 1000.0;
    clock.anchorSample = 7;
    clock.source = AcqClockSource::Device;
    writer->setClock(clock);
    AcqTimebase tb;
    tb.domain = acqClockDomainId("spectrum:1");
    tb.ticksPerSample = 125;
    tb.lock = AcqLockState::PhaseLocked;
    writer->stream().setTimebase(tb);

    QCOMPARE(writer->stream().clock().anchorSample, std::uint64_t(7));
    QCOMPARE(writer->stream().timebase().ticksPerSample, std::uint64_t(125));
    QVERIFY(produce(*writer, 4));
    auto view = reader->acquire(4, 0ns);
    QVERIFY(view);
    QCOMPARE(view->clock().source, AcqClockSource::Device);
    QCOMPARE(view->timebase().domain, tb.domain);
    QCOMPARE(view->timebase().lock, AcqLockState::PhaseLocked);
    QVERIFY(reader->release(*view).ok());
}

//=============================================================================================================

void TestAcqStream::testInfoVersioning()
{
    auto writer = AcqWriter::create(makeInfo(2), options(256));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    AcqStreamInfo gain = makeInfo(2);
    gain.channels[0].scale = 2.0;
    QVERIFY(writer->updateInfo(gain, 32).ok());
    QVERIFY(writer->updateInfo(makeInfo(3), 64).code() == AcqErrorCode::InvalidParameter);
    QVERIFY(produce(*writer, 64));
    QVERIFY(writer->updateInfo(gain, 10).code() == AcqErrorCode::InvalidParameter);

    QCOMPARE(writer->stream().info()->version, std::uint32_t(1));
    QCOMPARE(writer->stream().infoAt(10)->version, std::uint32_t(0));
    QCOMPARE(writer->stream().infoAt(40)->version, std::uint32_t(1));

    auto before = reader->acquire(1, 0ns);
    QVERIFY(before);
    QCOMPARE(before->sampleCount(), std::size_t(32));
    QCOMPARE(before->infoVersion(), std::uint32_t(0));
    QCOMPARE(before->scales()[0], 1.0);
    QVERIFY(reader->release(*before).ok());
    auto after = reader->acquire(1, 0ns);
    QVERIFY(after);
    QCOMPARE(after->firstSample(), std::uint64_t(32));
    QCOMPARE(after->infoVersion(), std::uint32_t(1));
    QCOMPARE(after->scales()[0], 2.0);
    QVERIFY(reader->release(*after).ok());
}

//=============================================================================================================

void TestAcqStream::testOutputWriterWaitsForDevice()
{
    auto writer = AcqWriter::create(makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output), options(16));
    QVERIFY(writer);
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(device);
    QCOMPARE(writer->space(), std::size_t(16));
    QVERIFY(produce(*writer, 16));
    QCOMPARE(writer->space(), std::size_t(0));
    QVERIFY(writer->beginWrite(1, 0ns).error() == AcqErrorCode::Timeout);

    std::thread deviceThread([&] {
        std::this_thread::sleep_for(20ms);
        auto view = device->acquire(4, 1s, 4);
        if (view) {
            device->release(*view);
        }
    });
    auto blocked = writer->beginWrite(4, 5s);
    deviceThread.join();
    QVERIFY(blocked);
    QCOMPARE(blocked->firstSample(), std::uint64_t(16));
    QCOMPARE(blocked->underrun(), std::uint64_t(0));
    fill(*blocked);
    writer->commit(4);
    QVERIFY(writer->stream().stats().writerWaits >= 1);
    QCOMPARE(writer->devicePosition(), std::uint64_t(4));

    auto rest = device->acquire(16, 0ns);
    QVERIFY(rest);
    QCOMPARE(rest->firstSample(), std::uint64_t(4));
    QVERIFY(matches(*rest));
    QVERIFY(device->release(*rest).ok());
}

//=============================================================================================================

void TestAcqStream::testOutputUnderrun()
{
    auto writer = AcqWriter::create(makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output), options(64));
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(device->acquire(4, 0ns).error() == AcqErrorCode::Timeout);
    QVERIFY(device->consumeIdle(4).ok());
    QCOMPARE(device->stats().idle, std::uint64_t(4));

    auto view = writer->beginWrite(8);
    QVERIFY(view);
    QCOMPARE(view->firstSample(), std::uint64_t(4));
    QCOMPARE(view->underrun(), std::uint64_t(4));
    fill(*view);
    writer->commit(8);
    QCOMPARE(writer->stream().stats().underrun, std::uint64_t(4));
    QCOMPARE(writer->stream().stats().underruns, std::uint64_t(1));

    auto played = device->acquire(8, 0ns);
    QVERIFY(played);
    QCOMPARE(played->firstSample(), std::uint64_t(4));
    QVERIFY(matches(*played));
    QVERIFY(device->release(*played).ok());
    auto next = writer->beginWrite(1);
    QCOMPARE(next->underrun(), std::uint64_t(0));
}

//=============================================================================================================

void TestAcqStream::testOutputScheduledWrite()
{
    auto writer = AcqWriter::create(makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output), options(128));
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(produce(*writer, 8));
    QCOMPARE(writer->earliestWritableSample(), std::uint64_t(8));

    auto scheduled = writer->beginWriteAt(64, 8, 0ns);
    QVERIFY(scheduled);
    QCOMPARE(scheduled->firstSample(), std::uint64_t(64));
    fill(*scheduled);
    writer->commit(8);
    QCOMPARE(writer->writePosition(), std::uint64_t(72));
    QVERIFY(writer->beginWriteAt(10, 4, 0ns).error() == AcqErrorCode::InvalidParameter); // written, not yet played

    auto view = device->acquire(72, 0ns);
    QVERIFY(view);
    std::int32_t gap = -1;
    std::memcpy(&gap, view->part(0).at(1, 30), sizeof(gap));
    QCOMPARE(gap, 0);
    std::int32_t pulse = 0;
    std::memcpy(&pulse, view->part(0).at(1, 64), sizeof(pulse));
    QCOMPARE(pulse, value(64, 1));
    QVERIFY(device->release(*view).ok());

    QVERIFY(writer->beginWriteAt(10, 4, 0ns).error() == AcqErrorCode::Late); // already played
    QVERIFY(device->consumeIdle(50).ok());
    QVERIFY(writer->beginWriteAt(100, 4, 0ns).error() == AcqErrorCode::Late);
    AcqTimebase tb;
    tb.sampleAlignment = 32;
    writer->stream().setTimebase(tb);
    QVERIFY(writer->beginWriteAt(130, 4, 0ns).error() == AcqErrorCode::NotOnGrid);
    QVERIFY(writer->beginWriteAt(128, 4, 0ns));
}

//=============================================================================================================

void TestAcqStream::testScheduledWriteAfterLongIdle()
{
    // Sparse stimulation: the device plays fill far beyond the ring capacity between two pulses.
    auto writer = AcqWriter::create(makeInfo(1, AcqLayout::Interleaved, AcqDirection::Output), options(64));
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(device->consumeIdle(1000).ok());
    auto pulse = writer->beginWriteAt(1010, 8, 0ns);
    QVERIFY2(pulse.hasValue(), pulse.error().text().c_str());
    QCOMPARE(pulse->firstSample(), std::uint64_t(1010));
    fill(*pulse);
    writer->commit(8);
    QVERIFY(device->consumeIdle(0).ok());
    auto played = device->acquire(18, 0ns);
    QVERIFY(played);
    QCOMPARE(played->firstSample(), std::uint64_t(1000));
    std::int32_t v = -1;
    std::memcpy(&v, played->part(0).at(0, 9), sizeof(v));
    QCOMPARE(v, 0);
    std::memcpy(&v, played->part(0).at(0, 10), sizeof(v));
    QCOMPARE(v, value(1010, 0));
    QVERIFY(device->release(*played).ok());
    QVERIFY(writer->beginWriteAt(500, 4, 0ns).error() == AcqErrorCode::Late);
}

//=============================================================================================================

void TestAcqStream::testOutputMonitorAndEvents()
{
    auto writer = AcqWriter::create(makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output), options(64));
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    auto monitor = writer->stream().reader(AcqReader::ReadMode::Lossless);

    AcqEvent pulse;
    pulse.sample = 40;
    pulse.code = 1;
    QVERIFY(writer->pushEvent(pulse).ok());
    QVERIFY(produce(*writer, 32));
    auto early = device->acquire(32, 0ns);
    QVERIFY(early);
    QCOMPARE(early->events().size(), std::size_t(0));
    QVERIFY(device->release(*early).ok());

    AcqEvent late;
    late.sample = 20;
    QVERIFY(writer->pushEvent(late).code() == AcqErrorCode::Late);
    QCOMPARE(writer->stream().stats().lateEvents, std::uint64_t(1));

    QVERIFY(produce(*writer, 16));
    auto withEvent = device->acquire(16, 0ns);
    QCOMPARE(withEvent->events().size(), std::size_t(1));
    QCOMPARE(withEvent->events()[0].sample, std::uint64_t(40));
    QVERIFY(device->release(*withEvent).ok());

    auto seen = monitor->acquire(48, 0ns);
    QVERIFY(seen);
    QCOMPARE(seen->sampleCount(), std::size_t(48));
    QVERIFY(matches(*seen));
    QVERIFY(monitor->release(*seen).ok());
}

//=============================================================================================================

void TestAcqStream::testWriterHandles()
{
    auto input = AcqWriter::create(makeInfo(2), options(64));
    QVERIFY(input->stream().writer().error() == AcqErrorCode::WrongDirection);
    QCOMPARE(input->devicePosition(), input->writePosition());
    QCOMPARE(input->earliestWritableSample(), input->writePosition());
    QVERIFY(input->beginWriteAt(0, 1).error() == AcqErrorCode::WrongDirection);

    auto output = AcqWriter::create(makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output), options(64));
    QVERIFY(output->stream().writer().error() == AcqErrorCode::WriterInUse);

    AcqWriter moved = std::move(*output);
    QCOMPARE(moved.stream().direction(), AcqDirection::Output);
    AcqWriter empty;
    QVERIFY(!empty.stream().isValid());
}

//=============================================================================================================

void TestAcqStream::testInvalidHandles()
{
    AcqStream stream;
    QVERIFY(!stream.info());
    QVERIFY(!stream.infoAt(0));
    QCOMPARE(stream.direction(), AcqDirection::Input);
    QCOMPARE(stream.stats().capacitySamples, std::size_t(0));
    QCOMPARE(stream.clock().sfreq, 0.0);
    QCOMPARE(stream.timebase().ticksPerSample, std::uint64_t(1));
    stream.setTimebase(AcqTimebase());
    QVERIFY(stream.reader(AcqReader::ReadMode::Lossless).error() == AcqErrorCode::InvalidState);
    QVERIFY(stream.writer().error() == AcqErrorCode::InvalidState);

    AcqWriter writer;
    QCOMPARE(writer.writePosition(), std::uint64_t(0));
    QCOMPARE(writer.devicePosition(), std::uint64_t(0));
    QCOMPARE(writer.space(), std::size_t(0));
    QCOMPARE(writer.earliestWritableSample(), std::uint64_t(0));
    QVERIFY(writer.beginWrite(1).error() == AcqErrorCode::InvalidState);
    QVERIFY(writer.beginWriteAt(0, 1).error() == AcqErrorCode::InvalidState);
    QVERIFY(writer.pushEvent(AcqEvent()).code() == AcqErrorCode::InvalidState);
    QVERIFY(writer.pushSegment(AcqSegmentHeader(), {}).code() == AcqErrorCode::InvalidState);
    QVERIFY(writer.updateInfo(makeInfo(1), 0).code() == AcqErrorCode::InvalidState);
    writer.commit(1);
    writer.setClock(AcqClockModel());
    writer.finish();

    AcqReader reader;
    QVERIFY(!reader.isValid());
    QCOMPARE(reader.mode(), AcqReader::ReadMode::Lossless);
    QCOMPARE(reader.channelCount(), std::size_t(0));
    QCOMPARE(reader.available(), std::size_t(0));
    QCOMPARE(reader.stats().views, std::uint64_t(0));
    QVERIFY(!reader.stream().isValid());
    AcqBlockView view;
    QVERIFY(reader.release(view).code() == AcqErrorCode::InvalidState);
    QVERIFY(reader.consumeIdle(1).code() == AcqErrorCode::InvalidState);

    auto real = AcqWriter::create(makeInfo(2), options(64));
    AcqWriter assigned;
    assigned = std::move(*real);
    QVERIFY(assigned.stream().isValid());
    QVERIFY(produce(assigned, 2));
    assigned.commit(0);
    QCOMPARE(assigned.writePosition(), std::uint64_t(2));

    auto r1 = assigned.stream().reader(AcqReader::ReadMode::Lossless);
    auto r2 = assigned.stream().reader(AcqReader::ReadMode::Latest);
    AcqReader target = std::move(*r1);
    target = std::move(*r2);
    QCOMPARE(target.mode(), AcqReader::ReadMode::Latest);
    QCOMPARE(assigned.stream().stats().activeReaders, std::size_t(1));
}

//=============================================================================================================

void TestAcqStream::testExternalPlanarBuffer()
{
    alignas(4096) static std::byte planar[4 * 512];
    AcqStreamOptions o = options(0);
    o.external.data = planar;
    o.external.bytes = sizeof(planar);
    o.external.channelStride = 512;
    o.capacitySamples = 64;
    auto writer = AcqWriter::create(makeInfo(4, AcqLayout::Planar), o);
    QVERIFY(writer);
    QCOMPARE(writer->stream().stats().capacitySamples, std::size_t(64));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(produce(*writer, 5));
    std::int32_t direct = 0;
    std::memcpy(&direct, planar + 2 * 512 + 3 * sizeof(std::int32_t), sizeof(direct));
    QCOMPARE(direct, value(3, 2));
    auto view = reader->acquire(5, 0ns);
    QVERIFY(view && matches(*view));
    QVERIFY(reader->release(*view).ok());

    AcqStreamOptions tooLong = o;
    tooLong.capacitySamples = 256;
    QVERIFY(AcqWriter::create(makeInfo(4, AcqLayout::Planar), tooLong).error() == AcqErrorCode::InvalidBuffer);
    AcqStreamOptions mismatch = options(32);
    mismatch.external.data = planar;
    mismatch.external.bytes = sizeof(planar);
    QVERIFY(AcqWriter::create(makeInfo(4), mismatch).error() == AcqErrorCode::InvalidParameter);
    AcqStreamOptions noChannels = options(0);
    noChannels.external.data = planar;
    noChannels.external.bytes = sizeof(planar);
    QVERIFY(AcqWriter::create(makeInfo(0), noChannels).error() == AcqErrorCode::InvalidBuffer);
    AcqStreamOptions badAlignment = options(64);
    badAlignment.rowAlignment = 48;
    QVERIFY(AcqWriter::create(makeInfo(4, AcqLayout::Planar), badAlignment).error() ==
            AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqStream::testDefaultCapacityLimitAndMemoryLock()
{
    AcqStreamInfo wide = makeInfo(0);
    wide.format = AcqSampleFormat::Float64;
    wide.channels.resize(8192);
    AcqStreamOptions o = options(0);
    o.prefault = false;
    auto big = AcqWriter::create(wide, o);
    QVERIFY(big);
    QCOMPARE(big->stream().stats().capacitySamples, std::size_t(4096));

    AcqStreamOptions locked = options(64);
    locked.lockMemory = true;
    auto small = AcqWriter::create(makeInfo(2), locked);
    QVERIFY(small);
    QVERIFY(produce(*small, 8));
}

//=============================================================================================================

void TestAcqStream::testHoldLastUnderrunFill()
{
    AcqStreamInfo info = makeInfo(2, AcqLayout::Planar, AcqDirection::Output);
    info.underrunFill = AcqUnderrunFill::HoldLast;
    auto writer = AcqWriter::create(info, options(16));
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    auto monitor = writer->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(produce(*writer, 4));
    auto played = device->acquire(4, 0ns);
    QVERIFY(device->release(*played).ok());
    QVERIFY(device->consumeIdle(3).ok());
    auto next = writer->beginWrite(1);
    QVERIFY(next);
    QCOMPARE(next->firstSample(), std::uint64_t(7));
    QCOMPARE(next->underrun(), std::uint64_t(3));
    auto held = monitor->acquire(7, 0ns);
    QVERIFY(held);
    for (std::size_t s = 4; s < 7; ++s) {
        std::int32_t v = 0;
        std::memcpy(&v, held->part(0).at(1, s), sizeof(v));
        QCOMPARE(v, value(3, 1));
    }
    QVERIFY(monitor->release(*held).ok());

    QVERIFY(device->consumeIdle(100).ok());
    auto far = writer->beginWrite(2);
    QVERIFY(far);
    QCOMPARE(far->firstSample(), std::uint64_t(107));
    QCOMPARE(far->underrun(), std::uint64_t(100));
}

//=============================================================================================================

void TestAcqStream::testOutputDeviceGoneAndLimits()
{
    auto writer = AcqWriter::create(makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output), options(16));
    QVERIFY(writer->beginWrite(17).error() == AcqErrorCode::InvalidParameter);
    QVERIFY(writer->beginWriteAt(0, 17).error() == AcqErrorCode::InvalidParameter);
    {
        AcqReaderOptions gating;
        gating.gating = true;
        auto device = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
        QVERIFY(writer->stream().reader(AcqReader::ReadMode::Lossless, gating).error() == AcqErrorCode::InvalidState);
        QVERIFY(produce(*writer, 16));
        QVERIFY(writer->beginWriteAt(16, 4, 1ms).error() == AcqErrorCode::Timeout);
        auto held = device->acquire(1, 0ns, 1);
        QVERIFY(device->consumeIdle(1).code() == AcqErrorCode::InvalidState);
        QVERIFY(device->release(*held).ok());
    }
    QVERIFY(writer->beginWrite(16, 1s).error() == AcqErrorCode::EndOfStream);
}

//=============================================================================================================

void TestAcqStream::testRecordOrdering()
{
    auto input = AcqWriter::create(makeInfo(2), options(64));
    AcqEvent e;
    e.sample = 10;
    QVERIFY(input->pushEvent(e).ok());
    e.sample = 9;
    QVERIFY(input->pushEvent(e).code() == AcqErrorCode::InvalidParameter);

    AcqStreamInfo info = makeInfo(2, AcqLayout::Interleaved, AcqDirection::Output);
    info.segmentation.mode = AcqSegmentation::Mode::Triggered;
    info.segmentation.headerBytes = 4;
    auto output = AcqWriter::create(info, options(64));
    AcqReaderOptions gating;
    gating.gating = true;
    auto device = output->stream().reader(AcqReader::ReadMode::Lossless, gating);
    AcqSegmentHeader h;
    h.firstSample = 20;
    QVERIFY(output->pushSegment(h, {}).ok());
    h.firstSample = 19;
    QVERIFY(output->pushSegment(h, {}).code() == AcqErrorCode::InvalidParameter);
    QVERIFY(device->consumeIdle(30).ok());
    h.firstSample = 25;
    QVERIFY(output->pushSegment(h, {}).code() == AcqErrorCode::Late);
}

//=============================================================================================================

void TestAcqStream::testInfoVersionLimit()
{
    auto writer = AcqWriter::create(makeInfo(1), options(64));
    AcqError last;
    for (int i = 0; i < 1100 && last.ok(); ++i) {
        last = writer->updateInfo(makeInfo(1), 0);
    }
    QVERIFY(last.code() == AcqErrorCode::InvalidState);
    QCOMPARE(writer->stream().info()->version, std::uint32_t(1023));
}

//=============================================================================================================

void TestAcqStream::testViewHelpers()
{
    AcqBlockView view;
    QVERIFY(view.empty());
    QVERIFY(view.events().empty());
    for (const AcqSampleFormat f : {AcqSampleFormat::Int16, AcqSampleFormat::Int24Packed, AcqSampleFormat::Int32,
                                    AcqSampleFormat::Float32, AcqSampleFormat::Float64, AcqSampleFormat::ComplexInt16,
                                    AcqSampleFormat::ComplexFloat32}) {
        AcqStreamInfo info = makeInfo(3);
        info.format = f;
        auto writer = AcqWriter::create(info, options(16));
        QVERIFY(writer);
        auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
        auto w = writer->beginWrite(2);
        QCOMPARE(std::size_t(w->part(0).sampleStride), 3 * bytesPerSample(f));
        writer->commit(2);
        auto r = reader->acquire(2, 0ns);
        QVERIFY(!r->empty());
        QCOMPARE(r->format(), f);
        QVERIFY(reader->release(*r).ok());
    }
}

//=============================================================================================================

QTEST_GUILESS_MAIN(TestAcqStream)
#include "test_acq_stream.moc"
