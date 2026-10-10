// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "LogRules.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>

namespace {

const QChar SENTINEL_NAME{0xE000};
const QChar SENTINEL_QUOTE{0xE002};
const QChar SENTINEL_NUMBER{0xE003};

/// One piece of a template: literal text, or a placeholder (`N`, `O`, `Q`, `#`, `*`), or a run
/// of whitespace (` `).
struct NODISCARD Segment final
{
    bool literal = false;
    QString value;
    char kind = 0;
};

NODISCARD std::vector<Segment> segmentsOf(const QString &text)
{
    std::vector<Segment> segments;
    QString current;
    const auto flush = [&segments, &current]() {
        if (!current.isEmpty()) {
            segments.push_back(Segment{true, current, 0});
            current.clear();
        }
    };
    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size;) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 < size) {
                current += text.at(i + 1);
                i += 2;
            } else {
                current += c;
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('<') && i + 2 < size && text.at(i + 2) == QLatin1Char('>')
            && QStringLiteral("NOQ#").contains(text.at(i + 1))) {
            flush();
            segments.push_back(Segment{false, QString{}, text.at(i + 1).toLatin1()});
            i += 3;
            continue;
        }
        if (c == QLatin1Char('*')) {
            flush();
            segments.push_back(Segment{false, QString{}, '*'});
            ++i;
            continue;
        }
        if (c.isSpace()) {
            flush();
            while (i < size && text.at(i).isSpace()) {
                ++i;
            }
            segments.push_back(Segment{false, QString{}, ' '});
            continue;
        }
        current += c;
        ++i;
    }
    flush();
    return segments;
}

/// The longest run of characters a regular expression needs literally, outside groups and
/// classes; empty when it has an alternative (`|`) anywhere, which could avoid any run.
NODISCARD QString regexLiteral(const QString &re, Qt::CaseSensitivity &caseSensitivity)
{
    caseSensitivity = re.startsWith(QStringLiteral("(?i)")) ? Qt::CaseInsensitive
                                                            : Qt::CaseSensitive;
    if (re.contains(QLatin1Char('|'))) {
        return QString{};
    }
    QString best;
    QString run;
    int depth = 0;
    const auto endRun = [&best, &run]() {
        if (run.size() > best.size()) {
            best = run;
        }
        run.clear();
    };
    const qsizetype size = re.size();
    for (qsizetype i = 0; i < size; ++i) {
        const QChar c = re.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 >= size) {
                break;
            }
            const QChar next = re.at(++i);
            if (next.isLetterOrNumber()) {
                endRun(); // a class or an assertion
            } else if (depth == 0) {
                run += next;
            }
            continue;
        }
        switch (c.unicode()) {
        case '[': {
            endRun();
            ++i;
            if (i < size && re.at(i) == QLatin1Char('^')) {
                ++i;
            }
            if (i < size && re.at(i) == QLatin1Char(']')) {
                ++i;
            }
            while (i < size && re.at(i) != QLatin1Char(']')) {
                if (re.at(i) == QLatin1Char('\\')) {
                    ++i;
                }
                ++i;
            }
            break;
        }
        case '(':
            endRun();
            ++depth;
            break;
        case ')':
            endRun();
            depth = std::max(0, depth - 1);
            break;
        case '?':
        case '*':
        case '{':
            // The character before may be missing.
            if (!run.isEmpty()) {
                run.chop(1);
            }
            endRun();
            if (c == QLatin1Char('{')) {
                while (i < size && re.at(i) != QLatin1Char('}')) {
                    ++i;
                }
            }
            break;
        case '+':
        case '.':
        case '^':
        case '$':
            endRun();
            break;
        default:
            if (depth == 0) {
                run += c;
            } else {
                endRun();
            }
            break;
        }
    }
    endRun();
    return best;
}

NODISCARD QString routeName(const LogRouteEnum route)
{
    return route == LogRouteEnum::SCREEN ? QStringLiteral("screen") : QStringLiteral("log");
}

/// Reads a string or a list of strings.
NODISCARD std::optional<QStringList> stringList(const QJsonValue &value)
{
    if (value.isString()) {
        return QStringList{value.toString()};
    }
    if (!value.isArray()) {
        return std::nullopt;
    }
    QStringList out;
    for (const QJsonValue &one : value.toArray()) {
        if (!one.isString()) {
            return std::nullopt;
        }
        out.append(one.toString());
    }
    return out;
}

