#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

/// MUME's account menu, read off the text MUME prints before a character is in the game.
///
/// GMCP says nothing until a character is played: MUME's account menu, its `list` of the
/// account's characters and its refusals are only text. They are read here, and published as
/// MMapper.Account.Menu, MMapper.Account.Chars and MMapper.Account.Reply. Nothing is taken
/// out of the terminal.
///
/// The shapes come from the powwow logs (2005-2006, e.g. powwow/logs/moria.gjurza.mov):
///
/// - the menu, "Account menu", a blank line, then one "  Play <name>   - Log the character
///   <name> into MUME" per command (a command whose usage is too long for the column has its
///   "- help" on the next line; a help that runs on continues, indented, on the lines after),
///   up to the `Account> ` prompt. Today (seen 2026-10-03) it opens on "Available commands:"
///   instead, its commands are lowercase ("  play <name>   - Play the character <name>"), and
///   the sorts follow them on a line of their own: "Where <sort> can be one of: side, race,
///   level, alphabetic, and custom.";
/// - `list [<sort>]`'s reply, `Characters in account "dmitry"`, the header "Name  Rce Lvl
///   Logon Area     Rent    Delete Host", then one row per character in fixed columns, up to
///   the blank line:
///
///       Gjurza       orc W40  8 days OrcCave  9 mths   never e177099165.adsl.alicedsl.de
///       Woland         bn  89 playing         unknown 12 mths c21725...
///       Porien              M 20 hrs  Valinor    free   never p548EF...
///
///   The name column was 14 wide in 2005 and 13 in 2006; every other column keeps its place
///   relative to "Rce", so the header's "Rce" says where the row's columns are. A host too
///   long for the line wraps onto a line of its own, which is not a row.
///
///   Since then (seen 2026-10-02) the header is "Name  Rce Sub Lvl   Logon Area     Rent
///   Delete Host": a subrace column, three wide, stands between the race and the level, and
///   every column after it is four further right:
///
///       Agronom      man dún  29  2 yrs  Valinor    free retired host...
///       Lator        h-e      43  2 yrs  Valinor    free retired host...
///       Kolbasjenish  bn      57  1 yrs  DolGldr    free retired host...
///
///   The header says which it is, by where "Lvl" stands after "Rce".
/// - MUME's pager, where the list is longer than the screen: a blank line, then
///   "*** Return: continue, b: back, r: redisplay, q: quit (84%) ***" and nothing more until
///   the player answers; the rows after it come with no title and no header, up to the blank
///   line and the `Account> ` prompt (or another pager line). The list is published as it
///   stands with `more` set while the pager waits, and whole once its rest has come. A page
///   shown again (back, redisplay) repeats rows: a name already in the list replaces its row.
///   Nothing is ever sent to answer the pager.
/// - "You must wait  6 mins before you can log in this character!" after `play`, and
///   "Unknown account command 'look'".
///
/// The host a character last logged in from is never kept.

/// One command of the account menu.
struct NODISCARD AccountMenuCommand final
{
    /// The command word, lowercase: "play", "create", "list", "bye".
    QString name;
    /// The usage as the menu gives it: "Play <name>", "List [<sort>]",
    /// "Move <name> [up|down] [-]n".
    QString usage;
    /// What it does, its continuation lines joined with single spaces.
    QString help;
};

/// The account menu, as MUME printed it last.
struct NODISCARD AccountMenu final
{
    std::vector<AccountMenuCommand> commands;
    /// The sort orders `list` takes, where the menu names them ("side, race, level, ...").
    QStringList sorts;
};

/// One row of `list`'s reply. The fields are the columns, less the host.
struct NODISCARD AccountChar final
{
    QString name;
    /// MUME's three-letter abbreviation, as printed: "orc", "dwa", "elf", "bn", "man", "zau",
    /// "tro", "hob", "h-e". Empty when the column is (Porien, a Maia, in the logs).
    QString race;
    /// The subrace column as printed ("dún", "roh", "eri", "fir", "nol", "sil", "tar", "cav");
    /// empty when the column is, or when the list has none (the logs of 2005-2006).
    QString sub;
    /// The Lvl column as printed: "W40", "100", "M".
    QString lvl;
    /// The dominant class's letter ("W", "M", "C", "T") when `lvl` begins with one.
    QString cls;
    /// The level, when `lvl` has one.
    std::optional<int64_t> level;
    /// How long ago the character last logged on ("8 days", "37 hrs", "86 mins"); empty while
    /// it is playing.
    QString logon;
    bool playing = false;
    /// The rent's area, abbreviated to 7 letters ("OrcCave", "Rivendl", "BM"); empty while
    /// playing.
    QString area;
    /// How long the rent is paid for: "free", "forever", "unknown", "9 mths", "11 yrs".
    QString rent;
    /// When an idle character is deleted: "never", "retired", "11 mths", "89 days".
    QString deletion;
};

