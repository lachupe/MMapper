#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

/// The character's own figures, read off MUME's replies to `stat`, `score` and `info`.
///
/// GMCP carries the pools, experience, mood and alertness (Char.Vitals, Char.StatusVars), but
/// not OB, DB, PB, armour, wimpy, the coins, the abilities, the language or the affects; MUME
/// prints those only when asked. The replies name themselves by their first line, so they are
/// read whoever asked: the player typing `stat`, an alias, or a frontend sending the command
/// quietly. Nothing is taken out of the terminal.
///
/// The shapes come from the powwow logs (1999-2017, one 2026 log for `stat` and `score`), where:
///
/// - `stat` is "OB: 60%, DB: 62%, PB: 60%, Armour: 66%. Wimpy: 120. Mood: wimpy.", then
///   "Needed: 1,136,776 xp, 0 tp. Gold: 103. Alert: normal." (at the highest level only "Gold:
///   519. Alert: normal."), then "Affected by:" and one "- name" per affect, up to the blank line;
/// - `score` is one line, "523/523 hits, 53/53 mana, and 155/155 moves.";
/// - `info` is the whole sheet: race, age, rank, "Your equipment weighs ... pounds. Heavy, but we
///   will manage...", the abilities, "Offensive Bonus: ...", the armour, the pools, the mood and
///   wimpy, experience and travel points, the renown line, what the next level needs, the
///   coins, the language, swimming and climbing, and "You are subjected to the following
///   temporary effects:" with its list.
///
/// A figure a reply did not state is left unset, never zero. Lines a mapper proxy added to
/// the logs (Pandora's "Timers:" and "Countdowns:" sections, its "(up for - 04:56)" and
/// "(unknown time)" after an affect) are tolerated and dropped.

/// MUME's reply to `stat`.
struct NODISCARD CharStat final
{
    std::optional<int64_t> ob;
    std::optional<int64_t> db;
    std::optional<int64_t> pb;
    /// Armour's average protection in percent; 0 for "Armour: none.".
    std::optional<int64_t> armour;
    std::optional<int64_t> wimpy;
    std::optional<int64_t> neededXp;
    std::optional<int64_t> neededTp;
    std::optional<int64_t> gold;
    /// War points, where the line said "Wp: 97." (old wording).
    std::optional<int64_t> wp;
    /// "wimpy", "aggressive", "prudent", "berserk", "normal": MUME's word, lowercase.
    QString mood;
    /// "normal", "paranoid".
    QString alert;
    /// The words after "Condition:": "hungry", "thirsty", "drunk".
    QStringList condition;
    /// The affects listed after "Affected by:", less the wounds, in MUME's order and words
    /// ("strength", "sense life", "poison (type: psylonia)"). Empty for a reply without the list.
    QStringList affects;
    /// The wounds from the same list, whole: "a light wound at the head (clean)".
    QStringList wounds;
    /// The reply as MUME wrote it, one line per line.
    QString text;
};

