// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharLines.h"

#include "../global/parserutils.h"

#include <array>
#include <utility>

#include <QRegularExpression>

namespace {

/// Bounds a reply that never closes: `info` is about 25 lines and a long affects list 30.
constexpr int MAX_REPLY_LINES = 200;
/// Lines that are none of the sheet's own before it is taken to have ended without a prompt:
/// an old sheet's "You are a citizen of: GoblinTown" is one.
constexpr int MAX_SHEET_STRANGERS = 8;

NODISCARD QString cleaned(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    return text.simplified();
}

/// "1,136,776" or "-45" as a number; nullopt when it is not one.
NODISCARD std::optional<int64_t> toNumber(QString digits)
{
    digits.remove(QLatin1Char(','));
    bool ok = false;
    const qlonglong value = digits.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<int64_t>(value);
}

NODISCARD std::optional<int64_t> captured(const QRegularExpressionMatch &match, const int group)
{
    if (!match.hasCaptured(group)) {
        return std::nullopt;
    }
    return toNumber(match.captured(group));
}

// -- stat ----------------------------------------------------------------------------------

// "OB: 60%, DB: 62%, PB: 60%, Armour: 66%. Wimpy: 120. Mood: wimpy." Read as pairs so that
// the rarer wordings fit too: "M_OB: 81%" between OB and DB, "Armour: none.", "Armour: 3/8.",
// a line without Wimpy or Mood.
const QRegularExpression g_statFirst{
    QStringLiteral(R"(^OB: *-?[\d,]+%, (?:M_OB: *-?[\d,]+%, )?DB:)")};
const QRegularExpression g_statPair{
    QStringLiteral(R"(\b(OB|DB|PB|Armour|Wimpy|Mood): *([^,.\s]+))")};
// "Needed: 1,136,776 xp, 0 tp." and the old "Needed: 653,899 xp and 2,337 tp."; at the highest
// level the line is only "Gold: 519. Alert: normal.". "Lauren" stands for gold in some logs.
const QRegularExpression g_statNeeded{
    QStringLiteral(R"(^Needed: ([\d,]+) xp(?:(?:,| and) ([\d,]+) tp)?\.)")};
const QRegularExpression g_statSecond{QStringLiteral(R"(^(?:Needed|Gold|Lauren): )")};
const QRegularExpression g_statField{
    QStringLiteral(R"(\b(Wp|Gold|Lauren|Alert|Condition): *([^.]+)\.)")};
// Pandora's additions to the list: an affect it timed, or could not.
const QRegularExpression g_proxyTimer{
    QStringLiteral(R"(\s*\((?:up for\b[^)]*|unknown time)\)\s*$)")};
const QRegularExpression g_wound{QStringLiteral(R"(^an? [\w-]+ wound (?:at|on|in) )"),
                                 QRegularExpression::CaseInsensitiveOption};

/// One "- name" line of an affects list, as the name MUME gave, less a proxy's timer and a
/// closing full stop ("- stored spell earthquake." in some logs).
NODISCARD QString listed(const QString &text)
{
    QString name = text.mid(2).trimmed();
    name.remove(g_proxyTimer);
    while (name.endsWith(QLatin1Char('.'))) {
        name.chop(1);
    }
    return name.trimmed();
}

void addListed(const QString &text, QStringList &names, QStringList &wounds)
{
    const QString name = listed(text);
    if (name.isEmpty()) {
        return;
    }
    if (g_wound.match(name).hasMatch()) {
        wounds.append(name);
    } else {
        names.append(name);
    }
}

void readStatFirst(const QString &text, CharStat &stat)
{
    auto it = g_statPair.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString key = m.captured(1);
        QString value = m.captured(2);
        if (key == QStringLiteral("Mood")) {
            stat.mood = value.toLower();
            continue;
        }
        if (key == QStringLiteral("Armour") && value == QStringLiteral("none")) {
            stat.armour = 0;
            continue;
        }
        if (value.endsWith(QLatin1Char('%'))) {
            value.chop(1);
        } else if (key != QStringLiteral("Wimpy")) {
            continue; // "Armour: 3/8." is not a percentage
        }
        const std::optional<int64_t> number = toNumber(value);
        if (!number.has_value()) {
            continue;
        }
        if (key == QStringLiteral("OB")) {
            stat.ob = number;
        } else if (key == QStringLiteral("DB")) {
            stat.db = number;
        } else if (key == QStringLiteral("PB")) {
            stat.pb = number;
        } else if (key == QStringLiteral("Armour")) {
            stat.armour = number;
        } else if (key == QStringLiteral("Wimpy")) {
            stat.wimpy = number;
        }
    }
}

