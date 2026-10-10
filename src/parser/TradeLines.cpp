// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TradeLines.h"

#include "../global/parserutils.h"
#include "../observer/gameobserver.h"
#include "CharLines.h"

#include <array>
#include <tuple>
#include <utility>

#include <QRegularExpression>

namespace {

/// Bounds a reply that never closes: `trop` of an old character runs to a few hundred lines.
constexpr int MAX_REPLY_LINES = 2000;
/// Lines that are none of a table's own before it is taken to have ended without a prompt:
/// someone arriving, a tell, a spell wearing off.
constexpr int MAX_STRANGERS = 8;
/// A keeper's tell, the "You sell" list and the retire request wrap at MUME's line width; they
/// are continued for at most this many lines.
constexpr int MAX_CONTINUED_LINES = 6;
/// The `list` and `prac <name>` commands remembered for the replies still to come.
constexpr size_t MAX_QUEUED_COMMANDS = 32;

/// The line with colour and the line ending removed, the spacing inside kept: the tables are
/// laid out in columns.
NODISCARD QString rawLine(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    while (!text.isEmpty() && text.back().isSpace()) {
        text.chop(1);
    }
    return text;
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

NODISCARD bool isNumberWordPart(const QString &part)
{
    static const std::array<const char *, 30> words{"zero",    "one",       "two",      "three",
                                                    "four",    "five",      "six",      "seven",
                                                    "eight",   "nine",      "ten",      "eleven",
                                                    "twelve",  "thirteen",  "fourteen", "fifteen",
                                                    "sixteen", "seventeen", "eighteen", "nineteen",
                                                    "twenty",  "thirty",    "forty",    "fifty",
                                                    "sixty",   "seventy",   "eighty",   "ninety",
                                                    "hundred", "thousand"};
    for (const char *const word : words) {
        if (part == QLatin1String(word)) {
            return true;
        }
    }
    return false;
}

/// "forty-one", "eleven", "hundred": one token of a number written in words.
NODISCARD bool isNumberWord(const QString &token)
{
    const QStringList parts = token.toLower().split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }
    for (const QString &part : parts) {
        if (!isNumberWordPart(part)) {
            return false;
        }
    }
    return true;
}

/// A count in digits or words, negative after a minus sign or "minus": "eleven", "-3", "0".
NODISCARD std::optional<int64_t> parseCount(const QString &words)
{
    const QString text = words.trimmed();
    if (const std::optional<int64_t> number = toNumber(text)) {
        return number;
    }
    static const QRegularExpression negative{QStringLiteral(R"(^(?:minus|negative)\s+)"),
                                             QRegularExpression::CaseInsensitiveOption};
    QString rest = text;
    const bool minus = rest.contains(negative);
    rest.remove(negative);
    const QStringList tokens = rest.split(QRegularExpression{QStringLiteral(R"(\s+)")},
                                          Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return std::nullopt;
    }
    for (const QString &token : tokens) {
        if (!isNumberWord(token) && token.compare(QStringLiteral("and"), Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
    }
    const std::optional<int64_t> value = parseNumberWords(rest);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return minus ? -*value : *value;
}

// -- the pager -----------------------------------------------------------------------------

// "*** Return: continue, b: back, r: redisplay, q: quit (73%) ***", "b: back one page",
// "q:quit", and ", >: bottom" before the percentage.
const QRegularExpression g_pager{
    QStringLiteral(R"(^\s*\*\*\* Return: continue,[^*]*?\bq: ?quit\b[^*(]*\((\d{1,3})%\) \*\*\*)")};

/// The pager at the front of `text`, and how many characters it takes there.
NODISCARD std::optional<PagerLine> matchPager(const QString &text, qsizetype &length)
{
    const QRegularExpressionMatch m = g_pager.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    PagerLine pager;
    pager.percent = m.captured(1).toInt();
    pager.text = m.captured(0).trimmed();
    length = m.capturedEnd(0);
    return pager;
}

// -- money ---------------------------------------------------------------------------------

struct NODISCARD Token final
{
    QString word;
    qsizetype start = 0;
    qsizetype end = 0;
};

NODISCARD std::vector<Token> tokenize(const QString &text)
{
    static const QRegularExpression
        token{QStringLiteral(
                  R"(\d{1,3}(?:,\d{3})+(?![\d])|\d+[gsc]?(?![\w])|[A-Za-z]+(?:-[A-Za-z]+)*|,)"),
              QRegularExpression::CaseInsensitiveOption};
    std::vector<Token> tokens;
    auto it = token.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        tokens.push_back(Token{m.captured(0).toLower(), m.capturedStart(0), m.capturedEnd(0)});
    }
    return tokens;
}

/// Copper per coin of that word; 0 when the word is no coin.
NODISCARD int64_t coinValue(const QString &word)
{
    if (word == QStringLiteral("gold") || word == QStringLiteral("lauren")
        || word == QStringLiteral("g")) {
        return 2000;
    }
    if (word == QStringLiteral("silver") || word == QStringLiteral("celeb")
        || word == QStringLiteral("s")) {
        return 100;
    }
    if (word == QStringLiteral("copper") || word == QStringLiteral("busc")
        || word == QStringLiteral("c")) {
        return 1;
    }
    return 0;
}

NODISCARD bool isCoinNoun(const QString &word)
{
    return word == QStringLiteral("coins") || word == QStringLiteral("coin")
           || word == QStringLiteral("pennies") || word == QStringLiteral("penny")
           || word == QStringLiteral("pieces") || word == QStringLiteral("piece");
}

/// A number starting at tokens[i]: digits, or a run of number words ("forty-one", "one
/// hundred and five", "a"), and the index after it.
NODISCARD std::optional<int64_t> readNumber(const std::vector<Token> &tokens, size_t &i)
{
    if (i >= tokens.size()) {
        return std::nullopt;
    }
    const QString &first = tokens[i].word;
    if (first.front().isDigit()) {
        if (first.back().isLetter()) {
            return std::nullopt; // "1g" is read with its coin
        }
        const std::optional<int64_t> number = toNumber(first);
        if (number.has_value()) {
            ++i;
        }
        return number;
    }
    // "a gold coin", but not "a silver rod".
    if ((first == QStringLiteral("a") || first == QStringLiteral("an")) && i + 2 < tokens.size()
        && coinValue(tokens[i + 1].word) > 0 && isCoinNoun(tokens[i + 2].word)) {
        ++i;
        return 1;
    }
    size_t j = i;
    QStringList words;
    while (j < tokens.size()) {
        const QString &word = tokens[j].word;
        if (isNumberWord(word)) {
            words.append(word);
            ++j;
            continue;
        }
        // "one hundred and five": an "and" inside a number follows hundred or thousand.
        if (word == QStringLiteral("and") && !words.isEmpty()
            && (words.back() == QStringLiteral("hundred")
                || words.back() == QStringLiteral("thousand"))
            && j + 1 < tokens.size() && isNumberWord(tokens[j + 1].word)) {
            words.append(word);
            ++j;
            continue;
        }
        break;
    }
    if (words.isEmpty()) {
        return std::nullopt;
    }
    const std::optional<int64_t> number = parseNumberWords(words.join(QLatin1Char(' ')));
    if (number.has_value()) {
        i = j;
    }
    return number;
}

/// An amount starting at tokens[i], and the index after its last coin.
NODISCARD std::optional<int64_t> readAmount(const std::vector<Token> &tokens,
                                            const size_t i,
                                            size_t &end)
{
    int64_t total = 0;
    int groups = 0;
    size_t j = i;
    while (j < tokens.size()) {
        size_t k = j;
        int64_t value = 0;
        int64_t count = 0;
        const QString &word = tokens[k].word;
        if (word.front().isDigit() && word.back().isLetter()) {
            // "1g", "2s", "40c"
            const std::optional<int64_t> number = toNumber(word.chopped(1));
            value = coinValue(word.right(1));
            if (!number.has_value() || value == 0) {
                break;
            }
            count = *number;
            ++k;
        } else {
            const std::optional<int64_t> number = readNumber(tokens, k);
            if (!number.has_value() || k >= tokens.size()) {
                break;
            }
            value = coinValue(tokens[k].word);
            if (value == 0) {
                break;
            }
            count = *number;
            ++k;
            if (k < tokens.size() && isCoinNoun(tokens[k].word)) {
                ++k;
            }
        }
        total += count * value;
        ++groups;
        end = k;
        j = k;
        // "4 gold 17 silver", "two gold and 11 silver", "15 gold, eleven silver, and 47 copper"
        if (j < tokens.size() && tokens[j].word == QStringLiteral(",")) {
            ++j;
        }
        if (j < tokens.size() && tokens[j].word == QStringLiteral("and")) {
            ++j;
        }
    }
    if (groups == 0) {
        return std::nullopt;
    }
    return total;
}

// -- shops ---------------------------------------------------------------------------------

const QRegularExpression g_listHeader{QStringLiteral(R"(^You can buy:$)")};
const QRegularExpression g_listNone{QStringLiteral(R"(^There are no such things for sale\.$)")};
const QRegularExpression g_listRow{QStringLiteral(R"(^(\d+)\. (.+?) up to (.+?)\.?$)")};
// A row without "up to" has never been seen; kept rather than dropped.
const QRegularExpression g_listRowPlain{QStringLiteral(R"(^(\d+)\. (.+?)\.?$)")};
const QRegularExpression g_listSeparator{QStringLiteral(R"(^-{3,}$)")};
const QRegularExpression g_brackets{QStringLiteral(R"(^(.*?)\s*\(([^()]*)\)$)")};
const QRegularExpression g_dealMiss{QStringLiteral(R"(^There is no such thing for sale\.$)")};
const QRegularExpression g_boughtLine{QStringLiteral(R"(^You now have (.+?)\.$)")};
// "You now have 10,300,000 experience points and ..." after losing a level is no purchase.
const QRegularExpression g_notBought{QStringLiteral(R"(^You now have [\d,]+ )")};
const QRegularExpression g_soldLine{QStringLiteral(R"(^You sell (.+)$)")};
const QRegularExpression g_tell{QStringLiteral(R"(^([^'"]+?) tells you '(.*)$)")};
const QRegularExpression g_closed{QStringLiteral(R"(\bSorry, we are closed\b)"),
                                  QRegularExpression::CaseInsensitiveOption};

// -- inns ----------------------------------------------------------------------------------

const QRegularExpression g_innCost{QStringLiteral(R"(\bIt will cost you (.+?) per day\b)")};
const QRegularExpression g_innLasts{QStringLiteral(R"(^You have enough money for .+$)")};
const QRegularExpression g_retireStart{
    QStringLiteral(R"(^If you (?:retire now|really want to retire)\b)")};
const QRegularExpression g_retireRepeat{QStringLiteral(R"(\brepeat that request\b)")};

// -- guilds --------------------------------------------------------------------------------

const QRegularExpression g_sessionsLeft{
    QStringLiteral(R"(^You have (.+?) practice sessions? left\.$)")};
const QRegularExpression g_teacherLine{
    QStringLiteral(R"(^(.+?) can teach you the (spells|skills) below\.$)")};
// "Spell         Sessions  Knowledge  Difficulty  Advice", without its first heading, and the
// 2015 "Sessions  Knowl.  Diffic.  Advice".
const QRegularExpression g_teacherHeader{QStringLiteral(
    R"(^(?:(Spells?|Skills?)\s+)?Sessions\s+Knowl(?:edge|\.)\s+Diffic(?:ulty|\.)\s+Advice$)")};
/// "spells" or "skills" from the heading's first word, empty without one.
NODISCARD QString teacherKind(const QRegularExpressionMatch &header)
{
    if (!header.hasCaptured(1)) {
        return QString{};
    }
    return header.captured(1).toLower().startsWith(QStringLiteral("spell"))
               ? QStringLiteral("spells")
               : QStringLiteral("skills");
}
const QRegularExpression g_teacherRow{
    QStringLiteral(R"(^(\S.*?)\s+(-?\d+)/\s*(-?\d+)\s+(-?\d+)%\s+(.*)$)")};
// "Skill / Spell  Knowledge  Difficulty  Class  Mana  Casting time" for a character with spells,
// and "Skill  Knowledge  Difficulty  Class" for one with none (mume3d's live test of 2026-10-03:
// a level 2 warrior's quiet `prac` was answered whole and no table was read from it).
const QRegularExpression g_skillsHeader{
    QStringLiteral(R"(^Skill(?:\s*/\s*Spell)?\s+Knowledge\s+Difficulty\b)")};
const QRegularExpression g_rule{QStringLiteral(R"(^-{5,}$)")};
const QRegularExpression g_columns{QStringLiteral(R"(\s{2,})")};
const QRegularExpression g_practised{QStringLiteral(
    R"(^You took (\S+) out of (\S+) sessions? in this (?:skill|spell)\. Your knowledge is now (-?\d+)%\.$)")};
// "You have to stand ..." (elvenrunes), and the level's limit: "You need to be more experienced
// before practicing this skill further." (mume3d's live test of 2026-10-03, 13:58: read as no reply
// before, so the client said the practice failed although MUME had answered).
const QRegularExpression g_practiseRefused{QStringLiteral(
    R"(^(?:You have to stand in order to practi[cs]e anything|You need to be more experienced before practi[cs]ing this (?:skill|spell) further)\.$)")};

// -- trophies ------------------------------------------------------------------------------

const QRegularExpression g_trophyHeader{QStringLiteral(R"(\*\*\* TROPHY \*\*\* \()")};
const QRegularExpression g_trophyRowStart{QStringLiteral(R"(^\s*\d[\d,]*,\s*-?\d+%,)")};
const QRegularExpression g_trophyCell{
    QStringLiteral(R"((\d[\d,]*),\s*(-?\d+)%,\s*([^|]*?)\s*(?:\||$))")};
const QRegularExpression g_trophyTotal{
    QStringLiteral(R"(^Total (?:matching )?kills: ([\d,]+) \(([\d,]+) distinct\)\.?$)")};

const std::array<const char *, 8> g_difficulties{"Extremely hard",
                                                 "Extremely easy",
                                                 "Very hard",
                                                 "Very easy",
                                                 "Impossible",
                                                 "Normal",
                                                 "Hard",
                                                 "Easy"};
const std::array<const char *, 8>
    g_knowledgeWords{"Very good", "Excellent", "Average", "Superb", "Good", "Fair", "Poor", "Bad"};

NODISCARD bool isKnown(const QString &word, const auto &known)
{
    for (const char *const k : known) {
        if (word.compare(QLatin1String(k), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

/// "Normal      Hard to improve" as the difficulty and the advice.
void splitDifficulty(const QString &rest, QString &difficulty, QString &advice)
{
    for (const char *const known : g_difficulties) {
        const QLatin1String word{known};
        if (rest.startsWith(word, Qt::CaseInsensitive)
            && (rest.size() == word.size() || rest.at(word.size()).isSpace())) {
            difficulty = rest.left(word.size());
            advice = rest.mid(word.size()).simplified();
            return;
        }
    }
    // A difficulty not seen before: the columns are two or more spaces apart.
    const QStringList columns = rest.split(g_columns, Qt::SkipEmptyParts);
    if (columns.size() >= 2) {
        difficulty = columns.front().simplified();
        advice = columns.mid(1).join(QLatin1Char(' ')).simplified();
        return;
    }
    difficulty = rest.section(QLatin1Char(' '), 0, 0);
    advice = rest.section(QLatin1Char(' '), 1).simplified();
}

/// "helms" as "helm", for the last word of an item's name.
NODISCARD QString singularWord(const QString &word)
{
    static const std::array<std::pair<const char *, const char *>, 20> irregular{
        {{"knives", "knife"},   {"staves", "staff"},  {"loaves", "loaf"},      {"wolves", "wolf"},
         {"halves", "half"},    {"elves", "elf"},     {"dwarves", "dwarf"},    {"leaves", "leaf"},
         {"thieves", "thief"},  {"scarves", "scarf"}, {"men", "man"},          {"women", "woman"},
         {"teeth", "tooth"},    {"feet", "foot"},     {"geese", "goose"},      {"mice", "mouse"},
         {"children", "child"}, {"axes", "axe"},      {"pickaxes", "pickaxe"}, {"lives", "life"}}};
    const QString lower = word.toLower();
    for (const auto &[plural, singular] : irregular) {
        if (lower == QLatin1String(plural)) {
            return QString::fromLatin1(singular);
        }
    }
    if (lower.endsWith(QStringLiteral("ies")) && lower.size() > 4) {
        return word.chopped(3) + QStringLiteral("y");
    }
    for (const char *const ending : {"ches", "shes", "sses", "xes", "zes"}) {
        if (lower.endsWith(QLatin1String(ending))) {
            return word.chopped(2);
        }
    }
    if (lower.endsWith(QStringLiteral("ss")) || lower.endsWith(QStringLiteral("us"))) {
        return word;
    }
    if (lower.endsWith(QLatin1Char('s')) && lower.size() > 2) {
        return word.chopped(1);
    }
    return word;
}

/// "blood-encrusted helms" as "blood-encrusted helm", "pairs of boots" as "pair of boots".
NODISCARD QString singularName(const QString &name)
{
    const qsizetype of = name.indexOf(QStringLiteral(" of "));
    const QString head = of > 0 ? name.left(of) : name;
    const QString tail = of > 0 ? name.mid(of) : QString{};
    const qsizetype space = head.lastIndexOf(QLatin1Char(' '));
    const QString last = head.mid(space + 1);
    return head.left(space + 1) + singularWord(last) + tail;
}

/// "forty-one blood-encrusted helms (flawless, new)" into the row's count, names, condition and
/// age.
void readListItem(const QString &item, ShopRow &row)
{
    QString name = item;
    if (const auto m = g_brackets.match(item); m.hasMatch()) {
        name = m.captured(1);
        const QStringList parts = m.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            row.condition = parts.front().trimmed();
        }
        if (parts.size() >= 2) {
            row.age = parts.mid(1).join(QLatin1Char(',')).trimmed();
        }
    }
    QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    row.count = 1;
    // The longest run of number words at the front: "thirty-two", "one hundred and five".
    qsizetype taken = 0;
    std::optional<int64_t> count;
    if (!words.isEmpty()) {
        if (const std::optional<int64_t> digits = toNumber(words.front())) {
            count = digits;
            taken = 1;
        } else if (words.front().compare(QStringLiteral("a"), Qt::CaseInsensitive) == 0
                   || words.front().compare(QStringLiteral("an"), Qt::CaseInsensitive) == 0) {
            count = 1;
            taken = 1;
        } else {
            for (qsizetype n = std::min<qsizetype>(words.size() - 1, 5); n >= 1; --n) {
                const std::optional<int64_t> value = parseCount(
                    words.mid(0, n).join(QLatin1Char(' ')));
                if (value.has_value()) {
                    count = value;
                    taken = n;
                    break;
                }
            }
        }
    }
    if (count.has_value() && taken < words.size()) {
        row.count = *count;
        words = words.mid(taken);
    }
    row.name = words.join(QLatin1Char(' '));
    row.singular = row.count == 1 ? row.name : singularName(row.name);
}

/// The items of "You sell a metal breastplate, a sturdy pair of greaves, and two metal
/// bucklers."
NODISCARD QStringList soldItems(QString list)
{
    while (list.endsWith(QLatin1Char('.'))) {
        list.chop(1);
    }
    QStringList items;
    if (!list.contains(QStringLiteral(", "))) {
        items = list.split(QStringLiteral(" and "), Qt::SkipEmptyParts);
    } else {
        items = list.split(QStringLiteral(", "), Qt::SkipEmptyParts);
        if (!items.isEmpty() && items.back().startsWith(QStringLiteral("and "))) {
            items.back() = items.back().mid(4);
        }
    }
    for (QString &item : items) {
        item = item.trimmed();
    }
    items.removeAll(QString{});
    return items;
}

NODISCARD QString joined(QStringList lines)
{
    while (!lines.isEmpty() && lines.back().trimmed().isEmpty()) {
        lines.removeLast();
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

std::optional<PagerLine> parsePagerLine(const QString &plainChunk)
{
    const QString text = rawLine(plainChunk);
    qsizetype length = 0;
    std::optional<PagerLine> pager = matchPager(text, length);
    if (!pager.has_value() || !text.mid(length).trimmed().isEmpty()) {
        return std::nullopt;
    }
    return pager;
}

bool stripPagerPrefix(QString &line)
{
    qsizetype length = 0;
    if (!matchPager(line, length).has_value()) {
        return false;
    }
    QString rest = line.mid(length);
    // MUME may wipe the pager line with a carriage return (and spaces) before the next page;
    // otherwise one space follows the closing stars.
    const qsizetype cr = rest.lastIndexOf(QLatin1Char('\r'));
    if (cr >= 0 && rest.left(cr).trimmed().isEmpty()) {
        rest = rest.mid(cr + 1);
    } else if (rest.startsWith(QLatin1Char(' '))) {
        rest = rest.mid(1);
    }
    line = rest;
    return true;
}

std::optional<Money> parseMoney(const QString &words)
{
    QString text = words.trimmed();
    while (text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char('!'))) {
        text.chop(1);
    }
    const std::vector<Token> tokens = tokenize(text);
    if (tokens.empty() || !text.left(tokens.front().start).trimmed().isEmpty()) {
        return std::nullopt;
    }
    size_t end = 0;
    const std::optional<int64_t> copper = readAmount(tokens, 0, end);
    if (!copper.has_value() || end != tokens.size()
        || !text.mid(tokens.back().end).trimmed().isEmpty()) {
        return std::nullopt;
    }
    return Money{*copper, text};
}

std::optional<Money> findMoney(const QString &sentence)
{
    const std::vector<Token> tokens = tokenize(sentence);
    for (size_t i = 0; i < tokens.size(); ++i) {
        size_t end = 0;
        if (const std::optional<int64_t> copper = readAmount(tokens, i, end)) {
            const qsizetype start = tokens[i].start;
            return Money{*copper, sentence.mid(start, tokens[end - 1].end - start)};
        }
    }
    return std::nullopt;
}

void TradeReplies::append(TradeReplies &&other)
{
    auto move = [](auto &to, auto &from) {
        for (auto &x : from) {
            to.push_back(std::move(x));
        }
    };
    move(lists, other.lists);
    move(deals, other.deals);
    move(teachers, other.teachers);
    move(practised, other.practised);
    move(skills, other.skills);
    move(inns, other.inns);
    move(trophies, other.trophies);
}

// -- the tracker ---------------------------------------------------------------------------

void TradeLinesTracker::receiveCommand(const QString &line)
{
    static const QRegularExpression list{QStringLiteral(R"(^lis?t?(?:\s+(.*))?$)"),
                                         QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression prac{QStringLiteral(
                                             R"(^pra(?:c(?:t(?:i(?:[cs]e?)?)?)?)?\s+(.+)$)"),
                                         QRegularExpression::CaseInsensitiveOption};
    const QString command = line.simplified();
    if (const auto m = list.match(command); m.hasMatch()) {
        m_listQueries.push_back(m.captured(1).trimmed());
        while (m_listQueries.size() > MAX_QUEUED_COMMANDS) {
            m_listQueries.pop_front();
        }
    } else if (const auto p = prac.match(command); p.hasMatch()) {
        m_pracNames.push_back(p.captured(1).trimmed());
        while (m_pracNames.size() > MAX_QUEUED_COMMANDS) {
            m_pracNames.pop_front();
        }
    }
}

void TradeLinesTracker::openTable(const TableEnum table, const QString &line)
{
    closeTable();
    m_table = table;
    m_tableHasRow = false;
    m_teacherBeforeHeading = false;
    m_strangers = 0;
    m_lines.clear();
    m_paged = false;
    m_afterPager = false;
    switch (table) {
    case TableEnum::LIST:
        m_list.emplace();
        if (!m_listQueries.empty()) {
            m_list->query = m_listQueries.front();
            m_listQueries.pop_front();
        }
        break;
    case TableEnum::TEACHER:
        m_teacher.emplace();
        m_teacher->sessionsLeft = m_sessionsLeft;
        if (m_sessionsLinePending) {
            m_lines.append(m_sessionsLine);
        }
        break;
    case TableEnum::SKILLS:
        m_skills.emplace();
        m_skills->sessionsLeft = m_sessionsLeft;
        if (m_sessionsLinePending) {
            m_lines.append(m_sessionsLine);
        }
        break;
    case TableEnum::TROPHIES:
        m_trophies.emplace();
        break;
    case TableEnum::NONE:
        break;
    }
    m_sessionsLinePending = false;
    m_lines.append(line);
}

void TradeLinesTracker::markOwnLine(const QString &line)
{
    m_lines.append(line);
    m_strangers = 0;
    if (m_paged) {
        m_afterPager = true;
    }
}

void TradeLinesTracker::closeTable()
{
    const TableEnum table = std::exchange(m_table, TableEnum::NONE);
    const QString text = joined(m_lines);
    const bool complete = !m_paged || m_afterPager;
    switch (table) {
    case TableEnum::LIST:
        if (m_list.has_value()) {
            m_list->text = text;
            m_list->paged = m_paged;
            m_list->complete = complete;
            m_done.lists.push_back(std::move(*m_list));
        }
        break;
    case TableEnum::TEACHER:
        if (m_teacher.has_value()) {
            m_teacher->text = text;
            m_teacher->paged = m_paged;
            m_teacher->complete = complete;
            m_done.teachers.push_back(std::move(*m_teacher));
        }
        break;
    case TableEnum::SKILLS:
        if (m_skills.has_value() && !m_skills->rows.empty()) {
            m_skills->text = text;
            m_skills->paged = m_paged;
            m_skills->complete = complete;
            m_done.skills.push_back(std::move(*m_skills));
        }
        break;
    case TableEnum::TROPHIES:
        if (m_trophies.has_value()) {
            m_trophies->text = text;
            m_trophies->paged = m_paged;
            m_trophies->complete = complete;
            m_done.trophies.push_back(std::move(*m_trophies));
        }
        break;
    case TableEnum::NONE:
        break;
    }
    m_list.reset();
    m_teacher.reset();
    m_skills.reset();
    m_trophies.reset();
    m_lines.clear();
    m_tableHasRow = false;
    m_teacherBeforeHeading = false;
    m_strangers = 0;
    m_paged = false;
    m_afterPager = false;
}

bool TradeLinesTracker::readTableLine(const QString &raw, const QString &text)
{
    if (m_lines.size() > MAX_REPLY_LINES) {
        closeTable();
        return false;
    }
    switch (m_table) {
    case TableEnum::LIST: {
        if (text.isEmpty()) {
            m_lines.append(raw);
            if (blankClosesTable()) {
                closeTable();
            }
            return true;
        }
        if (g_listSeparator.match(text).hasMatch()) {
            markOwnLine(raw);
            m_listGroup += 1;
            return true;
        }
        const QRegularExpressionMatch m = g_listRow.match(text);
        const QRegularExpressionMatch plain = m.hasMatch() ? m : g_listRowPlain.match(text);
        if (!plain.hasMatch()) {
            return false;
        }
        ShopRow row;
        row.number = toNumber(plain.captured(1)).value_or(0);
        readListItem(plain.captured(2), row);
        if (m.hasMatch()) {
            row.priceText = m.captured(3);
            if (const std::optional<Money> price = parseMoney(row.priceText)) {
                row.priceCopper = price->copper;
            }
        }
        row.group = m_listGroup;
        m_list->rows.push_back(std::move(row));
        m_tableHasRow = true;
        markOwnLine(raw);
        return true;
    }
    case TableEnum::TEACHER: {
        if (text.isEmpty()) {
            m_lines.append(raw);
            if (blankClosesTable()) {
                closeTable();
            }
            return true;
        }
        if (const auto h = g_teacherHeader.match(text); h.hasMatch()) {
            if (m_teacher->kind.isEmpty()) {
                m_teacher->kind = teacherKind(h);
            }
            m_teacherBeforeHeading = false;
            markOwnLine(raw);
            return true;
        }
        const QRegularExpressionMatch m = g_teacherRow.match(raw.trimmed());
        if (!m.hasMatch()) {
            return false;
        }
        GuildRow row;
        row.name = m.captured(1).simplified();
        row.used = toNumber(m.captured(2)).value_or(0);
        row.most = toNumber(m.captured(3)).value_or(0);
        row.knowledgePct = toNumber(m.captured(4));
        splitDifficulty(m.captured(5).trimmed(), row.difficulty, row.advice);
        m_teacher->rows.push_back(std::move(row));
        m_tableHasRow = true;
        markOwnLine(raw);
        return true;
    }
    case TableEnum::SKILLS: {
        if (text.isEmpty()) {
            m_lines.append(raw);
            if (blankClosesTable()) {
                closeTable();
            }
            return true;
        }
        if (g_rule.match(text).hasMatch()) {
            markOwnLine(raw);
            return true;
        }
        QStringList columns = raw.trimmed().split(g_columns, Qt::SkipEmptyParts);
        if (columns.size() < 4) {
            return false;
        }
        CharSkillRow row;
        row.name = columns[0].simplified();
        QString knowledge = columns[1].simplified();
        if (row.name.endsWith(QLatin1Char('*'))) {
            row.name.chop(1);
            row.name = row.name.trimmed();
            row.trained = false;
        }
        if (knowledge.startsWith(QLatin1Char('*'))) {
            knowledge = knowledge.mid(1).trimmed();
            row.trained = false;
        }
        row.knowledge = knowledge;
        row.difficulty = columns[2].simplified();
        row.skillClass = columns[3].simplified();
        if (!isKnown(row.knowledge, g_knowledgeWords) && !isKnown(row.difficulty, g_difficulties)) {
            return false;
        }
        if (columns.size() >= 5) {
            // 2006 wrote the two in one column, "30, Very short"
            // (logs/archives/log-2006.04.19-13.48.14.txt:118); today they are two.
            static const QRegularExpression manaAndCasting{QStringLiteral(R"(^(\d+),\s*(.+)$)")};
            if (const auto both = manaAndCasting.match(columns[4].simplified()); both.hasMatch()) {
                row.mana = toNumber(both.captured(1));
                row.casting = both.captured(2);
            } else {
                row.mana = toNumber(columns[4]);
                row.casting = columns.size() >= 6 ? columns[5].simplified() : QString{};
                if (!row.mana.has_value()) {
                    row.casting = columns[4].simplified();
                }
            }
        }
        m_skills->rows.push_back(std::move(row));
        m_tableHasRow = true;
        markOwnLine(raw);
        return true;
    }
    case TableEnum::TROPHIES: {
        if (text.isEmpty()) {
            // A blank line follows the heading and ends the rows; the totals come after it.
            m_lines.append(raw);
            return true;
        }
        if (const auto t = g_trophyTotal.match(text); t.hasMatch()) {
            m_trophies->totalKills = toNumber(t.captured(1));
            m_trophies->distinct = toNumber(t.captured(2));
            markOwnLine(raw);
            closeTable();
            return true;
        }
        if (!g_trophyRowStart.match(raw).hasMatch()) {
            return false;
        }
        auto it = g_trophyCell.globalMatch(raw);
        bool any = false;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QString name = m.captured(3).simplified();
            if (name.isEmpty()) {
                continue;
            }
            TrophyRow row;
            row.kills = toNumber(m.captured(1)).value_or(0);
            row.knowledgePct = toNumber(m.captured(2)).value_or(0);
            if (name.startsWith(QLatin1Char('#'))) {
                row.player = true;
                name = name.mid(1);
            }
            row.name = name;
            m_trophies->rows.push_back(std::move(row));
            any = true;
        }
        if (!any) {
            return false;
        }
        m_tableHasRow = true;
        markOwnLine(raw);
        return true;
    }
    case TableEnum::NONE:
        break;
    }
    return false;
}

void TradeLinesTracker::closeDeal()
{
    if (m_deal.has_value() && !m_deal->items.isEmpty()) {
        m_done.deals.push_back(std::move(*m_deal));
    }
    m_deal.reset();
    m_sellOpen = false;
}

void TradeLinesTracker::readTell(const QString &speaker,
                                 const QString &said,
                                 const QStringList &lines,
                                 TradeReplies & /*out*/)
{
    const QString text = lines.join(QLatin1Char('\n'));
    if (g_closed.match(said).hasMatch()) {
        closeDeal();
        ShopDeal deal;
        deal.kind = ShopDealKindEnum::CLOSED;
        deal.keeper = speaker;
        deal.said = said;
        deal.text = text;
        m_done.deals.push_back(std::move(deal));
        return;
    }
    if (const auto cost = g_innCost.match(said); cost.hasMatch()) {
        InnOffer offer;
        offer.keeper = speaker;
        offer.said = said;
        offer.perDayText = cost.captured(1);
        if (const std::optional<Money> money = findMoney(offer.perDayText)) {
            offer.perDayCopper = money->copper;
        }
        offer.text = text;
        if (m_inn.has_value() && !m_inn->confiscated.isEmpty()) {
            offer.confiscated = m_inn->confiscated;
            offer.text = m_inn->text + QLatin1Char('\n') + text;
        }
        m_inn = std::move(offer);
        return;
    }
    if (said.contains(QStringLiteral("confiscated"), Qt::CaseInsensitive)) {
        if (!m_inn.has_value()) {
            m_inn.emplace();
            m_inn->keeper = speaker;
            m_inn->text = text;
        } else {
            m_inn->text += QLatin1Char('\n') + text;
        }
        m_inn->confiscated = said;
        return;
    }
    const std::optional<Money> money = findMoney(said);
    if (!money.has_value()) {
        return; // anyone else's tell
    }
    closeDeal();
    ShopDeal deal;
    deal.kind = ShopDealKindEnum::BUY;
    deal.keeper = speaker;
    deal.amountCopper = money->copper;
    deal.amountText = money->text;
    deal.said = said;
    deal.text = text;
    m_deal = std::move(deal);
}

void TradeLinesTracker::closeTell(TradeReplies &out)
{
    if (!m_tell.has_value()) {
        return;
    }
    OpenTell tell = std::move(*m_tell);
    m_tell.reset();
    readTell(tell.speaker, tell.said, tell.lines, out);
}

bool TradeLinesTracker::readDealLine(const QString &text, TradeReplies & /*out*/)
{
    if (g_dealMiss.match(text).hasMatch()) {
        closeDeal();
        ShopDeal deal;
        deal.kind = ShopDealKindEnum::MISS;
        deal.text = text;
        m_done.deals.push_back(std::move(deal));
        return true;
    }
    if (const auto m = g_boughtLine.match(text); m.hasMatch()) {
        if (!m_deal.has_value() || g_notBought.match(text).hasMatch()
            || (m_deal->kind == ShopDealKindEnum::SELL && !m_deal->items.isEmpty())) {
            return false;
        }
        m_deal->kind = ShopDealKindEnum::BUY;
        m_deal->items.append(m.captured(1));
        m_deal->text += QLatin1Char('\n') + text;
        return true;
    }
    if (const auto m = g_soldLine.match(text); m.hasMatch()) {
        if (!m_deal.has_value() || !m_deal->items.isEmpty()) {
            // A sale whose tell was not seen is still one; its sum is unknown.
            closeDeal();
            m_deal.emplace();
            m_deal->text = text;
        } else {
            m_deal->text += QLatin1Char('\n') + text;
        }
        m_deal->kind = ShopDealKindEnum::SELL;
        m_sellList = m.captured(1);
        m_sellLines = 1;
        m_sellOpen = !m_sellList.endsWith(QLatin1Char('.'));
        if (!m_sellOpen) {
            m_deal->items = soldItems(m_sellList);
        }
        return true;
    }
    return false;
}

bool TradeLinesTracker::readInnLine(const QString &text, TradeReplies &out)
{
    if (g_innLasts.match(text).hasMatch() && m_inn.has_value()) {
        m_inn->lastsText = text;
        m_inn->text += QLatin1Char('\n') + text;
        out.inns.push_back(std::move(*m_inn));
        m_inn.reset();
        return true;
    }
    if (g_retireStart.match(text).hasMatch()) {
        m_retire = text;
        m_retireLines = 1;
        if (g_retireRepeat.match(text).hasMatch()) {
            closeRetire(out);
        }
        return true;
    }
    return false;
}

void TradeLinesTracker::closeRetire(TradeReplies &out)
{
    if (!m_retire.has_value()) {
        return;
    }
    InnOffer offer;
    offer.retireAsksRepeat = g_retireRepeat.match(*m_retire).hasMatch();
    offer.text = *m_retire;
    m_retire.reset();
    if (offer.retireAsksRepeat) {
        out.inns.push_back(std::move(offer));
    }
}

TradeLineKindEnum TradeLinesTracker::kindOfTable(const TableEnum table)
{
    switch (table) {
    case TableEnum::LIST:
        return TradeLineKindEnum::SHOP;
    case TableEnum::TEACHER:
    case TableEnum::SKILLS:
        return TradeLineKindEnum::GUILD;
    case TableEnum::TROPHIES:
        return TradeLineKindEnum::TROPHIES;
    case TableEnum::NONE:
        break;
    }
    return TradeLineKindEnum::NONE;
}

TradeReplies TradeLinesTracker::receiveLine(const QString &line)
{
    TradeReplies out;
    m_lastKind = TradeLineKindEnum::NONE;
    const QString raw = rawLine(line);
    const QString text = raw.simplified();

    // The continuations of what wraps at MUME's line width come first: they are no reply's
    // first line.
    if (m_tell.has_value()) {
        m_tell->lines.append(raw);
        m_tell->said += QLatin1Char(' ') + text;
        if (m_tell->said.endsWith(QLatin1Char('\''))) {
            m_tell->said.chop(1);
            closeTell(out);
        } else if (m_tell->lines.size() > MAX_CONTINUED_LINES) {
            m_tell.reset();
        }
        return out;
    }
    if (m_sellOpen && m_deal.has_value()) {
        m_sellList += QLatin1Char(' ') + text;
        m_deal->text += QLatin1Char('\n') + text;
        if (m_sellList.endsWith(QLatin1Char('.')) || ++m_sellLines > MAX_CONTINUED_LINES) {
            m_sellOpen = false;
            m_deal->items = soldItems(m_sellList);
        }
        m_lastKind = TradeLineKindEnum::SHOP;
        return out;
    }
    if (m_retire.has_value()) {
        *m_retire += QLatin1Char('\n') + raw;
        if (g_retireRepeat.match(text).hasMatch() || ++m_retireLines > 3) {
            closeRetire(out);
        }
        m_lastKind = TradeLineKindEnum::INN;
        return out;
    }

    // The first lines of the tables.
    if (g_listHeader.match(text).hasMatch()) {
        openTable(TableEnum::LIST, raw);
        m_listGroup = 0;
        m_lastKind = TradeLineKindEnum::SHOP;
        return out;
    }
    if (g_listNone.match(text).hasMatch()) {
        closeTable();
        ShopList none;
        none.empty = true;
        none.text = text;
        if (!m_listQueries.empty()) {
            none.query = m_listQueries.front();
            m_listQueries.pop_front();
        }
        m_done.lists.push_back(std::move(none));
        m_lastKind = TradeLineKindEnum::SHOP;
        return out;
    }
    if (const auto m = g_sessionsLeft.match(text); m.hasMatch()) {
        if (const std::optional<int64_t> left = parseCount(m.captured(1))) {
            closeTable();
            m_sessionsLeft = left;
            m_sessionsLinePending = true;
            m_sessionsLine = raw;
            m_lastKind = TradeLineKindEnum::GUILD;
            return out;
        }
    }
    if (const auto m = g_teacherLine.match(text); m.hasMatch()) {
        openTable(TableEnum::TEACHER, raw);
        m_teacher->teacher = m.captured(1);
        m_teacher->kind = m.captured(2);
        m_teacherBeforeHeading = true;
        m_lastKind = TradeLineKindEnum::GUILD;
        return out;
    }
    if (const auto h = g_teacherHeader.match(text);
        h.hasMatch() && !(m_table == TableEnum::TEACHER && m_teacherBeforeHeading)) {
        // A heading without its "can teach" line opens a table of its own.
        openTable(TableEnum::TEACHER, raw);
        m_teacher->kind = teacherKind(h);
        m_lastKind = TradeLineKindEnum::GUILD;
        return out;
    }
    if (g_skillsHeader.match(text).hasMatch()) {
        openTable(TableEnum::SKILLS, raw);
        m_lastKind = TradeLineKindEnum::GUILD;
        return out;
    }
    if (g_trophyHeader.match(text).hasMatch()) {
        openTable(TableEnum::TROPHIES, raw);
        m_lastKind = TradeLineKindEnum::TROPHIES;
        return out;
    }

    if (m_table != TableEnum::NONE) {
        const TradeLineKindEnum tableKind = kindOfTable(m_table);
        if (readTableLine(raw, text)) {
            m_lastKind = tableKind;
            return out;
        }
        if (m_table != TableEnum::NONE && ++m_strangers > MAX_STRANGERS) {
            closeTable();
        }
    }
    if (!text.isEmpty()) {
        m_sessionsLinePending = false;
    }

    if (const auto m = g_practised.match(text); m.hasMatch()) {
        GuildPractised practised;
        practised.used = parseCount(m.captured(1));
        practised.most = parseCount(m.captured(2));
        practised.knowledgePct = toNumber(m.captured(3));
        practised.text = text;
        if (!m_pracNames.empty()) {
            practised.name = m_pracNames.front();
            m_pracNames.pop_front();
        }
        out.practised.push_back(std::move(practised));
        m_lastKind = TradeLineKindEnum::GUILD;
        return out;
    }
    if (g_practiseRefused.match(text).hasMatch()) {
        GuildPractised refused;
        refused.refused = text;
        refused.text = text;
        if (!m_pracNames.empty()) {
            refused.name = m_pracNames.front();
            m_pracNames.pop_front();
        }
        out.practised.push_back(std::move(refused));
        m_lastKind = TradeLineKindEnum::GUILD;
        return out;
    }

    if (const auto m = g_tell.match(text); m.hasMatch()) {
        OpenTell tell;
        tell.speaker = m.captured(1).trimmed();
        tell.said = m.captured(2);
        tell.lines.append(raw);
        if (tell.said.endsWith(QLatin1Char('\''))) {
            tell.said.chop(1);
            readTell(tell.speaker, tell.said, tell.lines, out);
        } else {
            m_tell = std::move(tell);
        }
        return out;
    }
    if (readDealLine(text, out)) {
        m_lastKind = TradeLineKindEnum::SHOP;
        return out;
    }
    if (readInnLine(text, out)) {
        m_lastKind = TradeLineKindEnum::INN;
    }
    return out;
}

void TradeLinesTracker::receivePager(const PagerLine & /*pager*/)
{
    if (m_table != TableEnum::NONE) {
        m_paged = true;
        m_afterPager = false;
    }
}

TradeReplies TradeLinesTracker::receivePrompt()
{
    TradeReplies out;
    if (m_tell.has_value()) {
        // A tell whose closing quote never came is read as it is.
        closeTell(out);
    }
    if (m_sellOpen && m_deal.has_value()) {
        m_deal->items = soldItems(m_sellList);
    }
    closeDeal();
    closeTable();
    if (m_inn.has_value() && (m_inn->perDayCopper.has_value() || !m_inn->confiscated.isEmpty())) {
        m_done.inns.push_back(std::move(*m_inn));
    }
    m_inn.reset();
    m_retire.reset();
    m_sessionsLeft.reset();
    m_sessionsLinePending = false;
    out.append(std::exchange(m_done, TradeReplies{}));
    return out;
}

void TradeLinesTracker::reset()
{
    m_lastKind = TradeLineKindEnum::NONE;
    m_table = TableEnum::NONE;
    m_tableHasRow = false;
    m_teacherBeforeHeading = false;
    m_strangers = 0;
    m_lines.clear();
    m_paged = false;
    m_afterPager = false;
    m_listGroup = 0;
    m_list.reset();
    m_teacher.reset();
    m_skills.reset();
    m_trophies.reset();
    m_sessionsLeft.reset();
    m_sessionsLinePending = false;
    m_sessionsLine.clear();
    m_tell.reset();
    m_deal.reset();
    m_sellOpen = false;
    m_sellList.clear();
    m_sellLines = 0;
    m_inn.reset();
    m_retire.reset();
    m_retireLines = 0;
    m_done = TradeReplies{};
    m_listQueries.clear();
    m_pracNames.clear();
}

// -- the parser's side ---------------------------------------------------------------------

MudChunk classifyMudChunk(const bool goAhead, const bool backspace, const QString &plain)
{
    MudChunk chunk;
    chunk.plain = plain;
    if (backspace) {
        chunk.kind = MudChunkKindEnum::TWIDDLER;
        return chunk;
    }
    if (goAhead) {
        chunk.pager = parsePagerLine(plain);
        chunk.kind = chunk.pager.has_value() ? MudChunkKindEnum::PAGER : MudChunkKindEnum::PROMPT;
        return chunk;
    }
    chunk.kind = MudChunkKindEnum::LINE;
    qsizetype length = 0;
    if (std::optional<PagerLine> pager = matchPager(plain, length)) {
        if (stripPagerPrefix(chunk.plain)) {
            chunk.pager = std::move(pager);
        }
    }
    return chunk;
}

TradeReaders::TradeReaders(GameObserver &observer)
    : m_observer{observer}
{}

bool TradeReaders::receiveCommand(const QString &line)
{
    if (std::exchange(m_pagerOpen, false)) {
        return false;
    }
    m_tracker.receiveCommand(line);
    return true;
}

MudChunk TradeReaders::beginChunk(const bool goAhead,
                                  const bool backspace,
                                  const QString &plain,
                                  const QuietTrafficEnum traffic)
{
    MudChunk chunk = classifyMudChunk(goAhead, backspace, plain);
    m_chunk = chunk.kind;
    m_quietEnded = false;
    // Before the pager is published, which says whether it was hidden.
    if (m_quiet.isOpen()) {
        hideQuietChunk(chunk, traffic);
    }
    if (chunk.pager.has_value()) {
        m_tracker.receivePager(*chunk.pager);
        // A pager glued to a line was answered before the line came.
        m_pagerOpen = chunk.kind == MudChunkKindEnum::PAGER;
        m_observer.observePager(*chunk.pager);
    } else if (chunk.kind == MudChunkKindEnum::PROMPT) {
        m_pagerOpen = false;
    }
    return chunk;
}

void TradeReaders::hideQuietChunk(MudChunk &chunk, const QuietTrafficEnum traffic)
{
    switch (chunk.kind) {
    case MudChunkKindEnum::LINE: {
        // A chunk with nothing in it for the user (tags alone) is no line for any reader.
        if (chunk.plain.isEmpty()) {
            break;
        }
        // A pager glued to the front of the line came before it, and goes with it.
        const bool pagerHidden = chunk.pager.has_value() && m_quiet.receivePager().hidden;
        const QuietCapture::Verdict verdict = m_quiet.receiveLine(chunk.plain, traffic);
        chunk.hidden = verdict.hidden;
        chunk.captured = verdict.captured;
        if (chunk.pager.has_value()) {
            chunk.pager->hidden = pagerHidden && verdict.hidden;
        }
        break;
    }
    case MudChunkKindEnum::PAGER: {
        const QuietCapture::Verdict verdict = m_quiet.receivePager();
        chunk.hidden = verdict.hidden;
        chunk.pager->hidden = verdict.hidden;
        break;
    }
    case MudChunkKindEnum::PROMPT: {
        const QuietCapture::Verdict verdict = m_quiet.receivePrompt();
        chunk.hidden = verdict.hidden;
        m_quietEnded = verdict.ended;
        break;
    }
    case MudChunkKindEnum::TWIDDLER:
        break;
    }
}

bool TradeReaders::captureQuietLine(const QString &plain)
{
    return m_quiet.isOpen() && m_quiet.receiveLine(plain, QuietTrafficEnum::REPLY).captured;
}

void TradeReaders::receiveLine(const QString &plain)
{
    publish(m_tracker.receiveLine(plain));
}

void TradeReaders::receivePrompt()
{
    if (m_chunk == MudChunkKindEnum::PROMPT) {
        publish(m_tracker.receivePrompt());
    }
}

void TradeReaders::endChunk()
{
    const bool prompt = std::exchange(m_chunk, MudChunkKindEnum::LINE) == MudChunkKindEnum::PROMPT;
    // After every reader's package of this prompt, so that whoever asked finds them published;
    // and before sig2_realPrompt, at which the runner judges.
    if (std::exchange(m_quietEnded, false)) {
        m_observer.observeQuietEnded();
    }
    if (prompt) {
        m_observer.observeRealPrompt();
    }
}

void TradeReaders::reset()
{
    m_tracker.reset();
    m_chunk = MudChunkKindEnum::LINE;
    m_pagerOpen = false;
    m_quiet.reset();
    m_quietEnded = false;
}

void TradeReaders::publish(const TradeReplies &replies)
{
    for (const ShopList &list : replies.lists) {
        m_observer.observeShopList(list);
    }
    for (const ShopDeal &deal : replies.deals) {
        m_observer.observeShopDeal(deal);
    }
    for (const GuildTeacher &teacher : replies.teachers) {
        m_observer.observeGuildTeacher(teacher);
    }
    for (const GuildPractised &practised : replies.practised) {
        m_observer.observeGuildPractised(practised);
    }
    for (const CharSkills &skills : replies.skills) {
        m_observer.observeCharSkills(skills);
    }
    for (const InnOffer &offer : replies.inns) {
        m_observer.observeInnOffer(offer);
    }
    for (const CharTrophies &trophies : replies.trophies) {
        m_observer.observeCharTrophies(trophies);
    }
}
