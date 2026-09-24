#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "XmlElement.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include <QString>

/// What the weather and the ground are doing, read off MUME's weather and terrain lines.
///
/// MUME reports the sky where the character stands as the prompt's symbols in GMCP
/// Char.Vitals, and the sun, the moon and the Necromancer's Darkness as Event.* packages.
/// Everything else it says about the weather is prose. Most of it comes inside <weather>
/// elements, and the snow lying in a room inside the room's <terrain>. That prose covers
/// lightning seen and thunder heard, fog drifting in from a side, frost on the ground and ice
/// on water with their levels, a storm with rain or snow in it, and weather that someone
/// changed with magic ("How very strange!").
///
/// These are recognised here, once, from the element's own text, and published as
/// MMapper.Weather.Event, with the ground as MMapper.Weather.Ground. Every frontend would
/// otherwise have to match MUME's sentences itself.
///
/// The sentences come from the powwow logs (mume3d's docs/weather.md and
/// tools/weather_logs.py). Those logs are old, and MUME words the same weather differently in
/// different places, so matching is by the words that matter ("lightning", "thunder") where
/// the sentences vary. A line nothing here knows is passed on as UNKNOWN with its text.
enum class NODISCARD WeatherLineKindEnum : uint8_t {
    UNKNOWN,
    /// snow lying on the ground: none, some, lot, deep
    SNOW,
    /// frost on the ground: none, slight, frosty, very, frozen
    FROST,
    /// ice on water: none, film, some, thick, frozen
    ICE,
    /// lightning seen, or with the level "none" when it stopped
    LIGHTNING,
    /// thunder heard, lightning not seen
    THUNDER,
    /// fog drifting in (level "light", from `direction`), thickening ("dense"), or gone ("none")
    FOG,
    /// a storm: the weather "becomes stormy", or the sky "cracks and booms" as rain or snow falls
    STORM,
    /// the Necromancer's Darkness: start, present, end
    DARKNESS,
};

/// One line of MUME's weather prose, as far as it could be read.
struct NODISCARD WeatherLine final
{
    WeatherLineKindEnum kind = WeatherLineKindEnum::UNKNOWN;
    /// For SNOW, FROST, ICE, FOG and DARKNESS: the state now, or the state a change is heading
    /// for when `changing` is set. "none" for LIGHTNING that stopped. Empty otherwise.
    std::string_view level;
    /// True when the line reports a change under way ("The ground is hardening with frost.")
    /// rather than a state.
    bool changing = false;
    /// For FOG drifting in, the side it comes from, which MUME picks at random; for LIGHTNING
    /// and THUNDER, the side a storm elsewhere was seen or heard from. Empty when not said.
    std::string_view direction;
    /// For STORM: "rain", "snow", or empty when the line does not say.
    std::string_view precipitation;
    /// True for weather someone changed with magic: MUME ends those lines "How very strange!",
    /// "Quite strange!" or ", how strange...".
    bool magic = false;
    /// The line as MUME wrote it, colour and surrounding space removed.
    QString text;
};

/// The ground where the body stands, as the last room display and the lines after it said.
///
/// Each level is "unknown" when a display could not say. In dense fog or darkness MUME
/// replaces the room with a line saying so, and then nothing about the ground can be read
/// from it.
struct NODISCARD GroundState final
{
    std::string_view snow = "none";
    std::string_view frost = "none";
    std::string_view ice = "none";
    /// True when this came with a room display, so the character arrived somewhere or looked.
    /// False when the ground changed where they stand.
    bool entered = false;
};

/// One line of MUME's weather prose, colour removed or not.
NODISCARD WeatherLine parseWeatherLine(const QString &line);

/// The weather line a <weather> or <terrain> element carries, and nullopt for any other
/// element. A <terrain> element can hold several lines; the first one about the weather is
/// returned.
NODISCARD std::optional<WeatherLine> parseWeatherElement(const XmlElement &element);

NODISCARD std::string_view to_string_view(WeatherLineKindEnum kind);

/// Follows MUME's XML elements to keep the ground's state.
///
/// A room display starts it afresh: a room whose display has no snow line has no snow, and a
/// frosty room says so after its exits every time it is entered. Snow comes from the room's
/// own <terrain>, and frost and ice from the <weather> lines that follow it. The prompt that
/// ends the display makes the state complete. A frost or ice line with no room display before
/// it is a change where the character stands, and is folded into the state it changes.
class NODISCARD GroundTracker final
{
private:
    GroundState m_state;
    GroundState m_pending;
    bool m_dirty = false;

public:
    /// Feeds one completed element, and `line`, what parseWeatherElement() read from it.
    /// Returns the ground's state at a prompt that ends a room display or a change, and
    /// nullopt otherwise.
    NODISCARD std::optional<GroundState> receive(const XmlElement &element,
                                                 const std::optional<WeatherLine> &line);
    NODISCARD const GroundState &state() const { return m_state; }
    /// For a new session, or when XML mode goes away.
    void reset();
};
