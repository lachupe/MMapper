// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "QuietCapture.h"

#include <array>

namespace {

/// One command a quiet command may be: MUME's word for it, how short MUME's help says it may
/// be typed (`pr(actice)` is 2), and whether it may have arguments.
struct NODISCARD QuietWord final
{
    const char *word;
    qsizetype shortest;
    bool arguments;
};

/// The commands that only read, by MUME's help as read on 2026-10-02. The spellings and the
/// shortest forms are from https://mume.org/help/commands. What each does is from its own page
/// (https://mume.org/help/<word>), read for twelve of them: practice, score, info, stat,
/// equipment, time, who, trophy, exits, examine, look and list. `inventory` has no page of its
/// own: what its address shows only names it, beside `equipment`, as what shows what is
/// carried. It rests on that sentence and on the command list.
///
/// Arguments are let through only where the help gives the command arguments that read: the
/// format of `info` and `equipment`, the group or `number` of `who`, the sorting and the
/// filters of `trophy` and `list`, the thing or side of `look` and `examine`. Not for
/// `practice`, which with a skill spends a practice session; not for `score`, whose
/// `score report` tells the room the character's figures; and not for `stat`, `time`, `exits`
/// and `inventory`, for which the help has none.
///
/// `look` and `examine` change nothing by the help; whoever is looked at may be told (the help
/// does not say). Not here: `practise` (the help has only `practice`), `affects` (MUME has no
/// such command), and every command that was not asked for. To let another through, add it
/// here; nothing else knows the list.
constexpr std::array<QuietWord, 13> g_quietWords{{
    {"equipment", 2, true},
    {"examine", 3, true},
    {"exits", 2, false},
    {"info", 3, true},
    {"inventory", 1, false},
    {"list", 2, true},
    {"look", 1, true},
    {"practice", 2, false},
    {"score", 2, false},
    {"stat", 4, false},
    {"time", 2, false},
    {"trophy", 3, true},
    {"who", 2, true},
}};

enum class NODISCARD TagMeaningEnum : uint8_t {
    /// Marks other traffic: a room display, speech, a movement, a blow, magic, the weather,
    /// an achievement.
    OTHER,
    /// Says nothing about whose text it is: emphasis, the prompt.
    NEUTRAL,
    /// Not known to be either.
    UNKNOWN
};

NODISCARD TagMeaningEnum meaningOf(const XmlTagEnum tag)
{
    // A table's heading: MUME marks `prac`'s "Skill  Knowledge  Difficulty  Class" with
    // <header>, outside any room (mume3d's live test of 2026-10-03: the quiet `prac` printed it in
    // the terminal and left it out of its reply). A room's own header lies inside <room>, which
    // quietTrafficOf() takes as other traffic before asking here.
    if (tag == XmlTagEnum::HEADER) {
        return TagMeaningEnum::NEUTRAL;
    }
    switch (toXmlCategory(tag)) {
    case XmlCategoryEnum::ROOM:
    case XmlCategoryEnum::COMBAT:
    case XmlCategoryEnum::MAGIC:
    case XmlCategoryEnum::MOVEMENT:
    case XmlCategoryEnum::COMMUNICATION:
    case XmlCategoryEnum::PROGRESS:
        return TagMeaningEnum::OTHER;
    case XmlCategoryEnum::STATUS:
        if (tag == XmlTagEnum::WEATHER) {
            return TagMeaningEnum::OTHER;
        }
        // <status> wraps lines of more than one kind; a reply's own may be among them.
        return tag == XmlTagEnum::PROMPT ? TagMeaningEnum::NEUTRAL : TagMeaningEnum::UNKNOWN;
    case XmlCategoryEnum::FORMATTING:
        return TagMeaningEnum::NEUTRAL;
    case XmlCategoryEnum::ENTITY:
    case XmlCategoryEnum::UNKNOWN:
        break;
    }
    return TagMeaningEnum::UNKNOWN;
}

} // namespace