/// The distinct words of a line, split at whitespace.
NODISCARD QStringList wordsOf(const QString &plain)
{
    QStringList words;
    qsizetype i = 0;
    const qsizetype size = plain.size();
    while (i < size) {
        while (i < size && plain.at(i).isSpace()) {
            ++i;
        }
        const qsizetype start = i;
        while (i < size && !plain.at(i).isSpace()) {
            ++i;
        }
        if (i > start) {
            QString word = plain.mid(start, i - start);
            if (!words.contains(word)) {
                words.append(std::move(word));
            }
        }
    }
    return words;
}

using PrecedenceKey = std::tuple<bool, bool, int, int, int>;

NODISCARD PrecedenceKey precedenceOf(const LogRule &rule)
{
    return {rule.user, rule.hasPattern, rule.whenCount, rule.literalLength, rule.order};
}

} // namespace

int LogWhen::count() const
{
    return static_cast<int>(inFight.has_value()) + static_cast<int>(grouped.has_value())
           + static_cast<int>(outdoors.has_value()) + static_cast<int>(!race.isEmpty())
           + static_cast<int>(hpBelow.has_value()) + static_cast<int>(hpDrop.has_value())
           + static_cast<int>(!who.isEmpty());
}

bool LogWhen::matches(const LogContext &context, const QString &about) const
{
    if (inFight.has_value() && *inFight != context.inFight) {
        return false;
    }
    if (grouped.has_value() && *grouped != context.grouped) {
        return false;
    }
    if (outdoors.has_value() && *outdoors != context.outdoors) {
        return false;
    }
    if (!race.isEmpty() && (context.race.isEmpty() || !race.contains(context.race))) {
        return false;
    }
    if (hpBelow.has_value()
        && (!context.hpFraction.has_value() || !(*context.hpFraction < *hpBelow))) {
        return false;
    }
    if (hpDrop.has_value() && (!context.hpDrop.has_value() || !(*context.hpDrop >= *hpDrop))) {
        return false;
    }
    if (!who.isEmpty() && !who.contains(about)) {
        return false;
    }
    return true;
}

std::optional<LogTemplate> compileLogTemplate(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    const std::vector<Segment> segments = segmentsOf(trimmed);
    LogTemplate out;
    QString expression = QStringLiteral("^");
    for (const Segment &segment : segments) {
        if (segment.literal) {
            expression += QRegularExpression::escape(segment.value);
            out.literalLength += static_cast<int>(segment.value.size());
            if (segment.value.size() > out.longestWord.size()) {
                out.longestWord = segment.value;
            }
            continue;
        }
        switch (segment.kind) {
        case ' ':
            expression += QStringLiteral(R"(\s+)");
            out.literalLength += 1;
            break;
        case 'N':
        case 'O':
            expression += QStringLiteral("(.+?)");
            break;
        case 'Q':
            expression += QStringLiteral("([^']*)");
            break;
        case '#':
            expression += QStringLiteral(R"((-?\d+(?:[.,]\d+)*))");
            break;
        case '*':
            expression += QStringLiteral(".*");
            break;
        default:
            break;
        }
    }
    expression += QStringLiteral("$");
    out.expression = expression;

    const auto plainWord = [](const Segment &segment) {
        return segment.literal
               && !segment.value.contains(QRegularExpression{QStringLiteral(R"(\s)")});
    };
    const Segment &first = segments.front();
    if (plainWord(first) && (segments.size() == 1 || segments[1].kind == ' ')) {
        out.firstWord = first.value;
    }
    const Segment &last = segments.back();
    if (plainWord(last) && (segments.size() == 1 || segments[segments.size() - 2].kind == ' ')) {
        out.lastWord = last.value;
    }
    for (size_t i = 0; i < segments.size(); ++i) {
        const bool before = i == 0 || segments[i - 1].kind == ' ';
        const bool after = i + 1 == segments.size() || segments[i + 1].kind == ' ';
        if (plainWord(segments[i]) && before && after && !out.words.contains(segments[i].value)) {
            out.words.append(segments[i].value);
        }
    }
    return out;
}

