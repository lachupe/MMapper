#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "QuietCapture.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

class GameObserver;

/// MUME's shops, guilds, inns and trophies, read off the replies to `list`, `buy`, `sell`,
/// `prac`, `offer`, `rent retire` and `trop`, and MUME's pager, which can cut any of them.
///
/// None of this has a GMCP package. The replies name themselves by their first line ("You can
/// buy:", "You have eleven practice sessions left.", "*** TROPHY ***") or by a keeper's tell
/// followed by "You now have ..." or "You sell ...", so they are read whoever asked: the player,
/// an alias, or MMapper's own trade operations. Nothing is taken out of the terminal here; the
/// reply to a quiet command is, whatever the command (QuietCapture), and is still read here.
///
/// The shapes come from the player logs in mume-logs/elvenrunes (2013-2026) and the powwow logs
/// (2005-2006); see mume3d's docs/shops-and-guilds.md. MUME's output depends on the player's
/// settings, so these are evidence, not a spec. Replies never seen (an itemized `list <number>`,
/// `value`, a keeper refusing to trade, can't afford) are not read yet; ShopDealKindEnum names
/// VALUE and REFUSED so that a reader for them adds no new interface.

/// MUME's pager line: "*** Return: continue, b: back, r: redisplay, q: quit (73%) ***", also
/// with "b: back one page", "q:quit" and ", >: bottom" before the percentage. It has no newline
/// and comes on a GA, so the telnet filter hands it over as a prompt; it is not one.
struct NODISCARD PagerLine final
{
    /// How far through the text the pager is; -1 when it did not say.
    int percent = -1;
    QString text;
    /// Kept out of the terminal: the pager of a quiet command's reply, which the player cannot
    /// see and so only the runner can answer (QuietCapture).
    bool hidden = false;
};

/// The pager line, when `plainChunk` (colour removed) is one and nothing else.
NODISCARD std::optional<PagerLine> parsePagerLine(const QString &plainChunk);

/// Without a GA the pager line is glued to the front of the next line MUME sends. Removes it
/// from `line` and returns true when there was one.
NODISCARD bool stripPagerPrefix(QString &line);

/// An amount of money, in copper at 1 gold = 20 silver = 2,000 copper (so 1 silver is 100
/// copper), and MUME's words for it.
struct NODISCARD Money final
{
    int64_t copper = 0;
    QString text;
};

/// "4 gold 17 silver", "two gold and 11 silver", "fifteen gold, eleven silver, and forty-seven
/// copper", "12 gold coins, 5 silver pennies, and 94 copper pennies", "1g 2s", "11 lauren
/// coins" as copper; nullopt when `words` is not an amount and nothing else. A trailing full
/// stop or exclamation mark is allowed.
NODISCARD std::optional<Money> parseMoney(const QString &words);

/// The first amount of money inside a sentence ("That'll be two gold and 11 silver, please."),
/// or nullopt when there is none.
NODISCARD std::optional<Money> findMoney(const QString &sentence);

/// One row of `list`: " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17
/// silver." A row stands for all the pieces of one kind.
struct NODISCARD ShopRow final
{
    /// The stock number `buy` and `list <number>` take.
    int64_t number = 0;
    int64_t count = 0;
    /// As MUME wrote it, in the plural when count is more than one ("blood-encrusted helms").
    QString name;
    /// The name for one piece, made from `name` by English rules ("blood-encrusted helm");
    /// MUME never prints it in the list, so it is a best guess for anything irregular.
    QString singular;
    /// The words in the brackets: "flawless", "new". Empty when there were none.
    QString condition;
    QString age;
    /// The keeper's ceiling ("up to"), not a fixed price; unset when the words are no amount.
    std::optional<int64_t> priceCopper;
    QString priceText;
    /// Counts the "-----" separators above the row; what a group means is not known.
    int group = 0;
};

