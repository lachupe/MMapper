// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "RoomContents.h"

#include "../global/parserutils.h"
#include "../proxy/GmcpMessage.h"

#include <array>
#include <utility>

#include <QRegularExpression>

namespace {

// What MUME lets a player look into, open or loot. The first half is what the powwow logs show
// as the header of a container's listing ("chest (here) :", "skeleton (here) :", "rack (here)
// :"); the rest are the furniture and vessels MUME's zones are known to hold. A noun only
// counts as a whole word, so "boxwood" and "chestnut" are not boxes and chests.
constexpr std::array<const char *, 30> NOUNS{"corpse",      "chest",    "backpack",  "skeleton",
                                             "pouch",       "sack",     "crate",     "cabinet",
                                             "rack",        "peg",      "bookshelf", "cart",
                                             "cage",        "quiver",   "coffer",    "casket",
                                             "strongbox",   "trunk",    "box",       "barrel",
                                             "cask",        "keg",      "urn",       "coffin",
                                             "sarcophagus", "cupboard", "wardrobe",  "basket",
                                             "bag",         "remains"};

NODISCARD const QRegularExpression &nounPattern()
{
    static const QRegularExpression pattern = [] {
        QStringList words;
        for (const char *const noun : NOUNS) {
            words << QString::fromLatin1(noun);
        }
        return QRegularExpression{QStringLiteral("\\b(%1)\\b").arg(words.join(QLatin1Char('|'))),
                                  QRegularExpression::CaseInsensitiveOption};
    }();
    return pattern;
}

// MUME folds identical objects into one line on some settings. Neither form is in the logs we
// have, so both usual spellings are accepted: "[ 3] A torch lies here." and "A torch lies
// here. [3]".
const QRegularExpression g_countBefore{QStringLiteral(R"(^\[\s*(\d+)\]\s*(.+)$)")};
const QRegularExpression g_countAfter{QStringLiteral(R"(^(.+?)\s*\[\s*(\d+)\]$)")};

NODISCARD QString cleaned(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    return text.simplified();
}

NODISCARD QStringList linesOf(const QString &text)
{
    QStringList result;
    for (const QString &part : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString line = cleaned(part);
        if (!line.isEmpty()) {
            result << line;
        }
    }
    return result;
}

NODISCARD bool isPerson(const XmlTagEnum tag)
{
    return tag == XmlTagEnum::CHARACTER || tag == XmlTagEnum::PLAYER || tag == XmlTagEnum::ENEMY
           || tag == XmlTagEnum::FAMILIAR;
}

/// The elements anywhere inside `element` whose tag satisfies `wanted`, depth first.
template<typename Predicate>
void collect(const XmlElement &element, Predicate &&wanted, QStringList &into)
{
    for (const XmlElement &child : element.children) {
        if (wanted(child.tag)) {
            const QString text = cleaned(child.text);
            if (!text.isEmpty()) {
                into << text;
            }
        }
        collect(child, wanted, into);
    }
}

NODISCARD bool seesTheRoom(const XmlElement &room)
{
    // In dense fog or darkness MUME puts a line saying so where the name would be, and what
    // the rest of the display lists is then not the room's.
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

NODISCARD QString withoutFinalStop(const QString &line)
{
    return line.endsWith(QLatin1Char('.')) ? line.left(line.size() - 1) : line;
}

} // namespace

QString containerKeyword(const QString &line)
{
    const QString text = cleaned(line);
    auto it = nounPattern().globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString noun = match.captured(1).toLower();
        // A tree's trunk is not a chest: "The trunk of an ancient oak is covered in moss."
        if (noun == QStringLiteral("trunk")
            && (text.contains(QStringLiteral("tree"), Qt::CaseInsensitive)
                || text.mid(match.capturedEnd(1)).startsWith(QStringLiteral(" of")))) {
            continue;
        }
        return noun;
    }
    return QString{};
}

bool namesContainer(const QString &rawWord, const QString &keyword)
{
    const QString word = rawWord.toLower();
    if (word.isEmpty()) {
        return false;
    }
    const auto fits = [&word](const QString &noun) {
        // MUME takes any prefix of a keyword ("che" for chest), and an object's first keyword
        // is often a compound of the noun ("stonechest", "gccorpse").
        return (word.size() >= 3 && noun.startsWith(word)) || word == noun || word.endsWith(noun);
    };
    if (!keyword.isEmpty()) {
        return fits(keyword);
    }
    for (const char *const noun : NOUNS) {
        if (fits(QString::fromLatin1(noun))) {
            return true;
        }
    }
    return false;
}

