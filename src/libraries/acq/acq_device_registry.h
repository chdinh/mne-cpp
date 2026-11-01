//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_device_registry.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqUri and AcqDeviceRegistry: URI scheme -> backend factory, and discovery (control path).
 *
 * URIs have the form scheme://target?key=value&key=value. Every scheme opens with no parameters
 * where that is meaningful (synthetic://, lsl://<name>, fiff://<file>). Parameters are parsed
 * into a typed configuration by each backend; unknown keys are an InvalidParameter error, never
 * silently ignored. Built-in backends register at static-initialisation time; applications can add
 * their own.
 */

#ifndef ACQ_DEVICE_REGISTRY_H
#define ACQ_DEVICE_REGISTRY_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_backend.h"
#include "acq_error.h"
#include "acq_device.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * A parsed source URI. Percent-decoded; parameter order is preserved.
 */
struct AcqUri
{
    std::string scheme;                                      /**< Lower-case scheme without "://". */
    std::string target;                                      /**< Everything between "://" and "?". */
    std::vector<std::pair<std::string, std::string>> params; /**< Query parameters. */

    /** @return The parsed URI, or InvalidUri naming the offending position. */
    [[nodiscard]] static AcqExpected<AcqUri> parse(std::string_view uri);

    /** @return The value of @p key, if present. */
    [[nodiscard]] std::optional<std::string_view> param(std::string_view key) const noexcept;

    /** @return The canonical, percent-encoded string form. */
    [[nodiscard]] std::string toString() const;
};

//=============================================================================================================
/**
 * One parameter a scheme accepts, for help texts, the CLI and generated settings widgets.
 */
struct AcqParameterInfo
{
    std::string name;         /**< Query key. */
    std::string type;         /**< "int", "double", "bool", "string", "enum:a|b|c". */
    std::string defaultValue; /**< Value used when the key is absent; empty if required. */
    std::string description;  /**< One sentence. */
};

//=============================================================================================================
/**
 * Registration record of a scheme.
 */
struct AcqSchemeInfo
{
    std::string scheme;                       /**< e.g. "synthetic". */
    std::string description;                  /**< One sentence. */
    std::string exampleUri;                   /**< A URI that works out of the box, if any. */
    std::vector<AcqParameterInfo> parameters; /**< Accepted query parameters. */
};

//=============================================================================================================
/**
 * A source found by discovery, with a URI that opens it.
 */
struct AcqDiscoveredDevice
{
    std::string uri;       /**< Ready-to-use URI. */
    std::string label;     /**< Human-readable name. */
    std::string scheme;    /**< Scheme of the uri. */
    std::string detail;    /**< e.g. "8 ch, 250 Hz", or why it is unavailable. */
    bool available = true; /**< false for backends whose runtime library was not found (still listed). */
};

//=============================================================================================================
/**
 * Process-wide registry of source schemes.
 */
class ACQSHARED_EXPORT AcqDeviceRegistry
{
public:
    /** Opens a backend for a parsed URI. */
    using Factory = std::function<AcqExpected<std::unique_ptr<AcqBackend>>(const AcqUri&)>;

    /** Lists reachable sources of one scheme within a timeout. */
    using Discoverer = std::function<std::vector<AcqDiscoveredDevice>(std::chrono::milliseconds)>;

    /** @return The process-wide registry, with every compiled-in backend registered. */
    [[nodiscard]] static AcqDeviceRegistry& instance();

    //=========================================================================================================
    /**
     * Registers a scheme. Thread-safe.
     *
     * @param[in] info         Scheme description.
     * @param[in] factory      Opens a backend.
     * @param[in] discoverer   Optional discovery function.
     *
     * @return InvalidParameter if the scheme is already registered or malformed.
     */
    AcqError registerScheme(AcqSchemeInfo info, Factory factory, Discoverer discoverer = {});

    /** @return The opened source for @p uri; see AcqDevice::open(). */
    [[nodiscard]] AcqExpected<AcqDevice> open(std::string_view uri) const;

    /** @return All registered schemes, sorted by name. */
    [[nodiscard]] std::vector<AcqSchemeInfo> schemes() const;

    /** @return Reachable sources of every scheme, discovered in parallel within @p timeout. */
    [[nodiscard]] std::vector<AcqDiscoveredDevice> discover(std::chrono::milliseconds timeout) const;

    AcqDeviceRegistry(const AcqDeviceRegistry&) = delete;
    AcqDeviceRegistry& operator=(const AcqDeviceRegistry&) = delete;

private:
    AcqDeviceRegistry();
    ~AcqDeviceRegistry();

    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};

} // namespace ACQLIB

#endif // ACQ_DEVICE_REGISTRY_H