void readStatSecond(const QString &text, CharStat &stat)
{
    if (const auto needed = g_statNeeded.match(text); needed.hasMatch()) {
        stat.neededXp = captured(needed, 1);
        stat.neededTp = captured(needed, 2);
    }
    auto it = g_statField.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString key = m.captured(1);
        const QString value = m.captured(2).trimmed();
        if (key == QStringLiteral("Gold") || key == QStringLiteral("Lauren")) {
            stat.gold = toNumber(value);
        } else if (key == QStringLiteral("Wp")) {
            stat.wp = toNumber(value); // "Wp: involved in skirmishes." is no number
        } else if (key == QStringLiteral("Alert")) {
            stat.alert = value.toLower();
        } else if (key == QStringLiteral("Condition")) {
            stat.condition = value.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
    }
}

// -- score and info ------------------------------------------------------------------------

const QRegularExpression g_scoreLine{QStringLiteral(
    R"(^(-?[\d,]+)/([\d,]+) hits?,? (?:(-?[\d,]+)/([\d,]+) mana,? )?and (-?[\d,]+)/([\d,]+) moves\.$)")};

const QRegularExpression g_burden{
    QStringLiteral(R"(^Your equipment weighs (nothing|[a-z -]+? pounds?)\.(?: (.+))?$)")};
// An older wording: "You are carrying 0 pounds of equipment. Peanuts."
const QRegularExpression g_burdenOld{
    QStringLiteral(R"(^You are carrying ([\d,]+) pounds? of equipment\.(?: (.+))?$)")};

// The lines at the head of the sheet, which state no figure: known so that they keep it open,
// in whatever wording; readSheetInfo() reads the wordings it knows.
const std::array
    g_sheetInfoOnly{QRegularExpression{
                        QStringLiteral(R"(^You are an? (?:male|female|neuter) .+\.$)")},
                    QRegularExpression{QStringLiteral(R"(^You are [\d,]+ years?\b.* old\.)")},
                    QRegularExpression{QStringLiteral(R"(^You have played .*\(real time\))")},
                    QRegularExpression{QStringLiteral(R"(^This ranks you as .*\(level \d+\)\.$)")},
                    QRegularExpression{QStringLiteral(R"(^You are [a-z]+ feet\b.* and weigh )")},
                    QRegularExpression{QStringLiteral(R"(^Perception: vision)")},
                    QRegularExpression{QStringLiteral(R"(^You are (?:not )?welcome in )")},
                    QRegularExpression{QStringLiteral(R"(^You have reached the highest level\.)")}};

// What those lines state. "You are a male Black Numenorean."
const QRegularExpression g_sexRace{
    QStringLiteral(R"(^You are an? (male|female|neuter) (.+)\.$)")};
// "You are 19 years and 6 months old.", "You are 55 years, 11 months and 29 days old."
const QRegularExpression g_age{QStringLiteral(R"(^You are ([\d,]+) years?\b)")};
const QRegularExpression g_agePart{QStringLiteral(R"(\b([\d,]+) (month|day)s?\b)")};
// "You have played 4 hours (real time). Session: 10 mins."; old sheets have no session.
const QRegularExpression g_played{QStringLiteral(R"(^You have played (.+?) \(real time\))")};
const QRegularExpression g_session{QStringLiteral(R"(\(real time\)\.? Session: (.+?)\.?$)")};
// "This ranks you as Idwar the Man Adventurer (level 2).", "... as Ennor VI (level 58).",
// "... as Sardar (level 26).": the name is the first word, the title whatever follows it.
const QRegularExpression g_rank{
    QStringLiteral(R"(^This ranks you as (\S+)(?: (.+?))? ?\(level (\d+)\)\.$)")};
