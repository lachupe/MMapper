// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "LineTags.h"

#include <array>
#include <utility>

#include <QRegularExpression>

namespace {

constexpr const std::array<std::string_view, NUM_LINE_TAGS> g_tagNames{{
#define X_NAME(UPPER_CASE, name) name,
    XFOREACH_LINE_TAG(X_NAME)
#undef X_NAME
}};

void addName(LineFacts &facts, const QString &raw)
{
    QString name = raw.trimmed();
    // "*an Orc*" is a name as the player sees it; the stars mark it as an enemy.
    if (name.isEmpty() || name.compare(QStringLiteral("you"), Qt::CaseInsensitive) == 0) {
        return;
    }
    if (!facts.names.contains(name)) {
        facts.names.append(name);
    }
}

/// The first entity named inside an element: who spoke, who struck.
NODISCARD QString firstEntity(const XmlElement &element)
{
    for (const XmlElement &child : element.children) {
        if (toXmlCategory(child.tag) == XmlCategoryEnum::ENTITY) {
            return child.text.trimmed();
        }
        if (QString inner = firstEntity(child); !inner.isEmpty()) {
            return inner;
        }
    }
    return QString{};
}

/// The speaker of a line of communication MUME did not tag inside: the words before the verb.
NODISCARD QString speakerOf(const QString &text)
{
    static const QRegularExpression speaker{QStringLiteral(
        R"(^(.+?) (?:tells|narrates|says|yells|shouts|sings|prays|asks|exclaims|whispers)\b)")};
    const QString trimmed = text.trimmed();
    if (const auto m = speaker.match(trimmed); m.hasMatch()) {
        return m.captured(1);
    }
    return QString{};
}

} // namespace

std::string_view lineTagName(const LineTagEnum tag)
{
    return g_tagNames.at(static_cast<size_t>(tag));
}

std::optional<LineTagEnum> lineTagFromName(const QString &name)
{
    const QByteArray utf8 = name.toUtf8();
    const std::string_view wanted{utf8.constData(), static_cast<size_t>(utf8.size())};
    for (size_t i = 0; i < NUM_LINE_TAGS; ++i) {
        if (g_tagNames[i] == wanted) {
            return static_cast<LineTagEnum>(i);
        }
    }
    return std::nullopt;
}

QStringList LineTagSet::names() const
{
    QStringList out;
    for (size_t i = 0; i < NUM_LINE_TAGS; ++i) {
        if ((bits & (uint64_t{1} << i)) != 0) {
            out.append(QString::fromLatin1(g_tagNames[i].data(),
                                           static_cast<qsizetype>(g_tagNames[i].size())));
        }
    }
    return out;
}

std::optional<LineTagEnum> commTagOf(const XmlElement &element)
{
    switch (element.tag) {
    case XmlTagEnum::TELL: {
        const QString text = element.text.trimmed();
        if (text.contains(QStringLiteral(" tells the group '"))
            || text.startsWith(QStringLiteral("You tell the group"))) {
            return LineTagEnum::COMM_GTELL;
        }
        return LineTagEnum::COMM_TELL;
    }
    case XmlTagEnum::NARRATE:
        return LineTagEnum::COMM_NARRATE;
    case XmlTagEnum::YELL:
        return LineTagEnum::COMM_YELL;
    case XmlTagEnum::SHOUT:
        return LineTagEnum::COMM_SHOUT;
    case XmlTagEnum::SAY:
        return LineTagEnum::COMM_SAY;
    case XmlTagEnum::SONG:
        return LineTagEnum::COMM_SONG;
    case XmlTagEnum::PRAY:
        return LineTagEnum::COMM_PRAY;
    case XmlTagEnum::EMOTE:
        return LineTagEnum::COMM_EMOTE;
    case XmlTagEnum::SOCIAL:
        return LineTagEnum::COMM_SOCIAL;
    default:
        break;
    }
    return std::nullopt;
}

