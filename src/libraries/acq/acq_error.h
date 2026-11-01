//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_error.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqError: catalogued error codes of the acquisition API, and AcqExpected, a value-or-error result.
 *
 * Every error the public API can return has a catalogue entry (G12, T2.6): a stable code, a message that
 * names the cause, the fix, and a documentation anchor. The catalogue strings are static, so creating an
 * AcqError on the data path (Timeout, EndOfStream, Overrun) never allocates. Only control-path errors
 * attach a context string (a URI, a file path, a library search path).
 */

#ifndef ACQ_ERROR_H
#define ACQ_ERROR_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * Stable error codes. Values are part of the ABI and are never renumbered.
 */
enum class AcqErrorCode : std::uint16_t
{
    None = 0, /**< Success. */
    // Data path
    Timeout = 1,       /**< No data within the requested timeout; not a failure of the source. */
    EndOfStream = 2,   /**< The producer finished and every committed sample has been read. */
    Overrun = 3,       /**< The producer overwrote samples while a view was held; the view's data is invalid. */
    SourceStopped = 4, /**< The source was stopped while waiting for data. */
    // Control path
    InvalidUri = 100,          /**< The URI does not have the form scheme://target?key=value. */
    UnknownScheme = 101,       /**< No backend is registered for the URI scheme. */
    InvalidParameter = 102,    /**< A URI or configuration parameter is unknown or out of range. */
    BackendUnavailable = 103,  /**< A runtime-loaded library (BrainFlow, liblsl, vendor SDK) was not found. */
    UnsupportedVersion = 104,  /**< A runtime-loaded library has a version outside the supported range. */
    DeviceNotFound = 105,      /**< The device, file or network endpoint does not exist or does not answer. */
    DeviceIo = 106,            /**< The device or file failed during acquisition. */
    InvalidState = 107,        /**< The call is not allowed in the current source state. */
    UnsupportedFormat = 108,   /**< The sample format or layout cannot be used for this operation. */
    ShapeMismatch = 109,       /**< A caller-owned buffer has the wrong shape. */
    TooManyReaders = 110,      /**< All reader slots of the stream are in use. */
    InvalidBuffer = 111,       /**< An adopted external buffer is not page-aligned or not a power of two in samples. */
    OutOfMemory = 112,         /**< The ring or its memory lock could not be allocated. */
    PlatformUnsupported = 113, /**< The backend is not available on this operating system. */
    Internal = 199             /**< Invariant violation inside ACQLIB; please report it. */
};

//=============================================================================================================
/**
 * Catalogue entry of one error code. All strings have static storage duration.
 */
struct AcqErrorInfo
{
    AcqErrorCode code;          /**< The code this entry describes. */
    std::string_view name;      /**< Stable identifier, e.g. "UnknownScheme". */
    std::string_view message;   /**< What went wrong, naming the cause. */
    std::string_view fix;       /**< What the user can do about it. */
    std::string_view docAnchor; /**< Anchor on the acquisition error documentation page. */
};

//=============================================================================================================
/**
 * Returns the catalogue entry for @p code. Never fails: an unknown value maps to the Internal entry.
 *
 * @param[in] code   The error code.
 *
 * @return Reference to a static catalogue entry.
 */
ACQSHARED_EXPORT const AcqErrorInfo& acqErrorInfo(AcqErrorCode code) noexcept;

//=============================================================================================================
/**
 * An error value: a catalogue code plus an optional context string. Cheap to copy; a code without
 * context does not allocate.
 */
class ACQSHARED_EXPORT AcqError
{
public:
    AcqError() noexcept = default;

    //=========================================================================================================
    /**
     * Creates an error without context (allocation-free, usable on the data path).
     *
     * @param[in] code   The error code.
     */
    constexpr AcqError(AcqErrorCode code) noexcept
    : m_code(code)
    {
    } // NOLINT(google-explicit-constructor)

    //=========================================================================================================
    /**
     * Creates an error with context (control path only, allocates).
     *
     * @param[in] code      The error code.
     * @param[in] context   What the error refers to, e.g. the URI or file path.
     */
    AcqError(AcqErrorCode code, std::string context);

    /** @return true when this value represents success. */
    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return m_code == AcqErrorCode::None;
    }

    /** @return The error code. */
    [[nodiscard]] constexpr AcqErrorCode code() const noexcept
    {
        return m_code;
    }

    /** @return The context string, empty if none was attached. */
    [[nodiscard]] std::string_view context() const noexcept;

    /** @return The catalogue entry of the code. */
    [[nodiscard]] const AcqErrorInfo& info() const noexcept
    {
        return acqErrorInfo(m_code);
    }

    /** @return "message (context). fix" ready for printing (allocates). */
    [[nodiscard]] std::string text() const;

    friend constexpr bool operator==(const AcqError& lhs, AcqErrorCode rhs) noexcept
    {
        return lhs.m_code == rhs;
    }

private:
    AcqErrorCode m_code = AcqErrorCode::None;
    std::shared_ptr<const std::string> m_context;
};

//=============================================================================================================
/**
 * Value-or-error result (the subset of C++23 std::expected that ACQLIB needs). Holds either a @c T or
 * an AcqError whose code is not None. Neither state allocates by itself.
 *
 * @tparam T   The value type; may be move-only.
 */
template<typename T>
class AcqExpected
{
public:
    /** Creates a result holding @p value. */
    AcqExpected(T value)
    : m_state(std::in_place_index<0>, std::move(value))
    {
    } // NOLINT(google-explicit-constructor)

    /** Creates a result holding @p error. @pre !error.ok() */
    AcqExpected(AcqError error)
    : m_state(std::in_place_index<1>, std::move(error))
    {
    } // NOLINT(google-explicit-constructor)

    /** Creates a result holding the error @p code. @pre code != AcqErrorCode::None */
    AcqExpected(AcqErrorCode code)
    : m_state(std::in_place_index<1>, AcqError(code))
    {
    } // NOLINT(google-explicit-constructor)

    /** @return true when a value is held. */
    [[nodiscard]] bool hasValue() const noexcept
    {
        return m_state.index() == 0;
    }
    explicit operator bool() const noexcept
    {
        return hasValue();
    }

    /** @return The held value. @pre hasValue() */
    [[nodiscard]] T& value() &
    {
        return std::get<0>(m_state);
    }
    [[nodiscard]] const T& value() const&
    {
        return std::get<0>(m_state);
    }
    [[nodiscard]] T&& value() &&
    {
        return std::get<0>(std::move(m_state));
    }

    T& operator*() &
    {
        return value();
    }
    const T& operator*() const&
    {
        return value();
    }
    T* operator->()
    {
        return &value();
    }
    const T* operator->() const
    {
        return &value();
    }

    /** @return The held error, or a success value when a value is held. */
    [[nodiscard]] AcqError error() const
    {
        return hasValue() ? AcqError() : std::get<1>(m_state);
    }

private:
    std::variant<T, AcqError> m_state;
};

} // namespace ACQLIB

#endif // ACQ_ERROR_H
