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
/// - `info` is the whole sheet: sex and race, age, time played, rank and level, height and
///   weight, perception and alertness, the alignment sentence, where the character is welcome,
///   "Your equipment weighs ... pounds. Heavy, but we will manage...", the abilities,
///   "Offensive Bonus: ...", the armour, the pools, the mood and
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

    // The head of the sheet, each left empty or unset when its line was missing or in a
    // wording not known. None of them counts as a figure: a sheet of these alone is not
    // published.
    /// "male", "female", "neuter" and "Eriadorian", "Black Numenorean", "Tarkhnarb Orc" from
    /// "You are a male Eriadorian.".
    QString sex;
    QString race;
    /// "You are 19 years and 6 months old."; older sheets add the days.
    std::optional<int64_t> ageYears;
    std::optional<int64_t> ageMonths;
    std::optional<int64_t> ageDays;
    /// MUME's words from "You have played 4 hours (real time). Session: 10 mins.": "4 hours",
    /// "21 days and 14 hours", and "10 mins".
    QString played;
    QString session;
    /// "This ranks you as Idwar the Man Adventurer (level 2).": the first word, the rest ("the
    /// Man Adventurer", "VI", empty when there is none) and the level.
    QString name;
    QString title;
    std::optional<int64_t> level;
    /// MUME's words: "five feet nine", "eleven stone and eleven pounds".
    QString height;
    QString weight;
    /// "Perception: vision 40, hearing -31, smell -60. Alertness: normal."
    std::optional<int64_t> vision;
    std::optional<int64_t> hearing;
    std::optional<int64_t> smell;
    /// "normal", "paranoid": MUME's word, lowercase, as CharStat's `alert`.
    QString alertness;
    /// The alignment sentence, whole: "You are a well-meaning person, always glad to help your
    /// friends.", "You are totally corrupted by the Evilness of Morgoth!".
    QString alignment;
    /// The places of "You are welcome in Bree, Fornost, the Grey Havens, Rivendell, and the Blue
    /// Mountains.", in MUME's order and words ("the Blue Mountains").
    QStringList welcome;
    /// "You are not known for any acts of war.", whole; empty for any other renown line. The
    /// sentence is in `renown` too when it follows the experience line, as it always has been.
    QString war;

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

/// The line MMapper reads the level and the experience and travel points from, asked of MUME
/// with `info`'s format keys: %l level, %x experience, %X experience to level, %t travel points,
/// %T travel points to level. The reply is one line, "MMXP 12 34567 890 1234 56" (with
/// thousand separators when the player's setting, or a %, in the format, turns them on).
/// A frontend sends this command as it is (MMapper.Input.Command); MMapper publishes the reply
/// as MMapper.Char.Level and keeps it out of the terminal.
inline constexpr const char *const CHAR_LEVEL_REQUEST = "info MMXP %l %x %X %t %T";

/// The reply to CHAR_LEVEL_REQUEST. MUME's `info` format has no key for the experience at which
/// the current level began, only for what the next one needs.
struct NODISCARD CharLevel final
{
    int64_t level = 0;
    std::optional<int64_t> xp;
    /// Experience still needed for the next level (%X); what MUME prints once there is none
    /// left to gain is not known, so a figure that is not a number is left unset.
    std::optional<int64_t> neededXp;
    std::optional<int64_t> tp;
    /// Travel points still needed for the next level (%T). A character levels only when both
    /// are met.
    std::optional<int64_t> neededTp;
    QString text;
};

/// The reply to CHAR_LEVEL_REQUEST ("MMXP 12 34,567 890 1234 56"), or nullopt when `line` is
/// not one. The level must be a number; the other four are left unset when they are not.
NODISCARD std::optional<CharLevel> parseCharLevelLine(const QString &line);

/// The hit points below which the character flees, as MUME last stated them: in its reply to
/// `change wimpy N` ("Wimpy set to: 120", the powwow logs' wording; 0 turns it off), in `stat`
/// ("Wimpy: 120.") or in `info` ("You will flee if your hit points go below 315.", 0 for "You
/// will fight to the death."). One package for all three, so that a frontend has one figure
/// and need not work out which of several replayed replies was the last.
/// One row of the reply to a bare `change language`: "   60   Westron", "  100 * Orkish" (the
/// star marks the language being spoken).
struct NODISCARD CharLanguage final
{
    QString name;
    int64_t knowledge = 0;
    bool speaking = false;
};

/// The reply to a bare `change language` (also printed after `change language <name>`): "You have
/// the following knowledge in these languages:" and a row per language, to the blank line.
struct NODISCARD CharLanguages final
{
    std::vector<CharLanguage> rows;
};

/// A skill improved by use: "Yes! You're beginning to get the idea." (which names none: the one just
/// used) and "You feel your awareness improve." (which does). `skill` is empty where MUME named none.
struct NODISCARD CharImproved final
{
    QString skill;
    QString text;
};

struct NODISCARD CharWimpy final
{
    int64_t wimpy = 0;
};

/// "Wimpy set to: 120", or nullopt when `line` is not that reply.
NODISCARD std::optional<CharWimpy> parseWimpyLine(const QString &line);

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
    /// After the `stat` or sheet that stated it, when one did.
    std::vector<CharWimpy> wimpies;
    std::vector<CharImproved> improved;
    std::vector<CharLanguages> languages;

    NODISCARD bool empty() const
    {
        return stats.empty() && scores.empty() && burdens.empty() && wimpies.empty() && improved.empty()
               && languages.empty();
    }
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

    /// The languages table being read, between its heading and its blank line.
    std::optional<CharLanguages> m_languages;
    std::optional<CharScore> m_sheet;
    QStringList m_sheetLines;
    bool m_sheetInEffects = false;
    bool m_sheetAfterScored = false;
    /// The line before was the perception line: the alignment sentence comes next.
    bool m_sheetAfterPerception = false;
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
