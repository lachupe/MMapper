#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../parser/LineTags.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

/// The rules of the prioritised Log (MMapper.Log, spec section 37.2): which priority, 0 to 10,
/// and which route a line of MUME's gets.
///
/// Two layers: the shipped defaults (`:/log/rules.json`) and the user's file
/// (`MMapper-log-rules.json` beside MMapper's settings file). A rule matches on the line's plain
/// text (`text`, a template matched against the whole trimmed line, or `regex`, unanchored, for
/// hand-written defaults), on its tags (`tags` all present, `notTags` none) and on the situation
/// (`when`). Among the rules that match, the user layer beats the defaults; within a layer a
/// rule with a text or regex beats a tags-only one, then the one with more `when` conditions,
/// then the longer literal text, then the newer (later) rule. A line no rule knows gets 2.
///
/// A template is literal text with `<N>` (a name) and `<O>` (a phrase), both `(.+?)`, `<Q>`
/// (quoted words, `[^']*`), `<#>` (a number) and `*` (anything); a run of spaces matches any
/// whitespace, and `\` takes the next character literally.

enum class NODISCARD LogRouteEnum : uint8_t { LOG, SCREEN };
/// "log" or "screen".
NODISCARD QString logRouteName(LogRouteEnum route);

/// The situation a line is ranked in, as ImportantLog knows it when the prompt window closes.
struct NODISCARD LogContext final
{
    /// A blow by or at the player in the last 8 seconds.
    bool inFight = false;
    /// Someone else is in the player's group.
    bool grouped = false;
    /// The current room is not no-sundeath (MMapper's map flag).
    bool outdoors = false;
    /// The player's race as Char.StatusVars gave it, lower case; empty until it did.
    QString race;
    /// Hit points as a fraction of the maximum (Char.Vitals); unset until known.
    std::optional<double> hpFraction;
    /// The fraction of the maximum lost since the previous prompt; unset when unknown.
    std::optional<double> hpDrop;
};

/// One line to rank: its plain text, its tags and whom it is about ("you", "group", "other").
struct NODISCARD LogLine final
{
    QString plain;
    LineTagSet tags;
    QString about = QStringLiteral("other");
};

struct NODISCARD LogWhen final
{
    std::optional<bool> inFight;
    std::optional<bool> grouped;
    std::optional<bool> outdoors;
    /// Lower case; empty for no condition.
    QStringList race;
    std::optional<double> hpBelow;
    std::optional<double> hpDrop;
    QStringList who;

    /// How many conditions there are, for precedence.
    NODISCARD int count() const;
    NODISCARD bool matches(const LogContext &context, const QString &about) const;
};

struct NODISCARD LogRule final
{
    QString id;
    QString text;
    QString regex;
    QStringList tags;
    QStringList notTags;
    LogWhen when;
    int priority = 2;
    LogRouteEnum route = LogRouteEnum::LOG;
    QString created;
    /// The rule as read or set, unknown fields kept: what the user file and MMapper.Log.Rules
    /// are given back.
    QJsonObject json;

    // Compiled.
    QRegularExpression pattern;
    bool hasPattern = false;
    uint64_t tagMask = 0;
    uint64_t notTagMask = 0;
    /// A run of literal characters the line must contain, to skip the expression quickly.
    QString literal;
    Qt::CaseSensitivity literalCase = Qt::CaseSensitive;
    /// How much of the text is literal, for precedence.
    int literalLength = 0;
    /// Why the rule cannot be used ("when.swimming"); empty when it is active.
    QString inactiveReason;
    bool user = false;
    /// Its place in its layer: later is newer.
    int order = 0;
    /// when.count(), kept.
    int whenCount = 0;

    NODISCARD bool active() const { return inactiveReason.isEmpty(); }
};

/// What a line was ranked: the priority, the route, and the rule that decided (null for none).
struct NODISCARD LogResult final
{
    int priority = 2;
    LogRouteEnum route = LogRouteEnum::LOG;
    const LogRule *rule = nullptr;
};