/// `list [<sort>]`'s reply, in MUME's order (which is the sort asked for).
struct NODISCARD AccountChars final
{
    /// The account's name, from `Characters in account "dmitry"`; empty if that line was missed.
    QString account;
    std::vector<AccountChar> chars;
    /// MUME's pager waits after these rows: the list goes on once the player answers it.
    bool more = false;
    /// How far the pager says the list has come ("(84%)"), while `more` is set.
    std::optional<int64_t> percent;
};

enum class NODISCARD AccountReplyKindEnum : uint8_t {
    /// "You must wait  6 mins before you can log in this character!"
    WAIT,
    /// "Unknown account command 'look'"
    UNKNOWN_COMMAND,
};

/// A one-line answer of the account menu.
struct NODISCARD AccountReply final
{
    AccountReplyKindEnum kind = AccountReplyKindEnum::WAIT;
    /// WAIT: how long, in seconds.
    std::optional<int64_t> seconds;
    /// UNKNOWN_COMMAND: the command MUME did not know.
    QString command;
    /// The line as MUME sent it.
    QString text;
};

/// The protocol's name for `kind`: "wait" or "unknown".
NODISCARD const char *accountReplyKindName(AccountReplyKindEnum kind);

/// A row of `list`'s reply, whose "Rce" column starts at `raceColumn` (the header's), or
/// nullopt when `line` is not one. `withSub`: the list has the "Sub" column after "Rce".
NODISCARD std::optional<AccountChar> parseAccountCharRow(const QString &line,
                                                          qsizetype raceColumn,
                                                          bool withSub = false);

/// How far MUME's pager line says the listing has come, in percent ("*** Return: continue,
/// b: back, r: redisplay, q: quit (84%) ***"), or nullopt when `line` is not one.
NODISCARD std::optional<int64_t> parseAccountPagerLine(const QString &line);

/// A one-line answer of the account menu, or nullopt.
NODISCARD std::optional<AccountReply> parseAccountReplyLine(const QString &line);

/// What one line or prompt completed.
struct NODISCARD AccountReplies final
{
    std::vector<AccountMenu> menus;
    std::vector<AccountChars> lists;
    std::vector<AccountReply> replies;

    NODISCARD bool empty() const { return menus.empty() && lists.empty() && replies.empty(); }
    void append(AccountReplies &&other);
};

/// Gathers the menu and `list`'s reply line by line; the one-line answers come out at once.
///
/// The menu opens on "Account menu" (or "Available commands:") and closes at the `Account> `
/// prompt, as a line (MUME sends it without a GO-AHEAD, so it may come glued to what follows)
/// or as a prompt. The list
/// opens on `Characters in account "..."` or on its header, and closes at the blank line after
/// its rows, or the prompt. Each is published only when complete: a menu with commands, a
/// list whose header came (an account with no characters gives an empty list). A list MUME's
/// pager interrupts is published again with `more` set, and once more, whole, when the rows
/// after the pager have come; only the `Account> ` prompt or a new session ends that.
class NODISCARD AccountLinesTracker final
{
private:
    std::optional<AccountMenu> m_menu;
    std::optional<AccountChars> m_list;
    /// Where the header's "Rce" starts; unset until the header came.
    std::optional<qsizetype> m_raceColumn;
    /// The header had "Sub" between "Rce" and "Lvl".
    bool m_withSub = false;
    int m_lines = 0;
    /// The list as last published at its blank line or at the pager, with its columns: what a
    /// pager line right after the blank line, or the rows after the pager, go on from.
    struct NODISCARD Page final
    {
        AccountChars list;
        qsizetype raceColumn = 0;
        bool withSub = false;
        /// The pager's line came: the rows after it belong to `list`.
        bool waiting = false;
        /// Lines since: bounds how long a closed list can still be taken up.
        int lines = 0;
    };
    std::optional<Page> m_page;

public:
    /// Reads one line of MUME's output, colour removed, not trimmed (the rows are columns).
    NODISCARD AccountReplies receiveLine(const QString &line);
    /// A prompt closes whatever is open.
    NODISCARD AccountReplies receivePrompt();
    /// For a new session.
    void reset();
    /// Whether the menu or a list of characters is being read: the lines in between are the
    /// account's.
    NODISCARD bool reading() const { return m_menu.has_value() || m_list.has_value(); }

private:
    NODISCARD AccountReplies closeAll();
    void readMenuLine(const QString &line);
    /// Publishes the open list and keeps it as the page a pager line may take up.
    void publishList(AccountReplies &out);
    static void addRow(AccountChars &list, AccountChar &&row);
};