QString suggestLogTemplate(const QString &plain, const QStringList &names)
{
    QString s = plain.trimmed();
    static const QRegularExpression quoted{QStringLiteral(R"('[^']*')")};
    s.replace(quoted, QStringLiteral("'") + SENTINEL_QUOTE + QStringLiteral("'"));
    static const QRegularExpression starred{QStringLiteral(R"(\*[^*\s][^*]*\*)")};
    s.replace(starred, QString{SENTINEL_NAME});

    QStringList sorted;
    for (const QString &name : names) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty() && trimmed.compare(QStringLiteral("you"), Qt::CaseInsensitive) != 0
            && !sorted.contains(trimmed)) {
            sorted.append(trimmed);
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    for (const QString &name : sorted) {
        const QRegularExpression word{QStringLiteral(R"((?<![\w]))")
                                      + QRegularExpression::escape(name)
                                      + QStringLiteral(R"((?![\w]))")};
        s.replace(word, QString{SENTINEL_NAME});
    }
    static const QRegularExpression number{QStringLiteral(R"((?<![\w])-?\d+(?:[.,]\d+)*)")};
    s.replace(number, QString{SENTINEL_NUMBER});

    QString out;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == SENTINEL_NAME) {
            out += QStringLiteral("<N>");
        } else if (c == SENTINEL_QUOTE) {
            out += QStringLiteral("<Q>");
        } else if (c == SENTINEL_NUMBER) {
            out += QStringLiteral("<#>");
        } else if (c == QLatin1Char('\\') || c == QLatin1Char('*')) {
            out += QLatin1Char('\\');
            out += c;
        } else if (c == QLatin1Char('<') && i + 2 < s.size() && s.at(i + 2) == QLatin1Char('>')
                   && QStringLiteral("NOQ#").contains(s.at(i + 1))) {
            out += QStringLiteral("\\<");
        } else {
            out += c;
        }
    }
    return out;
}