/// MUME's reply to `list [filter]`.
struct NODISCARD ShopList final
{
    /// The list does not name the keeper; left empty.
    QString keeper;
    /// The words after `list` in the command this reply answers, when one was seen going out.
    QString query;
    std::vector<ShopRow> rows;
    /// "There are no such things for sale."
    bool empty = false;
    /// A pager line came while the reply was open; `complete` is false when no line of the
    /// reply came after the last one before the prompt (the pager was quit).
    bool paged = false;
    bool complete = true;
    QString text;
};

enum class NODISCARD ShopDealKindEnum : uint8_t {
    /// The keeper's tell with the price and "You now have ..." per item.
    BUY,
    /// The keeper's tell with the sum and "You sell ...".
    SELL,
    /// The reply to `value`: never seen in a log, so never read yet.
    VALUE,
    /// "There is no such thing for sale."
    MISS,
    /// "Sorry, we are closed. We will open in a couple of hours."
    CLOSED,
    /// A keeper refusing the player: never seen in a log, so never read yet.
    REFUSED
};

/// One exchange with a keeper.
struct NODISCARD ShopDeal final
{
    ShopDealKindEnum kind = ShopDealKindEnum::MISS;
    /// The keeper as MUME named them in the tell ("An armourer", "Kormock the orkish
    /// armourer"); empty for MISS.
    QString keeper;
    /// What the tell said the deal cost or paid; unset when it named no amount.
    std::optional<int64_t> amountCopper;
    QString amountText;
    /// What changed hands: one "You now have" per item bought, or the "You sell" list split.
    QStringList items;
    /// The keeper's tell, the words inside the quotes, lines joined.
    QString said;
    QString text;
};

/// One row of a guildmaster's table: "Block door        8/11        91%  Normal      Hard to
/// improve".
struct NODISCARD GuildRow final
{
    QString name;
    /// Sessions taken, and the most this teacher gives.
    int64_t used = 0;
    int64_t most = 0;
    std::optional<int64_t> knowledgePct;
    /// "Easy", "Normal", "Hard", "Very hard".
    QString difficulty;
    /// The teacher's words, kept as they are: "Hard to improve", "You know as much as I do".
    QString advice;
};

/// `prac` beside a guildmaster: "You have eleven practice sessions left.", "Erestor can teach
/// you the spells below.", the heading and a row per spell or skill.
struct NODISCARD GuildTeacher final
{
    /// Empty when the table came without its "can teach you" line.
    QString teacher;
    /// "spells" or "skills", from the "can teach" line or the heading; empty when neither said.
    QString kind;
    /// Digits or words, and negative when the character owes sessions.
    std::optional<int64_t> sessionsLeft;
    std::vector<GuildRow> rows;
    bool paged = false;
    bool complete = true;
    QString text;
};

/// "You took 10 out of 11 sessions in this skill. Your knowledge is now 64%." after `prac
/// <name>`, or a refusal ("You have to stand in order to practice anything.").
struct NODISCARD GuildPractised final
{
    /// From the `prac <name>` command the line answers, as typed; MUME's line does not name it.
    /// Empty when no such command was seen going out.
    QString name;
    std::optional<int64_t> used;
    std::optional<int64_t> most;
    std::optional<int64_t> knowledgePct;
    /// MUME's sentence when it refused; non-empty means a refusal.
    QString refused;
    QString text;
};

/// One row of the general table: "Armour               Excellent  Hard        Magic User    25
/// Very short".
struct NODISCARD CharSkillRow final
{
    QString name;
    /// "Bad", "Poor", "Fair", "Average", "Good", "Very good", "Excellent", "Superb", without the
    /// `*` that marks a skill not being trained.
    QString knowledge;
    /// False when the knowledge word carried the `*`.
    bool trained = true;
    QString difficulty;
    /// "None", "Magic User", "Cleric", "Thief", "Warrior".
    QString skillClass;
    /// Spells only: the mana and "Very short", "Short".
    std::optional<int64_t> mana;
    QString casting;
};

/// `prac` away from a guild: the character's own skills and spells, the only place MUME lists
/// them.
struct NODISCARD CharSkills final
{
    std::optional<int64_t> sessionsLeft;
    std::vector<CharSkillRow> rows;
    bool paged = false;
    bool complete = true;
    QString text;
};

