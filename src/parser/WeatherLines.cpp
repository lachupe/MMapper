// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "WeatherLines.h"

#include "../global/parserutils.h"

#include <array>
#include <utility>

#include <QRegularExpression>
#include <QStringList>

namespace {

struct NODISCARD Known final
{
    const char *line;
    WeatherLineKindEnum kind;
    std::string_view level;
    bool changing;
};

using K = WeatherLineKindEnum;

// Every sentence here is one the logs show MUME printing for the ground or the fog. Levels run
// from nothing to the most: snow none, some, lot, deep; frost none, slight, frosty, very,
// frozen; ice none, film, some, thick, frozen. A change is given the level it is heading for.
constexpr std::array KNOWN{
    Known{"There is some snow on the ground.", K::SNOW, "some", false},
    Known{"There is snow on the ground.", K::SNOW, "some", false},
    Known{"There is a lot of snow on the ground.", K::SNOW, "lot", false},
    Known{"The snow is so deep that it is almost impossible to move.", K::SNOW, "deep", false},
    Known{"Opening your way through the deep snow quickly depletes your strength.",
          K::SNOW,
          "deep",
          false},
    Known{"You don't see any more snow here.", K::SNOW, "none", false},
    Known{"You don't see anymore snow here.", K::SNOW, "none", false},

    Known{"The ground is slightly frosty.", K::FROST, "slight", false},
    Known{"The ground is frosty.", K::FROST, "frosty", false},
    Known{"The ground is very frosty.", K::FROST, "very", false},
    Known{"The ground is frozen solid.", K::FROST, "frozen", false},
    Known{"The ground no longer feels frosty.", K::FROST, "none", false},
    Known{"You see some frost on the ground now.", K::FROST, "slight", true},
    Known{"The ground is hardening with frost.", K::FROST, "very", true},
    Known{"The frost is making the ground feel solid.", K::FROST, "frozen", true},
    Known{"The frost is leaving its hold of the ground.", K::FROST, "very", true},
    Known{"The frosty ground is starting to soften.", K::FROST, "slight", true},

    Known{"There is a thin film of ice on the water.", K::ICE, "film", false},
    Known{"There is some ice on the water.", K::ICE, "some", false},
    Known{"Thick ice covers the water.", K::ICE, "thick", false},
    Known{"The water is frozen solid.", K::ICE, "frozen", false},
    Known{"There is no longer any ice here.", K::ICE, "none", false},
    Known{"The ice is getting thick.", K::ICE, "thick", true},
    Known{"The ice is slowly starting to melt.", K::ICE, "some", true},
    Known{"The ice is becoming very unstable.", K::ICE, "film", true},

    Known{"The fog begins to be very dense.", K::FOG, "dense", true},
    Known{"The fog disappears.", K::FOG, "none", false},

    Known{"Arda seems to wither as an evil power begins to grow...", K::DARKNESS, "start", false},
    Known{"Shrouds of dark clouds roll in above you, blotting out the skies.",
          K::DARKNESS,
          "present",
          false},
    Known{"The evil power begins to regress...", K::DARKNESS, "end", false},
};

NODISCARD QString cleaned(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    return text.simplified();
}

NODISCARD bool isMagic(const QString &text)
{
    // "Lightning starts to show in the sky. Quite strange!", "The weather suddenly becomes
    // extremely stormy. How very strange!" and the same with ", how strange..." or ", how
    // strange." -- the wordings of weather someone changed with a spell.
    const QString lower = text.toLower();
    return lower.endsWith(QStringLiteral("how very strange!"))
           || lower.endsWith(QStringLiteral("quite strange!"))
           || lower.contains(QStringLiteral(", how strange"));
}

/// The side a line says something came from, "from the north" or "in the distance from the
/// south", or empty.
NODISCARD std::string_view sideOf(const QString &text)
{
    static constexpr std::array<std::string_view, 4> sides{"north", "south", "east", "west"};
    static const QRegularExpression from(QStringLiteral("\\bfrom the (north|south|east|west)\\b"));
    const auto match = from.match(text);
    if (!match.hasMatch()) {
        return {};
    }
    const QString side = match.captured(1);
    for (const std::string_view candidate : sides) {
        if (side == QLatin1String(candidate.data(), static_cast<qsizetype>(candidate.size()))) {
            return candidate;
        }
    }
    return {};
}

NODISCARD bool isGroundKind(const WeatherLineKindEnum kind)
{
    return kind == K::SNOW || kind == K::FROST || kind == K::ICE;
}

NODISCARD bool seesTheRoom(const XmlElement &room)
{
    // In dense fog or darkness MUME puts a line saying so where the name would be, and the
    // rest of the display says nothing about the ground either.
    for (const XmlElement &child : room.children) {
        if (child.tag != XmlTagEnum::NAME) {
            continue;
        }
        const QString name = cleaned(child.text);
        return !name.isEmpty() && !name.contains(QStringLiteral("fog around you"))
               && !name.contains(QStringLiteral("pitch black"))
               && !name.contains(QStringLiteral("too dark"));
    }
    return false;
}

void apply(GroundState &state, const WeatherLine &line)
{
    switch (line.kind) {
    case K::SNOW:
        state.snow = line.level;
        break;
    case K::FROST:
        state.frost = line.level;
        break;
    case K::ICE:
        state.ice = line.level;
        break;
    default:
        break;
    }
}

} // namespace