std::optional<LogRule> LogRuleSet::parseRule(const QJsonObject &obj,
                                             const bool strict,
                                             QString *const error)
{
    const auto fail = [error](const QString &why) -> std::optional<LogRule> {
        if (error != nullptr) {
            *error = why;
        }
        return std::nullopt;
    };
    LogRule rule;
    rule.json = obj;
    const auto inactive = [&rule](const QString &why) {
        if (rule.inactiveReason.isEmpty()) {
            rule.inactiveReason = why;
        }
    };

    if (obj.contains(QStringLiteral("id"))) {
        const QJsonValue id = obj.value(QStringLiteral("id"));
        if (!id.isString()) {
            return fail(QStringLiteral("'id' must be a string"));
        }
        rule.id = id.toString().trimmed();
    }
    if (obj.contains(QStringLiteral("text"))) {
        const QJsonValue text = obj.value(QStringLiteral("text"));
        if (!text.isString() || text.toString().trimmed().isEmpty()) {
            return fail(QStringLiteral("'text' must be a non-empty string"));
        }
        rule.text = text.toString();
    }
    if (obj.contains(QStringLiteral("regex"))) {
        const QJsonValue regex = obj.value(QStringLiteral("regex"));
        if (!regex.isString() || regex.toString().isEmpty()) {
            return fail(QStringLiteral("'regex' must be a non-empty string"));
        }
        if (strict) {
            return fail(QStringLiteral("'regex' is for MMapper's own defaults; use 'text'"));
        }
        rule.regex = regex.toString();
    }
    if (!rule.text.isEmpty() && !rule.regex.isEmpty()) {
        return fail(QStringLiteral("a rule has 'text' or 'regex', not both"));
    }
    for (const auto &[key, list, mask] :
         {std::tuple<QString, QStringList *, uint64_t *>{QStringLiteral("tags"),
                                                         &rule.tags,
                                                         &rule.tagMask},
          std::tuple<QString, QStringList *, uint64_t *>{QStringLiteral("notTags"),
                                                         &rule.notTags,
                                                         &rule.notTagMask}}) {
        if (!obj.contains(key)) {
            continue;
        }
        const QJsonValue value = obj.value(key);
        if (!value.isArray()) {
            return fail(QStringLiteral("'%1' must be a list of tags").arg(key));
        }
        const auto names = stringList(value);
        if (!names.has_value()) {
            return fail(QStringLiteral("'%1' must be a list of tags").arg(key));
        }
        *list = *names;
        for (const QString &name : *names) {
            if (const auto tag = lineTagFromName(name)) {
                *mask |= LineTagSet::bit(*tag);
            } else if (strict) {
                return fail(QStringLiteral("unknown tag '%1'").arg(name));
            } else {
                inactive(QStringLiteral("tag.%1").arg(name));
            }
        }
    }
    if (rule.text.isEmpty() && rule.regex.isEmpty() && rule.tags.isEmpty()) {
        return fail(QStringLiteral("a rule needs 'text' or 'tags'"));
    }

    const QJsonValue priority = obj.value(QStringLiteral("priority"));
    if (!priority.isDouble()) {
        return fail(QStringLiteral("'priority' must be a number from 0 to 10"));
    }
    const double p = priority.toDouble();
    if (p != static_cast<double>(static_cast<int>(p)) || p < MIN_PRIORITY || p > MAX_PRIORITY) {
        return fail(QStringLiteral("'priority' must be a whole number from 0 to 10"));
    }
    rule.priority = static_cast<int>(p);

    if (obj.contains(QStringLiteral("route"))) {
        const QString route = obj.value(QStringLiteral("route")).toString();
        if (route == QStringLiteral("screen")) {
            rule.route = LogRouteEnum::SCREEN;
        } else if (route != QStringLiteral("log")) {
            return fail(QStringLiteral("'route' must be \"log\" or \"screen\""));
        }
    }
    if (obj.contains(QStringLiteral("created"))) {
        rule.created = obj.value(QStringLiteral("created")).toString();
    }

    if (obj.contains(QStringLiteral("when"))) {
        const QJsonValue whenValue = obj.value(QStringLiteral("when"));
        if (!whenValue.isObject()) {
            return fail(QStringLiteral("'when' must be an object"));
        }
        const QJsonObject when = whenValue.toObject();
        for (auto it = when.begin(); it != when.end(); ++it) {
            const QString key = it.key();
            const QJsonValue value = it.value();
            bool ok = true;
            if (key == QStringLiteral("inFight") || key == QStringLiteral("grouped")
                || key == QStringLiteral("outdoors")) {
                ok = value.isBool();
                if (ok) {
                    std::optional<bool> &slot = key == QStringLiteral("inFight")
                                                    ? rule.when.inFight
                                                    : (key == QStringLiteral("grouped")
                                                           ? rule.when.grouped
                                                           : rule.when.outdoors);
                    slot = value.toBool();
                }
            } else if (key == QStringLiteral("race")) {
                const auto races = stringList(value);
                ok = races.has_value() && !races->isEmpty();
                if (ok) {
                    for (const QString &race : *races) {
                        rule.when.race.append(race.trimmed().toLower());
                    }
                }
            } else if (key == QStringLiteral("hpBelow") || key == QStringLiteral("hpDrop")) {
                ok = value.isDouble() && value.toDouble() >= 0.0 && value.toDouble() <= 1.0;
                if (ok) {
                    (key == QStringLiteral("hpBelow") ? rule.when.hpBelow : rule.when.hpDrop)
                        = value.toDouble();
                }
            } else if (key == QStringLiteral("who")) {
                const auto who = stringList(value);
                ok = who.has_value() && !who->isEmpty();
                if (ok) {
                    for (const QString &one : *who) {
                        if (one != QStringLiteral("you") && one != QStringLiteral("group")
                            && one != QStringLiteral("other")) {
                            ok = false;
                        }
                    }
                    if (ok) {
                        rule.when.who = *who;
                    }
                }
            } else {
                // A state MMapper does not know (swimming, mounted): loaded, never matched.
                if (strict) {
                    return fail(QStringLiteral("unknown 'when' condition '%1'").arg(key));
                }
                inactive(QStringLiteral("when.%1").arg(key));
                continue;
            }
            if (!ok) {
                if (strict) {
                    return fail(QStringLiteral("bad 'when' condition '%1'").arg(key));
                }
                inactive(QStringLiteral("when.%1").arg(key));
            }
        }
    }

    if (!rule.text.isEmpty()) {
        const auto compiled = compileLogTemplate(rule.text);
        if (!compiled.has_value()) {
            return fail(QStringLiteral("'text' is empty"));
        }
        rule.pattern = QRegularExpression{compiled->expression};
        rule.literal = compiled->longestWord;
        rule.literalLength = compiled->literalLength;
        rule.hasPattern = true;
        if (!rule.pattern.isValid()) {
            return fail(QStringLiteral("'text' does not compile"));
        }
    } else if (!rule.regex.isEmpty()) {
        rule.pattern = QRegularExpression{rule.regex};
        rule.hasPattern = true;
        rule.literal = regexLiteral(rule.regex, rule.literalCase);
        rule.literalLength = static_cast<int>(rule.literal.size());
        if (!rule.pattern.isValid()) {
            inactive(QStringLiteral("regex"));
        }
    }
    return rule;
}

