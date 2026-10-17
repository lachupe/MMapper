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
constexpr std::array<const char *, 47> NOUNS{"corpse",      "chest",     "backpack",  "skeleton",
                                             "pouch",       "sack",      "crate",     "cabinet",
                                             "rack",        "peg",       "bookshelf", "cart",
                                             "cage",        "quiver",    "coffer",    "casket",
                                             "strongbox",   "trunk",     "box",       "barrel",
                                             "cask",        "keg",       "urn",       "coffin",
                                             "sarcophagus", "cupboard",  "wardrobe",  "basket",
                                             "bag",         "remains",   "satchel",   "haversack",
                                             "knapsack",    "rucksack",  "pack",      "sable",
                                             "wallet",      "purse",     "bladder",   "sheath",
                                             "scabbard",    "keyring",   "moneybag",  "web",
                                             "bolt-case",   "bolt case", "key ring"};

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

/// A person's line as it can be compared with Room.Chars: no final stop, no status MUME adds at
/// the end of the room line ("(linkless)"), and no accents. Room.Chars spells a name the way
/// the game does ("A Dúnadan soldier is here busily performing his duties.") while the room
/// display may carry it in ASCII ("A Dunadan soldier ..."), as in mume3d's live log of
/// 2026-10-11, where every soldier and officer of Fornost was left in the objects for that.
NODISCARD QString comparable(const QString &line)
{
    static const QRegularExpression trailingStatus{QStringLiteral(R"(\s*\([^()]*\)$)")};
    QString text = withoutFinalStop(line.trimmed());
    for (;;) {
        const QString shorter = QString{text}.remove(trailingStatus);
        if (shorter == text) {
            break;
        }
        text = withoutFinalStop(shorter.trimmed());
    }
    QString result;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            result += c;
        }
    }
    return result.toLower();
}

/// The words of a name as a command could say them, lowercased: "weapons rack" answers to
/// "weapons" and "rack".
NODISCARD QStringList nameWords(const QString &name)
{
    QStringList words;
    for (const QString &part : name.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        QString word = part;
        while (!word.isEmpty() && !word.front().isLetterOrNumber()) {
            word.remove(0, 1);
        }
        while (!word.isEmpty() && !word.back().isLetterOrNumber()) {
            word.chop(1);
        }
        if (!word.isEmpty()) {
            words << word;
        }
    }
    return words;
}

NODISCARD QString withoutArticle(const QString &phrase)
{
    static const QRegularExpression article{QStringLiteral(R"(^(?:(?:a|an|the|some)\s+)+)")};
    return QString{phrase}.remove(article).trimmed();
}

/// Whether `object` is one a command saying `word` counts: its keyword, or a word of its name.
NODISCARD bool answers(const RoomObject &object, const QString &word)
{
    return object.keyword == word || nameWords(object.name).contains(word);
}

/// "In the rack is a cudgel.": what lies in or on a container, shown on a line of its own.
NODISCARD bool isContentsLine(const QString &text)
{
    static const QRegularExpression contentsLine{
        QStringLiteral(
            R"(^(?:In|On|Inside|Upon) (?:the|a|an|some) .+? (?:is|are|lies|lie|rests|rest|hangs|hang|stands|stand)\b)"),
        QRegularExpression::CaseInsensitiveOption};
    return contentsLine.match(text).hasMatch();
}

} // namespace

QString containerKeyword(const QString &line)
{
    const QString text = cleaned(line);
    // What lies in or on a container, on a line of its own, is not the container: "In the rack is
    // a cudgel." (Fornost's Guild of Warriors, mume3d's live test of 2026-10-03, where the line was
    // taken for a second rack and `examine 2.rack` found none).
    if (isContentsLine(text)) {
        return QString{};
    }
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

QString objectKeyword(const QString &name)
{
    // Words that measure out what the name is about: "a pair of boots" is boots.
    static const QStringList measures{QStringLiteral("pair"),
                                      QStringLiteral("set"),
                                      QStringLiteral("suit"),
                                      QStringLiteral("bunch"),
                                      QStringLiteral("handful"),
                                      QStringLiteral("piece"),
                                      QStringLiteral("bundle"),
                                      QStringLiteral("pile"),
                                      QStringLiteral("heap"),
                                      QStringLiteral("lump"),
                                      QStringLiteral("sprig"),
                                      QStringLiteral("coil"),
                                      QStringLiteral("bit"),
                                      QStringLiteral("chunk"),
                                      QStringLiteral("slice"),
                                      QStringLiteral("stack"),
                                      QStringLiteral("roll")};
    // What follows these describes the thing rather than names it: "a keyring with several keys".
    static const QRegularExpression description{
        QStringLiteral(R"((?:,|\s+(?:with|that|which|named|entitled)\s).*$)")};
    QString phrase = withoutArticle(cleaned(name).toLower()).remove(description);
    for (;;) {
        const qsizetype of = phrase.indexOf(QStringLiteral(" of "));
        if (of <= 0) {
            break;
        }
        const QStringList before = nameWords(phrase.left(of));
        if (before.isEmpty()) {
            break;
        }
        QString head = before.back();
        if (head.endsWith(QLatin1Char('s'))) {
            head.chop(1);
        }
        if (!measures.contains(before.back()) && !measures.contains(head)) {
            return before.back();
        }
        phrase = withoutArticle(phrase.mid(of + 4));
    }
    const QStringList words = nameWords(phrase);
    return words.isEmpty() ? QString{} : words.back();
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
    const QString bare = comparable(line);
    for (const QString &person : m_people) {
        if (bare.contains(comparable(person))) {
            return true;
        }
    }
    // Room.Chars gives each character's line in the room as `desc`. Whether it always ends
    // with the same full stop as the display is not known, so that is not required.
    for (const auto &[id, desc] : m_chars) {
        if (!desc.isEmpty() && comparable(desc) == bare) {
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

    // MUME's <object> elements come in the order of the lines holding them, people's lines
    // included ("Cainmir ..., wielding a <object>backsword</object>, is standing here."), so
    // each line takes the next names it holds. A line can hold more than one when MUME nests
    // them ("water of a small stream" around "small stream"); the first is the line's.
    qsizetype nextName = 0;
    const auto takeName = [this, &nextName](const QString &line) -> QString {
        for (qsizetype j = nextName; j < m_objectNames.size(); ++j) {
            if (!line.contains(m_objectNames.at(j))) {
                continue;
            }
            nextName = j + 1;
            while (nextName < m_objectNames.size() && line.contains(m_objectNames.at(nextName))) {
                ++nextName;
            }
            return m_objectNames.at(j);
        }
        return QString{};
    };

    for (const QString &raw : m_lines) {
        const QString name = takeName(raw);
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
        object.name = name;
        const QString noun = containerKeyword(object.line);
        object.container = !noun.isEmpty();
        object.keyword = object.container ? noun : objectKeyword(object.name);
        // A thing in a container is named through the container ("get cudgel rack"), so it has
        // no target of its own and no place in the room's count.
        if (!object.keyword.isEmpty() && !isContentsLine(object.line)) {
            // MUME counts every object answering to the word, in the list's order, so that the
            // second torch is 2.torch whatever else lies between.
            int before = 0;
            for (const RoomObject &earlier : result.objects) {
                if (!isContentsLine(earlier.line) && answers(earlier, object.keyword)) {
                    before += earlier.count;
                }
            }
            object.target = (before == 0)
                                ? object.keyword
                                : QStringLiteral("%1.%2").arg(before + 1).arg(object.keyword);
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
