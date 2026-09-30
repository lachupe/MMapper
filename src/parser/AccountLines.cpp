// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "AccountLines.h"

#include <utility>

#include <QRegularExpression>

namespace {

/// Bounds a block that never closes: the menu is about 20 lines, a list one per character.
constexpr int MAX_BLOCK_LINES = 200;
/// Lines between `Characters in account "..."` and the header before the list is given up.
constexpr int MAX_LINES_BEFORE_HEADER = 3;

// The columns of a row, from the start of "Rce" (see AccountLines.h). Each is followed by
// one space.
constexpr qsizetype RACE_WIDTH = 3;
constexpr qsizetype LVL_OFFSET = 4;
constexpr qsizetype LVL_WIDTH = 3;
constexpr qsizetype LOGON_OFFSET = 8;
constexpr qsizetype LOGON_WIDTH = 7;
constexpr qsizetype AREA_OFFSET = 16;
constexpr qsizetype AREA_WIDTH = 7;
constexpr qsizetype RENT_OFFSET = 24;
constexpr qsizetype RENT_WIDTH = 7;
constexpr qsizetype DELETE_OFFSET = 32;
constexpr qsizetype DELETE_WIDTH = 7;

const QLatin1String PROMPT_WORD{"Account>"};

NODISCARD QString column(const QString &line, const qsizetype start, const qsizetype width)
{
    return line.mid(start, width).trimmed();
}

NODISCARD bool spaceAt(const QString &line, const qsizetype index)
{
    return index < 0 || index >= line.size() || line.at(index) == QLatin1Char(' ');
}

NODISCARD const QRegularExpression &listHeaderPattern()
{
    static const QRegularExpression re{QStringLiteral(R"(^Name +Rce +Lvl\b)")};
    return re;
}

NODISCARD const QRegularExpression &listTitlePattern()
{
    static const QRegularExpression re{QStringLiteral(R"re(^Characters in account "(.*)"\s*$)re")};
    return re;
}

/// The sort orders in the list command's help: "... can be one of side, race, level, ...".
NODISCARD QStringList sortsIn(const QString &help)
{
    static const QRegularExpression re{QStringLiteral(R"(can be one of:?\s+(.+)$)")};
    const QRegularExpressionMatch m = re.match(help);
    QStringList result;
    if (!m.hasMatch()) {
        return result;
    }
    QString words = m.captured(1).trimmed();
    if (words.endsWith(QLatin1Char('.'))) {
        words.chop(1);
    }
    for (const QString &word : words.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString sort = word.trimmed();
        if (!sort.isEmpty() && !sort.contains(QLatin1Char(' '))) {
            result.append(sort);
        }
    }
    return result;
}

} // namespace

const char *accountReplyKindName(const AccountReplyKindEnum kind)
{
    switch (kind) {
    case AccountReplyKindEnum::WAIT:
        return "wait";
    case AccountReplyKindEnum::UNKNOWN_COMMAND:
        return "unknown";
    }
    return "unknown";
}

std::optional<AccountChar> parseAccountCharRow(const QString &line, const qsizetype raceColumn)
{
    const qsizetype c = raceColumn;
    // Up to the deletion column at least; the host after it may be missing.
    if (c < 2 || line.size() < c + DELETE_OFFSET + 4) {
        return std::nullopt;
    }
    for (const qsizetype gap : {c - 1,
                                c + RACE_WIDTH,
                                c + LVL_OFFSET + LVL_WIDTH,
                                c + LOGON_OFFSET + LOGON_WIDTH,
                                c + AREA_OFFSET + AREA_WIDTH,
                                c + RENT_OFFSET + RENT_WIDTH,
                                c + DELETE_OFFSET + DELETE_WIDTH}) {
        if (!spaceAt(line, gap)) {
            return std::nullopt;
        }
    }

    AccountChar row;
    row.name = line.left(c).trimmed();
    row.race = column(line, c, RACE_WIDTH);
    row.lvl = column(line, c + LVL_OFFSET, LVL_WIDTH);
    row.logon = column(line, c + LOGON_OFFSET, LOGON_WIDTH);
    row.area = column(line, c + AREA_OFFSET, AREA_WIDTH);
    row.rent = column(line, c + RENT_OFFSET, RENT_WIDTH);
    row.deletion = column(line, c + DELETE_OFFSET, DELETE_WIDTH);

    static const QRegularExpression nameRe{QStringLiteral(R"(^\w+$)")};
    static const QRegularExpression raceRe{QStringLiteral(R"(^[a-z]*$)")};
    if (!nameRe.match(row.name).hasMatch() || !raceRe.match(row.race).hasMatch()
        || row.lvl.isEmpty() || row.logon.isEmpty() || row.rent.isEmpty()
        || row.deletion.isEmpty()) {
        return std::nullopt;
    }

    static const QRegularExpression lvlRe{QStringLiteral(R"(^([A-Z])?(\d+)$)")};
    if (const QRegularExpressionMatch m = lvlRe.match(row.lvl); m.hasMatch()) {
        row.cls = m.captured(1);
        row.level = m.captured(2).toLongLong();
    }
    if (row.logon == QStringLiteral("playing")) {
        row.playing = true;
        row.logon.clear();
    }
    return row;
}

