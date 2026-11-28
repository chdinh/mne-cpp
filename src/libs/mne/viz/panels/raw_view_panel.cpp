//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     raw_view_panel.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    RawViewPanel class definition.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "raw_view_panel.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <algorithm>
#include <charconv>
#include <string>
#include <system_error>
#include <utility>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace mne::viz;

//=============================================================================================================
// DEFINE STATIC METHODS
//=============================================================================================================

namespace
{

constexpr const char* kZoom = "viewZoomFactor";
constexpr const char* kWindow = "viewWindowSize";
constexpr const char* kSpacer = "distanceTimeSpacer";
constexpr const char* kSignal = "signalColor";
constexpr const char* kBackground = "backgroundColor";
constexpr const char* kImageType = "imageType";
constexpr const char* kScreenshot = "makeScreenshot";

constexpr const char* kBlue = "#0000ff";
constexpr const char* kWhite = "#ffffff";

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

RawViewPanel::RawViewPanel(const std::vector<std::string>& shown)
: Panel("FiffRawViewSettings", "Fiff Raw View Settings")
{
    set_width(330);
    const auto hidden = [&shown](const char* control) {
        return !shown.empty() && std::ranges::none_of(shown, [control](const std::string& name) { return name == control; });
    };
    SettingsForm& f = form();

    SettingField zoom;
    zoom.key = kZoom;
    zoom.kind = SettingField::Kind::Double;
    zoom.label = "Number of channels:";
    zoom.default_value = 16.0;
    zoom.minimum = 1.0;
    zoom.maximum = 100.0;
    zoom.decimals = 0;
    zoom.hidden = hidden("numberChannels");
    f.add(std::move(zoom));

    SettingField window;
    window.key = kWindow;
    window.kind = SettingField::Kind::Int;
    window.label = "Window size:";
    window.unit = " s";
    window.default_value = 10;
    window.minimum = 1;
    window.maximum = 120;
    window.decimals = 0;
    window.hidden = hidden("windowSize");
    f.add(std::move(window));

    SettingField spacer;
    spacer.key = kSpacer;
    spacer.kind = SettingField::Kind::Choice;
    spacer.label = "Time spacer every (ms):";
    spacer.choices = {{"100", "100"}, {"200", "200"}, {"500", "500"}, {"1000", "1000"}};
    spacer.default_value = std::string("1000");
    spacer.hidden = hidden("distanceSpacers");
    f.add(std::move(spacer));

    SettingField signal;
    signal.key = kSignal;
    signal.kind = SettingField::Kind::Color;
    signal.label = "Signal Color:";
    signal.default_value = std::string(kBlue);
    signal.hidden = hidden("signalColor");
    f.add(std::move(signal));

    SettingField background;
    background.key = kBackground;
    background.kind = SettingField::Kind::Color;
    background.label = "Background Color:";
    background.default_value = std::string(kWhite);
    background.hidden = hidden("backgroundColor");
    f.add(std::move(background));

    SettingField imageType;
    imageType.key = kImageType;
    imageType.kind = SettingField::Kind::Choice;
    imageType.label = "Image type:";
    imageType.choices = {{"PNG", "PNG"}, {"SVG", "SVG"}};
    imageType.default_value = std::string("PNG");
    imageType.hidden = hidden("screenshot");
    imageType.persistent = false;
    f.add(std::move(imageType));

    SettingField screenshot;
    screenshot.key = kScreenshot;
    screenshot.kind = SettingField::Kind::Action;
    screenshot.label = "Make screenshot";
    screenshot.hidden = hidden("screenshot");
    f.add(std::move(screenshot));

    m_listener = f.add_listener([this](const std::string& key, const std::optional<SettingValue>& value) {
        if (!m_updating && (value || key == kScreenshot)) {
            changed(key);
        }
    });
}

//=============================================================================================================

RawViewPanel::~RawViewPanel()
{
    form().remove_listener(m_listener);
}

//=============================================================================================================

double RawViewPanel::zoom() const
{
    return form().get<double>(kZoom);
}

//=============================================================================================================

void RawViewPanel::set_zoom(double channels)
{
    set_silently(kZoom, channels);
}

//=============================================================================================================

int RawViewPanel::window_size() const
{
    return form().get<int>(kWindow);
}

//=============================================================================================================

void RawViewPanel::set_window_size(int seconds)
{
    set_silently(kWindow, seconds);
}

//=============================================================================================================

int RawViewPanel::time_spacer() const
{
    const std::string text = form().get<std::string>(kSpacer);
    int milliseconds = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), milliseconds);
    return error == std::errc{} ? milliseconds : 1000;
}

//=============================================================================================================

void RawViewPanel::set_time_spacer(int milliseconds)
{
    set_silently(kSpacer, std::to_string(milliseconds));
}

//=============================================================================================================

Color RawViewPanel::signal_color() const
{
    return parse_setting_color(form().get<std::string>(kSignal)).value_or(Color{0, 0, 255, 255});
}

//=============================================================================================================

void RawViewPanel::set_signal_color(const Color& color)
{
    set_silently(kSignal, setting_color(color));
}

//=============================================================================================================

Color RawViewPanel::background_color() const
{
    return parse_setting_color(form().get<std::string>(kBackground)).value_or(Color::white());
}

//=============================================================================================================

void RawViewPanel::set_background_color(const Color& color)
{
    set_silently(kBackground, setting_color(color));
}

//=============================================================================================================

std::string RawViewPanel::image_type() const
{
    return form().get<std::string>(kImageType);
}

//=============================================================================================================

void RawViewPanel::load(const SettingsStore& store, const std::string& path)
{
    const bool updating = std::exchange(m_updating, true);
    Panel::load(store, path);
    m_updating = updating;
}

//=============================================================================================================

void RawViewPanel::set_silently(const std::string& key, const SettingValue& value)
{
    const bool updating = std::exchange(m_updating, true);
    form().set_value(key, value);
    m_updating = updating;
}

//=============================================================================================================

void RawViewPanel::changed(const std::string& key)
{
    if (key == kZoom && m_callbacks.zoom_changed) {
        m_callbacks.zoom_changed(zoom());
    } else if (key == kWindow && m_callbacks.window_size_changed) {
        m_callbacks.window_size_changed(window_size());
    } else if (key == kSpacer && m_callbacks.time_spacer_changed) {
        m_callbacks.time_spacer_changed(time_spacer());
    } else if (key == kSignal && m_callbacks.signal_color_changed) {
        m_callbacks.signal_color_changed(signal_color());
    } else if (key == kBackground && m_callbacks.background_color_changed) {
        m_callbacks.background_color_changed(background_color());
    } else if (key == kScreenshot && m_callbacks.screenshot_wanted) {
        m_callbacks.screenshot_wanted(image_type());
    }
}