LogRuleSet::Index LogRuleSet::buildIndex(const std::vector<LogRule> &rules)
{
    // Each template's whole words, and how many templates have each: a template is filed under
    // its rarest word (the longer of equals), so that a line meets few templates.
    std::vector<QStringList> words(rules.size());
    QHash<QString, int> shared;
    for (size_t i = 0; i < rules.size(); ++i) {
        if (!rules[i].text.isEmpty()) {
            if (const auto compiled = compileLogTemplate(rules[i].text)) {
                words[i] = compiled->words;
                for (const QString &word : compiled->words) {
                    ++shared[word];
                }
            }
        }
    }
    Index index;
    for (size_t i = 0; i < rules.size(); ++i) {
        const int at = static_cast<int>(i);
        if (words[i].isEmpty()) {
            index.others.push_back(at);
            continue;
        }
        const QString *best = nullptr;
        for (const QString &word : words[i]) {
            if (best == nullptr || shared.value(word) < shared.value(*best)
                || (shared.value(word) == shared.value(*best) && word.size() > best->size())) {
                best = &word;
            }
        }
        index.byWord[*best].push_back(at);
    }
    return index;
}

void LogRuleSet::rebuild()
{
    for (size_t i = 0; i < m_defaults.size(); ++i) {
        m_defaults[i].user = false;
        m_defaults[i].order = static_cast<int>(i);
        m_defaults[i].whenCount = m_defaults[i].when.count();
    }
    for (size_t i = 0; i < m_user.size(); ++i) {
        m_user[i].user = true;
        m_user[i].order = static_cast<int>(i);
        m_user[i].whenCount = m_user[i].when.count();
    }
    m_disabledSet = QSet<QString>{m_disabled.begin(), m_disabled.end()};
    m_defaultIndex = buildIndex(m_defaults);
    m_userIndex = buildIndex(m_user);
}

bool LogRuleSet::loadDefaults(const QByteArray &json, QString *const error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        if (error != nullptr) {
            *error = parseError.errorString();
        }
        return false;
    }
    m_defaults.clear();
    for (const QJsonValue &value : doc.object().value(QStringLiteral("rules")).toArray()) {
        QString why;
        auto rule = parseRule(value.toObject(), false, &why);
        if (!rule.has_value() || rule->id.isEmpty()) {
            qWarning() << "[log] default rule skipped:" << (why.isEmpty() ? "no id" : why)
                       << QJsonDocument{value.toObject()}.toJson(QJsonDocument::Compact);
            continue;
        }
        m_defaults.push_back(std::move(*rule));
    }
    rebuild();
    return true;
}