void RoomContentsTracker::receiveChars(const GmcpMessage &msg)
{
    const bool isSet = msg.isRoomCharsSet();
    const bool isChange = msg.isRoomCharsAdd() || msg.isRoomCharsUpdate();
    if (!isSet && !isChange && !msg.isRoomCharsRemove()) {
        return;
    }
    const auto &optDoc = msg.getJsonDocument();
    if (!optDoc.has_value()) {
        return;
    }
    if (msg.isRoomCharsRemove()) {
        // The payload is a bare number, the id of whoever left.
        if (const auto optId = optDoc->getInt()) {
            m_chars.erase(static_cast<int64_t>(*optId));
        }
        return;
    }
    const auto remember = [this](const JsonObj &obj) {
        const auto optId = obj.getInt("id");
        if (!optId.has_value()) {
            return;
        }
        QString &desc = m_chars[static_cast<int64_t>(*optId)];
        if (const auto optDesc = obj.getString("desc")) {
            desc = cleaned(*optDesc);
        }
    };
    if (isSet) {
        m_chars.clear();
        if (const auto optArray = optDoc->getArray()) {
            for (const auto &entry : *optArray) {
                if (const auto optObj = entry.getObject()) {
                    remember(*optObj);
                }
            }
        }
        return;
    }
    if (const auto optObj = optDoc->getObject()) {
        remember(*optObj);
    }
}

std::optional<RoomContentsSnapshot> RoomContentsTracker::receive(const XmlElement &element,
                                                                 const QString &roomLines,
                                                                 const QString &roomKey)
{
    switch (element.tag) {
    case XmlTagEnum::ROOM:
        m_lines = linesOf(roomLines);
        m_roomKey = roomKey;
        m_seen = seesTheRoom(element);
        m_objectNames.clear();
        m_people.clear();
        m_scenery.clear();
        collect(element, [](XmlTagEnum tag) { return tag == XmlTagEnum::OBJECT; }, m_objectNames);
        collect(element, isPerson, m_people);
        // The room's own parts. MMapper's buffer of dynamic lines also takes in the text of a
        // <terrain> element, such as the snow lying there, which is not an object.
        for (const XmlElement &child : element.children) {
            switch (child.tag) {
            case XmlTagEnum::NAME:
            case XmlTagEnum::DESCRIPTION:
            case XmlTagEnum::TERRAIN:
            case XmlTagEnum::EXITS:
            case XmlTagEnum::HEADER:
                m_scenery << linesOf(child.text);
                break;
            default:
                break;
            }
        }
        m_dirty = true;
        return std::nullopt;
    case XmlTagEnum::PROMPT:
        if (!m_dirty) {
            return std::nullopt;
        }
        m_dirty = false;
        return build();
    default:
        return std::nullopt;
    }
}

bool RoomContentsTracker::isCharacterLine(const QString &line) const
{
    for (const QString &person : m_people) {
        if (line.contains(person)) {
            return true;
        }
    }
    // Room.Chars gives each character's line in the room as `desc`. Whether it always ends
    // with the same full stop as the display is not known, so that is not required.
    const QString bare = withoutFinalStop(line);
    for (const auto &[id, desc] : m_chars) {
        if (!desc.isEmpty() && withoutFinalStop(desc).compare(bare, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

RoomContentsSnapshot RoomContentsTracker::build() const
{
    RoomContentsSnapshot result;
    result.roomKey = m_roomKey;
    result.seen = m_seen;
    result.entered = true;
    if (!m_seen) {
        return result;
    }

    // How many of each container noun came before, so that the next one's target can say
    // "2.chest" as MUME would count it.
    std::map<QString, int> seenOfKeyword;
    for (const QString &raw : m_lines) {
        if (m_scenery.contains(raw) || isCharacterLine(raw)) {
            continue;
        }
        RoomObject object;
        object.index = static_cast<int>(result.objects.size());
        object.line = raw;
        if (const auto before = g_countBefore.match(raw); before.hasMatch()) {
            object.count = std::max(1, before.captured(1).toInt());
            object.line = before.captured(2);
        } else if (const auto after = g_countAfter.match(raw); after.hasMatch()) {
            object.count = std::max(1, after.captured(2).toInt());
            object.line = after.captured(1);
        }
        for (const QString &name : m_objectNames) {
            if (object.line.contains(name)) {
                object.name = name;
                break;
            }
        }
        object.keyword = containerKeyword(object.line);
        object.container = !object.keyword.isEmpty();
        if (object.container) {
            int &before = seenOfKeyword[object.keyword];
            object.target = (before == 0)
                                ? object.keyword
                                : QStringLiteral("%1.%2").arg(before + 1).arg(object.keyword);
            before += object.count;
        }
        result.objects.push_back(std::move(object));
    }
    return result;
}

void RoomContentsTracker::reset()
{
    m_chars.clear();
    m_lines.clear();
    m_objectNames.clear();
    m_people.clear();
    m_scenery.clear();
    m_roomKey.clear();
    m_seen = true;
    m_dirty = false;
}