// "You are five feet nine and weigh eleven stone and eleven pounds."
const QRegularExpression g_heightWeight{
    QStringLiteral(R"(^You are ([a-z]+ feet\b[a-z -]*?) and weigh ([a-z -]+)\.$)")};
// "Perception: vision 40, hearing -31, smell -60. Alertness: normal."; an old wording is
// "Perception: vision 0 hearing -24 smelling -24."
const QRegularExpression g_perception{QStringLiteral(
    R"(^Perception: vision (-?\d+),? hearing (-?\d+),? smell(?:ing)? (-?\d+)\.)")};
const QRegularExpression g_alertness{QStringLiteral(R"(\bAlertness: ([^.]+)\.)")};
// "You are welcome in Bree, Fornost, the Grey Havens, Rivendell, and the Blue Mountains."
const QRegularExpression g_welcome{QStringLiteral(R"(^You are welcome in (.+)\.$)")};
const QRegularExpression g_welcomeSeparator{QStringLiteral(R"(,? and |, )")};

// The alignment sentence, in the wordings seen: the live sheet's "You are a well-meaning
// person, always glad to help your friends." and the logs' two extremes (with "on Arda", "weigh
// on it !" and "Morgoth !" in old ones). Any other wording is taken by its place, straight
// after the perception line.
const std::array
    g_alignments{QRegularExpression{QStringLiteral(R"(^You are an? [A-Za-z' -]+ person[,.!])")},
                 QRegularExpression{QStringLiteral(
                     R"(^You are totally corrupted by the Evilness of Morgoth ?!$)")},
                 QRegularExpression{QStringLiteral(
                     R"(^You must have been sent (?:to|on) Arda to free it from the sorrows that weigh (?:up)?on it ?[.!]$)")}};
// The renown line of a character without war points; `war` is this sentence only.
const QRegularExpression g_war{QStringLiteral(R"(^You are not known for any acts of war\.$)")};

const QRegularExpression g_abilities{QStringLiteral(R"(^Your (?:base )?abilities are: )")};
const QRegularExpression g_ability{QStringLiteral(R"(\b(Str|Int|Wis|Dex|Con|Wil|Per): *(-?\d+))")};
const QRegularExpression g_bonus{QStringLiteral(
    R"(^Offensive Bonus: (-?[\d,]+)%, Dodging Bonus: (-?[\d,]+)%, Parrying Bonus: (-?[\d,]+)%)")};
const QRegularExpression g_armour{QStringLiteral(
    R"(^Your armou?r (?:provides an average protection of|absorbs, on average,) (-?[\d,]+)%)")};
const QRegularExpression g_noArmour{QStringLiteral(R"(^You are not wearing any armou?r\.)")};
const QRegularExpression g_pools{QStringLiteral(R"(^You have -?[\d,]+/[\d,]+ hits?,)")};
const QRegularExpression g_pool{
    QStringLiteral(R"((-?[\d,]+)/([\d,]+) (hits?|mana|movement|moves)\b)")};
const QRegularExpression g_mood{QStringLiteral(
    R"(^Your mood is (\w+)\.(?: You will (?:flee if your hit points go below ([\d,]+)|(fight to the death))\.)?)")};
const QRegularExpression g_scored{QStringLiteral(
    R"(^You have scored ([\d,]+) experience points and (?:you have )?([\d,]+) travel points)")};
const QRegularExpression g_need{QStringLiteral(
    R"(^You need ([\d,]+) exp(?:\.|erience)?(?: points)? (?:and ([\d,]+) travel points )?to reach)")};
const QRegularExpression g_coins{QStringLiteral(R"(^You have [\d,]+ (?:gold|silver|copper)\b)")};
const QRegularExpression g_coin{QStringLiteral(R"(([\d,]+) (gold|silver|copper)\b)")};
const QRegularExpression g_speaking{
    QStringLiteral(R"(^You are (?:speaking|trying to speak) (?:in )?([A-Za-z' -]+?)\.$)")};
const QRegularExpression g_swim{QStringLiteral(R"(^You will\b.*\bswim\b.*\.$)")};
const QRegularExpression g_climb{QStringLiteral(R"(^You will\b.*\bclimb\b.*\.$)")};
const QRegularExpression g_effects{
    QStringLiteral(R"(^You are subjected to the following temporary effects:)")};