/// A template compiled: the anchored expression and what the index and precedence use.
struct NODISCARD LogTemplate final
{
    QString expression;
    /// The longest run of literal characters without whitespace.
    QString longestWord;
    int literalLength = 0;
    /// The first and the last word of the line when the template fixes them; empty otherwise.
    QString firstWord;
    QString lastWord;
    /// The literal words the line must have whole, between whitespace or at its ends.
    QStringList words;
};

/// Compiles a template (see above); nullopt for an empty one.
NODISCARD std::optional<LogTemplate> compileLogTemplate(const QString &text);

/// The template a line suggests (MMapper.Log.Explained): quoted speech becomes `<Q>`, numbers
/// `<#>`, and the names given and the `*starred*` names `<N>`; everything else literal, with
/// `*` and `\` escaped.
NODISCARD QString suggestLogTemplate(const QString &plain, const QStringList &names);

class NODISCARD LogRuleSet final
{
public:
    /// The version of the format this MMapper reads and writes.
    static constexpr const int VERSION = 1;
    static constexpr const int MIN_PRIORITY = 0;
    static constexpr const int MAX_PRIORITY = 10;
    static constexpr const int UNKNOWN_PRIORITY = 2;

    struct NODISCARD Edit final
    {
        bool ok = false;
        /// `invalid-rule`, `unwritable` or `newer-file` when not ok.
        QString error;
        QString message;
        QString id;
    };

private:
    std::vector<LogRule> m_defaults;
    std::vector<LogRule> m_user;
    QStringList m_disabled;
    QSet<QString> m_disabledSet;
    QString m_path;
    /// The user file's own version, when it is newer than VERSION: read, never written.
    int m_fileVersion = VERSION;
    /// The user file exists but could not be read: it is not overwritten either.
    QString m_unreadable;

    /// Indexes into one layer: a template by one of the whole words the line must have (the
    /// one fewest templates share), and the rest (regexes, tags-only rules, templates without a
    /// whole word), which every line is checked against.
    struct NODISCARD Index final
    {
        QHash<QString, std::vector<int>> byWord;
        std::vector<int> others;
    };
    Index m_defaultIndex;
    Index m_userIndex;

public:
    /// Reads the shipped defaults. False, with `error` set, when the document is no rule file.
    NODISCARD bool loadDefaults(const QByteArray &json, QString *error = nullptr);
    /// Reads the user's file at `path`, which becomes the file edits are saved to. A missing
    /// file is an empty user layer.
    void loadUserFile(const QString &path);

    /// Parses one rule. `strict` (a rule sent by a frontend) refuses what `strict == false` (a
    /// file) loads inactive: an unknown `when` key or tag. Nullopt with `error` set for a rule
    /// that cannot be used at all.
    NODISCARD static std::optional<LogRule> parseRule(const QJsonObject &obj,
                                                      bool strict,
                                                      QString *error);

    /// MMapper.Log.SetRule: adds the rule (named `u<n>`) or replaces the user rule with its id,
    /// and saves. Not ok when the rule is invalid, the file newer or unwritable; nothing changes
    /// then.
    NODISCARD Edit setRule(const QJsonObject &rule, const QString &nowIso);
    /// MMapper.Log.DeleteRule: removes a user rule, or disables a default one.
    NODISCARD Edit deleteRule(const QString &id);

    NODISCARD LogResult classify(const LogLine &line, const LogContext &context) const;

    /// MMapper.Log.Rules' payload: the version, the file, the user layer in full, the disabled
    /// defaults, the count of defaults and the inactive rules with why.
    NODISCARD QJsonObject toRulesJson() const;

    NODISCARD const QString &filePath() const { return m_path; }
    NODISCARD size_t defaultCount() const { return m_defaults.size(); }
    NODISCARD const std::vector<LogRule> &userRules() const { return m_user; }
    NODISCARD const QStringList &disabled() const { return m_disabled; }
    NODISCARD bool newerFile() const { return m_fileVersion > VERSION; }

private:
    NODISCARD static Index buildIndex(const std::vector<LogRule> &rules);
    void rebuild();
    /// Writes the user layer; false with `error` set when it could not.
    NODISCARD bool save(QString *error) const;
    NODISCARD QJsonObject toFileJson() const;
    NODISCARD QString nextUserId() const;
};