void LogRuleSet::loadUserFile(const QString &path)
{
    m_path = path;
    m_user.clear();
    m_disabled.clear();
    m_fileVersion = VERSION;
    m_unreadable.clear();
    QFile file{path};
    if (!file.exists()) {
        rebuild();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_unreadable = file.errorString();
        qWarning() << "[log] cannot read" << path << m_unreadable;
        rebuild();
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        m_unreadable = parseError.errorString();
        qWarning() << "[log] cannot read" << path << m_unreadable;
        rebuild();
        return;
    }
    const QJsonObject obj = doc.object();
    m_fileVersion = std::max(VERSION, obj.value(QStringLiteral("version")).toInt(VERSION));
    if (const auto disabled = stringList(obj.value(QStringLiteral("disabled")))) {
        m_disabled = *disabled;
    }
    for (const QJsonValue &value : obj.value(QStringLiteral("rules")).toArray()) {
        QString why;
        auto rule = parseRule(value.toObject(), false, &why);
        if (!rule.has_value()) {
            // Kept as it is, so that saving does not lose it, and never matched.
            LogRule kept;
            kept.json = value.toObject();
            kept.id = kept.json.value(QStringLiteral("id")).toString();
            kept.inactiveReason = QStringLiteral("invalid: %1").arg(why);
            m_user.push_back(std::move(kept));
            continue;
        }
        m_user.push_back(std::move(*rule));
    }
    rebuild();
}

QString LogRuleSet::nextUserId() const
{
    static const QRegularExpression numbered{QStringLiteral(R"(^u(\d+)$)")};
    int most = 0;
    for (const LogRule &rule : m_user) {
        if (const auto m = numbered.match(rule.id); m.hasMatch()) {
            most = std::max(most, m.captured(1).toInt());
        }
    }
    return QStringLiteral("u%1").arg(most + 1);
}

LogRuleSet::Edit LogRuleSet::setRule(const QJsonObject &obj, const QString &nowIso)
{
    Edit edit;
    if (newerFile()) {
        edit.error = QStringLiteral("newer-file");
        edit.message = QStringLiteral("The rule file is of a newer MMapper (version %1)")
                           .arg(m_fileVersion);
        return edit;
    }
    if (!m_unreadable.isEmpty()) {
        edit.error = QStringLiteral("unwritable");
        edit.message = QStringLiteral("The rule file could not be read (%1); it is left as it is")
                           .arg(m_unreadable);
        return edit;
    }
    QString why;
    auto parsed = parseRule(obj, true, &why);
    if (!parsed.has_value()) {
        edit.error = QStringLiteral("invalid-rule");
        edit.message = why;
        return edit;
    }
    LogRule rule = std::move(*parsed);
    if (rule.id.isEmpty()) {
        rule.id = nextUserId();
    }
    rule.json[QStringLiteral("id")] = rule.id;
    if (rule.created.isEmpty()) {
        rule.created = nowIso;
        rule.json[QStringLiteral("created")] = nowIso;
    }

    std::vector<LogRule> before = m_user;
    std::erase_if(m_user, [&rule](const LogRule &one) { return one.id == rule.id; });
    // Newest last: an edited rule is the newest of its layer.
    m_user.push_back(rule);
    QString error;
    if (!save(&error)) {
        m_user = std::move(before);
        edit.error = QStringLiteral("unwritable");
        edit.message = error;
        return edit;
    }
    rebuild();
    edit.ok = true;
    edit.id = rule.id;
    return edit;
}

LogRuleSet::Edit LogRuleSet::deleteRule(const QString &id)
{
    Edit edit;
    edit.id = id;
    if (newerFile()) {
        edit.error = QStringLiteral("newer-file");
        edit.message = QStringLiteral("The rule file is of a newer MMapper (version %1)")
                           .arg(m_fileVersion);
        return edit;
    }
    if (!m_unreadable.isEmpty()) {
        edit.error = QStringLiteral("unwritable");
        edit.message = QStringLiteral("The rule file could not be read (%1); it is left as it is")
                           .arg(m_unreadable);
        return edit;
    }
    std::vector<LogRule> before = m_user;
    const QStringList disabledBefore = m_disabled;
    const auto removed = std::erase_if(m_user, [&id](const LogRule &one) { return one.id == id; });
    if (removed == 0) {
        const bool isDefault = std::any_of(m_defaults.begin(),
                                           m_defaults.end(),
                                           [&id](const LogRule &one) { return one.id == id; });
        if (!isDefault) {
            edit.error = QStringLiteral("invalid-rule");
            edit.message = QStringLiteral("No rule '%1'").arg(id);
            return edit;
        }
        if (!m_disabled.contains(id)) {
            m_disabled.append(id);
        }
    }
    QString error;
    if (!save(&error)) {
        m_user = std::move(before);
        m_disabled = disabledBefore;
        edit.error = QStringLiteral("unwritable");
        edit.message = error;
        return edit;
    }
    rebuild();
    edit.ok = true;
    return edit;
}