std::optional<AccountReply> parseAccountReplyLine(const QString &line)
{
    const QString text = line.trimmed();
    static const QRegularExpression waitRe{
        QStringLiteral(R"(^You must wait\s+(\d+)\s+(sec|secs|min|mins|hr|hrs|hour|hours)\s+)"
                       R"(before you can log in this character!$)")};
    if (const QRegularExpressionMatch m = waitRe.match(text); m.hasMatch()) {
        AccountReply reply;
        reply.kind = AccountReplyKindEnum::WAIT;
        const int64_t count = m.captured(1).toLongLong();
        const QString unit = m.captured(2);
        const int64_t scale = unit.startsWith(QLatin1String("sec"))   ? 1
                              : unit.startsWith(QLatin1String("min")) ? 60
                                                                      : 3600;
        reply.seconds = count * scale;
        reply.text = text;
        return reply;
    }
    static const QRegularExpression unknownRe{
        QStringLiteral(R"(^Unknown account command '(.*)'$)")};
    if (const QRegularExpressionMatch m = unknownRe.match(text); m.hasMatch()) {
        AccountReply reply;
        reply.kind = AccountReplyKindEnum::UNKNOWN_COMMAND;
        reply.command = m.captured(1);
        reply.text = text;
        return reply;
    }
    return std::nullopt;
}

void AccountReplies::append(AccountReplies &&other)
{
    for (auto &menu : other.menus) {
        menus.push_back(std::move(menu));
    }
    for (auto &list : other.lists) {
        lists.push_back(std::move(list));
    }
    for (auto &reply : other.replies) {
        replies.push_back(std::move(reply));
    }
}

AccountReplies AccountLinesTracker::receiveLine(const QString &line)
{
    AccountReplies out;

    // The prompt, as a line of its own or glued to the line after it ("Account> pl woland",
    // "Account> You must wait ..."), closes the menu and the list.
    if (const QString lead = line.trimmed(); lead.startsWith(PROMPT_WORD)) {
        out = closeAll();
        const QString rest = lead.mid(PROMPT_WORD.size()).trimmed();
        if (!rest.isEmpty()) {
            out.append(receiveLine(rest));
        }
        return out;
    }

    if (const auto reply = parseAccountReplyLine(line)) {
        out.replies.push_back(*reply);
        return out;
    }

    const QString text = line.trimmed();
    if (text == QStringLiteral("Account menu")) {
        out = closeAll();
        m_menu = AccountMenu{};
        m_lines = 0;
        return out;
    }
    if (const QRegularExpressionMatch m = listTitlePattern().match(text); m.hasMatch()) {
        out = closeAll();
        m_list = AccountChars{};
        m_list->account = m.captured(1);
        m_raceColumn.reset();
        m_lines = 0;
        return out;
    }
    if (listHeaderPattern().match(line).hasMatch()) {
        if (!m_list.has_value() || m_raceColumn.has_value()) {
            out = closeAll();
            m_list = AccountChars{};
        }
        m_raceColumn = line.indexOf(QStringLiteral("Rce"));
        m_lines = 0;
        return out;
    }

    if (!m_list.has_value() && !m_menu.has_value()) {
        return out;
    }
    if (++m_lines > MAX_BLOCK_LINES) {
        reset();
        return out;
    }

    if (m_list.has_value()) {
        if (!m_raceColumn.has_value()) {
            if (m_lines > MAX_LINES_BEFORE_HEADER) {
                m_list.reset();
            }
            return out;
        }
        if (text.isEmpty()) {
            AccountReplies done;
            done.lists.push_back(std::move(*m_list));
            m_list.reset();
            m_raceColumn.reset();
            out.append(std::move(done));
            return out;
        }
        // A line that is not a row is the end of a host too long for its line.
        if (auto row = parseAccountCharRow(line, *m_raceColumn)) {
            m_list->chars.push_back(std::move(*row));
        }
        return out;
    }

    readMenuLine(line);
    return out;
}

void AccountLinesTracker::readMenuLine(const QString &line)
{
    const QString text = line.trimmed();
    if (text.isEmpty()) {
        return;
    }
    std::vector<AccountMenuCommand> &commands = m_menu->commands;
    // A command: two spaces, then its usage, then "- help" after a run of spaces.
    if (line.startsWith(QLatin1String("  ")) && line.size() > 2 && line.at(2).isLetter()) {
        static const QRegularExpression dashRe{QStringLiteral(R"(\s+- )")};
        const QRegularExpressionMatch m = dashRe.match(text);
        AccountMenuCommand command;
        command.usage = m.hasMatch() ? text.left(m.capturedStart()) : text;
        command.help = m.hasMatch() ? text.mid(m.capturedEnd()).trimmed() : QString{};
        command.name = command.usage.section(QLatin1Char(' '), 0, 0).toLower();
        commands.push_back(std::move(command));
        return;
    }
    // A continuation, indented further: the help of a command whose usage filled the line
    // ("- Move a character around in the custom list"), or more of the help before it.
    if (line.startsWith(QLatin1String("   ")) && !commands.empty()) {
        AccountMenuCommand &last = commands.back();
        if (last.help.isEmpty() && text.startsWith(QLatin1String("- "))) {
            last.help = text.mid(2).trimmed();
        } else {
            last.help += (last.help.isEmpty() ? QString{} : QStringLiteral(" ")) + text;
        }
    }
    // Anything else (a line a packet split) is not the menu's.
}

AccountReplies AccountLinesTracker::receivePrompt()
{
    return closeAll();
}

AccountReplies AccountLinesTracker::closeAll()
{
    AccountReplies out;
    if (m_menu.has_value() && !m_menu->commands.empty()) {
        for (const AccountMenuCommand &command : m_menu->commands) {
            if (command.name == QStringLiteral("list")) {
                m_menu->sorts = sortsIn(command.help);
            }
        }
        out.menus.push_back(std::move(*m_menu));
    }
    if (m_list.has_value() && m_raceColumn.has_value()) {
        out.lists.push_back(std::move(*m_list));
    }
    reset();
    return out;
}

void AccountLinesTracker::reset()
{
    m_menu.reset();
    m_list.reset();
    m_raceColumn.reset();
    m_lines = 0;
}