const QRegularExpression g_renownWp{QStringLiteral(R"(\s*\((-?[\d,]+) wps?\))")};

// -- the level line ------------------------------------------------------------------------

// CHAR_LEVEL_REQUEST's reply: the marker and five figures, which MUME prints with thousand
// separators when they are on ("34,567") and, at the highest level, perhaps not as numbers.
const QRegularExpression g_levelLine{
    QStringLiteral(R"(^MMXP (\d[\d,]*) (\S+) (\S+) (\S+) (\S+)$)")};

// -- wimpy ---------------------------------------------------------------------------------

// The reply to `change wimpy 120`: "Wimpy set to: 120" (powwow
// logs/archives/log-2006.02.09-19.48.52.txt:489, and some two thousand more; none ends in a
// full stop, which is tolerated all the same).
const QRegularExpression g_wimpySet{QStringLiteral(R"(^Wimpy set to: ([\d,]+)\.?$)")};

NODISCARD bool isWeakSheetLine(const QString &text)
{
    return g_coins.match(text).hasMatch()
           || text.startsWith(QStringLiteral("You have reached the highest level"));
}

NODISCARD bool isSheetInfoOnly(const QString &text)
{
    for (const QRegularExpression &re : g_sheetInfoOnly) {
        if (re.match(text).hasMatch()) {
            return true;
        }
    }
    return false;
}

NODISCARD bool isAlignment(const QString &text)
{
    for (const QRegularExpression &re : g_alignments) {
        if (re.match(text).hasMatch()) {
            return true;
        }
    }
    return false;
}

/// Reads what a line of g_sheetInfoOnly states. A wording none of these knows gives nothing,
/// and still keeps the sheet open.
void readSheetInfo(const QString &text, CharScore &sheet)
{
    if (const auto m = g_sexRace.match(text); m.hasMatch()) {
        sheet.sex = m.captured(1);
        sheet.race = m.captured(2).trimmed();
        return;
    }
    if (const auto m = g_age.match(text); m.hasMatch()) {
        sheet.ageYears = captured(m, 1);
        auto it = g_agePart.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch part = it.next();
            if (part.captured(2) == QStringLiteral("month")) {
                sheet.ageMonths = captured(part, 1);
            } else {
                sheet.ageDays = captured(part, 1);
            }
        }
        return;
    }
    if (const auto m = g_played.match(text); m.hasMatch()) {
        sheet.played = m.captured(1).trimmed();
        if (const auto session = g_session.match(text); session.hasMatch()) {
            sheet.session = session.captured(1).trimmed();
        }
        return;
    }
    if (const auto m = g_rank.match(text); m.hasMatch()) {
        sheet.name = m.captured(1);
        sheet.title = m.captured(2).trimmed();
        sheet.level = captured(m, 3);
        return;
    }
    if (const auto m = g_heightWeight.match(text); m.hasMatch()) {
        sheet.height = m.captured(1).trimmed();
        sheet.weight = m.captured(2).trimmed();
        return;
    }
    if (text.startsWith(QStringLiteral("Perception:"))) {
        if (const auto m = g_perception.match(text); m.hasMatch()) {
            sheet.vision = captured(m, 1);
            sheet.hearing = captured(m, 2);
            sheet.smell = captured(m, 3);
        }
        if (const auto alert = g_alertness.match(text); alert.hasMatch()) {
            sheet.alertness = alert.captured(1).trimmed().toLower();
        }
        return;
    }
    if (const auto m = g_welcome.match(text); m.hasMatch()) {
        const QStringList places = m.captured(1).split(g_welcomeSeparator, Qt::SkipEmptyParts);
        for (const QString &place : places) {
            const QString name = place.trimmed();
            if (!name.isEmpty() && !sheet.welcome.contains(name)) {
                sheet.welcome.append(name);
            }
        }
    }
}

} // namespace

// -- free functions ------------------------------------------------------------------------