/// The innkeeper's quote to `offer` or `rent`: "'It will cost you 12 gold coins, 5 silver
/// pennies, and 94 copper pennies per day.'" and "You have enough money for at least five
/// years!"; or MUME asking for `rent retire` again.
struct NODISCARD InnOffer final
{
    QString keeper;
    std::optional<int64_t> perDayCopper;
    QString perDayText;
    /// "You have enough money for at least five years!", whole.
    QString lastsText;
    /// A tell about confiscated equipment ("You can recover your confiscated equipment for 587
    /// gold."), whole; its exact words at an inn have not been seen.
    QString confiscated;
    /// The keeper's tell, the words inside the quotes, lines joined.
    QString said;
    QString text;
    /// "If you really want to retire, please repeat that request." came: the retirement waits
    /// for the same command again.
    bool retireAsksRepeat = false;
};

/// One entry of `trop`: "   1,  1%,  #Tuunbaq                  |".
struct NODISCARD TrophyRow final
{
    /// Without the `#` that marks a player.
    QString name;
    int64_t kills = 0;
    int64_t knowledgePct = 0;
    bool player = false;
};

/// `trop`: "*** TROPHY *** (Number Killed, Knowledge, Mobile)" (or "(Kills, Knowledge, Name)")
/// and rows of one to three entries each, then "Total kills: 63 (53 distinct)." The list is
/// long, so it is the pager's main case.
struct NODISCARD CharTrophies final
{
    std::vector<TrophyRow> rows;
    /// From the closing "Total kills: N (M distinct)." or "Total matching kills: N (M
    /// distinct)", when it came.
    std::optional<int64_t> totalKills;
    std::optional<int64_t> distinct;
    bool paged = false;
    bool complete = true;
    QString text;
};

/// A text MUME showed through its viewer (MUME.Client.View, or MPI's view), for a frontend
/// that claimed the viewer.
struct NODISCARD ViewText final
{
    QString title;
    QString text;
};

/// What one line or prompt completed.
struct NODISCARD TradeReplies final
{
    std::vector<ShopList> lists;
    std::vector<ShopDeal> deals;
    std::vector<GuildTeacher> teachers;
    std::vector<GuildPractised> practised;
    std::vector<CharSkills> skills;
    std::vector<InnOffer> inns;
    std::vector<CharTrophies> trophies;

    NODISCARD bool empty() const
    {
        return lists.empty() && deals.empty() && teachers.empty() && practised.empty()
               && skills.empty() && inns.empty() && trophies.empty();
    }
    void append(TradeReplies &&other);
};

/// Gathers the trade replies line by line. The tables (list, the teacher's and the general
/// practice tables, trophies) and the deals are published at the real prompt, since only then
/// is it known whether a pager cut them short; a practised line comes out at once, and an inn
/// quote once its "You have enough money" line has come.
///
/// A reply opens on its first line, lets foreign lines through (someone arriving, a tell) up to
/// a bound, and closes at its blank line, at the start of another reply, or at the prompt. A
/// pager line keeps an open reply open and marks it paged.
/// Which reply the last line read was a line of (TradeLinesTracker::lastLineKind()), for the
/// prioritised Log's tags.
enum class NODISCARD TradeLineKindEnum : uint8_t { NONE, SHOP, GUILD, INN, TROPHIES };

class NODISCARD TradeLinesTracker final
{
private:
    enum class NODISCARD TableEnum : uint8_t { NONE, LIST, TEACHER, SKILLS, TROPHIES };

    /// The table being read, and whether it had a row yet.
    TableEnum m_table = TableEnum::NONE;
    bool m_tableHasRow = false;
    /// The teacher's table before its heading: after the "can teach" line and its blank.
    bool m_teacherBeforeHeading = false;
    int m_strangers = 0;
    QStringList m_lines;
    bool m_paged = false;
    bool m_afterPager = false;
    /// The "-----" separators seen in the list being read.
    int m_listGroup = 0;