QJsonObject LogRuleSet::toFileJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("version")] = VERSION;
    obj[QStringLiteral("disabled")] = QJsonArray::fromStringList(m_disabled);
    QJsonArray rules;
    for (const LogRule &rule : m_user) {
        rules.append(rule.json);
    }
    obj[QStringLiteral("rules")] = rules;
    return obj;
}

bool LogRuleSet::save(QString *const error) const
{
    if (m_path.isEmpty()) {
        *error = QStringLiteral("MMapper has no file for the rules");
        return false;
    }
    if (!QDir{}.mkpath(QFileInfo{m_path}.absolutePath())) {
        *error = QStringLiteral("Cannot make the folder of %1").arg(m_path);
        return false;
    }
    if (QFile::exists(m_path)) {
        // The file as it was, kept once.
        const QString backup = m_path + QStringLiteral(".bak");
        QFile::remove(backup);
        if (!QFile::copy(m_path, backup)) {
            qWarning() << "[log] could not keep" << backup;
        }
    }
    QSaveFile file{m_path};
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument{toFileJson()}.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

LogResult LogRuleSet::classify(const LogLine &line, const LogContext &context) const
{
    LogResult best;
    PrecedenceKey bestKey;
    bool found = false;
    const QString plain = line.plain.trimmed();
    const QStringList words = wordsOf(plain);

    const auto consider = [&](const LogRule &rule) {
        if (!rule.active() || (!rule.user && m_disabledSet.contains(rule.id))) {
            return;
        }
        if ((line.tags.bits & rule.tagMask) != rule.tagMask
            || (line.tags.bits & rule.notTagMask) != 0) {
            return;
        }
        const PrecedenceKey key = precedenceOf(rule);
        if (found && !(bestKey < key)) {
            return;
        }
        if (!rule.when.matches(context, line.about)) {
            return;
        }
        if (rule.hasPattern) {
            if (!rule.literal.isEmpty() && !plain.contains(rule.literal, rule.literalCase)) {
                return;
            }
            if (!rule.pattern.match(plain).hasMatch()) {
                return;
            }
        }
        best.priority = rule.priority;
        best.route = rule.route;
        best.rule = &rule;
        bestKey = key;
        found = true;
    };
    const auto walk = [&](const Index &index, const std::vector<LogRule> &rules) {
        if (!index.byWord.isEmpty()) {
            for (const QString &word : words) {
                if (const auto it = index.byWord.find(word); it != index.byWord.end()) {
                    for (const int i : it.value()) {
                        consider(rules[static_cast<size_t>(i)]);
                    }
                }
            }
        }
        for (const int i : index.others) {
            consider(rules[static_cast<size_t>(i)]);
        }
    };
    walk(m_userIndex, m_user);
    walk(m_defaultIndex, m_defaults);
    return best;
}

QJsonObject LogRuleSet::toRulesJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("version")] = VERSION;
    obj[QStringLiteral("file")] = m_path;
    QJsonArray user;
    for (const LogRule &rule : m_user) {
        user.append(rule.json);
    }
    obj[QStringLiteral("user")] = user;
    obj[QStringLiteral("disabled")] = QJsonArray::fromStringList(m_disabled);
    obj[QStringLiteral("defaults")] = static_cast<qint64>(m_defaults.size());
    QJsonArray inactive;
    for (const std::vector<LogRule> *layer : {&m_user, &m_defaults}) {
        for (const LogRule &rule : *layer) {
            if (!rule.active()) {
                QJsonObject one;
                one[QStringLiteral("id")] = rule.id;
                one[QStringLiteral("reason")] = rule.inactiveReason;
                inactive.append(one);
            }
        }
    }
    obj[QStringLiteral("inactive")] = inactive;
    if (newerFile()) {
        // Read, and left as it is: edits are refused with `newer-file`.
        obj[QStringLiteral("fileVersion")] = m_fileVersion;
    }
    return obj;
}

QString logRouteName(const LogRouteEnum route)
{
    return routeName(route);
}