std::optional<int64_t> parseNumberWords(const QString &words)
{
    static const std::array<const char *, 20> units{"zero",    "one",       "two",      "three",
                                                    "four",    "five",      "six",      "seven",
                                                    "eight",   "nine",      "ten",      "eleven",
                                                    "twelve",  "thirteen",  "fourteen", "fifteen",
                                                    "sixteen", "seventeen", "eighteen", "nineteen"};
    static const std::array<const char *, 10>
        tens{"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    const QStringList parts = words.toLower().split(QRegularExpression{QStringLiteral("[\\s-]+")},
                                                    Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return std::nullopt;
    }
    int64_t total = 0;
    int64_t current = 0;
    for (const QString &part : parts) {
        if (part == QStringLiteral("and")) {
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < units.size() && !found; ++i) {
            if (part == QLatin1String(units[i])) {
                current += static_cast<int64_t>(i);
                found = true;
            }
        }
        for (size_t i = 2; i < tens.size() && !found; ++i) {
            if (part == QLatin1String(tens[i])) {
                current += static_cast<int64_t>(i * 10);
                found = true;
            }
        }
        if (!found && part == QStringLiteral("hundred")) {
            current = (current == 0 ? 1 : current) * 100;
            found = true;
        }
        if (!found && part == QStringLiteral("thousand")) {
            total += (current == 0 ? 1 : current) * 1000;
            current = 0;
            found = true;
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return total + current;
}

std::optional<CharBurden> parseBurdenLine(const QString &line)
{
    const QString text = cleaned(line);
    CharBurden burden;
    if (const auto m = g_burden.match(text); m.hasMatch()) {
        const QString said = m.captured(1);
        if (said == QStringLiteral("nothing")) {
            burden.pounds = 0;
        } else {
            QString words = said;
            words.remove(QRegularExpression{QStringLiteral(R"(\s+pounds?$)")});
            const std::optional<int64_t> pounds = parseNumberWords(words);
            if (!pounds.has_value()) {
                return std::nullopt;
            }
            burden.pounds = *pounds;
        }
        burden.word = m.captured(2).trimmed();
    } else if (const auto old = g_burdenOld.match(text); old.hasMatch()) {
        const std::optional<int64_t> pounds = toNumber(old.captured(1));
        if (!pounds.has_value()) {
            return std::nullopt;
        }
        burden.pounds = *pounds;
        burden.word = old.captured(2).trimmed();
    } else {
        return std::nullopt;
    }
    burden.text = text;
    return burden;
}

std::optional<CharScore> parseScoreLine(const QString &line)
{
    const QString text = cleaned(line);
    const QRegularExpressionMatch m = g_scoreLine.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    CharScore score;
    score.reply = QStringLiteral("score");
    score.hp = captured(m, 1);
    score.maxhp = captured(m, 2);
    score.mana = captured(m, 3);
    score.maxmana = captured(m, 4);
    score.mp = captured(m, 5);
    score.maxmp = captured(m, 6);
    score.text = text;
    return score;
}

std::optional<CharLevel> parseCharLevelLine(const QString &line)
{
    const QString text = cleaned(line);
    if (!text.startsWith(QStringLiteral("MMXP "))) {
        return std::nullopt;
    }
    const QRegularExpressionMatch m = g_levelLine.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const std::optional<int64_t> level = captured(m, 1);
    if (!level.has_value()) {
        return std::nullopt;
    }
    CharLevel result;
    result.level = *level;
    result.xp = captured(m, 2);
    result.neededXp = captured(m, 3);
    result.tp = captured(m, 4);
    result.neededTp = captured(m, 5);
    result.text = text;
    return result;
}

std::optional<CharWimpy> parseWimpyLine(const QString &line)
{
    const QString text = cleaned(line);
    if (!text.startsWith(QStringLiteral("Wimpy set to: "))) {
        return std::nullopt;
    }
    const QRegularExpressionMatch m = g_wimpySet.match(text);
    const std::optional<int64_t> wimpy = m.hasMatch() ? captured(m, 1) : std::nullopt;
    if (!wimpy.has_value()) {
        return std::nullopt;
    }
    return CharWimpy{*wimpy};
}

void CharReplies::append(CharReplies &&other)
{
    for (const CharWimpy &wimpy : other.wimpies) {
        wimpies.push_back(wimpy);
    }
    for (auto &stat : other.stats) {
        stats.push_back(std::move(stat));
    }
    for (auto &score : other.scores) {
        scores.push_back(std::move(score));
    }
    for (auto &burden : other.burdens) {
        burdens.push_back(std::move(burden));
    }
}

// -- the tracker ---------------------------------------------------------------------------

CharReplies CharLinesTracker::receiveLine(const QString &line)
{
    CharReplies out;
    const QString text = cleaned(line);

    // MumeXmlParser takes the level line out before it gets here; should one arrive anyway it
    // belongs to no reply, and does not count against an open sheet.
    if (text.startsWith(QStringLiteral("MMXP ")) && parseCharLevelLine(text).has_value()) {
        return out;
    }

    if (m_stat.has_value()) {
        if (acceptStat(text)) {
            return out;
        }
        out.append(closeStat());
    }

    // The reply to `change wimpy`: a line of its own, which is no part of an open sheet and
    // does not count against it.
    if (const auto wimpy = parseWimpyLine(text)) {
        out.wimpies.push_back(*wimpy);
        return out;
    }

    if (g_statFirst.match(text).hasMatch()) {
        out.append(closeSheet());
        m_stat.emplace();
        m_statLines = QStringList{text};
        m_statSection = StatSectionEnum::HEAD;
        m_statHasSecondLine = false;
        readStatFirst(text, *m_stat);
        return out;
    }

    if (auto score = parseScoreLine(text)) {
        out.append(closeSheet());
        out.scores.push_back(std::move(*score));
        return out;
    }

    if (m_sheet.has_value()) {
        if (text.isEmpty()) {
            if (m_sheetInEffects) {
                out.append(closeSheet());
            } else {
                m_sheetLines.append(text);
            }
            return out;
        }
        if (readSheetLine(text, out)) {
            m_sheetLines.append(text);
            m_sheetStrangers = 0;
            if (m_sheetLines.size() > MAX_REPLY_LINES) {
                out.append(closeSheet());
            }
            return out;
        }
        if (m_sheetInEffects) {
            // The list ended without its blank line; what follows is something else.
            out.append(closeSheet());
            return out;
        }
        m_sheetLines.append(text);
        if (++m_sheetStrangers >= MAX_SHEET_STRANGERS) {
            out.append(closeSheet());
        }
        return out;
    }

    if (text.isEmpty() || isWeakSheetLine(text)) {
        return out;
    }
    // Any line of the sheet opens it: MUME's `info` starts with the race, but a client that
    // gags the first lines, or a reply cut short, still gives what is left; old sheets begin
    // with the language. The coins only count inside it: "You have 1,000 gold coins" alone may
    // answer something else. (A lone "You will swim if necessary." answering `swim` is
    // published: it states the same thing.)
    m_sheet.emplace();
    m_sheet->reply = QStringLiteral("info");
    m_sheetLines.clear();
    m_sheetInEffects = false;
    m_sheetAfterScored = false;
    m_sheetAfterPerception = false;
    m_sheetHasFigure = false;
    m_sheetStrangers = 0;
    if (readSheetLine(text, out)) {
        m_sheetLines.append(text);
    } else {
        m_sheet.reset();
    }
    return out;
}

bool CharLinesTracker::acceptStat(const QString &text)
{
    CharStat &stat = *m_stat;
    if (m_statLines.size() > MAX_REPLY_LINES) {
        return false;
    }
    if (text.isEmpty()) {
        // Some logs have a blank line between the first two lines; a blank after anything
        // else ends the reply.
        if (m_statLines.size() == 1 && !m_statHasSecondLine) {
            return true;
        }
        return false;
    }
    if (!m_statHasSecondLine && m_statSection == StatSectionEnum::HEAD
        && g_statSecond.match(text).hasMatch()) {
        readStatSecond(text, stat);
        m_statHasSecondLine = true;
        m_statLines.append(text);
        return true;
    }
    if (text == QStringLiteral("Affected by:")) {
        m_statSection = StatSectionEnum::AFFECTS;
        m_statLines.append(text);
        return true;
    }
    if (text == QStringLiteral("Timers:") || text == QStringLiteral("Countdowns:")) {
        m_statSection = StatSectionEnum::PROXY; // Pandora's, not MUME's
        m_statLines.append(text);
        return true;
    }
    if (text.startsWith(QStringLiteral("- "))) {
        if (m_statSection == StatSectionEnum::AFFECTS) {
            addListed(text, stat.affects, stat.wounds);
        }
        m_statLines.append(text);
        return true;
    }
    if (text.startsWith(QStringLiteral("Exp: "))
        && text.contains(QStringLiteral("from last score"))) {
        m_statLines.append(text); // a client's addition in some logs
        return true;
    }
    return false;
}

CharReplies CharLinesTracker::closeStat()
{
    CharReplies out;
    if (!m_stat.has_value()) {
        return out;
    }
    CharStat stat = std::move(*m_stat);
    m_stat.reset();
    stat.text = m_statLines.join(QLatin1Char('\n'));
    m_statLines.clear();
    m_statSection = StatSectionEnum::HEAD;
    m_statHasSecondLine = false;
    if (stat.wimpy.has_value()) {
        out.wimpies.push_back(CharWimpy{*stat.wimpy});
    }
    out.stats.push_back(std::move(stat));
    return out;
}

bool CharLinesTracker::readSheetLine(const QString &text, CharReplies &out)
{
    CharScore &sheet = *m_sheet;
    const bool afterScored = std::exchange(m_sheetAfterScored, false);
    const bool afterPerception = std::exchange(m_sheetAfterPerception, false);

    if (m_sheetInEffects) {
        if (text.startsWith(QStringLiteral("- "))) {
            addListed(text, sheet.effects, sheet.wounds);
            return true;
        }
        return false;
    }
    if (auto burden = parseBurdenLine(text)) {
        out.burdens.push_back(std::move(*burden));
        return true;
    }
    if (isSheetInfoOnly(text)) {
        readSheetInfo(text, sheet);
        m_sheetAfterPerception = text.startsWith(QStringLiteral("Perception:"));
        return true;
    }
    if (g_abilities.match(text).hasMatch()) {
        static const std::array<const char *, 7>
            names{"Str", "Int", "Wis", "Dex", "Con", "Wil", "Per"};
        auto it = g_ability.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            for (size_t i = 0; i < names.size(); ++i) {
                if (m.captured(1) == QLatin1String(names[i])) {
                    sheet.abilities[i] = toNumber(m.captured(2));
                }
            }
        }
        m_sheetHasFigure = true;
        return true;
    }
    if (const auto m = g_bonus.match(text); m.hasMatch()) {
        sheet.ob = captured(m, 1);
        sheet.db = captured(m, 2);
        sheet.pb = captured(m, 3);
        m_sheetHasFigure = true;
        return true;
    }
    if (const auto m = g_armour.match(text); m.hasMatch()) {
        sheet.armour = captured(m, 1);
        m_sheetHasFigure = true;
        return true;
    }
    if (g_noArmour.match(text).hasMatch()) {
        sheet.armour = 0;
        m_sheetHasFigure = true;
        return true;
    }
    if (g_pools.match(text).hasMatch()) {
        auto it = g_pool.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString pool = m.captured(3);
            if (pool.startsWith(QStringLiteral("hit"))) {
                sheet.hp = captured(m, 1);
                sheet.maxhp = captured(m, 2);
            } else if (pool == QStringLiteral("mana")) {
                sheet.mana = captured(m, 1);
                sheet.maxmana = captured(m, 2);
            } else {
                sheet.mp = captured(m, 1);
                sheet.maxmp = captured(m, 2);
            }
        }
        m_sheetHasFigure = true;
        return true;
    }
    if (const auto m = g_mood.match(text); m.hasMatch()) {
        sheet.mood = m.captured(1).toLower();
        if (m.hasCaptured(2)) {
            sheet.wimpy = captured(m, 2);
        } else if (m.hasCaptured(3)) {
            sheet.wimpy = 0;
        }
        m_sheetHasFigure = true;
        return true;
    }
    if (const auto m = g_scored.match(text); m.hasMatch()) {
        sheet.xp = captured(m, 1);
        sheet.tp = captured(m, 2);
        m_sheetAfterScored = true;
        m_sheetHasFigure = true;
        return true;
    }
    if (const auto m = g_need.match(text); m.hasMatch()) {
        sheet.neededXp = captured(m, 1);
        sheet.neededTp = captured(m, 2);
        m_sheetHasFigure = true;
        return true;
    }
    if (g_coins.match(text).hasMatch()) {
        auto it = g_coin.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString coin = m.captured(2);
            const std::optional<int64_t> number = toNumber(m.captured(1));
            if (coin == QStringLiteral("gold")) {
                sheet.gold = number;
            } else if (coin == QStringLiteral("silver")) {
                sheet.silver = number;
            } else {
                sheet.copper = number;
            }
        }
        m_sheetHasFigure = true;
        return true;
    }
    if (const auto m = g_speaking.match(text); m.hasMatch()) {
        sheet.language = m.captured(1);
        m_sheetHasFigure = true;
        return true;
    }
    if (g_swim.match(text).hasMatch()) {
        sheet.swim = text;
        m_sheetHasFigure = true;
        return true;
    }
    if (g_climb.match(text).hasMatch()) {
        sheet.climb = text;
        m_sheetHasFigure = true;
        return true;
    }
    if (g_effects.match(text).hasMatch()) {
        m_sheetInEffects = true;
        sheet.effectsKnown = true;
        return true;
    }
    if (g_war.match(text).hasMatch()) {
        // Its own field, and `renown` below as before when it comes after the experience line.
        sheet.war = text;
        if (!afterScored) {
            return true;
        }
    }
    if (afterScored) {
        // The renown line has many wordings, and older ones carry no "(N wp)"; it is whatever
        // comes straight after the experience line.
        QString renown = text;
        if (const auto m = g_renownWp.match(renown); m.hasMatch()) {
            sheet.wp = captured(m, 1);
            renown.remove(g_renownWp);
        }
        sheet.renown = renown.trimmed();
        m_sheetHasFigure = true;
        return true;
    }
    // Last, so that it takes no line another reader knows. Like the lines of g_sheetInfoOnly
    // it states no figure: a sheet of nothing else is not published.
    if (isAlignment(text)
        || (afterPerception && text.startsWith(QStringLiteral("You "))
            && (text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char('!'))))) {
        sheet.alignment = text;
        return true;
    }
    return false;
}