WeatherLine parseWeatherLine(const QString &raw)
{
    WeatherLine result;
    result.text = cleaned(raw);
    const QString &text = result.text;
    result.magic = isMagic(text);

    for (const Known &known : KNOWN) {
        if (text == QLatin1String(known.line)) {
            result.kind = known.kind;
            result.level = known.level;
            result.changing = known.changing;
            return result;
        }
    }

    if (text.startsWith(QStringLiteral("You see some fog coming from the "))) {
        result.kind = K::FOG;
        result.level = "light";
        result.changing = true;
        result.direction = sideOf(text);
        return result;
    }

    const QString lower = text.toLower();
    // "The sky cracks and booms, and snow falls on you." -- a storm, and what falls in it.
    if (lower.startsWith(QStringLiteral("the sky cracks and booms"))
        || lower.contains(QStringLiteral("becomes stormy"))
        || lower.contains(QStringLiteral("extremely stormy"))) {
        result.kind = K::STORM;
        if (lower.contains(QStringLiteral("snow"))) {
            result.precipitation = "snow";
        } else if (lower.contains(QStringLiteral("rain"))) {
            result.precipitation = "rain";
        }
        return result;
    }
    // The places word their lightning differently -- "A flare of lightning branches out into
    // several small streaks above the mountains.", "Dark clouds in the sky light up, as
    // lightning strikes from within." -- so the word is what is matched. Only lines MUME marks
    // as weather get here, which keeps out the spell and the attacks of that name. "The
    // lightning has stopped." is not a flash but the end of them, so it has the level none.
    if (lower.contains(QStringLiteral("lightning"))) {
        result.kind = K::LIGHTNING;
        if (lower.contains(QStringLiteral("stopped"))) {
            result.level = "none";
        }
        // "Lightning lights the sky, followed by a rolling thunder from the north."
        result.direction = sideOf(text);
        return result;
    }
    // "You hear a crackle of thunder in the distance from the south.": a storm somewhere else.
    if (lower.contains(QStringLiteral("thunder"))) {
        result.kind = K::THUNDER;
        result.direction = sideOf(text);
        return result;
    }
    return result;
}

std::optional<WeatherLine> parseWeatherElement(const XmlElement &element)
{
    if (element.tag != XmlTagEnum::WEATHER && element.tag != XmlTagEnum::TERRAIN) {
        return std::nullopt;
    }
    std::optional<WeatherLine> first;
    for (const QString &part : element.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        WeatherLine line = parseWeatherLine(part);
        if (line.text.isEmpty()) {
            continue;
        }
        if (line.kind != K::UNKNOWN) {
            return line;
        }
        if (!first.has_value()) {
            first = std::move(line);
        }
    }
    return first;
}

std::string_view to_string_view(const WeatherLineKindEnum kind)
{
    switch (kind) {
    case K::UNKNOWN:
        return "unknown";
    case K::SNOW:
        return "snow";
    case K::FROST:
        return "frost";
    case K::ICE:
        return "ice";
    case K::LIGHTNING:
        return "lightning";
    case K::THUNDER:
        return "thunder";
    case K::FOG:
        return "fog";
    case K::STORM:
        return "storm";
    case K::DARKNESS:
        return "darkness";
    }
    return "unknown";
}

std::optional<GroundState> GroundTracker::receive(const XmlElement &element,
                                                  const std::optional<WeatherLine> &line)
{
    switch (element.tag) {
    case XmlTagEnum::ROOM: {
        // A room display says everything about the ground there: a line it lacks means
        // nothing of that kind lies here.
        m_pending = GroundState{};
        m_pending.entered = true;
        if (!seesTheRoom(element)) {
            m_pending.snow = m_pending.frost = m_pending.ice = "unknown";
        } else {
            for (const XmlElement &child : element.children) {
                if (child.tag != XmlTagEnum::TERRAIN) {
                    continue;
                }
                for (const QString &part : child.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                    const WeatherLine inside = parseWeatherLine(part);
                    if (isGroundKind(inside.kind)) {
                        apply(m_pending, inside);
                    }
                }
            }
        }
        m_dirty = true;
        return std::nullopt;
    }
    case XmlTagEnum::PROMPT: {
        if (!m_dirty) {
            return std::nullopt;
        }
        m_dirty = false;
        m_state = m_pending;
        m_pending = m_state;
        m_pending.entered = false;
        return m_state;
    }
    default:
        break;
    }

    if (line.has_value() && isGroundKind(line->kind)) {
        apply(m_pending, *line);
        m_dirty = true;
    }
    return std::nullopt;
}

void GroundTracker::reset()
{
    m_state = GroundState{};
    m_pending = GroundState{};
    m_dirty = false;
}
