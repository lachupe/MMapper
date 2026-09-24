// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "XmlElement.h"

#include "../global/Charset.h"
#include "../global/Consts.h"
#include "../global/TextUtils.h"

#include <functional>
#include <optional>
#include <sstream>

namespace {

struct NODISCARD XmlTagName final
{
    std::string_view name;
    XmlTagEnum tag;
    XmlCategoryEnum category;
};

const XmlTagName g_tagNames[] = {
#define X_DECL_XML_TAG_NAME(UPPER_CASE, lower_case, category) \
    XmlTagName{lower_case, XmlTagEnum::UPPER_CASE, XmlCategoryEnum::category},
    XFOREACH_XML_TAG(X_DECL_XML_TAG_NAME)
#undef X_DECL_XML_TAG_NAME
};

} // namespace

XmlTagEnum toXmlTag(const std::string_view name)
{
    for (const XmlTagName &entry : g_tagNames) {
        if (entry.name == name) {
            return entry.tag;
        }
    }
    return XmlTagEnum::UNKNOWN;
}

std::string_view to_string_view(const XmlTagEnum tag)
{
    for (const XmlTagName &entry : g_tagNames) {
        if (entry.tag == tag) {
            return entry.name;
        }
    }
    return "unknown";
}

XmlCategoryEnum toXmlCategory(const XmlTagEnum tag)
{
    for (const XmlTagName &entry : g_tagNames) {
        if (entry.tag == tag) {
            return entry.category;
        }
    }
    return XmlCategoryEnum::UNKNOWN;
}

std::string_view to_string_view(const XmlCategoryEnum category)
{
    switch (category) {
    case XmlCategoryEnum::ROOM:
        return "room";
    case XmlCategoryEnum::STATUS:
        return "status";
    case XmlCategoryEnum::COMBAT:
        return "combat";
    case XmlCategoryEnum::MAGIC:
        return "magic";
    case XmlCategoryEnum::ENTITY:
        return "entity";
    case XmlCategoryEnum::MOVEMENT:
        return "movement";
    case XmlCategoryEnum::COMMUNICATION:
        return "communication";
    case XmlCategoryEnum::PROGRESS:
        return "progress";
    case XmlCategoryEnum::FORMATTING:
        return "formatting";
    case XmlCategoryEnum::UNKNOWN:
        break;
    }
    return "unknown";
}

namespace {

/// A direction as MUME writes it in prose or in a `dir` attribute, in its canonical lowercase
/// form, or empty for any other word.
NODISCARD std::string directionWord(const QString &word)
{
    const QString lower = word.trimmed().toLower();
    static const char *const directions[] = {"north", "south", "east", "west", "up", "down"};
    for (const char *const direction : directions) {
        if (lower == QLatin1String{direction}) {
            return direction;
        }
    }
    if (lower == QLatin1String{"above"}) {
        return "up";
    }
    if (lower == QLatin1String{"below"}) {
        return "down";
    }
    return {};
}

/// Blanks out the text of every name inside the element, however deeply nested, so that a
/// direction word in somebody's name cannot be taken for the direction they went.
void blankNames(const XmlElement &element, QString &text)
{
    for (const XmlElement &child : element.children) {
        if (toXmlCategory(child.tag) == XmlCategoryEnum::ENTITY && !child.text.isEmpty()) {
            const qsizetype at = text.indexOf(child.text);
            if (at >= 0) {
                text.replace(at, child.text.length(), QString(child.text.length(), QChar{' '}));
            }
            continue;
        }
        blankNames(child, text);
    }
}

} // namespace

std::string deriveMovementDirection(const XmlElement &element)
{
    switch (element.tag) {
    case XmlTagEnum::MOVEMENT:
    case XmlTagEnum::MOVE_IN:
    case XmlTagEnum::MOVE_OUT:
        break;
    default:
        return {};
    }

    // MUME's own statement, wherever it makes one, outranks reading the line.
    for (const auto &attribute : element.attributes) {
        if (attribute.first == "dir") {
            const std::string stated = directionWord(QString::fromStdString(attribute.second));
            if (!stated.empty()) {
                return stated;
            }
        }
    }
    // The player's own move is only ever what MUME states; there is no line to read.
    if (element.tag == XmlTagEnum::MOVEMENT) {
        return {};
    }

    QString text = element.text;
    blankNames(element, text);
    std::string found;
    QString word;
    const auto consider = [&found, &word]() {
        if (!word.isEmpty()) {
            const std::string direction = directionWord(word);
            if (!direction.empty()) {
                found = direction;
            }
            word.clear();
        }
    };
    for (const QChar c : text) {
        if (c.isLetter()) {
            word.append(c);
        } else {
            consider();
        }
    }
    consider();
    return found;
}

// Lifted from MumeXmlParser::element(), which built this and then discarded the result.
// Shared rather than duplicated so that there is one answer to what MUME's attribute
// syntax means.
XmlAttributes parseXmlAttributes(const QString &tagBody)
{
    using namespace char_consts;

    enum class NODISCARD StateEnum : uint8_t {
        /// still reading the tag name, as in <room
        ELEMENT,
        /// <room terrain
        ATTRIBUTE,
        /// <room terrain=
        EQUALS,
        /// <room terrain=field
        UNQUOTED_VALUE,
        /// <room terrain='field'
        SINGLE_QUOTED_VALUE,
        /// <room terrain="field"
        DOUBLE_QUOTED_VALUE
    };

    XmlAttributes attributes;

    std::ostringstream os;
    std::optional<std::string> key;
    StateEnum state = StateEnum::ELEMENT;

    const auto makeAttribute = [&key, &os, &attributes, &state]() {
        if (key.has_value()) {
            // REVISIT: Translate XML entities into text
            attributes.emplace_back(key.value(), os.str());
            key.reset();
        }
        os.str(std::string());
        state = StateEnum::ATTRIBUTE;
    };

    for (const QChar qc : tagBody) {
        if (qc.unicode() >= 256) {
            continue;
        }
        const char c = mmqt::toLatin1(qc);

        switch (state) {
        case StateEnum::ELEMENT:
            if (ascii::isSpace(c)) {
                state = StateEnum::ATTRIBUTE;
            }
            break;
        case StateEnum::ATTRIBUTE:
            if (ascii::isSpace(c) || c == C_SLASH) {
                continue;
            } else if (c == C_EQUALS) {
                key = os.str();
                os.str(std::string());
                state = StateEnum::EQUALS;
            } else {
                os << c;
            }
            break;
        case StateEnum::EQUALS:
            if (ascii::isSpace(c)) {
                continue;
            } else if (c == C_SQUOTE) {
                state = StateEnum::SINGLE_QUOTED_VALUE;
            } else if (c == C_DQUOTE) {
                state = StateEnum::DOUBLE_QUOTED_VALUE;
            } else {
                os << c;
                state = StateEnum::UNQUOTED_VALUE;
            }
            break;
        case StateEnum::UNQUOTED_VALUE:
            // Note: This format is not valid according to the W3C XML standard, but MUME
            // sends it anyway.
            if (ascii::isSpace(c) || c == C_SLASH) {
                makeAttribute();
            } else {
                os << c;
            }
            break;
        case StateEnum::SINGLE_QUOTED_VALUE:
            if (c == C_SQUOTE) {
                makeAttribute();
            } else {
                os << c;
            }
            break;
        case StateEnum::DOUBLE_QUOTED_VALUE:
            if (c == C_DQUOTE) {
                makeAttribute();
            } else {
                os << c;
            }
            break;
        }
    }
    if (key.has_value()) {
        makeAttribute();
    }
    return attributes;
}