CharReplies CharLinesTracker::closeSheet()
{
    CharReplies out;
    if (!m_sheet.has_value()) {
        return out;
    }
    CharScore sheet = std::move(*m_sheet);
    m_sheet.reset();
    while (!m_sheetLines.isEmpty() && m_sheetLines.back().isEmpty()) {
        m_sheetLines.removeLast();
    }
    sheet.text = m_sheetLines.join(QLatin1Char('\n'));
    m_sheetLines.clear();
    m_sheetInEffects = false;
    m_sheetAfterScored = false;
    m_sheetAfterPerception = false;
    m_sheetStrangers = 0;
    if (std::exchange(m_sheetHasFigure, false)) {
        if (sheet.wimpy.has_value()) {
            out.wimpies.push_back(CharWimpy{*sheet.wimpy});
        }
        out.scores.push_back(std::move(sheet));
    }
    return out;
}

CharReplies CharLinesTracker::receivePrompt()
{
    CharReplies out = closeStat();
    // At the prompt the sheet is complete: without an effects list it had none. Only a real
    // sheet says so, not a lone "You will swim if necessary." answering `swim`.
    if (m_sheet.has_value()) {
        const CharScore &sheet = *m_sheet;
        if (sheet.ob.has_value() || sheet.armour.has_value() || sheet.hp.has_value()
            || sheet.xp.has_value() || !sheet.mood.isEmpty()) {
            m_sheet->effectsKnown = true;
        }
    }
    out.append(closeSheet());
    return out;
}

void CharLinesTracker::reset()
{
    m_stat.reset();
    m_statLines.clear();
    m_statSection = StatSectionEnum::HEAD;
    m_statHasSecondLine = false;
    m_sheet.reset();
    m_sheetLines.clear();
    m_sheetInEffects = false;
    m_sheetAfterScored = false;
    m_sheetAfterPerception = false;
    m_sheetHasFigure = false;
    m_sheetStrangers = 0;
}