    std::optional<ShopList> m_list;
    std::optional<GuildTeacher> m_teacher;
    std::optional<CharSkills> m_skills;
    std::optional<CharTrophies> m_trophies;

    /// "You have N practice sessions left." seen in this prompt window, for the table after it.
    std::optional<int64_t> m_sessionsLeft;
    bool m_sessionsLinePending = false;
    QString m_sessionsLine;

    /// A keeper's tell whose closing quote has not come yet, continued on the next lines.
    struct NODISCARD OpenTell final
    {
        QString speaker;
        QString said;
        QStringList lines;
    };
    std::optional<OpenTell> m_tell;
    /// The last complete tell with an amount in it, waiting for its "You now have" or "You sell".
    std::optional<ShopDeal> m_deal;
    /// "You sell ..." whose closing full stop has not come yet.
    bool m_sellOpen = false;
    QString m_sellList;
    int m_sellLines = 0;
    /// The inn's quote, and the retire request being continued.
    std::optional<InnOffer> m_inn;
    std::optional<QString> m_retire;
    int m_retireLines = 0;

    /// Replies closed in this prompt window, published at the prompt.
    TradeReplies m_done;
    TradeLineKindEnum m_lastKind = TradeLineKindEnum::NONE;

    /// The filters of `list` commands gone out, oldest first, and the names of `prac <name>`.
    std::deque<QString> m_listQueries;
    std::deque<QString> m_pracNames;

public:
    /// A line on its way to MUME: `list <filter>` names the query of the next list, `prac
    /// <name>` the skill of the next practised line.
    void receiveCommand(const QString &line);
    /// Reads one line of MUME's output, colour removed, a glued pager already stripped.
    NODISCARD TradeReplies receiveLine(const QString &line);
    /// A pager line: keeps the open reply open and marks it paged.
    void receivePager(const PagerLine &pager);
    /// A real prompt (not a pager) closes whatever is open and publishes the window's replies.
    NODISCARD TradeReplies receivePrompt();
    /// For a new session, or when XML mode goes away.
    void reset();
    /// Whose line the last one receiveLine() read was: a shop's, a guild's, an inn's, the
    /// trophies'; NONE for a line of no reply here (a keeper's tell is MUME's speech).
    NODISCARD TradeLineKindEnum lastLineKind() const { return m_lastKind; }

private:
    NODISCARD static TradeLineKindEnum kindOfTable(TableEnum table);
    /// A blank line ends a list or a practice table that has a row, except right after a pager:
    /// there it is what MUME puts before whatever else it says while the pager waits, and the
    /// next page goes on with the table.
    NODISCARD bool blankClosesTable() const
    {
        return m_tableHasRow && !(m_paged && !m_afterPager);
    }
    void closeTable();
    void closeTell(TradeReplies &out);
    void closeDeal();
    void openTable(TableEnum table, const QString &line);
    void markOwnLine(const QString &line);
    NODISCARD bool readTableLine(const QString &raw, const QString &text);
    NODISCARD bool readDealLine(const QString &text, TradeReplies &out);
    NODISCARD bool readInnLine(const QString &text, TradeReplies &out);
    void closeRetire(TradeReplies &out);
    void readTell(const QString &speaker,
                  const QString &said,
                  const QStringList &lines,
                  TradeReplies &out);
};

/// What MumeXmlParser does with one chunk of MUME's output.
enum class NODISCARD MudChunkKindEnum : uint8_t {
    /// A line ending in a newline.
    LINE,
    /// A chunk on a GA that is the pager line: not a prompt.
    PAGER,
    /// A chunk on a GA that is not the pager: the real prompt.
    PROMPT,
    /// A turn of the spinner (backspaces), neither.
    TWIDDLER
};

