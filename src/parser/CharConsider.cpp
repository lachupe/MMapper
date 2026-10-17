// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharConsider.h"

#include <QRegularExpression>

namespace {

struct NODISCARD ConsiderRow final
{
    const char *line;
    const char *verdict;
};

// Easiest first; the verdict's first place is the grade. Only "The perfect match!" and "You
// would need some luck!" are in the user's logs; the rest is the classic Diku ladder MUME's help
// describes ("killing a tiny spider would be easy indeed") and has not been seen live.
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
    {"Do you feel lucky, punk?", "deadly"},
    {"Are you mad!?", "mad"},
    {"You ARE mad!", "hopeless"},
};

const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};
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
    QString line = raw.trimmed();
    line.remove(g_twiddlers);
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
