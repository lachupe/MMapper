// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharConsider.h"

#include "LineText.h"

#include <utility>

#include <QRegularExpression>

namespace {

struct NODISCARD ConsiderRow final
{
    const char *line;
    const char *verdict;
};

// Easiest first; the verdict's first place is the grade. "Easy.", "The perfect match!", "A perfect
// match!" and "You would need some luck!" are in the user's logs, "You would need a lot of luck,
// great equipment, powerful spells, and supportive allies!" in the user's live play
// (2026-10-17, mume3d FIX.235); the rest is the classic Diku ladder MUME's help describes
// ("killing a tiny spider would be easy indeed") and has not been seen live.
const ConsiderRow g_rows[] = {
    {"Now where did that chicken go?", "pushover"},
    {"You could do it with a needle!", "trivial"},
    {"Easy.", "easy"},
    {"Fairly easy.", "fairly-easy"},
    {"The perfect match!", "even"},
    {"A perfect match!", "even"},
    {"You would need some luck!", "some-luck"},
    {"You would need a lot of luck!", "lot-of-luck"},
    {"You would need a lot of luck and great equipment!", "great-equipment"},
    {"You would need a lot of luck, great equipment, powerful spells, and supportive allies!",
     "allies"},
    {"Do you feel lucky, punk?", "deadly"},
    {"Are you mad!?", "mad"},
    {"You ARE mad!", "hopeless"},
};

// "<name> is at a slightly higher level than you.", "... at the same level as you."; the logs show
// "slightly higher", "slightly lower" and (live) "significantly higher".
const QRegularExpression g_level{QStringLiteral(
    R"(^(\S.*?) is at (?:an? |the )?([a-z][a-z ]*?) level (?:than|as) you\.(?:\s+(.*))?$)")};
const QRegularExpression g_sentenceEnd{QStringLiteral(R"([.!?]$)")};

// The most lines a verdict is gathered over before the reply is taken for something else.
constexpr int MAX_CONSIDER_LINES = 6;

QString cleanLine(const QString &raw)
{
    return line_text::trimmedWithoutTwiddlers(raw);
}

std::optional<CharConsider> matchAdvice(const QString &line)
{
    for (const ConsiderRow &r : g_rows) {
        if (line == QString::fromLatin1(r.line)) {
            CharConsider consider;
            consider.verdict = QString::fromLatin1(r.verdict);
            consider.grade = int(considerVerdicts().indexOf(consider.verdict));
            consider.text = line;
            return consider;
        }
    }
    return std::nullopt;
}

QString levelCode(const QString &comparison)
{
    QString code = comparison.simplified();
    code.replace(QLatin1Char(' '), QLatin1Char('-'));
    return code;
}

const QRegularExpression g_command{QStringLiteral(
                                       R"(^\s*con(?:s|si|sid|side|sider)?\s+(\S.*?)\s*$)"),
                                   QRegularExpression::CaseInsensitiveOption};

} // namespace

QString considerTarget(const QString &sentLine)
{
    const auto m = g_command.match(sentLine);
    return m.hasMatch() ? m.captured(1) : QString{};
}

std::optional<CharConsider> parseConsiderLine(const QString &raw)
{
    return matchAdvice(cleanLine(raw));
}

void ConsiderReader::reset()
{
    m_target.clear();
    m_lines.clear();
    m_received = 0;
    m_waiting = false;
}

void ConsiderReader::receiveCommand(const SentCommand &command)
{
    const QString target = considerTarget(command.line);
    if (target.isEmpty()) {
        return;
    }
    m_target = target;
    m_lines.clear();
    m_received = 0;
    m_waiting = true;
}

std::optional<CharConsider> ConsiderReader::receiveLine(const QString &raw)
{
    const QString line = cleanLine(raw);
    if (line.isEmpty()) {
        return std::nullopt;
    }
    if (!m_waiting) {
        // Not after the player's consider (or MMapper missed the command): a lone advice line.
        return matchAdvice(line);
    }
    ++m_received;
    if (m_lines.isEmpty() && g_sentenceEnd.match(line).hasMatch() && !matchAdvice(line)
        && !g_level.match(line).hasMatch()) {
        // A whole sentence before the reply that is none of it: someone arriving.
        return std::nullopt;
    }
    m_lines.append(line);
    const QString text = m_lines.join(QLatin1Char(' '));
    const auto level = g_level.match(text);
    if (level.hasMatch()) {
        const QString advice = level.captured(3);
        if (advice.isEmpty() || !g_sentenceEnd.match(advice).hasMatch()) {
            return std::nullopt;
        }
        CharConsider consider;
        if (auto known = matchAdvice(advice)) {
            consider = *known;
        } else {
            // An advice sentence not on the ladder yet: reported with its words, ungraded.
            consider.verdict = QStringLiteral("unknown");
            consider.grade = -1;
        }
        consider.text = text;
        consider.level = levelCode(level.captured(2));
        consider.target = std::exchange(m_target, QString{});
        m_lines.clear();
        m_waiting = false;
        return consider;
    }
    if (auto known = matchAdvice(text)) {
        known->target = std::exchange(m_target, QString{});
        m_lines.clear();
        m_waiting = false;
        return known;
    }
    if (m_lines.size() >= MAX_CONSIDER_LINES) {
        // An object's description, or something else: no verdict.
        m_lines.clear();
        m_target.clear();
        m_waiting = false;
    }
    return std::nullopt;
}

std::optional<CharConsider> ConsiderReader::receivePrompt()
{
    if (m_waiting && m_received == 0) {
        return std::nullopt;
    }
    std::optional<CharConsider> result;
    const QString text = m_lines.join(QLatin1Char(' '));
    const auto level = g_level.match(text);
    if (m_waiting && level.hasMatch()) {
        CharConsider consider;
        consider.verdict = QStringLiteral("unknown");
        consider.grade = -1;
        consider.text = level.capturedLength(3) > 0 ? text.left(level.capturedStart(3)).trimmed()
                                                    : text;
        consider.level = levelCode(level.captured(2));
        consider.target = m_target;
        result = consider;
    }
    m_lines.clear();
    m_target.clear();
    m_waiting = false;
    return result;
}

QStringList considerVerdicts()
{
    QStringList codes;
    for (const ConsiderRow &r : g_rows) {
        const QString code = QString::fromLatin1(r.verdict);
        if (!codes.contains(code)) {
            codes.append(code);
        }
    }
    return codes;
}