bool quietCommandAllowed(const QString &line)
{
    const QString text = line.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar c : text) {
        if (c.category() == QChar::Other_Control || c == QLatin1Char(';')) {
            return false;
        }
    }
    qsizetype end = 0;
    while (end < text.size() && !text.at(end).isSpace()) {
        ++end;
    }
    const QString typed = text.left(end).toLower();
    const bool hasArguments = end < text.size();
    for (const QuietWord &known : g_quietWords) {
        const QLatin1String word{known.word};
        if (typed.size() >= known.shortest && word.startsWith(typed)) {
            return known.arguments || !hasArguments;
        }
    }
    return false;
}

QuietTrafficEnum quietTrafficOf(const bool inRoom,
                                const bool combatProse,
                                const QString &line,
                                const std::vector<XmlElement> &closed,
                                const std::vector<XmlTagEnum> &open)
{
    if (inRoom) {
        return QuietTrafficEnum::OTHER;
    }
    // The fight reader goes by the words alone; without an element around them it may have
    // taken a line of the reply.
    bool unsure = combatProse;

    // An element still open after the line holds the line, or its end: a tell that wraps.
    for (const XmlTagEnum tag : open) {
        switch (meaningOf(tag)) {
        case TagMeaningEnum::OTHER:
            return QuietTrafficEnum::OTHER;
        case TagMeaningEnum::UNKNOWN:
            unsure = true;
            break;
        case TagMeaningEnum::NEUTRAL:
            break;
        }
    }

    for (const XmlElement &xml : closed) {
        switch (meaningOf(xml.tag)) {
        case TagMeaningEnum::OTHER: {
            if (toXmlCategory(xml.tag) == XmlCategoryEnum::ROOM) {
                return QuietTrafficEnum::OTHER;
            }
            // The whole line, or the line is the last of an element that began before it.
            // An element inside a longer line marks only its own words.
            const QString text = xml.text.trimmed();
            if (!text.isEmpty() && !line.isEmpty() && text.contains(line)) {
                return QuietTrafficEnum::OTHER;
            }
            unsure = true;
            break;
        }
        case TagMeaningEnum::UNKNOWN:
            unsure = true;
            break;
        case TagMeaningEnum::NEUTRAL:
            break;
        }
    }
    return unsure ? QuietTrafficEnum::UNSURE : QuietTrafficEnum::REPLY;
}

void QuietCapture::receiveCommand(const QuietCommandEnum what)
{
    switch (what) {
    case QuietCommandEnum::BEGIN:
        m_open = true;
        m_replied = false;
        m_shown = false;
        m_foreign = false;
        break;
    case QuietCommandEnum::FOREIGN:
        if (m_open) {
            m_foreign = true;
        }
        break;
    case QuietCommandEnum::END:
        reset();
        break;
    }
}

void QuietCapture::reset()
{
    m_open = false;
    m_replied = false;
    m_shown = false;
    m_foreign = false;
}

QuietCapture::Verdict QuietCapture::receiveLine(const QString &plain,
                                                const QuietTrafficEnum traffic)
{
    Verdict verdict;
    if (!m_open) {
        return verdict;
    }
    const bool blank = plain.trimmed().isEmpty();
    switch (traffic) {
    case QuietTrafficEnum::OTHER:
        m_shown = true;
        break;
    case QuietTrafficEnum::UNSURE:
        if (blank) {
            // Nothing to be unsure about: the spacing of a reply, or of what comes around it.
            verdict.hidden = true;
            verdict.captured = true;
            break;
        }
        verdict.captured = true;
        m_replied = true;
        m_shown = true;
        break;
    case QuietTrafficEnum::REPLY:
        verdict.hidden = true;
        verdict.captured = true;
        if (!blank) {
            m_replied = true;
        }
        break;
    }
    return verdict;
}

QuietCapture::Verdict QuietCapture::receivePager()
{
    Verdict verdict;
    if (!m_open) {
        return verdict;
    }
    if (m_replied) {
        verdict.hidden = true;
    } else {
        // No line of the reply yet: it cuts a reply of somebody else's, who must see it.
        m_shown = true;
    }
    return verdict;
}

QuietCapture::Verdict QuietCapture::receivePrompt()
{
    Verdict verdict;
    if (!m_open) {
        return verdict;
    }
    if (m_replied || m_foreign) {
        verdict.ended = true;
        verdict.hidden = m_replied && !m_shown;
        reset();
        return verdict;
    }
    // The prompt of something else; shown, and what comes next starts after a prompt again.
    m_shown = false;
    return verdict;
}
