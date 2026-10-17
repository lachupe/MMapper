#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <optional>

#include <QString>
#include <QStringList>

/// MUME's answer to `consider <character>` (MMapper.Char.Consider): a sentence comparing the two
/// levels ("A Dúnadan soldier is at a significantly higher level than you.") and an advice
/// sentence weighing the two sides' attack through the other's defence ("You would need a lot of
/// luck, great equipment, powerful spells, and supportive allies!"). Not hit points, special
/// attacks or damage (MUME's help, `consider`). Either sentence may be wrapped over lines.
///
/// The advice is one of a short set of sentences, matched whole; each is graded from the easiest
/// opponent to the deadliest. The sentences are the classic Diku ladder plus the ones the user's
/// logs show; a rung nobody has seen live is marked in CharConsider.cpp. The answer to
/// `consider <object>` is a block of item text and matches none of these.
struct NODISCARD CharConsider final
{
    /// A stable code, easiest first; see considerVerdicts().
    QString verdict;
    /// 0 (a pushover) upward, the verdict's place on the ladder, for a client that colours or
    /// sorts.
    int grade = 0;
    /// MUME's reply, colour removed, its wrapped lines joined by spaces: the level sentence and
    /// the advice, or the advice alone.
    QString text;
    /// The level sentence's comparison as a code ("slightly-higher", "significantly-higher",
    /// "slightly-lower", "same"), or empty when MUME sent none.
    QString level;
    /// What the player typed after `consider` ("orc", "2.orc"), when the command was seen.
    /// Empty when not known.
    QString target;
};

/// The `consider` command's target in a line the player sent ("con orc", "consider 2.orc"), or an
/// empty string for any other line.
NODISCARD QString considerTarget(const QString &sentLine);

/// The verdict `line` is (colour removed, matched whole), or nullopt. `target` is left empty.
NODISCARD std::optional<CharConsider> parseConsiderLine(const QString &line);

/// Every verdict code, easiest first, for the tests and the documentation.
NODISCARD QStringList considerVerdicts();

/// The reply to the player's own `consider`, gathered over its lines: from the command to the
/// verdict's last line, or to the prompt. A pager pause in between is no prompt and does not end
/// it. A line that ends a sentence and is no part of a verdict (someone arriving) is passed over.
/// Out of a reply, a lone advice line is still a verdict, without target.
class NODISCARD ConsiderReader final
{
private:
    QString m_target;
    QStringList m_lines;
    int m_received = 0;
    bool m_waiting = false;

public:
    /// A line on its way to MUME: a `consider` starts a reply.
    void receiveCommand(const QString &sentLine);
    /// A line from MUME, colour removed; not a prompt or the pager. The verdict when this line
    /// completes one.
    NODISCARD std::optional<CharConsider> receiveLine(const QString &line);
    /// The prompt ends the reply: a level sentence that came without advice is reported alone. A
    /// prompt before any line of the reply is an earlier command's and ends nothing.
    NODISCARD std::optional<CharConsider> receivePrompt();
};