struct NODISCARD MudChunk final
{
    MudChunkKindEnum kind = MudChunkKindEnum::LINE;
    /// PAGER, and a LINE that had a pager glued to its front.
    std::optional<PagerLine> pager;
    /// The plain text the line readers get: a glued pager taken off.
    QString plain;
    /// The chunk is of a quiet command's reply and must not be sent to the terminal
    /// (QuietCapture). Every reader still reads it.
    bool hidden = false;
    /// A line of a quiet command's reply, hidden or, where that could not be told, shown.
    bool captured = false;
};

/// Decides once what a chunk is, so that every reader agrees: `goAhead` and `backspace` are
/// how the telnet filter framed it, `plain` its text with colour removed.
NODISCARD MudChunk classifyMudChunk(bool goAhead, bool backspace, const QString &plain);

/// The trade readers as MumeXmlParser drives them, publishing into the GameObserver: the pager,
/// and the promise that at a real prompt every reader's package goes out before
/// sig2_realPrompt.
///
/// MumeXmlParser calls beginChunk() first for every chunk, receiveLine() with the line readers,
/// receivePrompt() with the other trackers' receivePrompt(), and endChunk() last, after
/// everything else it publishes for that chunk; endChunk() of a real prompt emits
/// sig2_realPrompt. A pager chunk publishes sig2_pager and nothing else, and does not count as
/// a prompt for any reader.
///
/// Since it is here that a chunk is told to be a line, the pager or the prompt, the window of
/// a quiet command (QuietCapture) is kept here too: beginChunk() says in the chunk whether the
/// terminal is sent it and whether it is of the reply, marks the pager it hid, and endChunk()
/// of the prompt that closed the window emits sig2_quietEnded, before sig2_realPrompt.
class NODISCARD TradeReaders final
{
private:
    GameObserver &m_observer;
    TradeLinesTracker m_tracker;
    MudChunkKindEnum m_chunk = MudChunkKindEnum::LINE;
    bool m_pagerOpen = false;
    QuietCapture m_quiet;
    /// The chunk being read is the prompt that closed the quiet command's window.
    bool m_quietEnded = false;

public:
    explicit TradeReaders(GameObserver &observer);

    /// A line on its way to MUME. False when it is the answer to a pager ("", "q", "b", ...),
    /// whoever sent it, which must not count as a command for any tracker.
    NODISCARD bool receiveCommand(const QString &line);
    /// Classifies the chunk; a pager (a whole chunk, or glued to a line) is published here.
    /// `traffic` is what MUME's markup says the line is, which only a quiet command's window
    /// asks (quietOpen()); see quietTrafficOf().
    NODISCARD MudChunk beginChunk(bool goAhead,
                                  bool backspace,
                                  const QString &plain,
                                  QuietTrafficEnum traffic = QuietTrafficEnum::REPLY);
    /// One line of MUME's output, as classified.
    void receiveLine(const QString &plain);
    /// Whose line the last one receiveLine() read was (TradeLinesTracker::lastLineKind()).
    NODISCARD TradeLineKindEnum lastLineKind() const { return m_tracker.lastLineKind(); }
    /// The real prompt: publishes what the window completed.
    void receivePrompt();
    /// After everything else for the chunk: sig2_realPrompt for a real prompt.
    void endChunk();
    /// Whether the last chunk from MUME left a pager waiting for its answer.
    NODISCARD bool pagerOpen() const { return m_pagerOpen; }
    void reset();

    /// What the runner says about its quiet command: begun, another line gone out, over.
    void receiveQuietCommand(QuietCommandEnum what) { m_quiet.receiveCommand(what); }
    /// Whether a quiet command's window is open, so that its reply is being kept apart.
    NODISCARD bool quietOpen() const { return m_quiet.isOpen(); }
    /// A line that is kept from the terminal anyway and never becomes a chunk (the MMXP line):
    /// in a quiet command's window it is a line of the reply all the same. True when it is.
    NODISCARD bool captureQuietLine(const QString &plain);

private:
    void publish(const TradeReplies &replies);
    /// Asks the quiet command's window what to do with the chunk.
    void hideQuietChunk(MudChunk &chunk, QuietTrafficEnum traffic);
};