/// MUME's reply to `score` (the pools only) or to `info` (the whole sheet).
struct NODISCARD CharScore final
{
    /// "score" for the one-line reply, "info" for the sheet.
    QString reply;
    /// Str, Int, Wis, Dex, Con, Wil, Per, in that order; each unset when not stated.
    std::array<std::optional<int64_t>, 7> abilities{};
    std::optional<int64_t> ob;
    std::optional<int64_t> db;
    std::optional<int64_t> pb;
    /// Average protection in percent; 0 for "You are not wearing any armour.".
    std::optional<int64_t> armour;
    std::optional<int64_t> hp;
    std::optional<int64_t> maxhp;
    std::optional<int64_t> mana;
    std::optional<int64_t> maxmana;
    std::optional<int64_t> mp;
    std::optional<int64_t> maxmp;
    /// The hit points below which the character flees; 0 for "You will fight to the death.".
    std::optional<int64_t> wimpy;
    std::optional<int64_t> xp;
    std::optional<int64_t> tp;
    std::optional<int64_t> neededXp;
    std::optional<int64_t> neededTp;
    std::optional<int64_t> gold;
    std::optional<int64_t> silver;
    std::optional<int64_t> copper;
    /// War points from the renown line's "(20 wp)".
    std::optional<int64_t> wp;
    QString mood;
    /// The renown line less its "(N wp)": "You have gained some renown in battles against the
    /// minions of the Dark Lord."
    QString renown;
    /// "Westron", "Orkish" -- from "You are speaking X." or "You are trying to speak X.".
    QString language;
    /// MUME's sentences: "You will swim if necessary.", "You will try to climb even under unsafe
    /// conditions."
    QString swim;
    QString climb;
    /// The temporary effects, less the wounds, and the wounds, as in CharStat.
    QStringList effects;
    QStringList wounds;
    /// True when the sheet had its effects list, or ended without one: `effects` and `wounds`
    /// are then the whole truth, even when empty.
    bool effectsKnown = false;
    QString text;
};

/// "Your equipment weighs one hundred fourteen pounds. Heavy, but we will manage..." from `info`.
struct NODISCARD CharBurden final
{
    /// 0 for "Your equipment weighs nothing."
    int64_t pounds = 0;
    /// MUME's comment on it: "Heavy, but we will manage...", "Peanuts.". Empty when none.
    QString word;
    QString text;
};

/// "one hundred twenty-one" as 121, or nullopt when the words are not a number.
NODISCARD std::optional<int64_t> parseNumberWords(const QString &words);

/// The burden line, or nullopt when `line` is not one.
NODISCARD std::optional<CharBurden> parseBurdenLine(const QString &line);

/// `score`'s one-line reply ("523/523 hits, 53/53 mana, and 155/155 moves."), or nullopt.
NODISCARD std::optional<CharScore> parseScoreLine(const QString &line);

/// What one line or prompt completed.
struct NODISCARD CharReplies final
{
    std::vector<CharStat> stats;
    std::vector<CharScore> scores;
    std::vector<CharBurden> burdens;

    NODISCARD bool empty() const { return stats.empty() && scores.empty() && burdens.empty(); }
    void append(CharReplies &&other);
};

/// Gathers the `stat` block and the `info` sheet line by line; `score`'s line and the burden
/// line are complete in themselves and come out at once.
///
/// `stat` opens on its OB line and closes at the blank line after it (or the prompt). The sheet
/// opens on any of its lines and closes at the prompt, at the blank line after its effects
/// list, when a `stat` begins, or after a run of lines that are none of its own. A sheet that
/// stated no figure is not published.
class NODISCARD CharLinesTracker final
{
private:
    std::optional<CharStat> m_stat;
    QStringList m_statLines;
    /// Which list the "- name" lines of the open `stat` belong to.
    enum class NODISCARD StatSectionEnum : uint8_t { HEAD, PROXY, AFFECTS };
    StatSectionEnum m_statSection = StatSectionEnum::HEAD;
    bool m_statHasSecondLine = false;

    std::optional<CharScore> m_sheet;
    QStringList m_sheetLines;
    bool m_sheetInEffects = false;
    bool m_sheetAfterScored = false;
    bool m_sheetHasFigure = false;
    int m_sheetStrangers = 0;

public:
    /// Reads one line of MUME's output, colour removed.
    NODISCARD CharReplies receiveLine(const QString &line);
    /// A prompt closes whatever is open.
    NODISCARD CharReplies receivePrompt();
    /// For a new session, or when XML mode goes away.
    void reset();

private:
    NODISCARD CharReplies closeStat();
    NODISCARD CharReplies closeSheet();
    NODISCARD bool acceptStat(const QString &text);
    /// True when the line is one of the sheet's own.
    NODISCARD bool readSheetLine(const QString &text, CharReplies &out);
};