void tagCombat(LineFacts &facts, const CombatEvent &event)
{
    facts.tags.insert(LineTagEnum::COMBAT);
    switch (event.kind) {
    case CombatKindEnum::BLOW:
        facts.tags.insert(LineTagEnum::COMBAT_BLOW);
        switch (event.outcome) {
        case BlowOutcomeEnum::HIT:
            facts.tags.insert(LineTagEnum::COMBAT_HIT);
            break;
        case BlowOutcomeEnum::PARRY:
            facts.tags.insert(LineTagEnum::COMBAT_PARRY);
            break;
        case BlowOutcomeEnum::DODGE:
            facts.tags.insert(LineTagEnum::COMBAT_DODGE);
            break;
        case BlowOutcomeEnum::MISS:
            facts.tags.insert(LineTagEnum::COMBAT_MISS);
            break;
        }
        break;
    case CombatKindEnum::FLEE:
        facts.tags.insert(LineTagEnum::COMBAT_FLEE);
        break;
    case CombatKindEnum::REFUSED:
        facts.tags.insert(LineTagEnum::COMBAT_REFUSED);
        break;
    case CombatKindEnum::BASH:
        facts.tags.insert(LineTagEnum::COMBAT_BASH);
        break;
    case CombatKindEnum::BACKSTAB:
        facts.tags.insert(LineTagEnum::COMBAT_BACKSTAB);
        break;
    case CombatKindEnum::CONDITION:
        facts.tags.insert(LineTagEnum::COMBAT_CONDITION);
        break;
    case CombatKindEnum::DEATH:
        facts.tags.insert(LineTagEnum::COMBAT_DEATH);
        break;
    case CombatKindEnum::CAST:
        facts.tags.insert(LineTagEnum::COMBAT_CAST);
        break;
    case CombatKindEnum::SELF:
        facts.tags.insert(LineTagEnum::COMBAT_SELF);
        break;
    case CombatKindEnum::AFFECT:
        facts.tags.insert(LineTagEnum::COMBAT_AFFECT);
        break;
    case CombatKindEnum::RESCUE:
        facts.tags.insert(LineTagEnum::COMBAT_RESCUE);
        break;
    case CombatKindEnum::ASSIST:
        facts.tags.insert(LineTagEnum::COMBAT_ASSIST);
        break;
    }
    const bool you = event.actor == QStringLiteral("you") || event.target == QStringLiteral("you")
                     || event.kind == CombatKindEnum::SELF;
    if (you) {
        facts.about = LineAboutEnum::YOU;
    }
    addName(facts, event.actor);
    addName(facts, event.target);
}

void tagElements(LineFacts &facts, const std::vector<XmlElement> &elements)
{
    for (const XmlElement &element : elements) {
        switch (toXmlCategory(element.tag)) {
        case XmlCategoryEnum::COMMUNICATION:
            if (const auto comm = commTagOf(element)) {
                facts.tags.insert(LineTagEnum::COMM);
                facts.tags.insert(*comm);
                const QString text = element.text.trimmed();
                if (*comm == LineTagEnum::COMM_GTELL) {
                    facts.about = LineAboutEnum::GROUP;
                } else if (*comm == LineTagEnum::COMM_TELL
                           && !text.startsWith(QStringLiteral("You "))) {
                    facts.about = LineAboutEnum::YOU;
                }
                QString speaker = firstEntity(element);
                if (speaker.isEmpty()) {
                    speaker = speakerOf(text);
                }
                addName(facts, speaker);
            }
            break;
        case XmlCategoryEnum::MOVEMENT:
            facts.tags.insert(LineTagEnum::MOVE);
            addName(facts, firstEntity(element));
            break;
        case XmlCategoryEnum::MAGIC:
            facts.tags.insert(LineTagEnum::MAGIC);
            break;
        case XmlCategoryEnum::PROGRESS:
            facts.tags.insert(LineTagEnum::ACHIEVEMENT);
            break;
        case XmlCategoryEnum::COMBAT:
            // The fight reader's tags are finer; an element it could not read still says this
            // much.
            facts.tags.insert(LineTagEnum::COMBAT);
            break;
        case XmlCategoryEnum::ROOM:
        case XmlCategoryEnum::STATUS:
        case XmlCategoryEnum::ENTITY:
        case XmlCategoryEnum::FORMATTING:
        case XmlCategoryEnum::UNKNOWN:
            break;
        }
    }
}

std::vector<LineFacts> LineTagger::finish(LineFacts facts, const bool commOpen)
{
    std::vector<LineFacts> out;
    const bool interrupts = facts.tags.contains(LineTagEnum::PROMPT)
                            || facts.tags.contains(LineTagEnum::PAGER);
    if (m_pending.has_value() && interrupts) {
        // A message whose element never closed before the prompt: what came of it goes out.
        out.push_back(std::exchange(m_pending, std::nullopt).value());
        m_pendingLines = 0;
    }
    if (m_pending.has_value()) {
        LineFacts &held = *m_pending;
        held.text += QLatin1Char('\n') + facts.text;
        if (!facts.plain.isEmpty()) {
            held.plain = held.plain.isEmpty() ? facts.plain
                                              : held.plain + QLatin1Char(' ') + facts.plain;
        }
        held.tags.bits |= facts.tags.bits;
        for (const QString &name : facts.names) {
            if (!held.names.contains(name)) {
                held.names.append(name);
            }
        }
        if (held.about == LineAboutEnum::UNKNOWN) {
            held.about = facts.about;
        }
        if (commOpen && ++m_pendingLines < MAX_JOINED_LINES) {
            return out;
        }
        facts = std::exchange(m_pending, std::nullopt).value();
        m_pendingLines = 0;
    } else if (commOpen && !interrupts) {
        m_pending = std::move(facts);
        m_pendingLines = 1;
        return out;
    }
    if (facts.tags.empty()) {
        facts.tags.insert(LineTagEnum::TEXT);
    }
    out.push_back(std::move(facts));
    return out;
}

void LineTagger::reset()
{
    m_pending.reset();
    m_pendingLines = 0;
}
